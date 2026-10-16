"""Startup/receipt regressions: bounded fake task lists and real temporary files.

No native threads, Popen workloads, historical image reads or study allocation.
Previous test modules and their assertions stay unchanged.
"""
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace
import os
import tempfile
import unittest
from unittest import mock

import bcrypt_observation_controller_017 as ctrl
import bcrypt_owner_backend_017 as backend
from bcrypt_guarded_owner_017 import Refused
from test_bcrypt_owner_backend_017 import bridge, Process, TARGET
from test_bcrypt_bootstrap_017 import BundleFixture


@contextmanager
def task_view(names=None, error=None):
    """Only substitute this process's task-directory boundary, not host discovery."""
    real = os.scandir
    calls = []
    @contextmanager
    def view():
        if error is not None:
            raise error
        yield iter(SimpleNamespace(name=n) for n in names)
    def scan(path):
        if path == '/proc/self/task':
            calls.append(path)
            return view()
        return real(path)
    with mock.patch.object(backend.os, 'scandir', side_effect=scan):
        yield calls


class OwnerThreadPolicyTests(unittest.TestCase):
    def ready(self):
        value, directory = bridge()
        value.reserve(); value.write('target.bin', TARGET)
        return value, directory

    def test_native_extra_task_refuses_before_popen(self):
        value, directory = self.ready()
        # Python's active_count can be one while a native task exists.
        with task_view([str(os.getpid()), '2147483646']), \
             mock.patch('threading.active_count', return_value=1), \
             mock.patch.object(backend.signal, 'getsignal', return_value=backend.signal.SIG_DFL), \
             mock.patch.object(backend.subprocess, 'Popen', return_value=Process()) as popen:
            with self.assertRaisesRegex(Refused, 'PREPARATION'):
                value.launch(TARGET)
            popen.assert_not_called()
        self.assertTrue(value.launch_used)
        self.assertEqual(directory.files['target.bin'], TARGET)

    def test_unavailable_task_view_is_not_single_threaded(self):
        for error in (FileNotFoundError('test'), PermissionError('test')):
            with self.subTest(error=type(error).__name__):
                value, _ = self.ready()
                with task_view(error=error), \
                     mock.patch.object(backend.signal, 'getsignal', return_value=backend.signal.SIG_DFL), \
                     mock.patch.object(backend.subprocess, 'Popen', return_value=Process()) as popen:
                    with self.assertRaisesRegex(Refused, 'PREPARATION'):
                        value.launch(TARGET)
                    popen.assert_not_called()

    def test_empty_or_foreign_task_view_refuses(self):
        for names in ([], ['2147483646']):
            with self.subTest(names=names):
                value, _ = self.ready()
                with task_view(names), \
                     mock.patch.object(backend.signal, 'getsignal', return_value=backend.signal.SIG_DFL), \
                     mock.patch.object(backend.subprocess, 'Popen', return_value=Process()) as popen:
                    with self.assertRaises(Refused): value.launch(TARGET)
                    popen.assert_not_called()

    def test_single_leader_reaches_original_popen_recipe_once(self):
        value, _ = self.ready()
        with task_view([str(os.getpid())]) as scans, \
             mock.patch.object(backend.signal, 'getsignal', return_value=backend.signal.SIG_DFL), \
             mock.patch.object(backend.subprocess, 'Popen', return_value=Process()) as popen:
            value.launch(TARGET)
        self.assertEqual(scans, ['/proc/self/task'])
        self.assertEqual(popen.call_count, 1)
        self.assertIs(popen.call_args.kwargs['preexec_fn'], backend.child_limits)

    def test_policy_checked_after_output_preparation(self):
        value, directory = self.ready()
        ready = False
        original = directory.durable_readback
        def prepare(expected):
            nonlocal ready
            original(expected); ready = True
        directory.durable_readback = prepare
        real = os.scandir
        @contextmanager
        def tasks():
            self.assertTrue(ready)
            yield iter([SimpleNamespace(name=str(os.getpid()))])
        def scan(path): return tasks() if path == '/proc/self/task' else real(path)
        with mock.patch.object(backend.os, 'scandir', side_effect=scan) as scans, \
             mock.patch.object(backend.signal, 'getsignal', return_value=backend.signal.SIG_DFL), \
             mock.patch.object(backend.subprocess, 'Popen', return_value=Process()):
            value.launch(TARGET)
        self.assertIn(mock.call('/proc/self/task'), scans.call_args_list)


class ControllerRecordReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mqb017-record-review-')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)/'records'
        self.records = ctrl.ControllerRecords(str(self.path))
        self.addCleanup(self.cleanup_records)

    def cleanup_records(self):
        if not self.records.closed:
            try: self.records.close()
            except (OSError, Refused): pass

    def test_unknown_directory_member_is_detected(self):
        self.records.save('reservation.json', b'{}\n')
        (self.path/'foreign.txt').write_bytes(b'keep me')
        with self.assertRaises(Refused): self.records.check()
        self.assertEqual((self.path/'foreign.txt').read_bytes(), b'keep me')

    def test_unowned_allowed_name_is_not_accepted(self):
        (self.path/'failure.json').write_bytes(b'{}\n')
        with self.assertRaises(Refused): self.records.check()

    def test_earlier_record_change_refuses_close(self):
        self.records.save('reservation.json', b'{}\n')
        self.records.save('assessment.json', b'{"ok":true}\n')
        (self.path/'reservation.json').write_bytes(b'[]\n')  # same byte count
        with self.assertRaises(Refused): self.records.close()
        self.assertTrue(self.records.closed)
        self.assertIsNone(self.records.fd)
        self.assertEqual((self.path/'reservation.json').read_bytes(), b'[]\n')

    def test_equal_bytes_replacement_is_not_original_file(self):
        self.records.save('reservation.json', b'{}\n')
        source = self.path/'reservation.json'
        replacement = Path(self.temp.name)/'replacement'; replacement.write_bytes(b'{}\n')
        os.replace(replacement, source)
        with self.assertRaises(Refused): self.records.close()
        self.assertTrue(self.records.closed)

    def test_missing_record_refuses_and_closes_descriptor(self):
        self.records.save('reservation.json', b'{}\n')
        (self.path/'reservation.json').unlink()
        fd = self.records.fd
        with self.assertRaises((OSError, Refused)): self.records.close()
        with self.assertRaises(OSError): os.fstat(fd)

    def test_linked_record_refuses_without_deleting_it(self):
        self.records.save('reservation.json', b'{}\n')
        linked = Path(self.temp.name)/'linked'
        os.link(self.path/'reservation.json', linked)
        with self.assertRaises(Refused): self.records.close()
        self.assertTrue(linked.exists())

    def test_late_corruption_refuses_controller_success(self):
        fixture = BundleFixture()
        try:
            original = ctrl.ControllerRecords.save
            def save(records, name, raw):
                original(records, name, raw)
                if name == 'assessment.json':
                    (records.path/'reservation.json').write_bytes(b'[]\n')
            with fixture.environment(), \
                 mock.patch.object(ctrl, 'ScopedOwnerTransport', fixture.transport_class()), \
                 mock.patch.object(ctrl.ControllerRecords, 'save', save):
                result = ctrl.ObservationController(fixture.plan_path, fixture.plan_sha).run()
            self.assertEqual(result['status'], 'refused')
            self.assertTrue(result['persistence_failed'])
            self.assertIsNone(result['assessment'])
            self.assertEqual((Path(fixture.plan['receipt_dir'])/'reservation.json').read_bytes(), b'[]\n')
        finally: fixture.close()

    def test_all_five_maximum_records_can_close(self):
        for name in sorted(ctrl.NAMES): self.records.save(name, b'x'*ctrl.RECEIPT_CAP)
        self.records.close()
        self.assertTrue(self.records.closed)
        self.assertEqual(sum(p.stat().st_size for p in self.path.iterdir()), 5*ctrl.RECEIPT_CAP)

    def test_final_content_pass_has_five_bounded_reads(self):
        for name in sorted(ctrl.NAMES): self.records.save(name, b'x'*ctrl.RECEIPT_CAP)
        original = os.pread
        with mock.patch.object(ctrl.os, 'pread', wraps=original) as reads:
            self.records.close()
        self.assertEqual(reads.call_count, 5)
        self.assertEqual([call.args[1:] for call in reads.call_args_list],
                         [(ctrl.RECEIPT_CAP+1, 0)]*5)

    def test_final_read_error_closes_once_and_keeps_file(self):
        self.records.save('reservation.json', b'{}\n')
        fd = self.records.fd
        with mock.patch.object(ctrl.os, 'pread', side_effect=OSError('test read failure')) as read:
            with self.assertRaises(OSError): self.records.close()
        self.assertEqual(read.call_count, 1)
        with self.assertRaises(OSError): os.fstat(fd)
        self.assertTrue(self.records.closed)
        self.assertEqual((self.path/'reservation.json').read_bytes(), b'{}\n')

    def test_partial_write_is_retained_and_close_cannot_bless_it(self):
        original = os.write
        count = 0
        def write(fd, raw):
            nonlocal count
            count += 1
            return original(fd, raw[:1]) if count == 1 else 0
        with mock.patch.object(ctrl.os, 'write', side_effect=write):
            with self.assertRaises(Refused): self.records.save('reservation.json', b'{}\n')
        self.assertEqual((self.path/'reservation.json').read_bytes(), b'{')
        with self.assertRaises(Refused): self.records.close()
        self.assertTrue(self.records.closed)
        self.assertEqual((self.path/'reservation.json').read_bytes(), b'{')

    def test_empty_directory_closes_without_inventing_records(self):
        self.records.close()
        self.assertEqual(list(self.path.iterdir()), [])


if __name__ == '__main__': unittest.main(verbosity=2)
