"""Both 015 outer phases use fake process, selector, streams and output."""
import contextlib
import hashlib
import io
import json
import signal
import subprocess
import tempfile
from pathlib import Path
import types
import unittest
from unittest import mock

import run_bounded_cli_015 as outer


def encode(value, cap):
    raw = (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()
    if len(raw) > cap:
        raise ValueError('constructed cap')
    return raw


class Pipe:
    def __init__(self, fd):
        self.fd, self.closes, self.fail = fd, 0, False

    def fileno(self):
        return self.fd

    def close(self):
        self.closes += 1
        if self.fail:
            raise OSError('constructed close failure')


class Process:
    pid = 900015

    def __init__(self):
        self.stdout, self.stderr = Pipe(900016), Pipe(900017)
        self.result, self.returncode, self.waits = 0, None, 0
        self.unreaped = False

    def wait(self, timeout):
        self.waits += 1
        if self.unreaped:
            raise subprocess.TimeoutExpired('constructed only', timeout)
        self.returncode = self.result
        return self.result

    def poll(self):
        return self.returncode


class Selector:
    def __init__(self, clock):
        self.clock, self.mapping, self.closes, self.late = clock, {}, 0, False

    def register(self, pipe, event, data):
        self.mapping[pipe] = types.SimpleNamespace(fileobj=pipe, data=data)

    def unregister(self, pipe):
        del self.mapping[pipe]

    def get_map(self):
        return self.mapping

    def select(self, timeout):
        return [(key, 1) for key in list(self.mapping.values())]

    def close(self):
        self.closes += 1
        if self.late:
            self.clock[0] = 21_000_000_000


class Output:
    def __init__(self, clock):
        self.clock, self.files, self.caps, self.durable, self.closes = clock, {}, {}, [], 0
        self.late_reserve = self.late_close = self.fail_report = False

    def create(self, name, raw, cap):
        if self.fail_report and name == 'report.json':
            raise OSError('constructed report failure')
        assert name not in self.files and len(raw) <= cap
        self.files[name], self.caps[name] = raw, cap

    def durable_readback(self, expected):
        assert expected == self.files
        self.durable.append(dict(expected))
        if self.late_reserve:
            self.clock[0] = 21_000_000_000

    def close(self):
        self.closes += 1
        if self.late_close:
            self.clock[0] = 21_000_000_000


class OuterTests(unittest.TestCase):
    def setUp(self):
        self.clock, self.process = [0], Process()
        self.selector, self.output = Selector(self.clock), Output(self.clock)
        self.streams, self.reads = {900016: [b''], 900017: [b'']}, []
        self.manifest = {'call_id': '015-synthetic-outer-pure', 'phase': 'actual',
                         'mode': 'synthetic', 'output_dir': '/constructed-owner-output'}

    def invoke(self):
        if not hasattr(outer, 'subprocess'):
            return outer.run_once('/constructed-manifest', '0' * 64, '/constructed-outer')
        def read(fd, count):
            self.reads.append((fd, count))
            chunk = self.streams[fd].pop(0)
            self.assertLessEqual(len(chunk), count)
            return chunk
        def popen(argv, **kwargs):
            self.assertEqual(len(self.output.durable), 1)
            self.assertEqual(set(self.output.files), {'consumed.json'})
            self.assertEqual(argv[1:3], ['-I', '-B'])
            self.assertTrue(kwargs['start_new_session'] and kwargs['close_fds'])
            self.assertEqual(kwargs['env']['DEBUGINFOD_URLS'], '')
            return self.process
        model = types.SimpleNamespace(encode_json=encode,
                                      limits_for=lambda phase: {'outer_seconds': 12 if phase == 'selection' else 20})
        with mock.patch.object(outer, 'load', return_value=(self.manifest, model, None)), \
                mock.patch.object(outer, 'output_directory', return_value=self.output), \
                mock.patch.object(outer.time, 'monotonic_ns', side_effect=lambda: self.clock[0]), \
                mock.patch.object(outer.signal, 'getsignal', return_value=signal.SIG_DFL), \
                mock.patch.object(outer.selectors, 'DefaultSelector', return_value=self.selector), \
                mock.patch.object(outer.os, 'set_blocking'), \
                mock.patch.object(outer.os, 'read', side_effect=read), \
                mock.patch.object(outer.os, 'killpg') as kill, \
                mock.patch.object(outer.subprocess, 'Popen', side_effect=popen) as popen_mock:
            self.kill, self.popen = kill, popen_mock
            return outer.run_once('/constructed-manifest', '0' * 64, '/constructed-outer')

    def test_both_phases_clean_reaped_and_finally_closed(self):
        for phase, seconds in [('selection', 12), ('actual', 20)]:
            with self.subTest(phase=phase):
                self.setUp()
                self.manifest['phase'] = phase
                result = self.invoke()
                self.assertIsInstance(result, dict)
                self.assertTrue(result['completion_candidate'])
                self.assertTrue(result['owner_reaped'])
                marker = json.loads(self.output.files['consumed.json'])
                self.assertEqual(marker['controller_seconds'], seconds)
                self.assertEqual(self.popen.call_count, 1)
                self.assertEqual(sum(self.output.caps.values()), 9283)
                self.assertEqual(len(self.output.durable), 2)
                self.assertEqual((self.output.closes, self.selector.closes,
                                  self.process.stdout.closes, self.process.stderr.closes), (1, 1, 1, 1))

    def test_detection_bytes_are_retained_and_refused(self):
        for fd, raw in [(900016, b'x'), (900017, b'x' * 66)]:
            with self.subTest(fd=fd):
                self.setUp()
                self.streams[fd] = [raw]
                result = self.invoke()
                self.assertFalse(result['completion_candidate'])
                self.assertEqual(result['controller_fault'], 'CLI_OUTPUT_CAP')
                self.assertEqual(self.kill.call_count, 1)

    def test_65_diagnostic_bytes_are_retained_but_not_clean_success(self):
        self.streams[900017] = [b'x' * 65, b'']
        result = self.invoke()
        self.assertIsInstance(result, dict)
        self.assertFalse(result['completion_candidate'])
        self.assertEqual(len(self.output.files['tool-stderr.bin']), 65)
        self.assertEqual(self.reads[-1], (900017, 1))

    def test_nonzero_crash_and_unreaped_owner_refuse(self):
        for code, unreaped in [(7, False), (-11, False), (0, True)]:
            with self.subTest(code=code, unreaped=unreaped):
                self.setUp()
                self.process.result, self.process.unreaped = code, unreaped
                result = self.invoke()
                self.assertFalse(result['completion_candidate'])
                self.assertEqual(result['owner_reaped'], not unreaped)
                self.assertEqual(self.popen.call_count, 1)

    def test_late_reservation_does_not_launch(self):
        self.output.late_reserve = True
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertEqual(self.popen.call_count, 0)

    def test_late_output_or_selector_close_refuses_after_cleanup(self):
        for late in ('output', 'selector'):
            with self.subTest(late=late):
                self.setUp()
                if late == 'output':
                    self.output.late_close = True
                else:
                    self.selector.late = True
                with self.assertRaises(TimeoutError):
                    self.invoke()
                self.assertEqual(self.process.stdout.closes, 1)
                self.assertEqual(self.process.stderr.closes, 1)

    def test_report_and_pipe_close_errors_never_return_success(self):
        for error in ('report', 'pipe'):
            with self.subTest(error=error):
                self.setUp()
                self.output.fail_report = error == 'report'
                self.process.stdout.fail = error == 'pipe'
                with self.assertRaises(OSError):
                    self.invoke()
                self.assertEqual(self.process.stderr.closes, 1)

    def test_already_reaped_interruption_never_signals_recycled_pid(self):
        def interrupted(timeout):
            self.process.returncode = 0
            raise KeyboardInterrupt()
        self.process.wait = interrupted
        result = self.invoke()
        self.assertFalse(result['completion_candidate'])
        self.kill.assert_not_called()
        self.assertTrue(result['owner_reaped'])

    def test_actual_destination_cannot_use_synthetic_override(self):
        self.manifest['mode'] = 'actual'
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertEqual(self.popen.call_count, 0)


class InputAndCLITests(unittest.TestCase):
    def test_strict_decode_and_canonical_bytes_precede_core_compilation(self):
        manifest = {'sources': {'run_bcrypt_disassembly_015.py':
                               {'path': outer.BOOTSTRAP, 'sha256': '0' * 64}}}
        canonical = encode(manifest, 32768)
        cases = [(canonical.replace(b'{"sources":', b'{"x":1,"x":2,"sources":'), ValueError('duplicate')),
                 (b' ' + canonical, None)]
        for raw, error in cases:
            with self.subTest(raw=raw):
                bootstrap = types.ModuleType('constructed015')
                bootstrap._decode = mock.Mock(return_value=manifest, side_effect=error)
                model = types.SimpleNamespace(validate_manifest=mock.Mock(), encode_json=encode)
                bootstrap._load_sources = mock.Mock(return_value=(model, None))
                with mock.patch.object(outer, 'bounded_read', side_effect=[raw, b'']), \
                        mock.patch.object(outer.types, 'ModuleType', return_value=bootstrap), \
                        self.assertRaises(ValueError):
                    outer.load('/constructed-manifest', '0' * 64)
                bootstrap._load_sources.assert_not_called()
                bootstrap._decode.assert_called_once_with(raw)

    def test_bounded_read_rejects_fd_or_path_link_count_change(self):
        with tempfile.TemporaryDirectory(prefix='bcrypt015-link-count-') as tmp:
            path = Path(tmp).resolve() / 'input'
            path.write_bytes(b'abc')
            before = path.stat()
            keys = ('st_dev', 'st_ino', 'st_mode', 'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
            changed = types.SimpleNamespace(**{key: getattr(before, key) for key in keys})
            changed.st_nlink += 1
            for location in ('fd', 'path'):
                with self.subTest(location=location), \
                        mock.patch.object(outer.os, 'fstat', side_effect=[before, changed if location == 'fd' else before]), \
                        mock.patch.object(outer.os, 'stat', return_value=changed if location == 'path' else before), \
                        self.assertRaises(ValueError):
                    outer.bounded_read(str(path), 3, hashlib.sha256(b'abc').hexdigest())

    def test_bounded_read_rejects_wrong_hash_oversize_and_symlink(self):
        with tempfile.TemporaryDirectory(prefix='bcrypt015-outer-input-') as tmp:
            path = Path(tmp).resolve() / 'input'
            path.write_bytes(b'abc')
            digest = hashlib.sha256(b'abc').hexdigest()
            self.assertEqual(outer.bounded_read(str(path), 3, digest), b'abc')
            for cap, sha in [(2, digest), (3, '0' * 64)]:
                with self.subTest(cap=cap), self.assertRaises(ValueError):
                    outer.bounded_read(str(path), cap, sha)
            alias = path.parent / 'alias'
            alias.symlink_to(path)
            with self.assertRaises(ValueError):
                outer.bounded_read(str(alias), 3, digest)

    def test_cli_success_refusal_and_setup_failure_exit_distinctly(self):
        result = {'call_id': '015-synthetic-cli-pure', 'completion_candidate': True,
                  'owner_returncode': 0, 'controller_fault': None}
        with mock.patch.object(outer, 'run_once', return_value=result), \
                mock.patch.object(outer.os, 'write', side_effect=lambda fd, raw: len(raw)):
            self.assertEqual(outer.main(['/manifest', '0' * 64, '/output']), 0)
            result['completion_candidate'] = False
            self.assertEqual(outer.main(['/manifest', '0' * 64, '/output']), 1)
        with mock.patch.object(outer, 'run_once', side_effect=OSError()), \
                mock.patch.object(outer.os, 'write', side_effect=lambda fd, raw: len(raw)):
            self.assertEqual(outer.main(['/manifest', '0' * 64, '/output']), 2)
        for outcome in (BrokenPipeError('constructed'), 0):
            with self.subTest(outcome=type(outcome).__name__), \
                    mock.patch.object(outer, 'run_once', return_value=result), \
                    mock.patch.object(outer.os, 'write', side_effect=[outcome]) as write, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(outer.main(['/manifest', '0' * 64, '/output']), 2)
                self.assertEqual(write.call_count, 1)


if __name__ == '__main__':
    unittest.main()
