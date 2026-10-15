"""File-system/protocol tests only. No native child is launched by this suite."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import bcrypt_outer_transport_017 as t


def spec(root):
    return dict(schema='bcrypt-owner-launch-017-v1',request_sha256='a'*64,
        argv=['/synthetic/python','-I','-S','-c','pass'],cwd=str(root),
        output_dir=str(root/'new-output'),environment=dict(t.ENV),wall_ms=16000)


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def owner(self):
        raw=t.encode(spec(self.root));return t.OwnerTransport(raw,t.fingerprint(raw)['sha256'])

    def test_untrusted_digest_refused_before_io(self):
        raw=t.encode(spec(self.root))
        with mock.patch.object(t,'RetainedTransport') as store,self.assertRaises(t.Refused):
            t.OwnerTransport(raw,'f'*64)
        store.assert_not_called()

    def test_bad_argv_budget_environment_or_deadline_refused(self):
        for key,value in [('argv',[]),('argv',['relative']),('argv',['/python']*25),
                          ('argv',['/python','a\0b']),('argv',['/python','x'*4097]),
                          ('environment',{}),('wall_ms',True),('wall_ms',0),('wall_ms',16001)]:
            v=spec(self.root);v[key]=value;raw=t.encode(v)
            with self.subTest(key=key),self.assertRaises(t.Refused):t.validate_spec(raw,t.fingerprint(raw)['sha256'])

    def test_zero_file_and_exact_caps_real_disk(self):
        out=t.RetainedTransport(str(self.root/'out'))
        for name,cap in t.CAPS.items():
            raw=b'x'*cap;out.create(name,raw)
            self.assertEqual((out.path/name).read_bytes(),raw)
        out.close()

    def test_oversize_rejected_without_file_creation(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        for name,cap in t.CAPS.items():
            with self.assertRaises(t.Refused):out.create(name,b'x'*(cap+1))
            self.assertFalse((out.path/name).exists())

    def test_no_overwrite_or_named_output_reuse(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        out.create('owner-final.json',b'first')
        with self.assertRaises(FileExistsError):out.create('owner-final.json',b'replacement')
        with self.assertRaises(FileExistsError):t.RetainedTransport(str(out.path))
        self.assertEqual((out.path/'owner-final.json').read_bytes(),b'first')

    def test_symlink_or_unknown_file_name_refused(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        target=self.root/'target';target.write_bytes(b'keep')
        (out.path/'owner-final.json').symlink_to(target)
        with self.assertRaises(OSError):out.create('owner-final.json',b'new')
        with self.assertRaises(t.Refused):out.create('../target',b'new')
        self.assertEqual(target.read_bytes(),b'keep')

    def test_directory_replaced_between_operations_refused(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        out.path.rename(self.root/'moved');out.path.mkdir()
        with self.assertRaises(t.Refused):out.create('launch.json',b'{}')

    def test_close_reuse_is_refused(self):
        out=t.RetainedTransport(str(self.root/'out'));out.close()
        with self.assertRaises(t.Refused):out.create('owner-final.json',b'')
        with self.assertRaises(t.Refused):out.close()

    def test_short_write_completed_within_cap(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        real=t.os.write
        with mock.patch.object(t.os,'write',side_effect=lambda fd,raw:real(fd,raw[:1])):
            out.create('owner-final.json',b'abc')
        self.assertEqual((out.path/'owner-final.json').read_bytes(),b'abc')

    def test_zero_write_failure_preserves_prefix(self):
        out=t.RetainedTransport(str(self.root/'out'));self.addCleanup(out.close)
        with mock.patch.object(t.os,'write',return_value=0),self.assertRaises(t.Refused):
            out.create('owner-final.json',b'abc')
        self.assertTrue((out.path/'owner-final.json').exists())
        with self.assertRaises(FileExistsError):out.create('owner-final.json',b'abc')

    def test_process_creation_failure_is_spent_and_unknown(self):
        transport=self.owner()
        with mock.patch.object(t.subprocess,'Popen',side_effect=OSError('private')) as launch:
            r=transport.run()
            with self.assertRaises(t.Refused):transport.run()
        launch.assert_called_once();self.assertTrue(r['launch_outcome_unknown'])
        self.assertFalse(r['owner_reaped'] or r['writers_quiescent'])
        self.assertNotIn('private',repr(r))
        self.assertTrue(r['output_closed'])

    def test_initial_save_failure_never_launches(self):
        with mock.patch.object(t.RetainedTransport,'create',side_effect=OSError('full')), \
             mock.patch.object(t.subprocess,'Popen') as launch:
            r=self.owner().run()
        launch.assert_not_called();self.assertTrue(r['persistence_failed'])
        self.assertFalse(r['launch_reserved'])

    def test_close_failure_cannot_be_reported_complete(self):
        close=t.RetainedTransport.close
        def error(out):close(out);raise OSError('close')
        with mock.patch.object(t.subprocess,'Popen',side_effect=OSError('launch')), \
             mock.patch.object(t.RetainedTransport,'close',error):
            r=self.owner().run()
        self.assertTrue(r['persistence_failed']);self.assertFalse(r['output_closed'])
        self.assertEqual(r['reason'],'TRANSPORT_IO')

    def test_poll_known_exit_never_kills_reused_pid(self):
        p=mock.Mock();p.poll.return_value=0;r=dict(owner_reaped=False)
        t._stop_root(p,r);p.kill.assert_not_called();p.wait.assert_not_called()
        self.assertTrue(r['owner_reaped']);self.assertEqual(r['owner_exit_code'],0)

    def test_live_root_one_kill_one_cleanup_wait(self):
        p=mock.Mock();p.poll.return_value=None;p.wait.return_value=-9;r=dict(owner_reaped=False)
        t._stop_root(p,r);p.kill.assert_called_once();p.wait.assert_called_once_with(timeout=1)
        self.assertTrue(r['owner_reaped'])
        t._stop_root(p,r);self.assertEqual(p.kill.call_count,1)

    def test_kill_failure_still_waits_without_retry(self):
        p=mock.Mock();p.poll.return_value=None;p.kill.side_effect=OSError('signal')
        p.wait.return_value=0;r=dict(owner_reaped=False)
        t._stop_root(p,r)
        p.kill.assert_called_once();p.wait.assert_called_once_with(timeout=1)
        self.assertTrue(r['cleanup_failed']);self.assertTrue(r['owner_reaped'])

    def test_unknown_poll_does_not_signal_guessed_pid(self):
        p=mock.Mock();p.poll.side_effect=OSError('unknown');r=dict(owner_reaped=False)
        t._stop_root(p,r);p.kill.assert_not_called();self.assertTrue(r['cleanup_failed'])

    def test_cleanup_timeout_does_not_claim_reaped(self):
        p=mock.Mock();p.poll.return_value=None;p.wait.side_effect=t.subprocess.TimeoutExpired('mock',1)
        r=dict(owner_reaped=False);t._stop_root(p,r)
        self.assertFalse(r['owner_reaped']);self.assertTrue(r['cleanup_failed'])

    def test_interruption_retained_without_new_attempt(self):
        with mock.patch.object(t.subprocess,'Popen',side_effect=KeyboardInterrupt()) as launch:
            r=self.owner().run()
        self.assertTrue(r['interrupted']);self.assertEqual(r['reason'],'INTERRUPTED')
        self.assertTrue(r['launch_outcome_unknown']);launch.assert_called_once()


if __name__=='__main__':unittest.main(verbosity=2)
