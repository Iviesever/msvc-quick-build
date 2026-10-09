"""Independent, fixed-data I/O and state contracts; no study execution."""
from contextlib import contextmanager
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import analysis_evidence_capture as capture


IDENTITY = {"study_id": "fault-contract-only", "allocation": "no-real-samples"}


def call_spec(name="A", records=None, stderr_empty=False):
    return {"call_id": name,
            "stdout_records": [{"kind": "fixed", "slot": 0}] if records is None else records,
            "stderr_empty": stderr_empty}


def record_bytes(records):
    return b"".join(json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
                    for row in records)


class FaultFile:
    """A real file with injected storage faults, not a substitute collector."""
    def __init__(self, target, *, short_write=False, zero_write=False,
                 flush_error=None, close_error=None):
        self.target = target
        self.short_write = short_write
        self.zero_write = zero_write
        self.flush_error = flush_error
        self.close_error = close_error

    def write(self, raw):
        if self.zero_write:
            return 0
        if self.short_write:
            prefix = raw[:max(1, len(raw) // 2)]
            return self.target.write(prefix)
        return self.target.write(raw)

    def flush(self):
        self.target.flush()
        if self.flush_error is not None:
            raise OSError(self.flush_error)

    def fileno(self):
        return self.target.fileno()

    def close(self):
        self.target.close()
        if self.close_error is not None:
            raise OSError(self.close_error)


class TrackedReader(io.BytesIO):
    def __init__(self, raw, maximum, short_read=None):
        super().__init__(raw)
        self.maximum = maximum
        self.short_read = short_read
        self.requests = []
        self.received = 0

    def read(self, size=-1):
        if type(size) is not int or not 0 < size <= self.maximum:
            raise AssertionError(f"unbounded binary read request: {size}")
        self.requests.append(size)
        raw = super().read(min(size, self.short_read) if self.short_read else size)
        self.received += len(raw)
        return raw


@contextmanager
def faulty_output(path, kind):
    """Inject faults only at a chosen real file's storage boundary."""
    original_open, original_fsync = capture._open_new, capture.os.fsync
    descriptors = set()
    marker = "injected-" + kind

    def open_target(candidate):
        target = original_open(candidate)
        if Path(candidate) != path:
            return target
        descriptors.add(target.fileno())
        return FaultFile(target, short_write=kind == "short", zero_write=kind == "zero",
                         flush_error=marker if kind == "flush" else None,
                         close_error=marker if kind == "close" else None)

    def fsync_target(descriptor):
        if kind == "fsync" and descriptor in descriptors:
            raise OSError(marker)
        return original_fsync(descriptor)

    with mock.patch.object(capture, "_open_new", side_effect=open_target), \
            mock.patch.object(capture.os, "fsync", side_effect=fsync_target):
        yield


def finish_invalid(test, session, index=0):
    with test.assertRaises(capture.CaptureError):
        session.finish_call(index, exit_code=0)
    result = session.finalize()
    test.assertFalse(result["complete"])
    test.assertEqual(result["integrity_status"], "INVALID")
    return result


def stored_receipt(root, index, role):
    events = [json.loads(line) for line in (root / "journal.jsonl").read_bytes().splitlines()]
    return next(event["receipt"] for event in events
                if event["event"] == "stream" and event["index"] == index and event["role"] == role)


def record_call(session, index, spec, stderr=b""):
    session.start_call(index)
    session.preserve_stream(index, "stdout", io.BytesIO(record_bytes(spec["stdout_records"])))
    session.preserve_stream(index, "stderr", io.BytesIO(stderr))
    session.finish_call(index, exit_code=0)


class CaptureFaultTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "capture"

    def fresh(self, name, *, plan=None, limits=None):
        root = Path(self.tmp.name) / name
        plan = [call_spec()] if plan is None else plan
        limits = capture.Limits() if limits is None else limits
        session = capture.CaptureSession.create(root, identity=IDENTITY, plan=plan, limits=limits)
        return root, session, plan, limits

    def test_caller_cannot_relax_already_frozen_byte_budget(self):
        plan = [call_spec(records=[])]
        original = capture.Limits(max_stream_bytes=8, max_total_bytes=16, chunk_bytes=3)
        expected = capture.Limits(max_stream_bytes=8, max_total_bytes=16, chunk_bytes=3)
        session = capture.CaptureSession.create(self.root, identity=IDENTITY, plan=plan, limits=original)
        manifest_before = (self.root / "manifest.json").read_bytes()
        self.assertEqual(json.loads(manifest_before)["limits"]["max_stream_bytes"], 8)
        # The API promises a frozen capture; the supplied object must not remain
        # an alias capable of changing the already-persisted policy.
        object.__setattr__(original, "max_stream_bytes", 16)
        session.start_call(0)
        session.preserve_stream(0, "stdout", io.BytesIO(b""))
        capture_failed = False
        try:
            session.preserve_stream(0, "stderr", io.BytesIO(b"x" * 9))
        except capture.CaptureError:
            capture_failed = True
        try:
            session.finish_call(0, exit_code=0)
        except capture.CaptureError:
            self.assertTrue(capture_failed)
        seal = session.finalize()
        offline = capture.audit_existing(self.root, identity=IDENTITY, plan=plan, limits=expected,
                                         expected_seal_sha256=seal["seal_sha256"])
        self.assertEqual((self.root / "manifest.json").read_bytes(), manifest_before)
        self.assertFalse(seal["complete"], json.dumps({
            "contract": "limits must remain frozen after create",
            "manifest_stream_limit": 8,
            "caller_object_after_mutation": original.max_stream_bytes,
            "attempted_stderr_bytes": 9,
            "capture_failed": capture_failed,
            "finalize_complete": seal["complete"],
            "offline_with_original_limits_complete": offline["complete"],
        }, sort_keys=True))
        self.assertTrue(capture_failed)
        self.assertFalse(offline["complete"])

    def test_secondary_flush_failure_is_persisted_without_replacing_first_error(self):
        plan = [call_spec()]
        session = capture.CaptureSession.create(self.root, identity=IDENTITY, plan=plan)
        session.start_call(0)
        raw = record_bytes(plan[0]["stdout_records"])
        target_path = self.root / "raw/000000.stdout.bin"
        original_open = capture._open_new
        marker = "injected-flush-after-short-write"

        def open_target(path):
            target = original_open(path)
            if Path(path) == target_path:
                return FaultFile(target, short_write=True, flush_error=marker)
            return target

        with mock.patch.object(capture, "_open_new", side_effect=open_target):
            with self.assertRaises(capture.CaptureError):
                session.preserve_stream(0, "stdout", io.BytesIO(raw))
        # A first failure must not discard the other available current stream.
        diagnostic = b"\xffavailable-stderr\x00"
        session.preserve_stream(0, "stderr", io.BytesIO(diagnostic))
        with self.assertRaises(capture.CaptureError):
            session.finish_call(0, exit_code=1, failure="synthetic failed capture")
        seal = session.finalize()
        self.assertFalse(seal["complete"])
        self.assertIn("short_write", seal["first_failure"])
        self.assertEqual((self.root / "backup/000000.stdout.bin").read_bytes(), raw)
        self.assertEqual(target_path.read_bytes(), raw[:max(1, len(raw) // 2)])
        for directory in ("raw", "backup"):
            self.assertEqual((self.root / directory / "000000.stderr.bin").read_bytes(), diagnostic)
        persistent = (self.root / "journal.jsonl").read_bytes() + (self.root / "result.json").read_bytes()
        self.assertIn(marker.encode(), persistent,
                      "The later flush cause must remain visible in persisted evidence while short_write stays first.")

    def test_oversized_exit_claim_cannot_be_relabelled_as_success(self):
        plan = [call_spec()]
        session = capture.CaptureSession.create(self.root, identity=IDENTITY, plan=plan)
        session.start_call(0)
        session.preserve_stream(0, "stdout", io.BytesIO(record_bytes(plan[0]["stdout_records"])))
        session.preserve_stream(0, "stderr", io.BytesIO(b""))
        error_type = None
        try:
            session.finish_call(0, exit_code=10 ** 5000)
        except (capture.CaptureError, ValueError, OverflowError) as error:
            error_type = type(error).__name__
        self.assertIsNotNone(error_type, "The out-of-range exit claim must be rejected.")
        relabelled = False
        try:
            session.finish_call(0, exit_code=0)
            relabelled = True
        except capture.CaptureError:
            pass
        seal = session.finalize()
        self.assertFalse(seal["complete"], json.dumps({
            "contract": "invalid exit claim is latched, never retry as exit zero",
            "first_rejection_type": error_type,
            "second_finish_zero_was_accepted": relabelled,
            "finalize_complete": seal["complete"],
        }, sort_keys=True))
        self.assertEqual(error_type, "CaptureError")
        self.assertFalse(relabelled)

    def test_replaced_raw_directory_cannot_write_into_foreign_target(self):
        plan = [call_spec()]
        session = capture.CaptureSession.create(self.root, identity=IDENTITY, plan=plan)
        foreign = Path(self.tmp.name) / "foreign"
        foreign.mkdir()
        sentinel = foreign / "sentinel"
        sentinel.write_bytes(b"foreign bytes must remain unchanged")
        raw_directory = self.root / "raw"
        raw_directory.rmdir()  # Only the empty directory created by this fixture.
        try:
            raw_directory.symlink_to(foreign, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            try:
                session.finalize()
            except capture.CaptureError:
                pass  # A missing directory still closes the journal and fails safely.
            self.skipTest(f"directory symlinks unavailable: {type(error).__name__}")
        capture_failed = False
        try:
            session.start_call(0)
            session.preserve_stream(0, "stdout", io.BytesIO(record_bytes(plan[0]["stdout_records"])))
        except capture.CaptureError:
            capture_failed = True
        try:
            seal = session.finalize()
        except capture.CaptureError:
            seal = None
        if seal is not None:
            self.assertFalse(seal["complete"])
        self.assertEqual(sentinel.read_bytes(), b"foreign bytes must remain unchanged")
        self.assertEqual(sorted(p.name for p in foreign.iterdir()), ["sentinel"],
                         "A stable replaced parent directory must be rejected before any foreign write.")
        self.assertTrue(capture_failed)

    def test_stream_budget_below_equal_and_one_byte_over(self):
        for size in (7, 8, 9):
            with self.subTest(bytes=size):
                limits = capture.Limits(max_stream_bytes=8, max_total_bytes=16, chunk_bytes=3)
                root, session, plan, _ = self.fresh(f"stream-{size}", plan=[call_spec(records=[])],
                                                   limits=limits)
                session.start_call(0)
                session.preserve_stream(0, "stdout", TrackedReader(b"", 3))
                source = TrackedReader(b"x" * size, 3)
                if size <= 8:
                    receipt = session.preserve_stream(0, "stderr", source)
                    session.finish_call(0, exit_code=0)
                    seal = session.finalize()
                    self.assertTrue(seal["complete"])
                    self.assertTrue(capture.audit_existing(root, identity=IDENTITY, plan=plan,
                        limits=limits, expected_seal_sha256=seal["seal_sha256"])["complete"])
                    self.assertTrue(receipt["eof"])
                    self.assertEqual(receipt["overflow_probe_hex"], "")
                else:
                    with self.assertRaises(capture.CaptureError):
                        session.preserve_stream(0, "stderr", source)
                    receipt = stored_receipt(root, 0, "stderr")
                    finish_invalid(self, session)
                    self.assertFalse(receipt["eof"])
                    self.assertEqual(receipt["overflow_probe_hex"], "78")
                    self.assertIsNotNone(receipt["capture_error"])
                expected = b"x" * min(size, 8)
                self.assertEqual(receipt["byte_count"], len(expected))
                self.assertEqual(receipt["sha256"], hashlib.sha256(expected).hexdigest())
                self.assertEqual(source.received, size)
                for directory in ("raw", "backup"):
                    self.assertEqual((root / directory / "000000.stderr.bin").read_bytes(), expected)
                if size >= 8:
                    self.assertEqual(source.requests[-1], 1, "exact-bound EOF/overflow needs one probe")

    def test_total_budget_is_shared_across_calls_and_roles(self):
        for second_size in (4, 5, 6):
            with self.subTest(second_bytes=second_size):
                limits = capture.Limits(max_stream_bytes=10, max_total_bytes=12, chunk_bytes=3)
                plan = [call_spec("A", records=[]), call_spec("B", records=[])]
                root, session, _, _ = self.fresh(f"total-{second_size}", plan=plan, limits=limits)
                record_call(session, 0, plan[0], b"a" * 7)
                session.start_call(1)
                session.preserve_stream(1, "stdout", io.BytesIO(b""))
                source = TrackedReader(b"b" * second_size, 3)
                if second_size <= 5:
                    receipt = session.preserve_stream(1, "stderr", source)
                    session.finish_call(1, exit_code=0)
                    self.assertTrue(session.finalize()["complete"])
                    self.assertTrue(receipt["eof"])
                else:
                    with self.assertRaises(capture.CaptureError):
                        session.preserve_stream(1, "stderr", source)
                    receipt = stored_receipt(root, 1, "stderr")
                    finish_invalid(self, session, 1)
                    self.assertEqual(receipt["overflow_probe_hex"], "62")
                    self.assertFalse(receipt["eof"])
                self.assertEqual(receipt["byte_count"], min(second_size, 5))
                for directory in ("raw", "backup"):
                    self.assertEqual((root / directory / "000000.stderr.bin").read_bytes(), b"a" * 7)
                    self.assertEqual((root / directory / "000001.stderr.bin").read_bytes(),
                                     b"b" * min(second_size, 5))

    def test_short_reads_and_invalid_binary_reader_results(self):
        root, session, plan, limits = self.fresh("short-read", limits=capture.Limits(chunk_bytes=4))
        payload = record_bytes(plan[0]["stdout_records"])
        source = TrackedReader(payload, 4, short_read=1)
        session.start_call(0)
        receipt = session.preserve_stream(0, "stdout", source)
        session.preserve_stream(0, "stderr", io.BytesIO(b""))
        session.finish_call(0, exit_code=0)
        self.assertTrue(session.finalize()["complete"])
        self.assertTrue(receipt["eof"])
        self.assertEqual(source.received, len(payload))
        self.assertEqual(len(source.requests), len(payload) + 1)
        for variant in ("none", "text", "bytearray", "oversized", "error", "interrupt"):
            with self.subTest(reader=variant):
                root, session, plan, _ = self.fresh("reader-" + variant,
                    plan=[call_spec("A"), call_spec("B")], limits=limits)
                calls = []

                class BadReader:
                    def read(self, size):
                        calls.append(size)
                        if len(calls) == 1:
                            return b'{"'
                        if variant == "error":
                            raise OSError("injected-reader-error")
                        if variant == "interrupt":
                            raise KeyboardInterrupt("injected-reader-interrupt")
                        return {"none": None, "text": "x", "bytearray": bytearray(b"x"),
                                "oversized": b"x" * (size + 1)}[variant]

                session.start_call(0)
                expected_error = KeyboardInterrupt if variant == "interrupt" else capture.CaptureError
                with self.assertRaises(expected_error):
                    session.preserve_stream(0, "stdout", BadReader())
                receipt = stored_receipt(root, 0, "stdout")
                self.assertFalse(receipt["eof"])
                self.assertEqual(receipt["byte_count"], 2)
                self.assertIsNotNone(receipt["capture_error"])
                diagnostic = b"\x00\xffavailable"
                session.preserve_stream(0, "stderr", io.BytesIO(diagnostic))
                with self.assertRaises(capture.CaptureError):
                    session.finish_call(0, exit_code=1, failure="substitute failed")
                with self.assertRaises(capture.CaptureError):
                    session.start_call(1)
                result = session.finalize()
                self.assertFalse(result["complete"])
                self.assertEqual(len(calls), 2)
                for directory in ("raw", "backup"):
                    self.assertEqual((root / directory / "000000.stdout.bin").read_bytes(), b'{"')
                    self.assertEqual((root / directory / "000000.stderr.bin").read_bytes(), diagnostic)
                    self.assertFalse((root / directory / "000001.stdout.bin").exists())

    def test_real_sink_short_zero_flush_fsync_and_close_failures(self):
        for directory in ("backup", "raw"):
            for kind in ("short", "zero", "flush", "fsync", "close"):
                with self.subTest(directory=directory, fault=kind):
                    root, session, plan, _ = self.fresh(f"sink-{directory}-{kind}",
                                                       limits=capture.Limits(chunk_bytes=8))
                    raw = record_bytes(plan[0]["stdout_records"])
                    session.start_call(0)
                    path = root / directory / "000000.stdout.bin"
                    with faulty_output(path, kind):
                        with self.assertRaises(capture.CaptureError):
                            session.preserve_stream(0, "stdout", io.BytesIO(raw))
                    receipt = stored_receipt(root, 0, "stdout")
                    self.assertIsNotNone(receipt["capture_error"])
                    session.preserve_stream(0, "stderr", io.BytesIO(b"available"))
                    result = finish_invalid(self, session)
                    backup = (root / "backup/000000.stdout.bin").read_bytes()
                    primary = (root / "raw/000000.stdout.bin").read_bytes()
                    if kind in ("short", "zero"):
                        part = raw[:4] if kind == "short" else b""
                        self.assertEqual(backup, part if directory == "backup" else raw[:8])
                        self.assertEqual(primary, part if directory == "raw" else b"")
                        self.assertEqual(receipt["byte_count"], 8, "receipt tracks admitted input, not a repaired prefix")
                        self.assertFalse(receipt["eof"])
                        self.assertIn("short_write", result["first_failure"])
                    else:
                        self.assertEqual(backup, raw)
                        self.assertEqual(primary, raw)
                        self.assertTrue(receipt["eof"], "an I/O close fault must not erase observed EOF")
                        self.assertIn("injected-" + kind, result["first_failure"])
                    self.assertEqual((root / "backup/000000.stderr.bin").read_bytes(), b"available")

    def test_journal_sync_failure_keeps_the_other_current_stream(self):
        root = Path(self.tmp.name) / "journal-write-failure"
        original_open = capture._open_new
        handles = []

        def open_target(path):
            target = original_open(path)
            if Path(path) == root / "journal.jsonl":
                wrapped = FaultFile(target)
                handles.append(wrapped)
                return wrapped
            return target

        with mock.patch.object(capture, "_open_new", side_effect=open_target):
            plan = [call_spec("A"), call_spec("B")]
            session = capture.CaptureSession.create(root, identity=IDENTITY, plan=plan)
            session.start_call(0)
            handles[0].flush_error = "injected-journal-sync"
            raw = record_bytes(plan[0]["stdout_records"])
            with self.assertRaises(capture.CaptureError):
                session.preserve_stream(0, "stdout", io.BytesIO(raw))
            diagnostic = b"\xffopaque-after-journal-failure"
            with self.assertRaises(capture.CaptureError):
                session.preserve_stream(0, "stderr", io.BytesIO(diagnostic))
            with self.assertRaises(capture.CaptureError):
                session.finish_call(0, exit_code=1, failure="journal unavailable")
            with self.assertRaises(capture.CaptureError):
                session.start_call(1)
            result = session.finalize()
        self.assertFalse(result["complete"])
        self.assertIn("injected-journal-sync", result["first_failure"])
        for directory in ("raw", "backup"):
            self.assertEqual((root / directory / "000000.stdout.bin").read_bytes(), raw)
            self.assertEqual((root / directory / "000000.stderr.bin").read_bytes(), diagnostic)
            self.assertFalse((root / directory / "000001.stdout.bin").exists())

    def test_journal_torn_missing_duplicate_order_and_metadata_tampering(self):
        for variant in ("torn", "missing", "duplicate", "order", "call_id", "receipt"):
            with self.subTest(change=variant):
                root, session, plan, _ = self.fresh("journal-" + variant,
                                                   plan=[call_spec("A"), call_spec("B")])
                for index, spec in enumerate(plan):
                    record_call(session, index, spec)
                path = root / "journal.jsonl"
                lines = path.read_bytes().splitlines(keepends=True)
                if variant == "torn":
                    changed = b"".join(lines)[:-9]
                elif variant == "missing":
                    changed = b"".join(lines[:-1])
                elif variant == "duplicate":
                    changed = b"".join(lines[:2] + [lines[1]] + lines[3:])
                elif variant == "order":
                    changed = b"".join([lines[1], lines[0]] + lines[2:])
                else:
                    event = json.loads(lines[1])
                    if variant == "call_id":
                        event["call_id"] = "wrong"
                    else:
                        event["receipt"]["sha256"] = "0" * 64
                    lines[1] = json.dumps(event, separators=(",", ":")).encode() + b"\n"
                    changed = b"".join(lines)
                original_streams = {p: p.read_bytes() for folder in ("raw", "backup")
                                    for p in (root / folder).iterdir()}
                path.write_bytes(changed)
                result = session.finalize()
                self.assertFalse(result["complete"])
                self.assertEqual(path.read_bytes(), changed)
                self.assertTrue(all(p.read_bytes() == raw for p, raw in original_streams.items()))

    def test_existing_perstream_destinations_are_not_overwritten_or_reread(self):
        for directory in ("backup", "raw"):
            with self.subTest(existing=directory):
                root, session, plan, _ = self.fresh("existing-" + directory)
                session.start_call(0)
                path = root / directory / "000000.stdout.bin"
                path.write_bytes(b"original destination")
                source = TrackedReader(record_bytes(plan[0]["stdout_records"]), 65536)
                with self.assertRaises(capture.CaptureError):
                    session.preserve_stream(0, "stdout", source)
                session.preserve_stream(0, "stderr", io.BytesIO(b"other current stream"))
                finish_invalid(self, session)
                self.assertEqual(path.read_bytes(), b"original destination")
                self.assertEqual(source.requests, [], "a collision must not consume source bytes")
                self.assertEqual((root / "backup/000000.stderr.bin").read_bytes(), b"other current stream")

    def test_result_collision_and_publication_faults_return_no_successful_seal(self):
        for kind in ("existing", "short", "zero", "flush", "fsync", "close"):
            with self.subTest(publication=kind):
                root, session, plan, _ = self.fresh("result-" + kind)
                record_call(session, 0, plan[0])
                path = root / "result.json"
                published = []
                if kind == "existing":
                    path.write_bytes(b"pre-existing result")
                    with self.assertRaises(capture.CaptureError):
                        published.append(session.finalize())
                    self.assertEqual(path.read_bytes(), b"pre-existing result")
                else:
                    with faulty_output(path, kind):
                        with self.assertRaises(capture.CaptureError):
                            published.append(session.finalize())
                self.assertEqual(published, [])
                saved = path.read_bytes() if path.exists() else None
                with self.assertRaises(capture.CaptureError):
                    session.finalize()
                self.assertEqual(path.read_bytes() if path.exists() else None, saved)

    def test_empty_record_plan_and_required_null_key_are_distinct(self):
        variants = [
            ("empty", [], b"", True),
            ("blank", [], b"\n", False),
            ("null", [{"nullable": None}], b'{"nullable":null}\n', True),
            ("missing", [{"nullable": None}], b"{}\n", False),
            ("false", [{"nullable": None}], b'{"nullable":false}\n', False),
        ]
        for name, records, raw, valid in variants:
            with self.subTest(shape=name):
                root, session, _, _ = self.fresh("records-" + name, plan=[call_spec(records=records)])
                session.start_call(0)
                session.preserve_stream(0, "stdout", io.BytesIO(raw))
                session.preserve_stream(0, "stderr", io.BytesIO(b""))
                if valid:
                    session.finish_call(0, exit_code=0)
                    self.assertTrue(session.finalize()["complete"])
                else:
                    finish_invalid(self, session)
                self.assertEqual((root / "backup/000000.stdout.bin").read_bytes(), raw)

    def test_invalid_state_transitions_latch_without_rewriting_streams(self):
        for variant in ("finish_before_start", "stream_before_start", "start_wrong_index",
                        "boolean_index", "duplicate_start", "unknown_role", "duplicate_stream"):
            with self.subTest(transition=variant):
                root, session, plan, _ = self.fresh("state-" + variant,
                                                   plan=[call_spec("A"), call_spec("B")])
                source = TrackedReader(b"must not be consumed", 65536)
                if variant in ("duplicate_start", "unknown_role", "duplicate_stream"):
                    session.start_call(0)
                if variant == "duplicate_stream":
                    session.preserve_stream(0, "stdout", io.BytesIO(record_bytes(plan[0]["stdout_records"])))
                before = {p: p.read_bytes() for folder in ("raw", "backup")
                          for p in (root / folder).iterdir()}
                action = {
                    "finish_before_start": lambda: session.finish_call(0, exit_code=0),
                    "stream_before_start": lambda: session.preserve_stream(0, "stdout", source),
                    "start_wrong_index": lambda: session.start_call(1),
                    "boolean_index": lambda: session.start_call(False),
                    "duplicate_start": lambda: session.start_call(0),
                    "unknown_role": lambda: session.preserve_stream(0, "stdout-alias", source),
                    "duplicate_stream": lambda: session.preserve_stream(0, "stdout", source),
                }[variant]
                with self.assertRaises(capture.CaptureError):
                    action()
                with self.assertRaises(capture.CaptureError):
                    session.start_call(1)
                self.assertFalse(session.finalize()["complete"])
                self.assertEqual(source.requests, [])
                self.assertTrue(all(p.read_bytes() == raw for p, raw in before.items()))

    def test_stream_hardlink_and_symlink_aliases_remain_invalid(self):
        for variant in ("hardlink", "symlink"):
            with self.subTest(alias=variant):
                root, session, plan, _ = self.fresh("file-alias-" + variant)
                record_call(session, 0, plan[0])
                raw = root / "raw/000000.stdout.bin"
                backup = root / "backup/000000.stdout.bin"
                original = raw.read_bytes()
                backup.unlink()
                try:
                    if variant == "hardlink":
                        os.link(raw, backup)
                    else:
                        backup.symlink_to(raw)
                except (OSError, NotImplementedError) as error:
                    session.finalize()
                    self.skipTest(f"{variant} unavailable: {type(error).__name__}")
                self.assertFalse(session.finalize()["complete"])
                self.assertEqual(raw.read_bytes(), original)
                self.assertEqual(backup.read_bytes(), original)
                if variant == "hardlink":
                    self.assertEqual(raw.stat().st_nlink, 2)
                else:
                    self.assertTrue(backup.is_symlink())

    def test_invalid_plans_and_limit_types_reject_before_output_creation(self):
        malformed = [
            [],
            [call_spec("same"), call_spec("same")],
            [call_spec(records=[{}])],
            [{"call_id": "A", "stdout_records": None, "stderr_empty": True}],
            [{"call_id": "A", "stderr_empty": True}],
        ]
        for index, plan in enumerate(malformed):
            with self.subTest(plan=index):
                root = Path(self.tmp.name) / f"invalid-plan-{index}"
                with self.assertRaises((ValueError, capture.CaptureError)):
                    capture.CaptureSession.create(root, identity=IDENTITY, plan=plan)
                self.assertFalse(root.exists())
        for value in (0, -1, True, 1024 * 1024 + 1):
            with self.subTest(stream_limit=value):
                with self.assertRaises(ValueError):
                    capture.Limits(max_stream_bytes=value)

    def test_journal_budget_exact_fit_and_one_byte_too_small(self):
        control_root, control, plan, _ = self.fresh("journal-size-control")
        record_call(control, 0, plan[0])
        control_seal = control.finalize()
        self.assertTrue(control_seal["complete"])
        length = len((control_root / "journal.jsonl").read_bytes())
        for maximum in (length, length - 1):
            with self.subTest(journal_budget=maximum):
                root, session, _, _ = self.fresh(f"journal-budget-{maximum}", plan=plan,
                                                limits=capture.Limits(max_journal_bytes=maximum))
                if maximum == length:
                    record_call(session, 0, plan[0])
                    self.assertTrue(session.finalize()["complete"])
                else:
                    with self.assertRaises(capture.CaptureError):
                        record_call(session, 0, plan[0])
                    self.assertFalse(session.finalize()["complete"])
                self.assertLessEqual((root / "journal.jsonl").stat().st_size, maximum)

    def test_exit_claim_bounds_and_boolean_type_are_enforced(self):
        accepted = (-(2 ** 31), -1, 2 ** 32 - 1, None)
        rejected = (-(2 ** 31) - 1, 2 ** 32, False, True)
        for index, value in enumerate(accepted + rejected):
            with self.subTest(exit_claim=value):
                root, session, plan, _ = self.fresh(f"exit-range-{index}")
                session.start_call(0)
                session.preserve_stream(0, "stdout", io.BytesIO(record_bytes(plan[0]["stdout_records"])))
                session.preserve_stream(0, "stderr", io.BytesIO(b""))
                with self.assertRaises(capture.CaptureError):
                    session.finish_call(0, exit_code=value)
                result = session.finalize()
                self.assertFalse(result["complete"])
                events = [json.loads(line) for line in (root / "journal.jsonl").read_bytes().splitlines()]
                finished = [event for event in events if event["event"] == "finish"]
                if index < len(accepted):
                    self.assertEqual(len(finished), 1, "valid nonzero/unknown exit is recorded as failure")
                    self.assertIs(type(finished[0]["exit_code"]), type(value))
                    self.assertEqual(finished[0]["exit_code"], value)
                else:
                    self.assertEqual(finished, [], "invalid exit field cannot form a finish record")

    def test_offline_seal_is_required_and_audit_does_not_rewrite_files(self):
        root, session, plan, limits = self.fresh("external-seal")
        record_call(session, 0, plan[0])
        seal = session.finalize()
        before = {p: p.read_bytes() for p in root.rglob("*") if p.is_file()}
        arguments = dict(identity=IDENTITY, plan=plan, limits=limits)
        with self.assertRaises(TypeError):
            capture.audit_existing(root, **arguments)
        for invalid in (None, "", "g" * 64, True):
            with self.subTest(seal=invalid):
                with self.assertRaises(ValueError):
                    capture.audit_existing(root, **arguments, expected_seal_sha256=invalid)
        self.assertFalse(capture.audit_existing(root, **arguments, expected_seal_sha256="0" * 64)["complete"])
        self.assertTrue(capture.audit_existing(root, **arguments,
                                             expected_seal_sha256=seal["seal_sha256"])["complete"])
        self.assertTrue(all(p.read_bytes() == raw for p, raw in before.items()))


if __name__ == "__main__":
    unittest.main()
