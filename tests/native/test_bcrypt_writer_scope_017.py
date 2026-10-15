"""Mocked kernel boundaries and real temporary-file readback. No child launch."""
import copy
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import bcrypt_writer_scope_017 as s
import bcrypt_scoped_transport_017 as c
from test_bcrypt_outer_transport_017 import spec


class Kernel:
    def __init__(self, states):
        self.states=list(states); self.reaper=0; self.killed=[]; self.closed=[]; self.reaped=[]
        self.now=0; self.children=[401]; self.signal_error=None; self.policy_error=None
        self.peek_calls=0
    def peek(self):
        self.peek_calls+=1
        value=self.states.pop(0) if len(self.states)>1 else self.states[0]
        if isinstance(value, BaseException): raise value
        return value
    def kill(self,fd,*args):
        self.killed.append(fd)
        if self.signal_error:raise self.signal_error
    def wait(self,pid,flags):
        self.reaped.append(pid);return pid,0
    def policy(self):
        if self.policy_error:raise self.policy_error
    def sleep(self,seconds):self.now+=int(seconds*1e9)
    def patches(self):
        return (mock.patch.object(s,'_policy',side_effect=self.policy),
            mock.patch.object(s,'_prctl',return_value=object()),
            mock.patch.object(s,'_get',side_effect=lambda f:self.reaper),
            mock.patch.object(s,'_set',side_effect=lambda f,v:setattr(self,'reaper',v)),
            mock.patch.object(s,'_peek',side_effect=self.peek),
            mock.patch.object(s,'_children',side_effect=lambda:self.children),
            mock.patch.object(s.signal,'signal'),
            mock.patch.object(s.os,'pidfd_open',side_effect=lambda p,f:p+1000),
            mock.patch.object(s.os,'waitid',return_value=None),
            mock.patch.object(s.os,'waitpid',side_effect=self.wait),
            mock.patch.object(s.signal,'pidfd_send_signal',side_effect=self.kill),
            mock.patch.object(s.os,'close',side_effect=self.closed.append),
            mock.patch.object(s.time,'monotonic_ns',side_effect=lambda:self.now),
            mock.patch.object(s.time,'sleep',side_effect=self.sleep))


