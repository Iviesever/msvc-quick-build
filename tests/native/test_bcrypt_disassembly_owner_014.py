"""Owner lifecycle tests use constructed bytes and a fake Popen only."""
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest import mock

from test_bcrypt_disassembly_model_014 import fixture, identity, canonical, argv

owner = importlib.import_module('bcrypt_disassembly_owner_014')


def listing(target, raw=b'\x90\xc3'):
    start = target['preferred_vma_start']
    lines = ['', 'target.bin:     file format binary', '', '',
             'Disassembly of section .data:', '', f'{start:016x} <.data>:']
    for offset, byte in enumerate(raw):
        assembly = 'ret' if byte == 0xc3 else 'nop'
        lines.append(f' {start + offset:x}:\t{byte:02x} \t{assembly}')
    return ('\n'.join(lines) + '\n').encode('ascii')


class FakeProcess:
    pid = 2000014

    def __init__(self, returncode=0, timeout=False, unreaped=False):
        self.returncode = None
        self.result = returncode
        self.timeout = timeout
        self.unreaped = unreaped
        self.waits = 0

    def wait(self, timeout=None):
        self.waits += 1
        if self.unreaped or (self.timeout and self.waits == 1):
            raise subprocess.TimeoutExpired('synthetic-only', timeout)
        self.returncode = self.result
        return self.result

    def poll(self):
        return self.returncode


class OwnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bcrypt014-owner-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.manifest, image, index, admission = fixture()
        self.target = image[self.manifest['target']['file_start']:
                            self.manifest['target']['file_end']]
        for role, rec in self.manifest['identities'].items():
            data = {'image': image, 'index': index, 'admission': admission}.get(role, role.encode())
            path = self.root / role
            path.write_bytes(data)
            rec.update(identity(str(path), data))
        source = self.root / 'source'
        source.mkdir()
        for name, rec in self.manifest['sources'].items():
            data = name.encode('ascii')
            path = source / name
            path.write_bytes(data)
            rec.update(identity(str(path), data))
        self.output = self.root / 'output'
        self.manifest['output_dir'] = str(self.output)
        self.manifest['tool_argv'] = argv(self.manifest['identities']['tool']['path'],
                                         self.manifest['target'])
        self.manifest_path = self.root / 'manifest.json'
        self.raw = canonical(self.manifest)
        self.manifest_path.write_bytes(self.raw)
        self.sha = hashlib.sha256(self.raw).hexdigest()
        self.stdout = listing(self.manifest['target'])
        self.stderr = b''
        self.process = FakeProcess()
        self.calls = []
        self.child_action = lambda: None

    def fake_popen(self, args, **kwargs):
        self.calls.append((args, kwargs))
        self.assertTrue((self.output / 'consumed.json').is_file())
        self.assertEqual((self.output / 'target.bin').read_bytes(), self.target)
        os.write(kwargs['stdout'], self.stdout)
        os.write(kwargs['stderr'], self.stderr)
        self.child_action()
        return self.process

    def run_owner(self, start_ns=None):
        start_ns = time.monotonic_ns() if start_ns is None else start_ns
        if not hasattr(owner, 'subprocess'):
            return owner.observe(self.manifest, self.raw, str(self.manifest_path),
                                 self.sha, start_ns)
        with mock.patch.object(owner.subprocess, 'Popen', side_effect=self.fake_popen), \
             mock.patch.object(owner.os, 'killpg') as kill_mock:
            self.kill_mock = kill_mock
            return owner.observe(self.manifest, self.raw, str(self.manifest_path),
                                 self.sha, start_ns)

    def read_report(self):
        self.assertTrue((self.output / 'report.json').is_file(),
                        'reserved observations must preserve a report when IO permits')
        return json.loads((self.output / 'report.json').read_bytes())

    def assert_refused(self, result):
        self.assertNotEqual(result[0], 0)
        self.assertLessEqual(len(result[1]), 65)
        self.assertNotIn(b'Traceback', result[1])

    def test_complete_reaped_byte_coverage_and_fixed_child_contract(self):
        self.assertEqual(self.run_owner(), (0, b''))
        report = self.read_report()
        self.assertEqual(report['status'], 'complete_disassembly')
        self.assertTrue(report['consumed'])
        self.assertTrue(report['child_reaped'])
        self.assertTrue(report['capture_stable'])
        self.assertTrue(report['completion_requires_clean_owner_exit'])
        self.assertEqual(report['child_launch_reservations'], 1)
        self.assertTrue(report.get('child_handle_obtained'),
                        'the report must name the observed Popen handle precisely')
        self.assertNotIn('child_process_created', report)
        self.assertEqual(len(report['instructions']), 2)
        self.assertEqual(report['raw_stdout']['sha256'], hashlib.sha256(self.stdout).hexdigest())
        self.assertEqual(set(p.name for p in self.output.iterdir()),
                         {'consumed.json', 'target.bin', 'tool-stdout.bin',
                          'tool-stderr.bin', 'report.json'})
        self.assertEqual(len(self.calls), 1)
        args, kw = self.calls[0]
        self.assertEqual(args, self.manifest['tool_argv'])
        self.assertEqual(kw['env'], self.manifest['environment'])
        self.assertEqual(kw['cwd'], str(self.output))
        self.assertTrue(kw['start_new_session'])
        self.assertTrue(kw['close_fds'])
        self.assertEqual(kw['stdin'], subprocess.DEVNULL)
        self.assertTrue(callable(kw['preexec_fn']))

    def test_existing_directory_is_never_overwritten_or_retried(self):
        self.assertEqual(self.run_owner(), (0, b''))
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.iterdir()})

    def test_raw_stdout_exact_cap_is_consumed_failure_even_exit_zero(self):
        self.stdout = b'x' * 131072
        self.assert_refused(self.run_owner())
        report = self.read_report()
        self.assertEqual(report['status'], 'refused')
        self.assertTrue(report['capture_stable'])
        self.assertEqual(report['raw_stdout']['bytes'], 131072)
        self.assertEqual(report['child_returncode'], 0)

    def test_raw_stderr_exact_cap_is_consumed_failure(self):
        self.stderr = b'x' * 131072
        self.assert_refused(self.run_owner())
        self.assertEqual(self.read_report()['raw_stderr']['bytes'], 131072)

    def test_nonzero_exit_preserves_complete_raw_capture(self):
        self.process = FakeProcess(returncode=9)
        self.assert_refused(self.run_owner())
        self.assertEqual(self.read_report()['child_returncode'], 9)
        self.assertEqual((self.output / 'tool-stdout.bin').read_bytes(), self.stdout)

    def test_unexpected_stderr_refuses_clean_exit(self):
        self.stderr = b'warning\n'
        self.assert_refused(self.run_owner())
        self.assertEqual(self.read_report()['status'], 'refused')

    def test_raw_mismatch_is_not_admitted(self):
        self.stdout = self.stdout.replace(b'90 ', b'91 ', 1)
        self.assert_refused(self.run_owner())
        self.assertEqual(self.read_report()['status'], 'refused')

    def test_changed_input_is_consumed_before_process_creation(self):
        Path(self.manifest['identities']['image']['path']).write_bytes(b'changed')
        self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 0)
        self.assertTrue((self.output / 'consumed.json').is_file())

    def test_timeout_reaps_once_and_cannot_report_success(self):
        self.process = FakeProcess(returncode=-9, timeout=True)
        self.assert_refused(self.run_owner())
        report = self.read_report()
        self.assertTrue(report['child_reaped'])
        self.assertEqual(report['child_returncode'], -9)
        self.assertEqual(len(self.calls), 1)

    def test_unreaped_capture_never_gets_final_digest(self):
        self.process = FakeProcess(unreaped=True)
        self.assert_refused(self.run_owner())
        report = self.read_report()
        self.assertFalse(report['child_reaped'])
        self.assertFalse(report['capture_stable'])
        self.assertIsNone(report['raw_stdout'])
        self.assertIsNone(report['raw_stderr'])

    def test_manifest_disk_change_is_consumed_without_launch(self):
        self.manifest_path.write_bytes(b'{}\n')
        self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 0)
        self.assertTrue(self.read_report()['consumed'])

    def test_post_child_source_change_refuses_complete_listing(self):
        path = Path(next(iter(self.manifest['sources'].values()))['path'])
        self.child_action = lambda: path.write_bytes(b'changed-after-child')
        self.assert_refused(self.run_owner())
        report = self.read_report()
        self.assertTrue(report['identities_before_verified'])
        self.assertFalse(report['identities_after_verified'])
        self.assertTrue(report['capture_stable'])

    def test_popen_creation_failure_consumes_one_launch_reservation(self):
        def fail():
            raise OSError('synthetic spawn error')
        self.child_action = fail
        self.assert_refused(self.run_owner())
        report = self.read_report()
        self.assertEqual(report['child_launch_reservations'], 1)
        self.assertIs(report.get('child_handle_obtained'), False)
        self.assertNotIn('child_process_created', report)
        self.assertEqual(len(self.calls), 1)
        self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)

    def test_deadline_before_launch_retains_reservation_and_no_child(self):
        current = [1000]
        original = owner.OutputDirectory.durable_readback
        def late(directory, expected):
            original(directory, expected)
            current[0] = 12_000_001_001
        with mock.patch.object(owner.time, 'monotonic_ns', side_effect=lambda: current[0]), \
             mock.patch.object(owner.OutputDirectory, 'durable_readback', new=late):
            self.assert_refused(self.run_owner(start_ns=1000))
        self.assertEqual(len(self.calls), 0)
        self.assertEqual(self.read_report()['reason'], 'DEADLINE')

    def test_late_final_readback_makes_complete_candidate_cli_nonzero(self):
        current, count = [1000], [0]
        original = owner.OutputDirectory.durable_readback
        def late(directory, expected):
            original(directory, expected)
            count[0] += 1
            if count[0] == 2:
                current[0] = 12_000_001_001
        with mock.patch.object(owner.time, 'monotonic_ns', side_effect=lambda: current[0]), \
             mock.patch.object(owner.OutputDirectory, 'durable_readback', new=late):
            self.assert_refused(self.run_owner(start_ns=1000))
        self.assertEqual(len(self.calls), 1)
        report = self.read_report()
        self.assertEqual(report['status'], 'complete_disassembly')
        self.assertTrue(report['completion_requires_clean_owner_exit'])

    def test_final_readback_error_cannot_return_zero(self):
        original = owner.OutputDirectory.durable_readback
        def fail_final(directory, expected):
            if 'report.json' in expected:
                raise OSError('synthetic final fsync failure')
            return original(directory, expected)
        with mock.patch.object(owner.OutputDirectory, 'durable_readback', new=fail_final):
            self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(self.read_report()['completion_requires_clean_owner_exit'])

    def test_report_write_failure_preserves_raw_without_success(self):
        original = owner.OutputDirectory.create
        def fail_report(directory, name, data, cap, mode=0o600):
            if name == 'report.json':
                raise OSError('synthetic report write failure')
            return original(directory, name, data, cap, mode)
        with mock.patch.object(owner.OutputDirectory, 'create', new=fail_report):
            self.assert_refused(self.run_owner())
        self.assertFalse((self.output / 'report.json').exists())
        self.assertEqual((self.output / 'tool-stdout.bin').read_bytes(), self.stdout)
        self.assertEqual(len(self.calls), 1)

    def test_marker_write_failure_still_consumes_directory(self):
        original = owner.OutputDirectory.create
        def fail_marker(directory, name, data, cap, mode=0o600):
            if name == 'consumed.json':
                raise OSError('synthetic marker write failure')
            return original(directory, name, data, cap, mode)
        with mock.patch.object(owner.OutputDirectory, 'create', new=fail_marker):
            self.assert_refused(self.run_owner())
        self.assertTrue(self.output.is_dir())
        self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 0)

    def test_final_close_error_is_not_success(self):
        original = owner.OutputDirectory.close
        def fail_close(directory):
            original(directory)
            raise OSError('synthetic close failure')
        with mock.patch.object(owner.OutputDirectory, 'close', new=fail_close):
            self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)

    def test_expired_request_never_reserves_or_launches(self):
        self.assert_refused(self.run_owner(start_ns=0))
        self.assertFalse(self.output.exists())
        self.assertEqual(len(self.calls), 0)

    def test_wait_interruption_after_reap_does_not_signal_reused_pid(self):
        class InterruptedAfterReap(FakeProcess):
            def wait(self, timeout=None):
                self.returncode = 0
                raise KeyboardInterrupt()
        self.process = InterruptedAfterReap()
        self.assert_refused(self.run_owner())
        self.kill_mock.assert_not_called()
        report = self.read_report()
        self.assertTrue(report['child_reaped'])
        self.assertEqual(report['child_returncode'], 0)

    def test_ignored_sigchld_refuses_launch_instead_of_guessing_exit_zero(self):
        with mock.patch.object(owner.signal, 'getsignal', return_value=owner.signal.SIG_IGN):
            self.assert_refused(self.run_owner())
        self.assertEqual(len(self.calls), 0)
        self.assertEqual(self.read_report()['reason'], 'CHILD_SIGNAL_POLICY')


if __name__ == '__main__':
    unittest.main()
