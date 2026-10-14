"""Native-bridge protocol tests: fake Popen/files/limits, zero native processes."""
import copy
import json
from types import SimpleNamespace
import unittest
from unittest import mock

import bcrypt_owner_backend_017 as b
import bcrypt_guarded_owner_017 as o
from test_bcrypt_guarded_owner_017 import TARGET, SHA, listing
from test_bcrypt_import_boundary_017 import fixture, module


def encode(value, cap):
    # Finite test values, not the production bounded encoder implementation.
    raw=json.dumps(value, sort_keys=True, separators=(',',':')).encode()+b'\n'
    if len(raw)>cap: raise ValueError('synthetic encode cap')
    return raw


def parse(raw, target, spec):
    return [dict(row, file_offset=row['rva'], preferred_vma=b.IMAGE_BASE+row['rva'])
            for row in listing(target)]


class Directory:
    def __init__(self,path):
        self.path,self.files,self.events=path,{},[]
        self.closed=False
    def create(self,name,data,cap,mode=0o600):
        self.events.append(('create',name,cap,mode))
        if name in self.files or len(data)>cap: raise OSError('exclusive')
        self.files[name]=data
    def read(self,name,cap):
        self.events.append(('read',name,cap))
        data=self.files[name]
        if len(data)>cap: raise OSError('cap')
        return data
    def open_capture(self,name):
        self.create(name,b'',b.CAPTURE_CAP)
        return 50 if name=='tool-stdout.bin' else 51
    def durable_readback(self,expected):
        self.events.append(('readback',))
        if expected!=self.files: raise OSError('readback')
    def close(self):
        if self.closed: raise OSError('double close')
        self.events.append(('close',));self.closed=True


class Process:
    pid=12345
    def __init__(self): self.returncode=None;self.waits=[];self.timeout=False
    def wait(self,timeout):
        self.waits.append(timeout)
        if self.timeout: raise b.subprocess.TimeoutExpired('synthetic command',timeout)
        self.returncode=0
        return 0
    def poll(self): return self.returncode


def bridge(directory=None):
    directory=directory or Directory('/synthetic/new-output')
    value=b.PreparedNativeBackend('/synthetic/new-output','/synthetic/objdump',SHA,'1'*64,
                                  output_factory=lambda _:directory, encode_json=encode,
                                  parse_listing=parse)
    return value,directory


def platform():
    # No actual resource/system calls; give the import-only bridge a POSIX view.
    return mock.patch.object(b.os,'name','posix')


