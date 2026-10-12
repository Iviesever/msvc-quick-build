"""015 lifecycle tests: constructed files and fake Popen, no real child."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import bcrypt_disassembly_owner_015 as owner


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()


class FakeProcess:
    pid = 2000015

    def __init__(self, result=0, timeout=False, unreaped=False):
        self.result, self.timeout, self.unreaped = result, timeout, unreaped
        self.returncode, self.waits = None, 0

    def wait(self, timeout=None):
        self.waits += 1
        if self.unreaped or self.timeout and self.waits == 1:
            raise subprocess.TimeoutExpired('constructed-only', timeout)
        self.returncode = self.result
        return self.result

    def poll(self):
        return self.returncode


class OwnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='bcrypt015-owner-pure-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.target = b'\x90' * 166 + b'\xc3'
        self.events, self.calls, self.stderr = [], [], b''
        self.process, self.child_action = FakeProcess(), lambda: None
        self.selection_error = False

        def records(names):
            result = {}
            for name in names:
                raw = name.encode()
                path = self.root / name
                path.write_bytes(raw)
                result[name] = {'path': str(path), 'bytes': len(raw),
                                'sha256': hashlib.sha256(raw).hexdigest()}
            return result

        self.output = self.root / 'output'
        target = {'file_start': 16, 'file_end': 183, 'rva_start': 4096,
                  'rva_end': 4263, 'preferred_vma_start': 0x180001000,
                  'runtime_function_index': 105, 'section_index': 0,
                  'sha256': hashlib.sha256(self.target).hexdigest()}
        roles = ['image', 'index', 'admission', 'caller', 'python', 'tool',
                 'loader_cache', 'selection', 'selection_review']
        roles += ['dep%02d' % i for i in range(1, 9)]
        self.manifest = {
            'phase': 'actual', 'mode': 'synthetic', 'call_id': '015-synthetic-owner-pure',
            'source_commit': 'a' * 40, 'baseline_commit': 'b' * 40,
            'output_dir': str(self.output), 'target': target,
            'identities': records(roles), 'sources': records(['source%d' % i for i in range(7)]),
            'provenance': records(['origin%d' % i for i in range(5)]),
            'environment': {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC', 'DEBUGINFOD_URLS': ''},
            'aliases': [], 'absent_paths': [], 'tool_argv': ['/constructed/tool', 'target.bin'],
        }
        self.raw = canonical(self.manifest)
        self.path = self.root / 'manifest.json'
        self.path.write_bytes(self.raw)
        self.sha = hashlib.sha256(self.raw).hexdigest()
        start = target['preferred_vma_start']
        lines = ['', 'target.bin:     file format binary', '', '',
                 'Disassembly of section .data:', '', f'{start:016x} <.data>:']
        lines += [f' {start + i:x}:\t{byte:02x} \t' + ('ret' if byte == 195 else 'nop')
                  for i, byte in enumerate(self.target)]
        self.stdout = ('\n'.join(lines) + '\n').encode()

    def run_owner(self, start_ns=None):
        started = time.monotonic_ns() if start_ns is None else start_ns
        if not hasattr(owner, 'subprocess'):
            return owner.observe(self.manifest, self.raw, str(self.path), self.sha, started)
        original = owner.verify_identity

        def verify(record, cap, keep=False):
            self.events.append(Path(record['path']).name)
            return original(record, cap, keep)

        def selection(*args):
            self.events.append('selection_validated')
            if self.selection_error:
                raise owner.Refusal('SELECTION_REFUSED')

        def popen(argv, **kwargs):
            self.calls.append((argv, kwargs))
            self.assertTrue((self.output / 'consumed.json').is_file())
            self.assertEqual((self.output / 'target.bin').read_bytes(), self.target)
            os.write(kwargs['stdout'], self.stdout)
            os.write(kwargs['stderr'], self.stderr)
            self.child_action()
            return self.process

        with mock.patch.object(owner, 'validate_manifest'), \
                mock.patch.object(owner, 'select_target', return_value=self.target), \
                mock.patch.object(owner, 'validate_selection_evidence', side_effect=selection), \
                mock.patch.object(owner, 'verify_identity', side_effect=verify), \
                mock.patch.object(owner.subprocess, 'Popen', side_effect=popen), \
                mock.patch.object(owner.os, 'killpg') as killed:
            self.killed = killed
            return owner.observe(self.manifest, self.raw, str(self.path), self.sha, started)

    def report(self):
        self.assertTrue((self.output / 'report.json').is_file())
        return json.loads((self.output / 'report.json').read_bytes())

    def refused(self, result):
        self.assertNotEqual(result[0], 0)
        self.assertLessEqual(len(result[1]), 65)
        self.assertNotIn(b'Traceback', result[1])

    def test_complete_167_bytes_and_selection_precedes_each_image_read(self):
        self.assertEqual(self.run_owner(), (0, b''))
        report = self.report()
        self.assertEqual(len(report['instructions']), 167)
        self.assertEqual(report['status'], 'complete_disassembly')
        self.assertTrue(report['child_reaped'] and report['capture_stable'])
        self.assertEqual(report['child_launch_reservations'], 1)
        self.assertTrue(report['completion_requires_clean_owner_exit'])
        self.assertEqual(len(self.calls), 1)
        for i, event in enumerate(self.events):
            if event == 'image':
                self.assertEqual(self.events[:i].count('selection_validated'),
                                 self.events[:i].count('image') + 1)
        self.assertEqual(self.events.count('image'), 2)
        argv, kwargs = self.calls[0]
        self.assertEqual(argv, self.manifest['tool_argv'])
        self.assertEqual(kwargs['env'], self.manifest['environment'])
        self.assertTrue(kwargs['start_new_session'] and kwargs['close_fds'])
        self.assertEqual(kwargs['stdin'], subprocess.DEVNULL)
        self.assertIs(kwargs['preexec_fn'], owner.child_limits)

    def test_unadmitted_selection_never_opens_image_or_launches(self):
        self.selection_error = True
        self.refused(self.run_owner())
        self.assertNotIn('image', self.events)
        self.assertFalse(self.calls)
        self.assertTrue(self.report()['consumed'])

    def test_reused_directory_preserves_every_byte(self):
        self.assertEqual(self.run_owner(), (0, b''))
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        self.refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.iterdir()})

    def test_stdout_and_stderr_caps_and_unexpected_stderr_refuse(self):
        for stream, raw in [('stdout', b'x' * 131072), ('stderr', b'x' * 131072),
                            ('stderr', b'warning')]:
            with self.subTest(stream=stream, bytes=len(raw)):
                self.setUp()
                setattr(self, stream, raw)
                self.refused(self.run_owner())
                self.assertEqual(self.report()['raw_' + stream]['bytes'], len(raw))

    def test_nonzero_crash_timeout_and_unreaped_are_not_success(self):
        for process in (FakeProcess(9), FakeProcess(-11), FakeProcess(-9, timeout=True),
                        FakeProcess(unreaped=True)):
            with self.subTest(process=vars(process)):
                self.setUp()
                self.process = process
                self.refused(self.run_owner())
                report = self.report()
                self.assertEqual(len(self.calls), 1)
                self.assertEqual(report['child_reaped'], not process.unreaped)
                if process.unreaped:
                    self.assertFalse(report['capture_stable'])
                    self.assertIsNone(report['raw_stdout'])

    def test_changed_identity_before_launch_and_after_child_refuse(self):
        Path(self.manifest['identities']['image']['path']).write_bytes(b'changed')
        self.refused(self.run_owner())
        self.assertFalse(self.calls)
        self.setUp()
        path = Path(self.manifest['provenance']['origin4']['path'])
        self.child_action = lambda: path.write_bytes(b'changed')
        self.refused(self.run_owner())
        self.assertFalse(self.report()['identities_after_verified'])

    def test_listing_mismatch_or_extra_instruction_refuses(self):
        self.stdout = self.stdout.replace(b'90 ', b'91 ', 1)
        self.refused(self.run_owner())
        self.assertEqual(self.report()['status'], 'refused')
        self.setUp()
        self.stdout += b' 1800010a7:\t90 \tnop\n'
        self.refused(self.run_owner())

    def test_spawn_failure_consumes_reservation_without_owned_handle(self):
        self.child_action = lambda: (_ for _ in ()).throw(OSError('constructed spawn failure'))
        self.refused(self.run_owner())
        report = self.report()
        self.assertEqual(report['child_launch_reservations'], 1)
        self.assertFalse(report['child_handle_obtained'])

    def test_sigchld_policy_and_expired_request_never_launch(self):
        with mock.patch.object(owner.signal, 'getsignal', return_value=owner.signal.SIG_IGN):
            self.refused(self.run_owner())
        self.assertFalse(self.calls)
        self.setUp()
        self.refused(self.run_owner(start_ns=0))
        self.assertFalse(self.calls)

    def test_already_reaped_interrupt_never_signals_recycled_pid(self):
        def interrupted(timeout=None):
            self.process.returncode = 0
            raise KeyboardInterrupt()
        self.process.wait = interrupted
        self.refused(self.run_owner())
        self.killed.assert_not_called()
        self.assertTrue(self.report()['child_reaped'])

    def test_late_persistence_and_final_close_cannot_return_success(self):
        for operation in ('durable_readback', 'close'):
            with self.subTest(operation=operation):
                self.setUp()
                clock = [1000]
                original = getattr(owner.OutputDirectory, operation)
                def late(directory, *args):
                    result = original(directory, *args)
                    if operation == 'close' or 'report.json' in args[0]:
                        clock[0] = 12_000_001_001
                    return result
                with mock.patch.object(owner.time, 'monotonic_ns', side_effect=lambda: clock[0]), \
                        mock.patch.object(owner.OutputDirectory, operation, new=late):
                    self.refused(self.run_owner(start_ns=1000))
                self.assertEqual(len(self.calls), 1)

    def test_final_durability_or_close_error_refuses(self):
        original = owner.OutputDirectory.durable_readback
        def failed(directory, expected):
            if 'report.json' in expected:
                raise OSError('constructed fsync failure')
            return original(directory, expected)
        with mock.patch.object(owner.OutputDirectory, 'durable_readback', new=failed):
            self.refused(self.run_owner())
        self.assertEqual(len(self.calls), 1)

    def test_new_target_and_report_caps_reject_old_larger_limits(self):
        out = owner.OutputDirectory(str(self.root / 'cap-output'))
        self.addCleanup(out.close)
        for name, count in [('target.bin', 168), ('report.json', 262145)]:
            with self.subTest(name=name), self.assertRaises(owner.Refusal):
                out.create(name, b'x' * count, count)

    def test_child_limits_install_and_verify_each_soft_hard_pair(self):
        installed = {}
        with mock.patch.object(owner.resource, 'setrlimit', side_effect=installed.__setitem__), \
                mock.patch.object(owner.resource, 'getrlimit', side_effect=installed.__getitem__), \
                mock.patch.object(owner.signal, 'signal'):
            owner.child_limits()
        self.assertEqual(installed, {owner.resource.RLIMIT_FSIZE: (131072, 131072),
                                     owner.resource.RLIMIT_AS: (268435456, 268435456),
                                     owner.resource.RLIMIT_CPU: (2, 2), owner.resource.RLIMIT_CORE: (0, 0),
                                     owner.resource.RLIMIT_NOFILE: (64, 64)})
        with mock.patch.object(owner.resource, 'setrlimit') as install, \
                mock.patch.object(owner.resource, 'getrlimit', return_value=(0, 0)), \
                self.assertRaises(owner.Refusal):
            owner.child_limits()
        self.assertEqual(install.call_count, 1)


if __name__ == '__main__':
    unittest.main()
