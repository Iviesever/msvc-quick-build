"""Independent Linux driver faults; retain every fresh synthetic evidence root.

Full-module launch ceiling: 17 direct fixed-substitute Popen attempts. A
successful implementation attempts nine; preflight rejections launch nothing.
The dangling-link probes block Popen before it can create any actual child.
No codec, MQB, MSVC, ETW, or real measurement process is permitted here.
"""
import copy
import errno
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import analysis_evidence_capture as capture

try:
    import analysis_substitute_driver as driver
except ModuleNotFoundError as error:
    if error.name != "analysis_substitute_driver":
        raise
    driver = None

try:
    import analysis_substitute_transport as transport
except ModuleNotFoundError as error:
    if error.name != "analysis_substitute_transport":
        raise
    transport = None


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


class FaultWriter:
    """Change one actual file's writes; all flush/close operations remain real."""

    def __init__(self, target, kind, seen):
        self.target, self.kind, self.seen = target, kind, seen

    def __getattr__(self, name):
        return getattr(self.target, name)

    def __enter__(self):
        return self

    def __exit__(self, *arguments):
        return self.target.__exit__(*arguments)

    def write(self, raw):
        self.seen.append(len(raw))
        if self.kind == "raise":
            raise OSError("injected external-seal write failure")
        if self.kind == "record":
            raise OSError("injected call-record write failure")
        length = len(raw) // 2
        self.target.write(raw[:length])
        return length