class NativeBridgeTests(unittest.TestCase):
    def test_constructor_has_no_side_effect(self):
        backend,d=bridge()
        self.assertEqual(d.events,[])
        self.assertIsNone(backend.directory)

    def test_bad_paths_and_hashes(self):
        for bad in ('relative','/a/../b','//other/tool','/a/','/x\n',True):
            with self.assertRaises(o.Refused): b.absolute(bad)
        for bad in ('',None,True,'f'*63,'G'*64):
            with self.assertRaises(o.Refused): b.sha256_text(bad)

    def test_reservation_exclusive_and_non_authorizing(self):
        backend,d=bridge();backend.reserve()
        marker=json.loads(d.files['consumed.json'])
        self.assertIs(marker['execution_admitted'],False)
        self.assertIs(marker['retry_allowed'],False)
        with self.assertRaises(o.Refused): backend.reserve()

    def test_target_readonly_and_no_other_range(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        self.assertIn(('create','target.bin',103,0o400),d.events)
        with self.assertRaises(o.Refused): backend.write('target.bin',TARGET[:-1])
        with self.assertRaises(o.Refused): backend.write('anything',b'')

    def test_exact_native_invocation_no_shell_path_search_or_real_exec(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        process=Process()
        with platform(), mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_DFL), \
             mock.patch.object(b.subprocess,'Popen',return_value=process) as popen:
            child=backend.launch(TARGET)
        args,kwargs=popen.call_args
        expected=['/synthetic/objdump','-D','-z','-b','binary','-m','i386:x86-64','-M',
            'intel','--insn-width=16','--wide','--show-raw-insn','--visualize-jumps=off',
            '--disassembler-color=off','--adjust-vma=0x18001117c',
            '--start-address=0x18001117c','--stop-address=0x1800111e3','target.bin']
        self.assertEqual(args,(expected,))
        self.assertIs(kwargs['shell'],False)
        self.assertIs(kwargs['close_fds'],True)
        self.assertIs(kwargs['start_new_session'],True)
        self.assertIs(kwargs['preexec_fn'],b.child_limits)
        self.assertEqual(kwargs['env'],b.ENV)
        self.assertEqual(kwargs['cwd'],'/synthetic/new-output')
        self.assertEqual((kwargs['stdout'],kwargs['stderr']),(50,51))
        self.assertIs(child.process,process)
        with self.assertRaises(o.Refused):backend.launch(TARGET)

    def test_launch_missing_target_never_calls_popen(self):
        backend,d=bridge();backend.reserve()
        with mock.patch.object(b.subprocess,'Popen') as popen, self.assertRaises(KeyError):
            backend.launch(TARGET)
        popen.assert_not_called()

    def test_changed_prepared_file_suppresses_popen(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        d.files['target.bin']=b'x'*103
        with mock.patch.object(b.subprocess,'Popen') as popen, self.assertRaises(o.Refused):
            backend.launch(TARGET)
        popen.assert_not_called()

    def test_child_signal_policy_failure_suppresses_popen(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        with mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_IGN), \
             mock.patch.object(b.subprocess,'Popen') as popen, self.assertRaises(o.Refused):
            backend.launch(TARGET)
        popen.assert_not_called()

    def test_popen_failure_not_retried_or_reconstructed(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        with mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_DFL), \
             mock.patch.object(b.subprocess,'Popen',side_effect=OSError('synthetic')) as popen:
            with self.assertRaises(OSError):backend.launch(TARGET)
            with self.assertRaises(o.Refused):backend.launch(TARGET)
        self.assertEqual(popen.call_count,1)
        self.assertIn('tool-stdout.bin',d.files)

    def test_retained_capture_not_read_until_reap(self):
        d=Directory('/synthetic');p=Process();child=b.RetainedChild(p,d)
        with self.assertRaises(o.Refused):child.capture(b.CAPTURE_CAP)
        self.assertEqual(d.events,[])
        child.wait(timeout=1);d.files.update({'tool-stdout.bin':b'x','tool-stderr.bin':b''})
        self.assertEqual(child.capture(b.CAPTURE_CAP),(b'x',b''))
        child.close()
        with self.assertRaises(o.Refused):child.capture(b.CAPTURE_CAP)
        self.assertFalse(d.closed)  # The sink, not child, owns those descriptors.

    def test_timeout_translation_not_false_reap(self):
        p=Process();p.timeout=True;child=b.RetainedChild(p,Directory('/synthetic'))
        with self.assertRaisesRegex(o.Refused,'DEADLINE'):child.wait(timeout=0.2)
        self.assertFalse(child.reaped)
        self.assertEqual(p.waits,[0.2])

    def test_reaped_process_never_signalled(self):
        p=Process();p.returncode=0;child=b.RetainedChild(p,Directory('/synthetic'))
        with mock.patch.object(b.os,'killpg',create=True) as kill:
            child.kill()
        kill.assert_not_called()
        self.assertTrue(child.reaped)
        with self.assertRaises(o.Refused):child.kill()

    def test_live_group_signalled_once_and_not_claimed_reaped(self):
        child=b.RetainedChild(Process(),Directory('/synthetic'))
        with mock.patch.object(b.os,'killpg',create=True) as kill:
            child.kill()
            with self.assertRaises(o.Refused):child.kill()
        kill.assert_called_once_with(12345,b.signal.SIGKILL)
        self.assertFalse(child.reaped)

    def test_raw_evidence_is_checked_not_rewritten(self):
        backend,d=bridge();backend.reserve();backend.captures_open=True
        d.files['tool-stdout.bin']=b'raw'
        backend.write('stdout.bin',b'raw')
        before=dict(d.files)
        with self.assertRaises(o.Refused):backend.write('stdout.bin',b'changed')
        self.assertEqual(d.files,before)
        self.assertFalse(any(e[0]=='create' and e[1]=='tool-stdout.bin' for e in d.events))

    def test_candidate_unknown_capture_skips_false_durability(self):
        backend,d=bridge();backend.reserve();backend.captures_open=True
        d.events.clear();backend.candidate({'capture_stable':False,'status':'refused'})
        self.assertNotIn(('readback',),d.events)
        self.assertIn('report.json',d.files)

    def test_candidate_stable_files_must_match_all_retained_members(self):
        backend,d=bridge();backend.reserve();d.files['unexpected']=b'x'
        with self.assertRaises(OSError):backend.candidate({'capture_stable':True})
        self.assertIn('report.json',d.files)  # No overwrite/retry of this candidate.

    def test_listing_old_projection_requires_exact_offset_types(self):
        backend,d=bridge()
        self.assertEqual(backend.parse(b'synthetic',TARGET),listing())
        for key,value in (('file_offset',False),('file_offset',1),('preferred_vma',False),
                          ('preferred_vma',1),('rva',True)):
            rows=parse(b'',TARGET,{})
            rows[0][key]=value;backend.parser=lambda *_:rows
            with self.assertRaises(o.Refused):backend.parse(b'',TARGET)

    def test_child_limits_are_called_only_inside_explicit_callback(self):
        fake=SimpleNamespace(RLIMIT_FSIZE=1,RLIMIT_AS=2,RLIMIT_CPU=3,RLIMIT_CORE=4,RLIMIT_NOFILE=5)
        saved={}
        fake.setrlimit=lambda kind,pair:saved.update({kind:pair})
        fake.getrlimit=lambda kind:saved[kind]
        with mock.patch.object(b,'resource',fake), mock.patch.object(b.signal,'signal') as signals:
            b.child_limits()
        self.assertEqual(saved,{1:(131072,131072),2:(268435456,268435456),
                                3:(2,2),4:(0,0),5:(64,64)})
        self.assertGreaterEqual(signals.call_count,1)

    def test_child_limits_mismatch_refuses(self):
        fake=SimpleNamespace(RLIMIT_FSIZE=1,RLIMIT_AS=2,RLIMIT_CPU=3,RLIMIT_CORE=4,RLIMIT_NOFILE=5,
             setrlimit=lambda *_:None,getrlimit=lambda *_:(999,999))
        with mock.patch.object(b,'resource',fake),self.assertRaises(o.Refused):b.child_limits()

    def test_complete_composition_uses_actual_core_and_bridge_with_fake_os(self):
        backend,d=bridge();runtime,expected=fixture()
        owner=o.GuardedPreparedOwner(expected,runtime=runtime,clock=lambda:0)
        process=Process()
        def popen(*args,**kwargs):
            d.files['tool-stdout.bin']=b'synthetic listing\n'
            return process
        with mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_DFL), \
             mock.patch.object(b.subprocess,'Popen',side_effect=popen) as launch:
            result=owner.run(TARGET,TARGET,SHA,sink=backend,launch=backend.launch,
                             parse=backend.parse,verify=lambda phase:None)
        self.assertEqual(result['status'],'owner_complete_unreviewed')
        self.assertEqual(launch.call_count,1)
        self.assertTrue(d.closed)
        candidate=json.loads(d.files['report.json'])
        self.assertEqual(candidate['status'],'body_complete_unreviewed')
        self.assertFalse(candidate['observation_accepted'])
        self.assertTrue(candidate['completion_requires_outer_receipt'])
        self.assertEqual(set(d.files),{'consumed.json','target.bin','tool-stdout.bin',
                                       'tool-stderr.bin','report.json'})

    def test_composed_import_failure_cleans_mock_os_and_preserves_refusal(self):
        backend,d=bridge();runtime,expected=fixture()
        owner=o.GuardedPreparedOwner(expected,runtime=runtime,clock=lambda:0)
        def popen(*args,**kwargs):
            runtime.modules['late']=module('late')
            return Process()
        with mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_DFL), \
             mock.patch.object(b.subprocess,'Popen',side_effect=popen), \
             mock.patch.object(b.os,'killpg',create=True) as kill:
            result=owner.run(TARGET,TARGET,SHA,sink=backend,launch=backend.launch,
                             parse=backend.parse,verify=lambda phase:None)
        self.assertEqual(result['first_failure'],'IMPORT_BOUNDARY')
        self.assertTrue(result['child_reaped'])
        self.assertEqual(kill.call_count,1)
        self.assertEqual(json.loads(d.files['report.json'])['status'],'refused')

    def test_close_once_even_if_factory_never_opened(self):
        backend,d=bridge();backend.close()
        self.assertFalse(d.closed)
        with self.assertRaises(o.Refused):backend.close()
        with self.assertRaises(o.Refused):backend.reserve()
        self.assertEqual(d.events, [])

    def test_wait_and_poll_after_child_close_are_no_operation(self):
        process=Process();child=b.RetainedChild(process,Directory('/synthetic'))
        child.close()
        with self.assertRaises(o.Refused):child.wait(timeout=1)
        with self.assertRaises(o.Refused):child.poll()
        self.assertEqual(process.waits,[])

    def test_durability_error_before_process_creation_is_not_retried(self):
        backend,d=bridge();backend.reserve();backend.write('target.bin',TARGET)
        d.files['foreign']=b'retained'
        with mock.patch.object(b.signal,'getsignal',return_value=b.signal.SIG_DFL), \
             mock.patch.object(b.subprocess,'Popen') as popen:
            with self.assertRaises(OSError):backend.launch(TARGET)
            with self.assertRaises(o.Refused):backend.launch(TARGET)
        popen.assert_not_called()
        self.assertEqual(d.files['foreign'],b'retained')

    def test_backend_close_error_remains_spent(self):
        backend,d=bridge();backend.reserve()
        with mock.patch.object(d,'close',side_effect=OSError('close')) as close:
            with self.assertRaises(OSError):backend.close()
            with self.assertRaises(o.Refused):backend.close()
        self.assertEqual(close.call_count,1)


if __name__=='__main__':unittest.main(verbosity=2)
