"""Pure IO contracts; three explicitly opted-in bounded synthetic children."""

import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import signal
import stat
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock


SOURCE = Path(__file__).with_name("bcrypt_disassembly_io_014.py")
LIMITS = {"identity_bytes": 67108864, "identity_chunk_bytes": 65536,
          "index_bytes": 327680, "path_chars": 256, "alias_count": 12,
          "absent_path_count": 8, "marker_bytes": 1024, "target_bytes": 391,
          "child_file_bytes": 131072, "report_bytes": 524288,
          "child_cpu_seconds": 2, "child_address_space_bytes": 268435456,
          "child_core_bytes": 0, "child_open_files": 64}


class Refusal(ValueError):
    pass


def require(condition, code):
    if not condition:
        raise Refusal(code)


def implementation():
    if not SOURCE.is_file():
        return types.SimpleNamespace()
    spec = importlib.util.spec_from_file_location("_io014_test", SOURCE)
    module = importlib.util.module_from_spec(spec)
    # Exercise real IO with the small fixed model interface; model semantics
    # and the verified bootstrap's real module loading are tested separately.
    contract = types.ModuleType("bcrypt_disassembly_model_014")
    contract.LIMITS, contract.Refusal, contract.require = LIMITS, Refusal, require
    with mock.patch.dict(sys.modules, {contract.__name__: contract}):
        spec.loader.exec_module(module)
    return module