class ScopeTests(unittest.TestCase):
    def kernel(self,states):
        k=Kernel(states)
        for p in k.patches():p.start();self.addCleanup(p.stop)
        return k

    def test_no_child_means_echild_not_empty_proc_list(self):
        k=self.kernel([ChildProcessError(), None]);k.children=[]
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertFalse(r['descendant_domain_empty'])
        self.assertFalse(r['policy_restored'])
        self.assertIn(r['reason'],('SCOPE_DEADLINE','SCOPE_SCAN_CAP'))
        self.assertEqual(k.reaper,1)

    def test_clean_scope_rechecks_and_restores_only_own_policy(self):
        k=self.kernel([ChildProcessError()]);scope=s.DescendantScope();scope.activate()
        r=scope.drain();self.assertEqual(r['status'],'descendant_scope_complete_unreviewed')
        self.assertTrue(r['descendant_domain_empty'] and r['policy_restored'])
        self.assertFalse(r['writers_quiescent'] or r['observation_accepted'])
        scope.assert_empty();self.assertEqual(k.reaper,0);self.assertEqual(k.killed,[])
        with self.assertRaises(s.Refused):scope.drain()
        with self.assertRaises(s.Refused):scope.activate()

    def test_preexisting_live_child_rejects_without_signal_or_prctl(self):
        k=self.kernel([None]);scope=s.DescendantScope()
        with self.assertRaisesRegex(s.Refused,'SCOPE_PREEXISTING_CHILD'):scope.activate()
        self.assertEqual(k.reaper,0);self.assertEqual(k.killed,[])

    def test_preexisting_zombie_rejects_without_reaping(self):
        k=self.kernel([SimpleNamespace(si_pid=401)]);scope=s.DescendantScope()
        with self.assertRaises(s.Refused):scope.activate()
        self.assertEqual(k.reaped,[])

    def test_preexisting_subreaper_refuses(self):
        k=self.kernel([ChildProcessError()]);k.reaper=1
        with self.assertRaisesRegex(s.Refused,'SCOPE_PREEXISTING_REAPER'):s.DescendantScope().activate()
        self.assertEqual(k.reaper,1)

    def test_adopted_live_child_signalled_once_then_reaped(self):
        k=self.kernel([ChildProcessError(),None,None,SimpleNamespace(si_pid=401),ChildProcessError()])
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertEqual(k.killed,[1401]);self.assertEqual(k.closed,[1401]);self.assertEqual(k.reaped,[401])
        self.assertTrue(r['descendant_domain_empty'] and r['policy_restored'])
        self.assertEqual(r['reason'],'SCOPE_ORPHANS')
        with self.assertRaises(s.Refused):scope.assert_empty()

    def test_adopted_zombie_never_signalled(self):
        k=self.kernel([ChildProcessError(),SimpleNamespace(si_pid=401),ChildProcessError()])
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertEqual(k.killed,[]);self.assertEqual(k.reaped,[401])
        self.assertTrue(r['unexpected_children']);self.assertEqual(r['reason'],'SCOPE_ORPHANS')

    def test_signal_error_still_reaps_and_preserves_error(self):
        k=self.kernel([ChildProcessError(),None,SimpleNamespace(si_pid=401),ChildProcessError()])
        k.signal_error=PermissionError('no')
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertEqual(r['reason'],'SCOPE_SIGNAL');self.assertTrue(r['cleanup_failed'])
        self.assertTrue(r['descendant_domain_empty']);self.assertEqual(k.killed,[1401]);self.assertEqual(k.reaped,[401])

    def test_lost_child_ownership_never_signals(self):
        k=self.kernel([ChildProcessError(),None]);scope=s.DescendantScope();scope.activate()
        with mock.patch.object(s.os,'waitid',side_effect=ChildProcessError()):r=scope.drain()
        self.assertFalse(r['descendant_domain_empty']);self.assertEqual(k.killed,[])
        self.assertEqual(k.closed,[1401]);self.assertTrue(r['cleanup_failed'])

    def test_pidfd_failure_has_no_numeric_pid_fallback(self):
        k=self.kernel([ChildProcessError(),None]);scope=s.DescendantScope();scope.activate()
        with mock.patch.object(s.os,'pidfd_open',side_effect=OSError()):r=scope.drain()
        self.assertFalse(r['descendant_domain_empty']);self.assertEqual(k.killed,[])

    def test_changed_signal_or_thread_policy_never_claims_empty(self):
        k=self.kernel([ChildProcessError()]);scope=s.DescendantScope();scope.activate()
        k.policy_error=s.Refused('SCOPE_THREADS');r=scope.drain()
        self.assertEqual(r['reason'],'SCOPE_THREADS');self.assertFalse(r['descendant_domain_empty'])

    def test_empty_observation_race_does_not_restore_or_claim_success(self):
        k=self.kernel([ChildProcessError(),ChildProcessError(),None]);scope=s.DescendantScope();scope.activate()
        r=scope.drain();self.assertFalse(r['descendant_domain_empty'] or r['policy_restored'])
        self.assertEqual(r['reason'],'SCOPE_RESTORE')

    def test_reap_count_is_bounded(self):
        k=self.kernel([ChildProcessError()]+[SimpleNamespace(si_pid=401)]*17)
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertEqual(len(k.reaped),16);self.assertEqual(r['reason'],'SCOPE_CHILD_CAP')

    def test_echild_with_unaccounted_handle_is_not_success(self):
        k=self.kernel([ChildProcessError(),None,ChildProcessError()])
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        self.assertFalse(r['descendant_domain_empty'])
        self.assertEqual(r['reason'],'SCOPE_LOST_WAIT_OWNERSHIP')
        self.assertEqual(k.closed,[1401])

    def test_wait_failure_is_not_reap(self):
        k=self.kernel([ChildProcessError(),SimpleNamespace(si_pid=401)])
        scope=s.DescendantScope();scope.activate()
        with mock.patch.object(s.os,'waitpid',return_value=(0,0)):r=scope.drain()
        self.assertEqual(r['children_reaped'],0);self.assertFalse(r['descendant_domain_empty'])

    def test_proc_parser_bounds_duplicates_and_own_pid(self):
        for raw in (b'a',b'401 401',str(os.getpid()).encode(),b'0',b'1',b'2 '*17,b'1'*257):
            with mock.patch('builtins.open',mock.mock_open(read_data=raw)),self.assertRaises(s.Refused):s._children()
        with mock.patch('builtins.open',mock.mock_open(read_data=b'401 402 ')):
            self.assertEqual(s._children(),[401,402])

    def test_result_mutation_does_not_enable_reader(self):
        k=self.kernel([ChildProcessError(),None]);k.children=[]
        scope=s.DescendantScope();scope.activate();r=scope.drain()
        r.update(status='descendant_scope_complete_unreviewed',descendant_domain_empty=True)
        with self.assertRaises(s.Refused):scope.assert_empty()


class ReadbackTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.data=self.root/'data';self.data.mkdir()
    def read(self,name='a',cap=3,exact=True):return c._read_directory(str(self.data),[name],{name:cap},exact)

    def test_real_exact_bytes_and_zero_length(self):
        (self.data/'a').write_bytes(b'abc');self.assertEqual(self.read(),{'a':b'abc'})
        (self.data/'a').write_bytes(b'');self.assertEqual(self.read(cap=0),{'a':b''})

    def test_missing_extra_and_oversize_refused(self):
        with self.assertRaises(s.Refused):self.read()
        (self.data/'a').write_bytes(b'abcd')
        with self.assertRaises(s.Refused):self.read()
        (self.data/'a').write_bytes(b'abc');(self.data/'extra').write_bytes(b'')
        with self.assertRaises(s.Refused):self.read()

    def test_symlink_hardlink_directory_and_fifo_refused(self):
        target=self.root/'target';target.write_bytes(b'abc');a=self.data/'a'
        a.symlink_to(target)
        with self.assertRaises(OSError):self.read()
        a.unlink();os.link(target,a)
        with self.assertRaises(s.Refused):self.read()
        a.unlink();a.mkdir()
        with self.assertRaises(s.Refused):self.read()
        a.rmdir();os.mkfifo(a)
        with self.assertRaises(s.Refused):self.read()

    def test_read_change_after_first_read_refused(self):
        a=self.data/'a';a.write_bytes(b'abc');original=c.os.read
        def changed(fd,n):
            data=original(fd,n)
            if data:a.write_bytes(b'xyz')
            return data
        with mock.patch.object(c.os,'read',side_effect=changed),self.assertRaises(s.Refused):self.read()

    def test_scope_gate_before_any_artifact_open(self):
        raw=c.OwnerTransport.__module__
        v=spec(self.root);v['wall_ms']=14000
        import bcrypt_outer_transport_017 as t
        raw=t.encode(v);wrapper=c.ScopedOwnerTransport(raw,t.fingerprint(raw)['sha256'])
        with mock.patch.object(c,'_read_directory') as read,self.assertRaises(s.Refused):
            wrapper.read_artifacts(str(self.data))
        read.assert_not_called()

    def test_smoke_final_uses_existing_canonical_wire_format(self):
        import check_bcrypt_writer_scope_smoke_017 as smoke
        import bcrypt_outer_transport_017 as t
        raw=smoke.FINAL.encode('ascii')
        value=t.decode(raw,t.FINAL_CAP)
        self.assertEqual(value['status'],'owner_complete_unreviewed')
        self.assertEqual(raw,t.encode(value))

    def test_all_five_smoke_programs_compile_without_running(self):
        import ast
        import check_bcrypt_writer_scope_smoke_017 as smoke
        total=0
        for case in ('clean','joined','orphan','double','timeout'):
            source=smoke.program(case,self.root/(case+'-unexpected.bin'))
            tree=ast.parse(source);compile(tree,case,'exec')
            for n in ast.walk(tree):
                if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute):
                    if n.func.attr=='fork':total+=1
                    if n.func.attr=='sleep':self.assertEqual(ast.literal_eval(n.args[0]),3)
        self.assertEqual(total,5)

    def test_scoped_deadline_reserves_cleanup_margin(self):
        import bcrypt_outer_transport_017 as t
        v=spec(self.root);raw=t.encode(v)
        with self.assertRaisesRegex(s.Refused,'SCOPE_WALL_BUDGET'):
            c.ScopedOwnerTransport(raw,t.fingerprint(raw)['sha256'])


if __name__=='__main__':unittest.main(verbosity=2)
