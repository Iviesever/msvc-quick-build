"""Test-first contracts for the real, fixed-byte substitute process adapter."""
import hashlib
import importlib
import json
import os
from dataclasses import asdict
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch


class SubstituteDriverTests(unittest.TestCase):
    def setUp(self):
        try:
            self.driver = importlib.import_module("analysis_substitute_driver")
            self.fixture = importlib.import_module("analysis_fixed_substitute")
            self.transport = importlib.import_module("analysis_substitute_transport")
        except ModuleNotFoundError as error:
            self.fail(f"required fixed-substitute driver is not implemented: {error.name}")
        parent = os.environ.get("MQB_DRIVER_EVIDENCE_ROOT")
        if parent:
            Path(parent).mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(prefix=self._testMethodName + "-", dir=parent))

    def protocol(self, modes, limits=None):
        values = {"protocol_id": self._testMethodName,
                  "calls": [{"call_id": f"call-{index}", "mode": mode}
                            for index, mode in enumerate(modes)]}
        if limits is not None:
            values["limits"] = limits
        return self.driver.freeze_protocol(**values)

    def run_plan(self, protocol):
        root = self.directory / "bundle"
        result = self.driver.run_frozen_substitutes(
            root, protocol=protocol, expected_protocol_sha256=protocol.sha256,
            seal_path=self.directory / "external-seal.sha256")
        return root, result

    def audit(self, root, protocol, seal):
        return self.driver.audit_driver_evidence(
            root, protocol=protocol, expected_protocol_sha256=protocol.sha256,
            expected_driver_seal_sha256=seal)

    @staticmethod
    def record(root, index):
        return json.loads((root / "calls" / f"{index:06d}.json").read_bytes())

    def test_normal_empty_and_large_dual_pipes_preserve_exact_bytes(self):
        modes = ["normal", "empty_stderr", "dual"]
        protocol = self.protocol(modes)
        root, result = self.run_plan(protocol)
        self.assertTrue(result["complete"], result)
        self.assertEqual("VERIFIED", result["integrity_status"])
        self.assertEqual(3, result["launched_calls"])
        self.assertEqual(3, result["reaped_calls"])
        self.assertEqual(result["driver_seal_sha256"],
                         (self.directory / "external-seal.sha256").read_text().strip())
        for index, mode in enumerate(modes):
            expected = self.fixture.fixture_spec(mode)
            record = self.record(root, index)
            self.assertEqual(0, record["returncode"])
            self.assertTrue(record["reaped"])
            for role in ("stdout", "stderr"):
                receipt = record["streams"][role]
                self.assertTrue(receipt["pipe_eof"])
                self.assertEqual(len(expected[role]), receipt["byte_count"])
                self.assertEqual(hashlib.sha256(expected[role]).hexdigest(), receipt["sha256"])
                for name in ("spool", "capture/raw", "capture/backup"):
                    self.assertEqual(expected[role],
                                     (root / name / f"{index:06d}.{role}.bin").read_bytes())
        self.assertIn(b"\r\n", self.fixture.fixture_spec("normal")["stdout"])
        self.assertIn(b"\x00", self.fixture.fixture_spec("normal")["stderr"])
        self.assertEqual(b"", (root / "spool/000001.stderr.bin").read_bytes())
        self.assertGreater(len(self.fixture.fixture_spec("dual")["stderr"]), 128 * 1024)
        self.assertTrue(self.audit(root, protocol, result["driver_seal_sha256"])["complete"])

    def test_nonzero_keeps_both_prefixes_and_never_launches_next_call(self):
        protocol = self.protocol(["nonzero", "normal"])
        root, result = self.run_plan(protocol)
        self.assertFalse(result["complete"])
        self.assertEqual(1, result["launch_attempts"])
        self.assertEqual(1, result["launched_calls"])
        self.assertEqual(1, result["reaped_calls"])
        self.assertEqual(7, self.record(root, 0)["returncode"])
        for role in ("stdout", "stderr"):
            self.assertEqual(self.fixture.fixture_spec("nonzero")[role],
                             (root / "capture/raw" / f"000000.{role}.bin").read_bytes())
        self.assertFalse((root / "calls/000001.json").exists())
        self.assertFalse(self.audit(root, protocol, result["driver_seal_sha256"])["complete"])

    def test_stream_overflow_records_probe_and_cannot_become_spool_eof_success(self):
        limits = self.driver.DriverLimits(max_stream_bytes=64, chunk_bytes=32)
        protocol = self.protocol(["dual", "normal"], limits)
        root, result = self.run_plan(protocol)
        self.assertFalse(result["complete"])
        self.assertEqual(1, result["launched_calls"])
        receipts = self.record(root, 0)["streams"]
        self.assertTrue(any(r["overflow_probe_hex"] for r in receipts.values()))
        for role, receipt in receipts.items():
            self.assertLessEqual(receipt["byte_count"], 64)
            if receipt["overflow_probe_hex"]:
                self.assertEqual(2, len(receipt["overflow_probe_hex"]))
                self.assertFalse(receipt["pipe_eof"])
            raw = (root / "capture/raw" / f"000000.{role}.bin").read_bytes()
            self.assertEqual(receipt["byte_count"], len(raw))
            self.assertEqual(receipt["sha256"], hashlib.sha256(raw).hexdigest())

    def test_total_budget_is_shared_across_roles_and_calls(self):
        fixed = self.fixture.fixture_spec("normal")
        budget = len(fixed["stdout"]) + len(fixed["stderr"]) + 32
        limits = self.driver.DriverLimits(max_total_bytes=budget, chunk_bytes=16)
        protocol = self.protocol(["normal", "dual", "normal"], limits)
        root, result = self.run_plan(protocol)
        self.assertFalse(result["complete"])
        self.assertEqual(2, result["launched_calls"])
        total = sum(r["byte_count"] for i in (0, 1)
                    for r in self.record(root, i)["streams"].values())
        self.assertLessEqual(total, budget)
        self.assertTrue(any(r["overflow_probe_hex"]
                            for r in self.record(root, 1)["streams"].values()))
        self.assertFalse((root / "calls/000002.json").exists())

    def test_late_raw_damage_after_all_calls_is_invalid_at_final_gate(self):
        protocol = self.protocol(["normal", "empty_stderr"])
        original = self.driver.CaptureSession.finalize
        root = self.directory / "bundle"

        def damage_then_finalize(session):
            (root / "capture/raw/000000.stdout.bin").write_bytes(b"")
            return original(session)

        with patch.object(self.driver.CaptureSession, "finalize", damage_then_finalize):
            _, result = self.run_plan(protocol)
        self.assertFalse(result["complete"])
        self.assertEqual(2, result["reaped_calls"])
        capture = json.loads((root / "capture/result.json").read_bytes())
        self.assertTrue(capture["execution_finished"])
        self.assertFalse(capture["complete"])
        self.assertGreater((root / "capture/backup/000000.stdout.bin").stat().st_size, 0)

    def test_offline_audit_rechecks_spool_lifecycle_protocol_and_capture(self):
        protocol = self.protocol(["normal"])
        root, result = self.run_plan(protocol)
        self.assertTrue(result["complete"])
        mutations = {
            "spool": "spool/000000.stdout.bin",
            "lifecycle": "calls/000000.json",
            "protocol": "protocol.json",
            "capture": "capture/raw/000000.stdout.bin",
        }
        for label, filename in mutations.items():
            with self.subTest(label=label):
                variant = self.directory / label
                shutil.copytree(root, variant)
                (variant / filename).write_bytes(b"")
                before = {str(p.relative_to(variant)): p.read_bytes()
                          for p in variant.rglob("*") if p.is_file()}
                audit = self.audit(variant, protocol, result["driver_seal_sha256"])
                self.assertFalse(audit["complete"], label)
                self.assertEqual(1, audit["launch_attempts"], "lost evidence cannot refund an observed launch")
                self.assertEqual(1, audit["launched_calls"])
                self.assertEqual(1, audit["reaped_calls"])
                self.assertEqual(before, {str(p.relative_to(variant)): p.read_bytes()
                                          for p in variant.rglob("*") if p.is_file()})
        unknown = self.audit(root, protocol, "0" * 64)
        self.assertFalse(unknown["complete"])
        self.assertIsNone(unknown["launch_attempts"], "an untrusted seal means unknown consumption, not zero")
        self.assertIsNone(unknown["launched_calls"])
        self.assertIsNone(unknown["reaped_calls"])

    def test_spawn_failure_records_unknown_exit_and_no_original_pipe_eof(self):
        protocol = self.protocol(["normal", "normal"])
        with patch.object(self.transport.subprocess, "Popen", side_effect=OSError("fixed-spawn-fault")):
            root, result = self.run_plan(protocol)
        self.assertFalse(result["complete"])
        self.assertEqual(1, result["launch_attempts"])
        self.assertEqual(0, result["launched_calls"])
        record = self.record(root, 0)
        self.assertIsNone(record["returncode"])
        self.assertFalse(record["reaped"])
        self.assertTrue(all(not r["pipe_eof"] for r in record["streams"].values()))
        self.assertFalse((root / "calls/000001.json").exists())

    def test_invalid_protocol_and_limits_are_rejected_before_any_launch(self):
        for value in (True, 0, -1, float("nan"), float("inf"), 3001):
            with self.subTest(timeout=value):
                with self.assertRaises(ValueError):
                    self.driver.DriverLimits(call_timeout_ms=value)
        cases = [[], [{"call_id": "x", "mode": "arbitrary-command"}],
                 [{"call_id": "x", "mode": "normal", "argv": ["sh"]}],
                 [{"call_id": "x", "mode": "normal"}, {"call_id": "x", "mode": "normal"}]]
        with patch.object(self.transport.subprocess, "Popen", side_effect=AssertionError("unexpected launch")):
            for calls in cases:
                with self.subTest(calls=calls), self.assertRaises(ValueError):
                    self.driver.freeze_protocol(protocol_id="invalid", calls=calls)

    def test_existing_root_and_in_bundle_seal_are_rejected_without_changes(self):
        protocol = self.protocol(["normal"])
        root = self.directory / "existing"
        root.mkdir()
        marker = root / "keep"
        marker.write_bytes(b"original evidence\x00")
        with patch.object(self.transport.subprocess, "Popen", side_effect=AssertionError("unexpected launch")):
            with self.assertRaises((FileExistsError, self.driver.DriverError)):
                self.driver.run_frozen_substitutes(root, protocol=protocol,
                    expected_protocol_sha256=protocol.sha256,
                    seal_path=self.directory / "existing.seal")
            fresh = self.directory / "fresh"
            with self.assertRaises((ValueError, self.driver.DriverError)):
                self.driver.run_frozen_substitutes(fresh, protocol=protocol,
                    expected_protocol_sha256=protocol.sha256, seal_path=fresh / "seal")
        self.assertEqual(b"original evidence\x00", marker.read_bytes())
        self.assertFalse((self.directory / "fresh").exists())

    def test_python_binary_identity_is_streamed_with_bounded_reads(self):
        requests = []
        original_read = self.transport.os.read

        def observed_read(descriptor, amount):
            requests.append(amount)
            return original_read(descriptor, amount)

        with patch.object(self.transport.os, "read", side_effect=observed_read):
            try:
                protocol = self.protocol(["normal"])
            except Exception as error:
                self.fail(f"the installed Python binary could not be fingerprinted: {error}")
        identity = protocol.document()["sources"]["python"]
        path = Path(self.driver.sys.executable).resolve()
        self.assertEqual(str(path), identity["path"])
        self.assertEqual(path.stat().st_size, identity["bytes"])
        with path.open("rb") as source:
            self.assertEqual(hashlib.file_digest(source, "sha256").hexdigest(), identity["sha256"])
        self.assertTrue(requests)
        self.assertLessEqual(max(requests), 64 * 1024)
        self.assertLessEqual(identity["bytes"], 64 * 1024 * 1024)

    def test_reaped_nonzero_child_drains_both_pipes_without_signalling_old_group(self):
        # A released process ID must never receive a later TERM/KILL. Exercise
        # both signal checkpoints with real buffered pipes and no child launch.
        expected = {"stdout": b'fixed-prefix\x00\r\n', "stderr": b'failure-prefix\n'}
        for after_grace in (False, True):
            with self.subTest(after_grace=after_grace):
                pipe_handles = []
                for role in ("stdout", "stderr"):
                    read_fd, write_fd = os.pipe()
                    os.write(write_fd, expected[role])
                    os.close(write_fd)
                    pipe_handles.append(os.fdopen(read_fd, "rb", buffering=0))

                class ReapedChild:
                    pid = 2 ** 30
                    returncode = 7
                    stdout, stderr = pipe_handles

                    def poll(self):
                        return 7

                    def wait(self, timeout=None):
                        return 7

                times = iter((0, 0, 0))
                now = 300_000_000 if after_grace else 0
                clock = lambda: next(times, now)
                spool = self.directory / ("after-grace" if after_grace else "before-grace")
                spool.mkdir()
                identities = {path: self.transport.directory_identity(path)
                              for path in (self.directory, spool)}
                try:
                    with patch.object(self.transport.subprocess, "Popen", return_value=ReapedChild()), \
                            patch.object(self.transport.os, "waitpid", return_value=(ReapedChild.pid, 7 << 8)), \
                            patch.object(self.transport.time, "monotonic_ns", side_effect=clock), \
                            patch.object(self.transport.os, "killpg") as signal_group:
                        record = self.transport._capture_process(index=0, argv=("simulated-no-child",),
                            cwd=str(self.directory), env={}, spool_dir=spool,
                            limits=asdict(self.driver.DriverLimits()), remaining_total_bytes=1024,
                            overall_deadline_ns=20_000_000_000, directory_ids=identities)
                finally:
                    for handle in pipe_handles:
                        handle.close()
                with (spool / "reaped-signal-observation.json").open("x") as target:
                    json.dump({"process_kind": "simulated-no-child", "actual_launches": 0,
                               "signal_calls": [list(call.args) for call in signal_group.call_args_list],
                               "transport": record}, target, indent=2)
                self.assertTrue(record["reaped"])
                self.assertEqual(7, record["returncode"])
                self.assertEqual("child_exit:7", record["first_failure"])
                self.assertFalse(record["transport_ok"])
                for role in ("stdout", "stderr"):
                    self.assertEqual(expected[role], (spool / f"000000.{role}.bin").read_bytes())
                    self.assertTrue(record["streams"][role]["pipe_eof"])
                signal_group.assert_not_called()
                self.assertFalse(record["term_sent"])
                self.assertFalse(record["kill_sent"])

    def test_late_reap_with_both_eofs_still_checks_the_active_deadline(self):
        # Deterministic scheduling boundary: real empty pipes, a simulated child,
        # and a monotonic jump before its final reap. No process is launched.
        pipe_handles = []
        for _ in range(2):
            read_fd, write_fd = os.pipe()
            os.close(write_fd)
            pipe_handles.append(os.fdopen(read_fd, "rb", buffering=0))

        class SimulatedChild:
            pid = 2 ** 30
            returncode = None
            polls = 0
            stdout, stderr = pipe_handles

            def poll(self):
                self.polls += 1
                if self.polls >= 2:
                    self.returncode = 0
                return self.returncode

            def wait(self, timeout=None):
                self.returncode = 0
                return 0

        times = iter((0, 0, 0))
        clock = lambda: next(times, 4_000_000_000)
        spool = self.directory / "spool"
        spool.mkdir()
        identities = {path: self.transport.directory_identity(path)
                      for path in (self.directory, spool)}
        try:
            with patch.object(self.transport.subprocess, "Popen", return_value=SimulatedChild()), \
                    patch.object(self.transport.os, "waitpid", side_effect=((0, 0), (SimulatedChild.pid, 0))), \
                    patch.object(self.transport.time, "monotonic_ns", side_effect=clock), \
                    patch.object(self.transport.os, "killpg", side_effect=AssertionError("no actual signal allowed")):
                record = self.transport._capture_process(index=0, argv=("simulated-no-child",),
                    cwd=str(self.directory), env={}, spool_dir=spool,
                    limits=asdict(self.driver.DriverLimits()), remaining_total_bytes=1024,
                    overall_deadline_ns=20_000_000_000, directory_ids=identities)
        finally:
            for handle in pipe_handles:
                handle.close()
        with (self.directory / "deadline-observation.json").open("x") as target:
            json.dump({"process_kind": "simulated-no-child", "actual_launches": 0,
                       "transport": record}, target, indent=2)
        self.assertTrue(record["reaped"])
        self.assertTrue(all(r["pipe_eof"] for r in record["streams"].values()))
        self.assertFalse(record["transport_ok"], "a late final reap bypassed the deadline")
        self.assertEqual("call_timeout", record["first_failure"])


if __name__ == "__main__":
    unittest.main()