class DriverFaultTests(unittest.TestCase):
    def setUp(self):
        parent = os.environ.get("MQB_DRIVER_EVIDENCE_ROOT")
        if parent:
            Path(parent).mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(
            prefix=self._testMethodName + "-", dir=parent))
        self.root = self.directory / "bundle"
        self.seal = self.directory / "driver-result.sha256"
        self.launch_budget = 0
        self.launches, self.children, self.cleaned_children = [], [], []
        self.addCleanup(self.save_test_receipt)
        print(f"retained_driver_fault_evidence={self.directory}", flush=True)
        self.assertIsNotNone(driver, "analysis substitute driver is not implemented")
        self.assertIsNotNone(transport, "analysis substitute transport is not implemented")
        self.original_popen = subprocess.Popen
        patcher = mock.patch.object(subprocess, "Popen", side_effect=self.track_launch)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.cleanup_children)

    def save_test_receipt(self):
        document = {
            "test": self.id(), "launch_ceiling": self.launch_budget,
            "popen_attempts": len(self.launches), "launches": self.launches,
            "children": [{"pid": child.pid, "returncode": child.returncode}
                         for child in self.children],
            "test_cleanup_terminated": self.cleaned_children,
            "real_measurement_launches": 0,
        }
        with (self.directory / "test-receipt.json").open("x", encoding="utf-8") as target:
            json.dump(document, target, sort_keys=True, indent=2)
            target.write("\n")

    def track_launch(self, *args, **kwargs):
        self.assertLess(len(self.launches), self.launch_budget,
                        "driver exceeded this contract's launch allocation")
        argv = args[0] if args else kwargs["args"]
        self.launches.append({"argv": list(argv), "pid": None})
        self.assertIs(kwargs.get("shell", False), False)
        self.assertIs(kwargs.get("start_new_session"), True)
        self.assertIs(kwargs.get("close_fds"), True)
        self.assertEqual(kwargs.get("stdin"), subprocess.DEVNULL)
        self.assertEqual(kwargs.get("stdout"), subprocess.PIPE)
        self.assertEqual(kwargs.get("stderr"), subprocess.PIPE)
        child = self.original_popen(*args, **kwargs)
        self.children.append(child)
        self.launches[-1]["pid"] = child.pid
        return child

    def cleanup_children(self):
        for child in self.children:
            if child.poll() is None:
                self.cleaned_children.append(child.pid)
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=1)
        self.assertEqual(self.cleaned_children, [], "driver left a running child")

    def protocol(self, *modes, limits=None):
        calls = [{"call_id": f"fixed-{index}", "mode": mode}
                 for index, mode in enumerate(modes)]
        arguments = {"protocol_id": self._testMethodName, "calls": calls}
        if limits is not None:
            arguments["limits"] = limits
        return driver.freeze_protocol(**arguments)

    def supervision_limits(self):
        return driver.DriverLimits(call_timeout_ms=750, terminate_grace_ms=80,
                                   kill_wait_ms=500, poll_ms=5,
                                   overall_timeout_ms=3000)

    def run_protocol(self, protocol, expected_sha=None):
        return driver.run_frozen_substitutes(
            self.root, protocol=protocol,
            expected_protocol_sha256=expected_sha or protocol.sha256,
            seal_path=self.seal)

    def audit(self, protocol, result):
        return driver.audit_driver_evidence(
            self.root, protocol=protocol,
            expected_protocol_sha256=protocol.sha256,
            expected_driver_seal_sha256=result["driver_seal_sha256"])

    def call(self, index=0):
        return json.loads((self.root / "calls" / f"{index:06d}.json").read_bytes())

    def assert_invalid_stopped(self, result):
        self.assertIs(result["complete"], False)
        self.assertEqual(result["integrity_status"], "INVALID")
        self.assertTrue(result["first_failure"])
        self.assertEqual(result["launch_attempts"], 1)
        self.assertEqual(result["launched_calls"], 1)
        self.assertEqual(result["reaped_calls"], 1)
        self.assertEqual(len(self.launches), 1)
        self.assertFalse((self.root / "calls/000001.json").exists())
        for role in ("stdout", "stderr"):
            self.assertFalse((self.root / "spool" / f"000001.{role}.bin").exists())
            self.assertFalse((self.root / "capture/raw" / f"000001.{role}.bin").exists())

    def assert_preserved_prefixes(self, record):
        for role in ("stdout", "stderr"):
            name = f"000000.{role}.bin"
            raw = (self.root / "spool" / name).read_bytes()
            self.assertEqual(record["streams"][role]["byte_count"], len(raw))
            self.assertEqual(record["streams"][role]["sha256"], sha(raw))
            for folder in ("raw", "backup"):
                self.assertEqual((self.root / "capture" / folder / name).read_bytes(), raw)

    def test_ignored_term_is_killed_reaped_and_blocks_the_next_call(self):
        self.launch_budget = 2
        protocol = self.protocol("ignore_term", "normal", limits=self.supervision_limits())
        result = self.run_protocol(protocol)
        self.assert_invalid_stopped(result)
        record = self.call()
        self.assertIn("timeout", record["first_failure"])
        self.assertIs(record["term_sent"], True)
        self.assertIs(record["kill_sent"], True)
        self.assertIs(record["reaped"], True)
        self.assertIs(type(record["returncode"]), int)
        self.assertNotEqual(record["returncode"], 0)
        self.assert_preserved_prefixes(record)
        self.assertGreater(record["streams"]["stdout"]["byte_count"], 0)
        self.assertGreater(record["streams"]["stderr"]["byte_count"], 0)
        self.assertIs(self.audit(protocol, result)["complete"], False)

    def test_closed_pipes_are_not_a_successful_process_exit(self):
        self.launch_budget = 2
        protocol = self.protocol("closed_pipes", "normal", limits=self.supervision_limits())
        result = self.run_protocol(protocol)
        self.assert_invalid_stopped(result)
        record = self.call()
        self.assertIn("timeout", record["first_failure"])
        self.assertIs(record["streams"]["stdout"]["pipe_eof"], True)
        self.assertIs(record["streams"]["stderr"]["pipe_eof"], True)
        self.assertIs(record["term_sent"], True)
        self.assertIs(record["reaped"], True)
        self.assert_preserved_prefixes(record)

    def test_active_drain_still_obeys_the_call_deadline(self):
        self.launch_budget = 2
        protocol = self.protocol("busy", "normal", limits=self.supervision_limits())
        result = self.run_protocol(protocol)
        self.assert_invalid_stopped(result)
        record = self.call()
        self.assertIn("timeout", record["first_failure"])
        self.assertIs(record["term_sent"], True)
        self.assertIs(record["reaped"], True)
        self.assertGreater(sum(item["byte_count"] for item in record["streams"].values()), 0)
        self.assert_preserved_prefixes(record)

    def test_spool_short_write_cannot_shrink_the_received_receipt(self):
        self.launch_budget = 2
        protocol = self.protocol("normal", "normal")
        target_path = self.root / "spool/000000.stdout.bin"
        original_open, seen = transport._open_new, []

        def open_target(path):
            target = original_open(path)
            return FaultWriter(target, "short", seen) if Path(path) == target_path else target

        with mock.patch.object(transport, "_open_new", side_effect=open_target):
            result = self.run_protocol(protocol)
        self.assertTrue(seen, "short-write injection never reached the real spool")
        self.assert_invalid_stopped(result)
        record = self.call()
        receipt = record["streams"]["stdout"]
        preserved = target_path.read_bytes()
        self.assertGreater(receipt["byte_count"], len(preserved))
        self.assertNotEqual(receipt["sha256"], sha(preserved))
        self.assertTrue(receipt["capture_error"])
        for folder in ("raw", "backup"):
            self.assertEqual((self.root / "capture" / folder / target_path.name).read_bytes(),
                             preserved)
            self.assertTrue((self.root / "capture" / folder / "000000.stderr.bin").is_file())
        self.assertIs(self.audit(protocol, result)["complete"], False)

    def test_existing_external_seal_rejects_without_overwrite_or_launch(self):
        self.launch_budget = 1
        protocol = self.protocol("normal")
        retained = b"pre-existing external anchor must remain unchanged\n"
        with self.seal.open("xb") as target:
            target.write(retained)
        with self.assertRaises((driver.DriverError, FileExistsError)):
            self.run_protocol(protocol)
        self.assertEqual(self.seal.read_bytes(), retained)
        self.assertFalse(self.root.exists())
        self.assertEqual(self.launches, [])

    def test_external_seal_write_failure_never_returns_success(self):
        self.launch_budget = 1
        protocol = self.protocol("normal")
        original_open, seen = transport._open_new, []

        def open_target(path):
            target = original_open(path)
            return FaultWriter(target, "raise", seen) if Path(path) == self.seal else target

        with mock.patch.object(transport, "_open_new", side_effect=open_target):
            with self.assertRaises(driver.DriverError):
                self.run_protocol(protocol)
        self.assertTrue(seen, "external-seal write fault was not exercised")
        self.assertEqual(len(self.launches), 1)
        self.assertEqual(self.seal.read_bytes(), b"")
        self.assertTrue((self.root / "driver-result.json").is_file())
        self.assertTrue((self.root / "capture/result.json").is_file())

    def test_call_record_write_failure_preserves_capture_then_raises(self):
        self.launch_budget = 2
        protocol = self.protocol("normal", "normal")
        target_path = self.root / "calls/000000.json"
        original_open, seen = transport._open_new, []

        def open_target(path):
            target = original_open(path)
            return FaultWriter(target, "record", seen) if Path(path) == target_path else target

        returned, failure = None, None
        with mock.patch.object(transport, "_open_new", side_effect=open_target):
            try:
                returned = self.run_protocol(protocol)
            except driver.DriverError as error:
                failure = error
        observation = {
            "actual_popen_attempts": len(self.launches),
            "child_returncodes": [child.returncode for child in self.children],
            "returned_verdict": returned,
            "exception_type": type(failure).__name__ if failure else None,
            "exception": str(failure)[:256] if failure else None,
        }
        with (self.directory / "call-record-failure-observation.json").open("x", encoding="utf-8") as target:
            json.dump(observation, target, sort_keys=True, indent=2)
            target.write("\n")
        self.assertTrue(seen, "call-record write failure was not exercised")
        self.assertEqual(len(self.launches), 1)
        self.assertEqual(len(self.children), 1)
        self.assertEqual(self.children[0].returncode, 0, "completed child was not reaped")
        captured = json.loads((self.root / "capture/result.json").read_bytes())
        self.assertIs(captured["complete"], False)
        self.assertEqual(captured["integrity_status"], "INVALID")
        self.assertEqual(captured["started_calls"], 1)
        self.assertEqual(captured["finished_calls"], 1)
        for role in ("stdout", "stderr"):
            name = f"000000.{role}.bin"
            original = (self.root / "spool" / name).read_bytes()
            self.assertTrue(original)
            for folder in ("raw", "backup"):
                self.assertEqual((self.root / "capture" / folder / name).read_bytes(), original)
            self.assertFalse((self.root / "spool" / f"000001.{role}.bin").exists())
        self.assertIsInstance(failure, driver.DriverError,
                              "lost call receipt returned a verdict instead of raising")
        self.assertIsNone(returned)
        self.assertFalse((self.root / "driver-result.json").exists())
        self.assertFalse(self.seal.exists())

    def test_layout_enumeration_stops_at_the_first_unexpected_member(self):
        directory = self.directory / "bounded-members"
        directory.mkdir()

        class Entry:
            name = "unexpected-first"

        class Entries:
            def __init__(self):
                self.pulls, self.closed = 0, False

            def __enter__(self):
                return self

            def __exit__(self, *_):
                self.closed = True

            def __iter__(self):
                return self

            def __next__(self):
                self.pulls += 1
                if self.pulls > 1:
                    raise AssertionError("enumeration continued beyond the first unexpected member")
                return Entry()

        entries = Entries()
        problems = driver._Problems()
        with mock.patch.object(os, "listdir", side_effect=AssertionError("eager os.listdir forbidden")) as listed:
            with mock.patch.object(Path, "iterdir", side_effect=AssertionError("eager Path.iterdir forbidden")) as iterated:
                with mock.patch.object(os, "scandir", return_value=entries) as scanned:
                    driver._members(directory, {"expected-only"}, problems)
        observation = {"listdir_calls": listed.call_count, "iterdir_calls": iterated.call_count,
                       "scandir_calls": scanned.call_count, "entries_pulled": entries.pulls,
                       "iterator_closed": entries.closed, "failures": problems.items,
                       "actual_child_launches": len(self.launches)}
        with (self.directory / "bounded-enumeration-observation.json").open("x", encoding="utf-8") as target:
            json.dump(observation, target, sort_keys=True, indent=2)
            target.write("\n")
        self.assertEqual(listed.call_count, 0)
        self.assertEqual(iterated.call_count, 0)
        scanned.assert_called_once_with(directory)
        self.assertEqual(entries.pulls, 1)
        self.assertIs(entries.closed, True)
        self.assertEqual(problems.count, 1)
        self.assertIn("unexpected_member", problems.items[0])
        self.assertEqual(self.launches, [])

    def test_damage_after_capture_finalize_cannot_pass_driver_fresh_audit(self):
        self.launch_budget = 1
        protocol = self.protocol("normal")
        original_finalize, changed = capture.CaptureSession.finalize, []
        target_path = self.root / "capture/raw/000000.stdout.bin"

        def finalize_then_damage(session):
            result = original_finalize(session)
            self.assertIs(result["complete"], True, "control capture never completed")
            changed.append(target_path.read_bytes())
            target_path.write_bytes(b"")
            return result

        with mock.patch.object(capture.CaptureSession, "finalize", new=finalize_then_damage):
            result = self.run_protocol(protocol)
        self.assertEqual(len(changed), 1)
        self.assertTrue(changed[0])
        self.assertIs(result["complete"], False)
        self.assertEqual(result["integrity_status"], "INVALID")
        self.assertEqual(len(self.launches), 1)
        self.assertEqual(target_path.read_bytes(), b"")
        self.assertEqual((self.root / "capture/backup" / target_path.name).read_bytes(), changed[0])
        self.assertEqual((self.root / "spool" / target_path.name).read_bytes(), changed[0])
        self.assertIs(self.audit(protocol, result)["complete"], False)

    def test_source_drift_rejects_before_root_creation_or_launch(self):
        self.launch_budget = 1
        protocol = self.protocol("normal")
        original_sources = driver._source_identities

        def drifted_sources():
            sources = copy.deepcopy(original_sources())
            source = sources[next(iter(sources))]
            old = source["sha256"]
            source["sha256"] = ("0" if old[0] != "0" else "1") + old[1:]
            return sources

        with mock.patch.object(driver, "_source_identities", side_effect=drifted_sources):
            with self.assertRaises(driver.DriverError):
                self.run_protocol(protocol)
        self.assertFalse(self.root.exists())
        self.assertFalse(self.seal.exists())
        self.assertEqual(self.launches, [])

    def test_dangling_root_or_seal_link_rejects_without_following_its_target(self):
        # Zero actual-child allocation, even when a broken preflight reaches Popen.
        protocol = self.protocol("normal")
        for role in ("root", "seal"):
            with self.subTest(existing_link=role):
                directory = self.directory / role
                directory.mkdir()
                root, seal = directory / "bundle", directory / "external.sha256"
                foreign = directory / "foreign-missing-target"
                alias = root if role == "root" else seal
                alias.symlink_to(foreign, target_is_directory=role == "root")
                original_target = os.readlink(alias)
                blocked = []

                def reject_launch(*args, **kwargs):
                    argv = args[0] if args else kwargs["args"]
                    blocked.append(list(argv))
                    raise OSError("actual child forbidden in dangling-link preflight probe")

                failure, result = None, None
                with mock.patch.object(subprocess, "Popen", side_effect=reject_launch):
                    try:
                        result = driver.run_frozen_substitutes(
                            root, protocol=protocol, expected_protocol_sha256=protocol.sha256,
                            seal_path=seal)
                    except Exception as error:
                        failure = error
                observation = {
                    "link_role": role, "link": str(alias), "target": str(foreign),
                    "actual_child_launches": 0, "blocked_popen_calls": blocked,
                    "target_exists": foreign.exists(), "link_preserved": alias.is_symlink(),
                    "returned_complete": result.get("complete") if type(result) is dict else None,
                    "exception_type": type(failure).__name__ if failure else None,
                    "exception": str(failure)[:256] if failure else None,
                }
                with (directory / "preflight-observation.json").open("x", encoding="utf-8") as target:
                    json.dump(observation, target, sort_keys=True, indent=2)
                    target.write("\n")
                self.assertEqual(blocked, [], "preflight attempted to launch through an existing link")
                self.assertFalse(foreign.exists(), "preflight wrote through an existing dangling link")
                self.assertTrue(alias.is_symlink())
                self.assertEqual(os.readlink(alias), original_target)
                self.assertIsInstance(failure, (driver.DriverError, FileExistsError))

    def test_caller_mutation_cannot_change_scheduling_or_audit_policy(self):
        self.launch_budget = 2
        calls = [{"call_id": "frozen-a", "mode": "normal"},
                 {"call_id": "frozen-b", "mode": "empty_stderr"}]
        limits = driver.DriverLimits(max_calls=2, max_stream_bytes=131072,
                                     max_total_bytes=262144, chunk_bytes=4096)
        protocol = driver.freeze_protocol(protocol_id=self._testMethodName,
                                          calls=calls, limits=limits)
        frozen_sha, frozen_document = protocol.sha256, protocol.document()
        calls[0].update(call_id="changed", mode="nonzero")
        calls.append({"call_id": "extra", "mode": "ignore_term"})
        object.__setattr__(limits, "max_calls", 1)
        object.__setattr__(limits, "max_stream_bytes", 1)
        exposed_copy = protocol.document()
        exposed_copy.clear()
        self.assertEqual(protocol.document(), frozen_document)
        self.assertEqual(protocol.sha256, frozen_sha)
        result = self.run_protocol(protocol, expected_sha=frozen_sha)
        self.assertIs(result["complete"], True)
        self.assertEqual(result["integrity_status"], "VERIFIED")
        self.assertEqual(result["planned_calls"], 2)
        self.assertEqual(result["launch_attempts"], 2)
        self.assertEqual(result["launched_calls"], 2)
        self.assertEqual(result["reaped_calls"], 2)
        self.assertEqual(len(self.launches), 2)
        self.assertEqual(json.loads((self.root / "protocol.json").read_bytes()), frozen_document)
        self.assertEqual((self.root / "spool/000001.stderr.bin").read_bytes(), b"")
        for folder in ("raw", "backup"):
            self.assertEqual((self.root / "capture" / folder / "000001.stderr.bin").read_bytes(), b"")
        self.assertIs(self.audit(protocol, result)["complete"], True)

    def test_wrong_external_protocol_hash_rejects_before_launch(self):
        self.launch_budget = 1
        protocol = self.protocol("normal")
        old = protocol.sha256
        wrong = ("0" if old[0] != "0" else "1") + old[1:]
        with self.assertRaises(driver.DriverError):
            self.run_protocol(protocol, expected_sha=wrong)
        self.assertFalse(self.root.exists())
        self.assertFalse(self.seal.exists())
        self.assertEqual(self.launches, [])

    def test_sigchld_ignored_rejects_freeze_run_and_low_level_launch(self):
        # Every attempted Popen entry is blocked: this contract starts no child.
        protocol = self.protocol("normal")
        document = protocol.document()
        original_getsignal = signal.getsignal

        def ignored_sigchld(number):
            return signal.SIG_IGN if number == signal.SIGCHLD else original_getsignal(number)

        for phase in ("freeze", "run", "low-level"):
            with self.subTest(phase=phase):
                blocked = []

                def reject_launch(*args, **kwargs):
                    argv = args[0] if args else kwargs["args"]
                    blocked.append(list(argv))
                    raise OSError("actual child forbidden in SIGCHLD preflight probe")

                if phase == "low-level":
                    directory = self.directory / "policy-low-level"
                    directory.mkdir()
                    spool = directory / "spool"
                    spool.mkdir()
                    directory_ids = {path: transport.directory_identity(path)
                                     for path in (directory, spool)}
                failure, returned = None, None
                with mock.patch.object(signal, "getsignal", side_effect=ignored_sigchld) as queried:
                    with mock.patch.object(subprocess, "Popen", side_effect=reject_launch):
                        try:
                            if phase == "freeze":
                                returned = self.protocol("normal")
                            elif phase == "run":
                                returned = self.run_protocol(protocol)
                            else:
                                returned = transport._capture_process(
                                    index=0, argv=tuple(document["calls"][0]["argv"]),
                                    cwd=document["runtime"]["cwd"],
                                    env=dict(document["runtime"]["environment"]),
                                    spool_dir=spool, limits=dict(document["limits"]),
                                    remaining_total_bytes=document["limits"]["max_total_bytes"],
                                    overall_deadline_ns=time.monotonic_ns() + 4_000_000_000,
                                    directory_ids=directory_ids)
                        except Exception as error:
                            failure = error
                observation = {
                    "kind": "simulated-no-child", "phase": phase, "actual_child_launches": 0,
                    "blocked_popen_calls": blocked,
                    "getsignal_requests": [int(call.args[0]) for call in queried.call_args_list],
                    "root_exists": self.root.exists(),
                    "returned_type": type(returned).__name__ if returned is not None else None,
                    "returned_record": returned if type(returned) is dict else None,
                    "exception_type": type(failure).__name__ if failure else None,
                    "exception": str(failure)[:256] if failure else None,
                }
                with (self.directory / f"sigchld-{phase}-observation.json").open("x", encoding="utf-8") as target:
                    json.dump(observation, target, sort_keys=True, indent=2)
                    target.write("\n")
                self.assertEqual(blocked, [], "SIGCHLD policy failure reached the launch entry")
                if phase != "low-level":
                    self.assertIsInstance(failure, (ValueError, driver.DriverError))
                    self.assertFalse(self.root.exists())
                    self.assertFalse(self.seal.exists())
                else:
                    self.assertIsNone(failure)
                    self.assertIs(returned["launch_attempted"], False)
                    self.assertIs(returned["launched"], False)
                    self.assertIs(returned["reaped"], False)
                    self.assertIsNone(returned["returncode"])
                    self.assertIs(returned["transport_ok"], False)
                    self.assertIn("sigchld_default_required", returned["first_failure"])
        self.assertEqual(document["runtime"].get("reaper_policy"),
                         "exclusive-driver-waitpid-sigchld-default")
        self.assertEqual(self.launches, [])

    def test_unavailable_wait_status_is_not_synthesized_as_success(self):
        # Popen is wholly simulated; the two prefilled pipes are real local fds.
        # Their writer ends are closed, so their EOF must remain independently true.
        protocol = self.protocol("normal", "normal")
        document = protocol.document()
        expected = driver.fixture_spec("normal")
        simulated, launch_calls, wait_calls = [], [], []

        def closed_writer_pipe(raw):
            read_fd, write_fd = os.pipe()
            reader = os.fdopen(read_fd, "rb", buffering=0)
            self.addCleanup(reader.close)
            try:
                self.assertEqual(os.write(write_fd, raw), len(raw))
            finally:
                os.close(write_fd)
            return reader

        class SimulatedChild:
            def __init__(self, pid):
                self.pid, self.returncode = pid, None
                self.stdout = closed_writer_pipe(expected["stdout"])
                self.stderr = closed_writer_pipe(expected["stderr"])
                self.poll_calls = self.wait_calls = 0

            def poll(self):
                self.poll_calls += 1
                self.returncode = 0
                return 0

            def wait(self, timeout=None):
                self.wait_calls += 1
                self.returncode = 0
                return 0

        def simulated_popen(*args, **kwargs):
            index = len(simulated)
            self.assertLess(index, 2, "simulated driver exceeded its frozen call plan")
            argv = args[0] if args else kwargs["args"]
            self.assertEqual(list(argv), document["calls"][index]["argv"])
            child = SimulatedChild(2_147_483_000 + index)
            simulated.append(child)
            launch_calls.append({"kind": "simulated-no-child", "argv": list(argv), "pid": child.pid})
            return child

        def unavailable_status(pid, options):
            wait_calls.append({"pid": pid, "options": options})
            raise ChildProcessError(errno.ECHILD, "injected exclusive wait status unavailable")

        with mock.patch.object(subprocess, "Popen", side_effect=simulated_popen):
            with mock.patch.object(os, "waitpid", side_effect=unavailable_status):
                with mock.patch.object(os, "killpg", side_effect=AssertionError("unknown wait status must not signal")) as signaled:
                    result = self.run_protocol(protocol)
        record = self.call()
        observation = {
            "kind": "simulated-no-child", "actual_child_launches": 0,
            "simulated_launches": launch_calls, "waitpid_calls": wait_calls,
            "popen_poll_calls": sum(child.poll_calls for child in simulated),
            "popen_wait_calls": sum(child.wait_calls for child in simulated),
            "killpg_calls": signaled.call_count, "result": result, "call_record": record,
        }
        with (self.directory / "unavailable-wait-status-observation.json").open("x", encoding="utf-8") as target:
            json.dump(observation, target, sort_keys=True, indent=2)
            target.write("\n")
        self.assertIs(result["complete"], False, "unknown wait status was synthesized as success")
        self.assertEqual(result["integrity_status"], "INVALID")
        self.assertEqual(len(simulated), 1, "next call launched after unknown wait status")
        self.assertEqual(result["launch_attempts"], 1)
        self.assertEqual(result["launched_calls"], 1)
        self.assertEqual(result["reaped_calls"], 0)
        self.assertIsNone(record["returncode"])
        self.assertIs(record["reaped"], False)
        self.assertIs(record.get("wait_status_unavailable"), True)
        self.assertIs(record["transport_ok"], False)
        self.assertTrue(wait_calls, "driver never requested an actual kernel wait status")
        self.assertTrue(all(call["pid"] == simulated[0].pid and call["options"] == os.WNOHANG
                            for call in wait_calls))
        self.assertEqual(signaled.call_count, 0)
        self.assertEqual(sum(child.poll_calls + child.wait_calls for child in simulated), 0)
        self.assert_preserved_prefixes(record)
        for role in ("stdout", "stderr"):
            self.assertIs(record["streams"][role]["pipe_eof"], True)
            self.assertEqual((self.root / "spool" / f"000000.{role}.bin").read_bytes(), expected[role])
            self.assertFalse((self.root / "spool" / f"000001.{role}.bin").exists())
        events = [json.loads(line) for line in (self.root / "capture/journal.jsonl").read_bytes().splitlines()]
        finishes = [event for event in events if event["event"] == "finish"]
        self.assertEqual(len(finishes), 1)
        self.assertIsNone(finishes[0]["exit_code"])
        self.assertIs(self.audit(protocol, result)["complete"], False)
        self.assertEqual(self.launches, [])
        self.assertEqual(self.children, [])


if __name__ == "__main__":
    unittest.main()
