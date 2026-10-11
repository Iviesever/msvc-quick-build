"""Synthetic owner contracts; never read the retained 011 image or run native code."""

import hashlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

try:
    import run_bcrypt_pe_diagnosis_012 as owner
except ModuleNotFoundError as error:
    if error.name != "run_bcrypt_pe_diagnosis_012":
        raise
    owner = None


def encoded(value):
    return json.dumps(value, ensure_ascii=True, allow_nan=False,
                      sort_keys=True, separators=(",", ":")).encode("ascii")


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


CORE = {"schema": "bcrypt-refusal-context-012-v1", "status": "located_refusal",
        "parser_calls": 1, "reason": "UNWIND_OPCODE_INVALID",
        "exception_type": "PeValidationError", "context_unavailable_reason": None,
        "context": {"synthetic": True}, "context_work_units": 12,
        "static_index_admitted": False}


class Clock:
    def __init__(self):
        self.now = 10_000_000_000

    def __call__(self):
        return self.now


class Process:
    def __init__(self, *, running=False):
        self.stdin = io.BytesIO()
        self.stdout = types.SimpleNamespace(fileno=lambda: 123, close=lambda: None)
        self.pid = 987654
        self.returncode = None if running else 0
        self.kills = 0
        self.sent = b""
        original_close = self.stdin.close

        def close():
            self.sent = self.stdin.getvalue()
            original_close()
        self.stdin.close = close

    def poll(self):
        return self.returncode

    def kill(self):
        self.kills += 1
        self.returncode = -9

    def wait(self, timeout=None):
        if self.returncode is None:
            raise owner.subprocess.TimeoutExpired("synthetic", timeout)
        return self.returncode


class OwnerTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(owner, "012 exclusive offline owner is not implemented")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.image = self.base / "synthetic-image.bytes"
        self.image.write_bytes(b"synthetic bytes, not the retained image")
        self.called = self.base / "diagnose-called"
        self.context = self.base / "producer_bcrypt_refusal_012.py"
        self.context.write_text(
            "from pathlib import Path\n"
            "def diagnose(module, image_bytes, init_rva):\n"
            f"    Path({str(self.called)!r}).write_bytes(b'called')\n"
            f"    return {CORE!r}\n", encoding="utf-8")
        native = Path(owner.__file__).resolve().parent
        python = identity(sys.executable)
        python.update(implementation=sys.implementation.name,
                      version=list(sys.version_info))
        self.manifest = {
            "schema": "bcrypt-pe-diagnosis-012-manifest-v1", "stage": "refusal_context",
            "call_id": "012-refusal-context-1",
            "input": dict(identity(self.image), init_rva=0x1010),
            "sources": {"owner": identity(native / "run_bcrypt_pe_diagnosis_012.py"),
                        "io": identity(native / "bcrypt_diagnosis_io_012.py"),
                        "context": identity(self.context),
                        "parser": identity(native / "producer_bcrypt_pe.py")},
            "python": python,
            "argv_prefix": [python["path"], "-I", "-S", "-B",
                            str(native / "run_bcrypt_pe_diagnosis_012.py"), "--worker"],
            "output_dir": str(self.base / "output"), "limits": dict(owner.LIMITS)}
        self.manifest_path = self.base / "manifest.json"
        self.freeze()

    def freeze(self):
        raw = encoded(self.manifest)
        self.manifest_path.write_bytes(raw)
        self.sha = hashlib.sha256(raw).hexdigest()

    def packet(self, core=None):
        raw = encoded(CORE if core is None else core)
        return b"D" + struct.pack("<I", len(raw)) + raw

    def controlled(self, *, chunks=None, startup_ns=0, stall=False,
                   process=None, popen_error=None, after_read=None,
                   late_fsync_ns=0, cli=False):
        clock = Clock()
        process = Process(running=stall) if process is None else process
        stream = list(chunks if chunks is not None else
                      [b"R" + bytes.fromhex(self.sha), self.packet(), b""])
        observed = []

        class Selector:
            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def register(self, *_args):
                return None

            def select(self, timeout):
                if stall:
                    clock.now += int(timeout * 1_000_000_000) + 1
                    return []
                return [(types.SimpleNamespace(fileobj=process.stdout), 1)]

        def launch(argv, **kwargs):
            marker = Path(self.manifest["output_dir"]) / "consumed.json"
            value = json.loads(marker.read_bytes())
            self.assertEqual(value["owner_pid"], os.getpid())
            self.assertEqual(value["manifest_sha256"], self.sha)
            self.assertEqual(value["parser_call_reservations"], 1)
            self.assertGreaterEqual(len(observed), 2, "marker not fsynced before launch")
            self.assertEqual(argv, self.manifest["argv_prefix"] +
                             [str(self.manifest_path), self.sha])
            self.assertIs(kwargs["shell"], False)
            self.assertEqual(kwargs["stderr"], owner.subprocess.STDOUT)
            clock.now += startup_ns
            if popen_error is not None:
                raise popen_error
            return process

        def read(_fd, count):
            value = stream.pop(0)
            chunk, rest = value[:count], value[count:]
            if rest:
                stream.insert(0, rest)
            if after_read is not None:
                after_read(chunk)
            return chunk

        real_fsync = os.fsync

        def sync(fd):
            observed.append(fd)
            real_fsync(fd)
            if len(observed) == 4:
                clock.now += late_fsync_ns

        with patch.object(owner.time, "monotonic_ns", clock), \
                patch.object(owner.subprocess, "Popen", side_effect=launch) as launch_mock, \
                patch.object(owner.selectors, "DefaultSelector", Selector), \
                patch.object(owner.os, "read", side_effect=read), \
                patch.object(owner.os, "fsync", side_effect=sync):
            if cli:
                process.cli_stderr = io.StringIO()
                with patch.object(owner.sys, "argv", [owner.__file__, str(self.manifest_path), self.sha]), \
                        patch.object(owner.sys, "stderr", process.cli_stderr):
                    process.cli_exit = owner.main()
                report = json.loads((Path(self.manifest["output_dir"]) / "report.json").read_bytes())
            else:
                report = owner.run_once(str(self.manifest_path), self.sha)
        self.assertEqual(launch_mock.call_count, 1)
        raw = (Path(self.manifest["output_dir"]) / "report.json").read_bytes()
        self.assertEqual(json.loads(raw), report)
        self.assertLessEqual(len(raw), 15360)
        return report, process

    def rejected_before_launch(self, code):
        with patch.object(owner.subprocess, "Popen") as launch:
            with self.assertRaisesRegex(ValueError, code):
                owner.run_once(str(self.manifest_path), self.sha)
            launch.assert_not_called()

    def marker(self, *, sha=None, pid=None):
        output = Path(self.manifest["output_dir"])
        output.mkdir()
        value = {"schema": "bcrypt-pe-diagnosis-012-consumed-v1",
                 "manifest_sha256": self.sha if sha is None else sha,
                 "owner_pid": os.getppid() if pid is None else pid,
                 "call_id": "012-refusal-context-1", "parser_call_reservations": 1}
        (output / "consumed.json").write_bytes(encoded(value))

    def worker(self, go=b""):
        output = io.BytesIO()
        with patch.object(owner.sys, "stdin", types.SimpleNamespace(buffer=io.BytesIO(go))), \
                patch.object(owner.sys, "stdout", types.SimpleNamespace(buffer=output)):
            code = owner.worker(str(self.manifest_path), self.sha)
        return code, output.getvalue()

    def test_valid_result_is_exclusive_bounded_and_still_not_pe_admission(self):
        result, process = self.controlled()
        self.assertEqual(result["status"], "located_refusal")
        self.assertFalse(result["static_index_admitted"])
        self.assertEqual(result["parser_calls"], 1)
        self.assertEqual(result["core"], CORE)
        self.assertEqual(len(process.sent), 9)
        self.assertEqual(process.sent[:1], b"G")
        self.assertEqual(process.kills, 0)
        output = Path(self.manifest["output_dir"])
        self.assertEqual({path.name for path in output.iterdir()},
                         {"consumed.json", "report.json"})
        self.assertLessEqual(sum(p.stat().st_size for p in output.iterdir()), 16384)
        self.assertTrue(all(v["post_verified"] for v in result["identity_checks"].values()))

    def test_changed_manifest_is_rejected_before_reservation(self):
        self.manifest_path.write_bytes(self.manifest_path.read_bytes() + b" ")
        self.rejected_before_launch("MANIFEST_HASH")
        self.assertFalse(Path(self.manifest["output_dir"]).exists())

    def test_changed_input_is_rejected_before_reservation(self):
        self.image.write_bytes(b"changed")
        self.rejected_before_launch("IDENTITY_INPUT")

    def test_changed_source_is_rejected_before_reservation(self):
        self.context.write_bytes(b"changed")
        self.rejected_before_launch("IDENTITY_CONTEXT")

    def test_wrong_tool_identity_is_rejected_before_reservation(self):
        self.manifest["python"]["sha256"] = "0" * 64
        self.freeze()
        self.rejected_before_launch("IDENTITY_PYTHON")

    def test_duplicate_manifest_key_is_rejected(self):
        raw = encoded(self.manifest)
        raw = b'{"stage":"refusal_context",' + raw[1:]
        self.manifest_path.write_bytes(raw)
        self.sha = hashlib.sha256(raw).hexdigest()
        self.rejected_before_launch("JSON_DUPLICATE")

    def test_limits_are_fixed_and_bool_is_not_an_integer(self):
        for key, value in (("seconds", 6), ("parser_calls", True),
                           ("stream_bytes", 8231), ("traceback_nodes", 65)):
            with self.subTest(key=key):
                original = self.manifest["limits"][key]
                self.manifest["limits"][key] = value
                self.freeze()
                self.rejected_before_launch("LIMITS")
                self.manifest["limits"][key] = original

    def test_unlisted_argv_and_extra_manifest_fields_are_rejected(self):
        self.manifest["argv_prefix"].append("--another-command")
        self.freeze()
        self.rejected_before_launch("ARGV")
        self.manifest["argv_prefix"].pop()
        self.manifest["retry"] = True
        self.freeze()
        self.rejected_before_launch("MANIFEST_FIELDS")

    def test_existing_directory_or_report_refuses_reentry(self):
        output = Path(self.manifest["output_dir"])
        output.mkdir()
        (output / "report.json").write_bytes(b"old")
        self.rejected_before_launch("OUTPUT_EXISTS")
        self.assertEqual((output / "report.json").read_bytes(), b"old")

    def test_consumed_marker_cannot_be_bypassed_by_a_new_manifest_hash(self):
        self.controlled()
        output = Path(self.manifest["output_dir"])
        before = {path.name: path.read_bytes() for path in output.iterdir()}
        self.manifest["input"]["init_rva"] += 1
        self.freeze()
        self.rejected_before_launch("OUTPUT_EXISTS")
        self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()}, before)

    def test_late_process_creation_never_sends_go_and_consumes_once(self):
        result, process = self.controlled(startup_ns=5_000_000_001,
                                          process=Process(running=True))
        self.assertEqual(result["failure_code"], "DEADLINE")
        self.assertFalse(result["worker"]["go_sent"])
        self.assertEqual(process.sent, b"")
        self.assertEqual(process.kills, 1)

    def test_timeout_kills_only_owned_process_and_does_not_retry(self):
        result, process = self.controlled(stall=True)
        self.assertEqual(result["failure_code"], "DEADLINE")
        self.assertEqual(process.kills, 1)
        self.assertEqual(result["worker"]["pid"], process.pid)
        self.assertEqual(result["parser_call_reservations"], 1)

    def test_process_creation_failure_preserves_consumed_evidence(self):
        result, _process = self.controlled(popen_error=OSError("synthetic"))
        self.assertEqual(result["failure_code"], "PROCESS_CREATE")
        self.assertIsNone(result["parser_calls"])

    def test_exception_text_cannot_exceed_the_failure_report_budget(self):
        try:
            result, _process = self.controlled(popen_error=ValueError("x" * 20000))
        except ValueError:
            self.fail("unbounded exception text prevented the mandatory bounded failure report")
        self.assertEqual(result["status"], "failed_consumed")
        self.assertLessEqual(len(result["failure_code"]), 64)

    def test_delayed_report_fsync_requires_failed_cli_even_with_located_report(self):
        result, process = self.controlled(late_fsync_ns=6_000_000_000, cli=True)
        self.assertEqual(result["status"], "located_refusal")
        self.assertTrue(result["completion_requires_clean_owner_exit"])
        self.assertEqual(process.cli_exit, 2)
        self.assertIn("DURABILITY_OR_OBSERVATION_LATE", process.cli_stderr.getvalue())
        self.assertTrue(result["consumed"])
        self.rejected_before_launch("OUTPUT_EXISTS")

    def test_wrong_ready_identity_is_rejected_without_go(self):
        result, process = self.controlled(chunks=[b"R" + b"\0" * 32])
        self.assertEqual(result["failure_code"], "READY")
        self.assertEqual(process.sent, b"")

    def test_result_before_go_is_rejected(self):
        result, process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha) + self.packet()])
        self.assertEqual(result["failure_code"], "EARLY_RESULT")
        self.assertEqual(process.sent, b"")

    def test_non_protocol_stderr_is_rejected(self):
        result, process = self.controlled(chunks=[b"Traceback: unexpected stderr"])
        self.assertEqual(result["failure_code"], "READY")
        self.assertEqual(process.sent, b"")

    def test_fragmented_protocol_is_accepted(self):
        ready = b"R" + bytes.fromhex(self.sha)
        packet = self.packet()
        result, _process = self.controlled(
            chunks=[ready[:2], ready[2:], packet[:3], packet[3:40], packet[40:], b""])
        self.assertEqual(result["status"], "located_refusal")

    def test_declared_payload_cap_is_rejected_before_buffer_growth(self):
        result, _process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha), b"D" + struct.pack("<I", 8193)])
        self.assertEqual(result["failure_code"], "CORE_SIZE")
        self.assertLessEqual(result["worker"]["stream_bytes"], 8231)

    def test_exact_payload_and_stream_caps_are_admitted_without_losing_metadata(self):
        core = dict(CORE, context={"synthetic": ""})
        core["context"]["synthetic"] = "x" * (8192 - len(encoded(core)))
        self.assertEqual(len(encoded(core)), 8192)
        result, _process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha), self.packet(core), b""])
        self.assertEqual(result["status"], "located_refusal")
        self.assertEqual(result["worker"]["stream_bytes"], 8230)
        self.assertEqual(result["core"], core)

    def test_actual_stream_cap_is_bounded_and_preserved(self):
        result, _process = self.controlled(chunks=[b"R" + bytes.fromhex(self.sha),
                                                  b"D" + struct.pack("<I", 8192),
                                                  b" " * 20000])
        self.assertEqual(result["failure_code"], "STREAM_LIMIT")
        self.assertEqual(result["worker"]["stream_bytes"], 8231)
        self.assertLessEqual(len(result["worker"]["stream_prefix_hex"]), 256)

    def test_extra_bytes_and_early_eof_are_rejected(self):
        for suffix, code in ((b"x", "PROTOCOL_LENGTH"), (None, "PROTOCOL_EOF")):
            with self.subTest(code=code):
                self.manifest["output_dir"] = str(self.base / code)
                self.freeze()
                packet = self.packet() + suffix if suffix is not None else self.packet()[:-1]
                result, _process = self.controlled(
                    chunks=[b"R" + bytes.fromhex(self.sha), packet, b""])
                self.assertEqual(result["failure_code"], code)

    def test_bad_core_schema_cannot_admit_an_index(self):
        for key, value in (("static_index_admitted", True), ("parser_calls", True),
                           ("context_work_units", 16385), ("schema", "other")):
            with self.subTest(key=key):
                self.manifest["output_dir"] = str(self.base / key)
                self.freeze()
                bad = dict(CORE, **{key: value})
                result, _process = self.controlled(
                    chunks=[b"R" + bytes.fromhex(self.sha), self.packet(bad), b""])
                self.assertEqual(result["failure_code"], "CORE_SCHEMA")
                self.assertFalse(result["static_index_admitted"])

    def test_unexpected_core_outcome_is_retained_and_stops(self):
        core = dict(CORE, status="unexpected_success", context=None,
                    reason=None, exception_type=None)
        result, _process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha), self.packet(core), b""])
        self.assertEqual(result["status"], "failed_consumed")
        self.assertEqual(result["core"]["status"], "unexpected_success")

    def test_nonzero_worker_exit_does_not_turn_payload_into_success(self):
        process = Process()
        process.returncode = 7
        result, _process = self.controlled(process=process)
        self.assertEqual(result["failure_code"], "PROCESS_EXIT")

    def test_source_postcheck_detects_change_after_go(self):
        def change(chunk):
            if chunk == b"":
                self.context.write_bytes(b"changed after observation")
        result, _process = self.controlled(after_read=change)
        self.assertEqual(result["failure_code"], "IDENTITY_CONTEXT")
        self.assertEqual(result["status"], "failed_consumed")

    def test_manifest_postcheck_detects_change_after_go(self):
        def change(chunk):
            if chunk == b"":
                self.manifest_path.write_bytes(self.manifest_path.read_bytes() + b" ")
        result, _process = self.controlled(after_read=change)
        self.assertEqual(result["failure_code"], "MANIFEST_HASH")
        self.assertEqual(result["status"], "failed_consumed")

    def test_private_worker_without_marker_does_not_diagnose(self):
        code, raw = self.worker()
        self.assertNotEqual(code, 0)
        self.assertEqual(raw, b"")
        self.assertFalse(self.called.exists())

    def test_private_worker_rejects_wrong_marker_hash_and_parent(self):
        for key in ("sha", "pid"):
            with self.subTest(key=key):
                self.manifest["output_dir"] = str(self.base / key)
                self.freeze()
                self.marker(**{key: "0" * 64 if key == "sha" else os.getppid() + 1})
                code, raw = self.worker()
                self.assertNotEqual(code, 0)
                self.assertEqual(raw, b"")
                self.assertFalse(self.called.exists())

    def test_private_worker_without_go_never_diagnoses(self):
        self.marker()
        code, raw = self.worker()
        self.assertNotEqual(code, 0)
        self.assertEqual(raw, b"R" + bytes.fromhex(self.sha))
        self.assertFalse(self.called.exists())

    def test_expired_or_wrong_go_never_diagnoses(self):
        for go in (b"X" + struct.pack("<Q", 1), b"G" + struct.pack("<Q", 1)):
            with self.subTest(go=go):
                self.manifest["output_dir"] = str(self.base / go[:1].hex())
                self.freeze()
                self.marker()
                code, _raw = self.worker(go)
                self.assertNotEqual(code, 0)
                self.assertFalse(self.called.exists())

    def test_real_isolated_cpython_worker_observes_only_synthetic_fixture(self):
        from test_producer_bcrypt_pe import fixture, put
        data, at = fixture()
        self.image.write_bytes(put(data, at["rdata"] + 0x505, 0x21, "<B"))
        native = Path(owner.__file__).resolve().parent
        self.manifest["input"] = dict(identity(self.image), init_rva=at["init_rva"])
        self.manifest["sources"]["context"] = identity(native / "producer_bcrypt_refusal_012.py")
        self.freeze()
        result = owner.run_once(str(self.manifest_path), self.sha)
        self.assertEqual(result["status"], "located_refusal")
        self.assertEqual(result["parser_calls"], 1)
        self.assertTrue(result["worker"]["ready"])
        self.assertTrue(result["worker"]["go_sent"])
        self.assertTrue(result["worker"]["reaped"])
        self.assertEqual(result["worker"]["returncode"], 0)
        self.assertLessEqual(result["worker"]["stream_bytes"], 8230)
        self.assertEqual(result["core"]["context"]["guard"]["line"], 427)
        self.assertFalse(result["static_index_admitted"])
        self.assertEqual(json.loads((Path(self.manifest["output_dir"]) / "report.json").read_bytes()),
                         result)


if __name__ == "__main__":
    unittest.main()