def identity(path, raw):
    return {"path": str(path), "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


class IOTestCase(unittest.TestCase):
    def setUp(self):
        self.io = implementation()
        self.temp = tempfile.TemporaryDirectory(prefix="bcrypt014-io-")
        self.root = Path(self.temp.name).resolve()
        self.addCleanup(self.temp.cleanup)

    def api(self, name):
        value = getattr(self.io, name, None)
        self.assertTrue(callable(value), "014 IO API is not implemented: " + name)
        return value

    def input(self, raw=b"synthetic identity bytes"):
        path = self.root / "input.bin"
        path.write_bytes(raw)
        return path, identity(path, raw)

    def output(self):
        value = self.api("OutputDirectory")(str(self.root / "output"))
        self.addCleanup(value.close)
        return value


class IOContractTests(IOTestCase):
    def test_identity_full_hash_stream_and_optional_retention(self):
        verify = self.api("verify_identity")
        raw = b"x" * 150001
        path, record = self.input(raw)
        self.assertIsNone(verify(record, 200000))
        self.assertEqual(verify(record, 200000, keep=True), raw)
        self.assertEqual(path.read_bytes(), raw)

    def test_identity_rejects_wrong_digest_and_declared_size(self):
        verify = self.api("verify_identity")
        _, record = self.input()
        with self.assertRaises(Refusal):
            verify(dict(record, sha256="0" * 64), 1024)
        with self.assertRaises(Refusal):
            verify(dict(record, bytes=record["bytes"] + 1), 1024)

    def test_identity_rejects_large_keep_before_open(self):
        verify = self.api("verify_identity")
        _, record = self.input()
        with mock.patch.object(self.io.os, "open", side_effect=AssertionError("must not open")):
            with self.assertRaises(Refusal):
                verify(record, 67108864, keep=True)

    def test_identity_rejects_noncanonical_symlink_and_nonregular(self):
        verify = self.api("verify_identity")
        path, record = self.input()
        alias = self.root / "alias"
        alias.symlink_to(path)
        for value in (dict(record, path=str(alias)),
                      dict(record, path=str(self.root / ".." / self.root.name / "input.bin")),
                      dict(record, path=str(self.root))):
            with self.subTest(path=value["path"]), self.assertRaises(Refusal):
                verify(value, 1024)

    def test_identity_rejects_fifo_without_blocking_read(self):
        verify = self.api("verify_identity")
        path = self.root / "fifo"
        os.mkfifo(path)
        with self.assertRaises(Refusal):
            verify(identity(path, b"x"), 1024)

    def test_identity_detects_changed_inode_during_hash(self):
        verify = self.api("verify_identity")
        path, record = self.input(b"x" * 70000)
        read = self.io.os.read
        changed = False
        def replace(fd, count):
            nonlocal changed
            data = read(fd, count)
            if not changed:
                changed = True
                replacement = self.root / "replacement"
                replacement.write_bytes(b"x" * 70000)
                replacement.replace(path)
            return data
        with mock.patch.object(self.io.os, "read", side_effect=replace):
            with self.assertRaises(Refusal):
                verify(record, 100000)

    def test_environment_accepts_exact_alias_and_absent_path(self):
        verify = self.api("verify_environment")
        path, record = self.input()
        alias = self.root / "alias"
        alias.symlink_to(path)
        manifest = {"identities": {"tool": record}, "sources": {},
                    "aliases": [{"path": str(alias), "target": str(path)}],
                    "absent_paths": [str(self.root / "absent")]}
        self.assertIsNone(verify(manifest))

    def test_environment_rejects_wrong_alias_target(self):
        verify = self.api("verify_environment")
        path, record = self.input()
        alias = self.root / "alias"
        alias.symlink_to(path)
        with self.assertRaises(Refusal):
            verify({"identities": {"tool": record}, "sources": {},
                    "aliases": [{"path": str(alias), "target": str(self.root / "wrong")}],
                    "absent_paths": []})

    def test_environment_absence_rejects_dangling_symlink(self):
        verify = self.api("verify_environment")
        dangling = self.root / "dangling"
        dangling.symlink_to(self.root / "missing")
        with self.assertRaises(Refusal):
            verify({"identities": {}, "sources": {}, "aliases": [],
                    "absent_paths": [str(dangling)]})

    def test_directory_is_exclusive_and_retains_evidence(self):
        output = self.output()
        output.create("consumed.json", b"{}\n", 1024)
        self.assertEqual(output.read("consumed.json", 1024), b"{}\n")
        output.close()
        self.assertTrue((self.root / "output" / "consumed.json").is_file())
        with self.assertRaises(Refusal):
            self.io.OutputDirectory(str(self.root / "output"))

    def test_existing_output_symlink_is_refused(self):
        constructor = self.api("OutputDirectory")
        (self.root / "output").symlink_to(self.root / "missing")
        with self.assertRaises(Refusal):
            constructor(str(self.root / "output"))

    def test_noncanonical_parent_is_refused(self):
        constructor = self.api("OutputDirectory")
        alias = self.root / "parent-alias"
        alias.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(Refusal):
            constructor(str(alias / "output"))

    def test_only_five_names_and_reviewed_caps_are_allowed(self):
        output = self.output()
        for name, raw, cap in (("../escape", b"x", 1), ("extra", b"x", 1),
                               ("target.bin", b"x" * 392, 392),
                               ("report.json", b"{}", 524289)):
            with self.subTest(name=name), self.assertRaises(Refusal):
                output.create(name, raw, cap)
        self.assertEqual(list((self.root / "output").iterdir()), [])

    def test_create_never_overwrites_an_owned_file(self):
        output = self.output()
        output.create("target.bin", b"abc", 391, mode=0o400)
        with self.assertRaises(Refusal):
            output.create("target.bin", b"xyz", 391)
        self.assertEqual(output.read("target.bin", 391), b"abc")
        self.assertEqual(stat.S_IMODE((self.root / "output" / "target.bin").stat().st_mode), 0o400)

    def test_capture_uses_owned_regular_file_descriptor(self):
        output = self.output()
        fd = output.open_capture("tool-stdout.bin")
        self.assertTrue(stat.S_ISREG(os.fstat(fd).st_mode))
        os.write(fd, b"complete captured bytes")
        self.assertEqual(output.read("tool-stdout.bin", 131072), b"complete captured bytes")
        with self.assertRaises(Refusal):
            output.open_capture("target.bin")
        with self.assertRaises(Refusal):
            output.open_capture("tool-stdout.bin")

    def test_full_durable_readback_covers_every_owned_file(self):
        output = self.output()
        expected = {"consumed.json": b"{}\n", "target.bin": b"abc",
                    "tool-stdout.bin": b"out", "tool-stderr.bin": b"",
                    "report.json": b"{}\n"}
        for name, raw in expected.items():
            if name.startswith("tool-"):
                fd = output.open_capture(name)
                os.write(fd, raw)
            else:
                output.create(name, raw, {"consumed.json": 1024, "target.bin": 391,
                                          "report.json": 524288}[name])
        self.assertIsNone(output.durable_readback(expected))
        with self.assertRaises(Refusal):
            output.durable_readback({"target.bin": b"abc"})

    def test_readback_detects_replaced_path_inode(self):
        output = self.output()
        output.create("target.bin", b"abc", 391)
        path = self.root / "output" / "target.bin"
        path.rename(path.with_name("moved"))
        path.write_bytes(b"abc")
        with self.assertRaises(Refusal):
            output.read("target.bin", 391)

    def test_readback_detects_replaced_directory(self):
        output = self.output()
        output.create("target.bin", b"abc", 391)
        directory = self.root / "output"
        directory.rename(self.root / "old-output")
        directory.mkdir()
        (directory / "target.bin").write_bytes(b"abc")
        with self.assertRaises(Refusal):
            output.read("target.bin", 391)

    def test_readback_refuses_changed_complete_bytes(self):
        output = self.output()
        output.create("target.bin", b"abc", 391)
        (self.root / "output" / "target.bin").write_bytes(b"abd")
        with self.assertRaises(Refusal):
            output.durable_readback({"target.bin": b"abc"})

    def test_short_write_keeps_partial_file_without_retry(self):
        output = self.output()
        write = self.io.os.write
        def partial(fd, raw):
            return write(fd, raw[:1])
        with mock.patch.object(self.io.os, "write", side_effect=partial):
            with self.assertRaises(Refusal):
                output.create("target.bin", b"abc", 391)
        self.assertEqual((self.root / "output" / "target.bin").read_bytes(), b"a")
        with self.assertRaises(Refusal):
            output.create("target.bin", b"abc", 391)

    def test_fsync_failure_keeps_created_file(self):
        output = self.output()
        with mock.patch.object(self.io.os, "fsync", side_effect=OSError(errno.EIO, "synthetic")):
            with self.assertRaises((Refusal, OSError)):
                output.create("target.bin", b"abc", 391)
        self.assertEqual((self.root / "output" / "target.bin").read_bytes(), b"abc")

    def test_close_is_idempotent_and_prevents_more_io(self):
        output = self.output()
        fd = output.open_capture("tool-stdout.bin")
        output.close()
        output.close()
        with self.assertRaises(OSError):
            os.fstat(fd)
        with self.assertRaises(Refusal):
            output.read("tool-stdout.bin", 131072)

    def test_first_capture_stat_failure_closes_acquired_descriptor(self):
        output = self.output()
        opened = []
        original_open, original_fstat = self.io.os.open, self.io.os.fstat
        def track_open(path, *args, **kwargs):
            fd = original_open(path, *args, **kwargs)
            if path == "tool-stdout.bin":
                opened.append(fd)
            return fd
        def fail_first_stat(fd):
            if fd in opened:
                raise OSError(errno.EIO, "synthetic first stat failure")
            return original_fstat(fd)
        try:
            with mock.patch.object(self.io.os, "open", side_effect=track_open), \
                 mock.patch.object(self.io.os, "fstat", side_effect=fail_first_stat):
                with self.assertRaises(Refusal):
                    output.open_capture("tool-stdout.bin")
            output.close()
            self.assertEqual(len(opened), 1)
            with self.assertRaises(OSError):
                original_fstat(opened[0])
        finally:
            for fd in opened:
                try:
                    os.close(fd)
                except OSError:
                    pass

    def test_child_limit_values_and_signal_restore_without_parent_mutation(self):
        child_limits = self.api("child_limits")
        expected = {resource.RLIMIT_FSIZE: 131072, resource.RLIMIT_AS: 268435456,
                    resource.RLIMIT_CPU: 2, resource.RLIMIT_CORE: 0,
                    resource.RLIMIT_NOFILE: 64}
        before = {key: resource.getrlimit(key) for key in expected}
        selected = {}
        with mock.patch.object(self.io.resource, "setrlimit", side_effect=lambda k, v: selected.__setitem__(k, v)), \
             mock.patch.object(self.io.resource, "getrlimit", side_effect=lambda k: selected[k]), \
             mock.patch.object(self.io.signal, "signal") as restore:
            child_limits()
        self.assertEqual(selected, {key: (value, value) for key, value in expected.items()})
        restore.assert_any_call(signal.SIGXFSZ, signal.SIG_DFL)
        restore.assert_any_call(signal.SIGPIPE, signal.SIG_DFL)
        self.assertEqual(before, {key: resource.getrlimit(key) for key in expected})


# These exact source bytes perform no file opens, network work, parser or tool call.
# Two cases overflow one selected regular descriptor. The third receives a
# partial first write at the cap and exits zero, proving why equality is refused.
SYNTHETIC_CHILD = """import os,resource,signal,sys
expected=((resource.RLIMIT_FSIZE,131072),(resource.RLIMIT_AS,268435456),(resource.RLIMIT_CPU,2),(resource.RLIMIT_CORE,0),(resource.RLIMIT_NOFILE,64))
assert all(resource.getrlimit(k)==(v,v) for k,v in expected)
if sys.argv[1]=='partial':
    signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
    assert os.write(1,b'x'*131073)==131072
    sys.exit(0)
signal.signal(signal.SIGXFSZ,signal.SIG_DFL)
fd=1 if sys.argv[1]=='stdout' else 2
assert os.write(fd,b'x'*131072)==131072
os.write(fd,b'x')
os._exit(77)
"""


@unittest.skipUnless(os.environ.get("MQB_RUN_014_IO_RLIMIT_CASES") == "1",
                     "three synthetic process reservations require explicit opt-in")
class SyntheticRlimitTests(IOTestCase):
    def run_case(self, mode):
        retained = Path(os.environ["MQB_014_IO_PROCESS_EVIDENCE"]).resolve(strict=True) / mode
        retained.mkdir(mode=0o700)  # Exclusive per-case reservation; no replay.
        self.root = retained
        output = self.output()
        stdout = output.open_capture("tool-stdout.bin")
        stderr = output.open_capture("tool-stderr.bin")
        keys = (resource.RLIMIT_FSIZE, resource.RLIMIT_AS, resource.RLIMIT_CPU,
                resource.RLIMIT_CORE, resource.RLIMIT_NOFILE)
        before = {key: resource.getrlimit(key) for key in keys}
        argv = [str(Path(sys.executable).resolve()), "-I", "-S", "-B", "-c", SYNTHETIC_CHILD, mode]
        environment = {"LANG": "C", "LC_ALL": "C", "TZ": "UTC", "DEBUGINFOD_URLS": ""}
        submission = {"case": mode, "argv": argv, "environment": environment, "cwd": "/",
                      "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                      "test_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      "process_reservations": 1, "original_image_accesses": 0}
        with (retained / "submission.json").open("x", encoding="ascii") as stream:
            json.dump(submission, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        process = None
        try:
            process = subprocess.Popen(argv,
                                       stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                       cwd="/", env=environment,
                                       close_fds=True, shell=False, start_new_session=True,
                                       preexec_fn=self.api("child_limits"))
            code = process.wait(timeout=4)
        finally:
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=1)
        raw_out = output.read("tool-stdout.bin", 131072)
        raw_err = output.read("tool-stderr.bin", 131072)
        self.assertEqual(before, {key: resource.getrlimit(key) for key in keys})
        self.assertLessEqual(len(raw_out) + len(raw_err), 262144)
        output.durable_readback({"tool-stdout.bin": raw_out, "tool-stderr.bin": raw_err})
        result = {"case": mode, "process_starts": 1, "returncode": code,
                  "reaped": process.returncode is not None, "parent_limits_unchanged": True,
                  "stdout": {"bytes": len(raw_out), "sha256": hashlib.sha256(raw_out).hexdigest()},
                  "stderr": {"bytes": len(raw_err), "sha256": hashlib.sha256(raw_err).hexdigest()}}
        with (retained / "result.json").open("x", encoding="ascii") as stream:
            json.dump(result, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        return code, raw_out, raw_err

    def test_process_stdout_overflow_has_kernel_cap(self):
        code, stdout, stderr = self.run_case("stdout")
        self.assertEqual(code, -signal.SIGXFSZ)
        self.assertEqual(stdout, b"x" * 131072)
        self.assertEqual(stderr, b"")

    def test_process_stderr_overflow_has_kernel_cap(self):
        code, stdout, stderr = self.run_case("stderr")
        self.assertEqual(code, -signal.SIGXFSZ)
        self.assertEqual(stderr, b"x" * 131072)
        self.assertEqual(stdout, b"")

    def test_process_partial_write_at_cap_can_exit_zero(self):
        code, stdout, stderr = self.run_case("partial")
        self.assertEqual(code, 0)
        self.assertEqual(stdout, b"x" * 131072)
        self.assertEqual(stderr, b"")


if __name__ == "__main__":
    unittest.main()
