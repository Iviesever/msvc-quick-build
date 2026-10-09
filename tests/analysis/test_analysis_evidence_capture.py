"""Deterministic data-only contracts; these tests allocate no study samples."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from analysis_evidence_capture import CaptureError, CaptureSession, Limits, audit_existing


IDENTITY = {"study_id": "stub-only", "registration_id": "no-sample-allocation",
            "input_manifest_sha256": "0" * 64, "collector_sha256": "1" * 64}


def plan_for(*names, stderr_empty=True):
    return [{"call_id": name, "stdout_records": [
        {"kind": "prepared", "case": name},
        {"kind": "observation", "case": name, "trial": 0, "instrumented": False},
        {"kind": "observation", "case": name, "trial": 1, "instrumented": False}],
        "stderr_empty": stderr_empty} for name in names]


def output_for(spec):
    # Preserve these literal bytes, including CRLF, whitespace and UTF-8.
    rows = [dict(row, note="原文", value=17) for row in spec["stdout_records"]]
    return b"".join(json.dumps(row, ensure_ascii=False).encode("utf-8") + b"\r\n"
                    for row in rows)


class BoundedReader(io.BytesIO):
    def __init__(self, raw, maximum):
        super().__init__(raw)
        self.maximum = maximum
        self.requests = []

    def read(self, size=-1):
        if not 0 < size <= self.maximum:
            raise AssertionError(f"unbounded or oversized read: {size}")
        self.requests.append(size)
        return super().read(size)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "capture"
        self.plan = plan_for("A", "B")
        self.limits = Limits()

    def create(self, plan=None, limits=None):
        self.plan = plan if plan is not None else self.plan
        self.limits = limits if limits is not None else self.limits
        return CaptureSession.create(self.root, identity=IDENTITY, plan=self.plan,
                                     limits=self.limits)

    def record(self, session, index, stdout=None, stderr=b""):
        session.start_call(index)
        for role, data in [("stdout", stdout if stdout is not None else output_for(self.plan[index])),
                           ("stderr", stderr)]:
            session.preserve_stream(index, role, io.BytesIO(data))
        session.finish_call(index, exit_code=0)

    def audit(self, seal, identity=None, plan=None):
        return audit_existing(self.root, identity=identity or IDENTITY,
                              plan=plan or self.plan, limits=self.limits,
                              expected_seal_sha256=seal["seal_sha256"])

    def test_round_trip_and_fresh_offline_audit(self):
        session = self.create(plan_for("A", stderr_empty=False), Limits(chunk_bytes=7))
        stderr = b"\xff\x00\r\nunterminated\xe4\xb8\xad"
        session.start_call(0)
        original = output_for(self.plan[0])
        source = BoundedReader(original, 7)
        receipt = session.preserve_stream(0, "stdout", source)
        self.assertEqual(receipt["sha256"], hashlib.sha256(original).hexdigest())
        self.assertEqual(receipt["byte_count"], len(original))
        self.assertEqual(receipt["lf_count"], 3)
        self.assertTrue(receipt["eof"])
        self.assertTrue(receipt["ends_with_lf"])
        session.preserve_stream(0, "stderr", BoundedReader(stderr, 7))
        session.finish_call(0, exit_code=0)
        seal = session.finalize()
        self.assertTrue(seal["complete"])
        self.assertTrue(seal["execution_finished"])
        self.assertEqual(seal["integrity_status"], "VERIFIED")
        self.assertEqual(seal["verified_streams"], 2)
        self.assertEqual(seal["verified_records"], 3)
        self.assertTrue(self.audit(seal)["complete"])
        for role, expected in [("stdout", original), ("stderr", stderr)]:
            for directory in ("raw", "backup"):
                self.assertEqual((self.root / directory / f"000000.{role}.bin").read_bytes(), expected)

    def test_earlier_raw_truncated_after_later_call_cannot_complete(self):
        session = self.create()
        self.record(session, 0)
        self.record(session, 1)
        damaged = self.root / "raw/000000.stdout.bin"
        original_backup = (self.root / "backup/000000.stdout.bin").read_bytes()
        damaged.write_bytes(b"")
        seal = session.finalize()
        self.assertFalse(seal["complete"])
        self.assertTrue(seal["execution_finished"])
        self.assertEqual(seal["integrity_status"], "INVALID")
        self.assertEqual(damaged.read_bytes(), b"")
        self.assertEqual((self.root / "backup/000000.stdout.bin").read_bytes(), original_backup)
        self.assertFalse(self.audit(seal)["complete"])

    def test_post_seal_damage_is_reaudited_without_rewriting_seal(self):
        session = self.create(plan_for("A"))
        self.record(session, 0)
        seal = session.finalize()
        original_seal = (self.root / "result.json").read_bytes()
        (self.root / "raw/000000.stdout.bin").write_bytes(b"")
        self.assertFalse(self.audit(seal)["complete"])
        self.assertEqual((self.root / "result.json").read_bytes(), original_seal)
        with self.assertRaises(CaptureError):
            session.finalize()

    def test_offline_audit_accepts_faithful_copy_with_external_seal(self):
        session = self.create(plan_for("A"))
        self.record(session, 0)
        seal = session.finalize()
        copied = Path(self.tmp.name) / "downloaded-copy"
        shutil.copytree(self.root, copied)
        self.assertTrue(audit_existing(copied, identity=IDENTITY, plan=self.plan,
                                       limits=self.limits,
                                       expected_seal_sha256=seal["seal_sha256"])["complete"])

    def test_each_late_stream_damage_rejected(self):
        for target, change in [("raw", "append"), ("raw", "same_length"),
                               ("backup", "truncate"), ("raw", "delete"),
                               ("both", "same_length")]:
            with self.subTest(target=target, change=change), tempfile.TemporaryDirectory() as d:
                self.root = Path(d) / "capture"
                session = self.create(plan_for("A"))
                self.record(session, 0)
                for directory in (("raw", "backup") if target == "both" else (target,)):
                    path = self.root / directory / "000000.stdout.bin"
                    before = path.read_bytes()
                    if change == "delete":
                        path.unlink()
                    elif change == "append":
                        path.write_bytes(before + b" ")
                    elif change == "truncate":
                        path.write_bytes(before[:-1])
                    else:
                        path.write_bytes(before.replace(b'"value": 17', b'"value": 18'))
                self.assertFalse(session.finalize()["complete"])

    def test_strict_record_identity_count_order_and_json(self):
        spec = plan_for("A")[0]
        original = output_for(spec)
        lines = original.splitlines(keepends=True)
        variants = {
            "missing": b"".join(lines[:-1]),
            "extra": original + lines[-1],
            "duplicate_trial": lines[0] + lines[1] + lines[1],
            "wrong_call": original.replace(b'"case": "A"', b'"case": "B"'),
            "reordered": lines[0] + lines[2] + lines[1],
            "bool_is_not_int": original.replace(b'"trial": 0', b'"trial": false'),
            "duplicate_key": original.replace(b'"trial": 0', b'"trial": 0, "trial": 0'),
            "nonfinite": original.replace(b'"value": 17', b'"value": NaN'),
            "invalid_utf8": original.replace("原文".encode(), b"\xff"),
            "torn_tail": original[:-1],
            "blank_line": original + b"\n",
        }
        for name, raw in variants.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as d:
                self.root = Path(d) / "capture"
                session = self.create(plan_for("A"))
                session.start_call(0)
                session.preserve_stream(0, "stdout", io.BytesIO(raw))
                session.preserve_stream(0, "stderr", io.BytesIO(b""))
                with self.assertRaises(CaptureError):
                    session.finish_call(0, exit_code=0)
                self.assertFalse(session.finalize()["complete"])
                self.assertEqual((self.root / "backup/000000.stdout.bin").read_bytes(), raw)

    def test_nonempty_stderr_is_preserved_but_not_accepted(self):
        session = self.create(plan_for("A"))
        with self.assertRaises(CaptureError):
            self.record(session, 0, stderr=b"\x00\xfferror without newline")
        self.assertFalse(session.finalize()["complete"])
        self.assertEqual((self.root / "backup/000000.stderr.bin").read_bytes(), b"\x00\xfferror without newline")

    def test_missing_call_or_stream_never_completes(self):
        session = self.create()
        self.record(session, 0)
        self.assertFalse(session.finalize()["complete"])

    def test_existing_root_rejected_without_touching_it(self):
        self.root.mkdir()
        sentinel = self.root / "sentinel"
        sentinel.write_bytes(b"original")
        with self.assertRaises((CaptureError, FileExistsError)):
            self.create()
        self.assertEqual(sentinel.read_bytes(), b"original")
        self.assertEqual(list(self.root.iterdir()), [sentinel])

    def test_inputs_are_frozen_and_offline_identity_must_match(self):
        mutable = copy.deepcopy(IDENTITY)
        plan = plan_for("A")
        session = CaptureSession.create(self.root, identity=mutable, plan=plan, limits=self.limits)
        mutable["study_id"] = "changed"
        plan[0]["stdout_records"][0]["case"] = "changed"
        self.plan = plan_for("A")
        self.record(session, 0)
        seal = session.finalize()
        self.assertTrue(seal["complete"])
        self.assertFalse(self.audit(seal, identity=mutable)["complete"])
        self.assertFalse(self.audit(seal, plan=plan)["complete"])

    def test_finalized_session_cannot_append_or_overwrite(self):
        session = self.create(plan_for("A"))
        self.record(session, 0)
        seal = session.finalize()
        journal = (self.root / "journal.jsonl").read_bytes()
        result = (self.root / "result.json").read_bytes()
        for action in (lambda: session.start_call(0),
                       lambda: session.preserve_stream(0, "stdout", io.BytesIO(b"new")),
                       lambda: session.finish_call(0, exit_code=0), lambda: session.finalize()):
            with self.assertRaises(CaptureError):
                action()
        self.assertEqual((self.root / "journal.jsonl").read_bytes(), journal)
        self.assertEqual((self.root / "result.json").read_bytes(), result)
        self.assertTrue(self.audit(seal)["complete"])

    def test_extra_file_rejected_and_not_deleted(self):
        session = self.create(plan_for("A"))
        self.record(session, 0)
        extra = self.root / "raw/foreign.stdout.bin"
        extra.write_bytes(b"foreign")
        self.assertFalse(session.finalize()["complete"])
        self.assertEqual(extra.read_bytes(), b"foreign")

    def test_seal_anchor_required_and_tampering_rejected(self):
        session = self.create(plan_for("A"))
        self.record(session, 0)
        seal = session.finalize()
        path = self.root / "result.json"
        document = json.loads(path.read_text())
        document["complete"] = False
        path.write_text(json.dumps(document) + "\n")
        self.assertFalse(self.audit(seal)["complete"])


if __name__ == "__main__":
    unittest.main()
