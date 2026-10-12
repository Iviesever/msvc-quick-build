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
    import run_bcrypt_pe_index_013 as owner
except ModuleNotFoundError as error:
    if error.name != "run_bcrypt_pe_index_013":
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


CORE = {"schema": "bcrypt-pe-core-013-v1", "status": "parser_refused",
        "parser_calls": 1, "reason": "UNWIND_OPCODE_INVALID",
        "exception_type": "PeValidationError", "refusal_context": None,
        "static_index_admitted": False, "index": None}


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
        self.assertIsNotNone(owner, "013 exclusive complete-index owner is not implemented")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.image = self.base / "synthetic-image.bytes"
        self.image.write_bytes(b"synthetic bytes, not the retained image")
        self.called = self.base / "observe-called"
        self.observation = self.base / "bcrypt_index_observation_013.py"
        self.observation.write_text(
            "from pathlib import Path\n"
            "def observe(module, image_bytes, init_rva):\n"
            f"    Path({str(self.called)!r}).write_bytes(b'called')\n"
            f"    return {CORE!r}\n", encoding="utf-8")
        native = Path(owner.__file__).resolve().parent
        python = identity(sys.executable)
        python.update(implementation=sys.implementation.name,
                      version=list(sys.version_info))
        self.manifest = {
            "schema": "bcrypt-pe-index-013-manifest-v1", "stage": "complete_index",
            "call_id": "013-complete-index-1",
            "input": dict(identity(self.image), init_rva=0x1010),
            "sources": {"owner": identity(native / "run_bcrypt_pe_index_013.py"),
                        "io": identity(native / "bcrypt_index_io_013.py"),
                        "codec": identity(native / "bcrypt_index_codec_013.py"),
                        "observation": identity(self.observation),
                        "parser": identity(native / "producer_bcrypt_pe_013.py")},
            "python": python,
            "argv_prefix": [python["path"], "-I", "-S", "-B",
                            str(native / "run_bcrypt_pe_index_013.py"), "--worker"],
            "output_dir": str(self.base / "output"), "limits": dict(owner.LIMITS),
            "codec_profiles": self.profiles()}
        self.manifest_path = self.base / "manifest.json"
        self.freeze()

    def profiles(self):
        import bcrypt_index_codec_013 as codec
        return {name: dict(getattr(codec, name.upper() + "_LIMITS"))
                for name in ("core", "report", "manifest", "marker")}

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
        self.assertLessEqual(len(raw), owner.LIMITS["report_bytes"])
        return report, process

    def rejected_before_launch(self, code):
        with patch.object(owner.subprocess, "Popen") as launch:
            with self.assertRaisesRegex(ValueError, code):
                owner.run_once(str(self.manifest_path), self.sha)
            launch.assert_not_called()

    def marker(self, *, sha=None, pid=None):
        output = Path(self.manifest["output_dir"])
        output.mkdir()
        value = {"schema": "bcrypt-pe-index-013-consumed-v1",
                 "manifest_sha256": self.sha if sha is None else sha,
                 "owner_pid": os.getppid() if pid is None else pid,
                 "call_id": "013-complete-index-1", "parser_call_reservations": 1}
        (output / "consumed.json").write_bytes(encoded(value))

    def worker(self, go=b""):
        output = io.BytesIO()
        with patch.object(owner.sys, "stdin", types.SimpleNamespace(buffer=io.BytesIO(go))), \
                patch.object(owner.sys, "stdout", types.SimpleNamespace(buffer=output)):
            code = owner.worker(str(self.manifest_path), self.sha)
        return code, output.getvalue()

    def test_valid_result_is_exclusive_bounded_and_still_not_pe_admission(self):
        result, process = self.controlled()
        self.assertEqual(result["status"], "observed_refusal")
        self.assertFalse(result["static_index_admitted"])
        self.assertEqual(result["parser_calls"], 1)
        self.assertEqual(result["core"], CORE)
        self.assertEqual(len(process.sent), 9)
        self.assertEqual(process.sent[:1], b"G")
        self.assertEqual(process.kills, 0)
        output = Path(self.manifest["output_dir"])
        self.assertEqual({path.name for path in output.iterdir()},
                         {"consumed.json", "report.json"})
        self.assertLessEqual(sum(p.stat().st_size for p in output.iterdir()), owner.LIMITS["persistent_bytes"])
        self.assertTrue(all(v["post_verified"] for v in result["identity_checks"].values()))

    def test_changed_manifest_is_rejected_before_reservation(self):
        self.manifest_path.write_bytes(self.manifest_path.read_bytes() + b" ")
        self.rejected_before_launch("MANIFEST_HASH")
        self.assertFalse(Path(self.manifest["output_dir"]).exists())

    def test_changed_input_is_rejected_before_reservation(self):
        self.image.write_bytes(b"changed")
        self.rejected_before_launch("IDENTITY_INPUT")

    def test_changed_source_is_rejected_before_reservation(self):
        self.observation.write_bytes(b"changed")
        self.rejected_before_launch("IDENTITY_OBSERVATION")

    def test_wrong_tool_identity_is_rejected_before_reservation(self):
        self.manifest["python"]["sha256"] = "0" * 64
        self.freeze()
        self.rejected_before_launch("IDENTITY_PYTHON")

    def test_duplicate_manifest_key_is_rejected(self):
        raw = encoded(self.manifest)
        raw = b'{"stage":"complete_index",' + raw[1:]
        self.manifest_path.write_bytes(raw)
        self.sha = hashlib.sha256(raw).hexdigest()
        self.rejected_before_launch("JSON_DUPLICATE")

    def test_limits_are_fixed_and_bool_is_not_an_integer(self):
        for key, value in (("seconds", 6), ("parser_calls", True),
                           ("stream_bytes", 67108903), ("traceback_nodes", 65)):
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
        self.assertEqual(result["status"], "observed_refusal")
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
        self.assertEqual(result["status"], "observed_refusal")

    def test_declared_payload_cap_is_rejected_before_buffer_growth(self):
        result, _process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha), b"D" + struct.pack("<I", owner.LIMITS["core_bytes"]+1)])
        self.assertEqual(result["failure_code"], "CORE_SIZE")
        self.assertLessEqual(result["worker"]["stream_bytes"], owner.LIMITS["stream_bytes"]+1)

    def test_stream_overflow_probe_is_rejected_with_a_smaller_protocol_profile(self):
        cap = 256
        with patch.dict(owner.LIMITS, stream_bytes=cap, core_bytes=cap-38):
            self.manifest["limits"] = dict(owner.LIMITS)
            self.freeze()
            result, _process = self.controlled(chunks=[b"R" + bytes.fromhex(self.sha),
                b"D" + struct.pack("<I", cap-38), b" " * 512])
        self.assertEqual(result["failure_code"], "STREAM_LIMIT")
        self.assertEqual(result["worker"]["stream_bytes"], cap+1)
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
                           ("schema", "other")):
            with self.subTest(key=key):
                self.manifest["output_dir"] = str(self.base / key)
                self.freeze()
                bad = dict(CORE, **{key: value})
                result, _process = self.controlled(
                    chunks=[b"R" + bytes.fromhex(self.sha), self.packet(bad), b""])
                self.assertEqual(result["failure_code"], "CORE_SCHEMA")
                self.assertFalse(result["static_index_admitted"])

    def test_unexpected_core_outcome_is_retained_and_stops(self):
        core = dict(CORE, status="parser_error", reason="PARSER_EXCEPTION",
                    exception_type="Exception")
        result, _process = self.controlled(
            chunks=[b"R" + bytes.fromhex(self.sha), self.packet(core), b""])
        self.assertEqual(result["status"], "failed_consumed")
        self.assertEqual(result["core"]["status"], "parser_error")

    def test_nonzero_worker_exit_does_not_turn_payload_into_success(self):
        process = Process()
        process.returncode = 7
        result, _process = self.controlled(process=process)
        self.assertEqual(result["failure_code"], "PROCESS_EXIT")

    def test_source_postcheck_detects_change_after_go(self):
        def change(chunk):
            if chunk == b"":
                self.observation.write_bytes(b"changed after observation")
        result, _process = self.controlled(after_read=change)
        self.assertEqual(result["failure_code"], "IDENTITY_OBSERVATION")
        self.assertEqual(result["status"], "failed_consumed")

    def test_manifest_postcheck_detects_change_after_go(self):
        def change(chunk):
            if chunk == b"":
                self.manifest_path.write_bytes(self.manifest_path.read_bytes() + b" ")
        result, _process = self.controlled(after_read=change)
        self.assertEqual(result["failure_code"], "MANIFEST_HASH")
        self.assertEqual(result["status"], "failed_consumed")

    def test_private_worker_without_marker_does_not_observe(self):
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

    def test_private_worker_without_go_never_observes(self):
        self.marker()
        code, raw = self.worker()
        self.assertNotEqual(code, 0)
        self.assertEqual(raw, b"R" + bytes.fromhex(self.sha))
        self.assertFalse(self.called.exists())

    def test_expired_or_wrong_go_never_observes(self):
        for go in (b"X" + struct.pack("<Q", 1), b"G" + struct.pack("<Q", 1)):
            with self.subTest(go=go):
                self.manifest["output_dir"] = str(self.base / go[:1].hex())
                self.freeze()
                self.marker()
                code, _raw = self.worker(go)
                self.assertNotEqual(code, 0)
                self.assertFalse(self.called.exists())

    def install_synthetic_image(self, *, escaped_names=False):
        from test_producer_bcrypt_pe import fixture, put
        data, at = fixture()
        if escaped_names:
            changed = bytearray(data)
            changed[at["rdata"]+0x600:at["rdata"]+0x800] = b'"' * 511 + b"\0"
            changed[at["rdata"]+0x800:at["rdata"]+0xA02] = b"\0\0" + b"\\" * 511 + b"\0"
            data = put(changed, at["rdata"]+12, at["rdata_rva"]+0x600)
            for offset in (at["rdata"]+0x100, at["data"]):
                data = put(data, offset, at["rdata_rva"]+0x800, "<Q")
        self.image.write_bytes(data)
        self.manifest["input"] = dict(identity(self.image), init_rva=at["init_rva"])
        return data, at

    def test_real_isolated_worker_observes_a_complete_synthetic_index(self):
        data, at = self.install_synthetic_image(escaped_names=True)
        native = Path(owner.__file__).resolve().parent
        self.manifest["sources"]["observation"] = identity(native / "bcrypt_index_observation_013.py")
        self.freeze()
        result = owner.run_once(str(self.manifest_path), self.sha)
        self.assertEqual(result["status"], "complete_index")
        self.assertEqual(result["parser_calls"], 1)
        self.assertTrue(result["worker"]["ready"])
        self.assertTrue(result["worker"]["go_sent"])
        self.assertTrue(result["worker"]["reaped"])
        self.assertEqual(result["worker"]["returncode"], 0)
        self.assertTrue(result["static_index_admitted"])
        self.assertEqual(result["core"]["index"]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(result["core"]["index"]["entrypoint"]["rva"], at["init_rva"])
        self.assertFalse(result["core"]["index"]["runtime_execution_observed"])
        self.assertGreater(len(encoded(result["core"])), 8192)
        self.assertEqual(result["core"]["index"]["imports"][0]["dll"], '"' * 511)
        self.assertEqual(result["core"]["index"]["iat_index"][0]["name"], "\\" * 511)

    def test_index_identity_and_complete_envelope_are_required(self):
        import producer_bcrypt_pe_013 as parser
        data, at = self.install_synthetic_image()
        index = parser.inspect_bcrypt_pe(data, at["init_rva"])
        for mode in ("sha256", "bytes", "entrypoint", "missing_table", "policy", "runtime"):
            with self.subTest(mode=mode):
                bad = json.loads(encoded(index))
                if mode == "sha256":
                    bad["sha256"] = "0" * 64
                elif mode == "bytes":
                    bad["bytes"] += 1
                elif mode == "entrypoint":
                    bad["entrypoint"]["rva"] += 1
                elif mode == "missing_table":
                    del bad["runtime_functions"]
                elif mode == "policy":
                    bad["compatibility_policy"]["id"] = "OTHER"
                else:
                    bad["runtime_execution_observed"] = True
                core = dict(CORE, status="indexed", reason=None, exception_type=None,
                            index=bad, static_index_admitted=True)
                self.manifest["output_dir"] = str(self.base / mode)
                self.freeze()
                result, _process = self.controlled(
                    chunks=[b"R" + bytes.fromhex(self.sha), self.packet(core), b""])
                self.assertEqual(result["failure_code"], "CORE_SCHEMA")
                self.assertFalse(result["static_index_admitted"])

    def test_new_owner_allocates_for_complete_index_and_binds_profiles(self):
        self.assertEqual(owner.LIMITS["core_bytes"], 67108864)
        self.assertEqual(owner.LIMITS["stream_bytes"], 67108902)
        self.assertEqual(owner.LIMITS["report_bytes"], 67125248)
        self.manifest["codec_profiles"]["core"]["depth"] += 1
        self.freeze()
        self.rejected_before_launch("CODEC_PROFILES")

    def test_changed_codec_is_rejected_before_import(self):
        self.manifest["sources"]["codec"]["sha256"] = "0" * 64
        self.freeze()
        self.rejected_before_launch("IDENTITY_CODEC")

    def test_noncanonical_manifest_is_rejected_before_launch(self):
        raw = json.dumps(self.manifest, indent=2).encode()
        self.manifest_path.write_bytes(raw)
        self.sha = hashlib.sha256(raw).hexdigest()
        self.rejected_before_launch("JSON_SYNTAX|JSON_NON_CANONICAL")


if __name__ == "__main__":
    unittest.main()
