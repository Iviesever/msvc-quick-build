"""Finite synthetic lifecycle cases; no subprocess, native loader or target read."""
import copy
import hashlib
import math
import unittest

import bcrypt_guarded_owner_017 as o
from test_bcrypt_import_boundary_017 import fixture, module

TARGET = bytes(range(103))  # Synthetic bytes only, not the retained bcrypt image.
SHA = hashlib.sha256(TARGET).hexdigest()


def listing(target=TARGET):
    return [dict(rva=o.RVA_START+i, length=1, raw_hex=bytes([b]).hex(),
                 assembly='synthetic_not_a_decode') for i, b in enumerate(target)]


class Rig:
    def __init__(self):
        self.runtime, self.expected = fixture()
        self.events, self.files, self.snapshots = [], {}, []
        self.time, self.returncode = 0.0, None
        self.stdout, self.stderr = b'synthetic listing\n', b''
        self.hooks = {}
        self.owner = o.GuardedPreparedOwner(self.expected, runtime=self.runtime,
                                            clock=lambda: self.time)

    def event(self, name):
        self.events.append(name)
        fn = self.hooks.get(name)
        if fn:
            return fn()
        return None

    def reserve(self): self.event('reserve')
    def write(self, name, raw):
        self.event('write:'+name)
        if name in self.files: raise OSError('synthetic exclusive write')
        self.files[name] = raw

    def candidate(self, report):
        self.event('candidate')
        self.snapshots.append(copy.deepcopy(report))

    def sink_close(self): self.event('sink_close')
    def launch(self, target):
        self.event('launch')
        assert target == TARGET
        return self

    def wait(self, timeout):
        self.events.append(('timeout', timeout))
        code = self.event('wait')
        self.returncode = 0 if code is None else code
        return self.returncode

    def poll(self):
        self.event('poll')
        return self.returncode

    def kill(self): self.event('kill')
    def capture(self, cap):
        self.event('capture')
        assert cap == o.CAPTURE_CAP
        return self.stdout, self.stderr

    def close(self): self.event('child_close')
    def verify(self, phase): return self.event('verify:'+phase)
    def parse(self, raw, target):
        value = self.event('parse')
        return listing(target) if value is None else value

    def run(self, **changes):
        class Sink:
            reserve = lambda s: self.reserve()
            write = lambda s, n, b: self.write(n, b)
            candidate = lambda s, r: self.candidate(r)
            close = lambda s: self.sink_close()
        values = dict(primary=TARGET, independent=TARGET, expected_sha=SHA,
                      sink=Sink(), launch=self.launch, parse=self.parse, verify=self.verify)
        values.update(changes)
        return self.owner.run(**values)


def raising(error):
    def callback(): raise error
    return callback


class LifecycleTests(unittest.TestCase):
    def test_complete_body_cannot_assert_actual_acceptance(self):
        r = Rig(); result = r.run()
        self.assertEqual(result['status'], 'owner_complete_unreviewed')
        self.assertEqual(result['launch_reservations'], 1)
        self.assertTrue(result['child_reaped'] and result['child_closed'] and result['output_closed'])
        self.assertEqual(result['boundary']['status'], 'closed_unchanged')
        self.assertEqual(result['instructions'], listing())
        self.assertTrue(result['completion_requires_outer_receipt'])
        for name in ('execution_admitted', 'source_independently_reviewed', 'runtime_loader_verified',
                     'python_module_bytes_verified', 'original_lineage_verified',
                     'descendant_cleanup_verified', 'observation_accepted', 'clears_hold'):
            self.assertIs(result[name], False)
        self.assertIsNone(result['cause'])
        self.assertNotIn('kill', r.events)
        self.assertEqual(r.events.count('launch'), 1)
        self.assertEqual(r.events.count('capture'), 1)
        self.assertLess(r.events.index('verify:before'), r.events.index('launch'))
        self.assertLess(r.events.index('wait'), r.events.index('capture'))
        self.assertLess(r.events.index('verify:after'), r.events.index('candidate'))
        self.assertFalse(r.snapshots[0]['output_closed'])
        self.assertIsNone(r.snapshots[0]['boundary'])
        self.assertNotEqual(r.snapshots[0]['status'], result['status'])

    def test_second_run_is_no_operation_and_not_new_budget(self):
        r=Rig(); r.run(); before=list(r.events)
        with self.assertRaisesRegex(o.Refused, 'REUSED'): r.run()
        self.assertEqual(r.events, before)

    def test_bad_activation_does_not_touch_sink_or_launch(self):
        r=Rig(); r.runtime.modules['late']=module('late')
        result=r.run()
        self.assertEqual(result['status'], 'refused')
        self.assertEqual(r.events, [])
        self.assertFalse(result['reservation_attempted'])

    def test_bad_preparations_cannot_reach_launch(self):
        for changes in (dict(primary=b''), dict(primary=bytearray(TARGET)),
                        dict(independent=TARGET[:-1]), dict(independent=b'x'*103),
                        dict(expected_sha=None), dict(expected_sha=True),
                        dict(expected_sha='f'*64), dict(expected_sha='A'*64)):
            with self.subTest(changes=list(changes)):
                r=Rig(); result=r.run(**changes)
                self.assertEqual(result['first_failure'], 'PREPARATION')
                self.assertNotIn('launch', r.events)
                self.assertTrue(result['candidate_written'] and result['output_closed'])

    def test_verify_false_is_not_silent_success(self):
        for value in (False, True, {}, []):
            r=Rig(); r.hooks['verify:before']=lambda: value; result=r.run()
            self.assertEqual(result['first_failure'], 'PREPARATION')
            self.assertNotIn('launch', r.events)

    def test_fixed_target_geometry_and_owned_listing(self):
        rows=listing(); result=o.checked_listing(rows,TARGET)
        rows[0]['assembly']='changed'
        self.assertNotEqual(result[0]['assembly'], rows[0]['assembly'])
        self.assertEqual(o.RVA_END-o.RVA_START,103)

    def test_listing_corruptions_refused(self):
        for alteration in ('count','gap','overlap','bool','bytes','key','long','control','empty','end'):
            with self.subTest(alteration=alteration):
                rows=listing()
                if alteration=='count': rows*=2
                elif alteration=='gap': rows[1]['rva']+=1
                elif alteration=='overlap': rows[1]['rva']-=1
                elif alteration=='bool': rows[0]['length']=True
                elif alteration=='bytes': rows[0]['raw_hex']='ff'
                elif alteration=='key': rows[0]['extra']='x'
                elif alteration=='long': rows[0]['assembly']='x'*385
                elif alteration=='control': rows[0]['assembly']='bad\n'
                elif alteration=='empty': rows=[]
                else: rows.pop()
                r=Rig(); r.hooks['parse']=lambda: rows; result=r.run()
                self.assertEqual(result['first_failure'],'LISTING')
                self.assertIsNone(result['instructions'])
                self.assertEqual(r.files['stdout.bin'],r.stdout)

    def test_nonzero_exit_preserves_capture_and_refuses_parse(self):
        r=Rig(); r.hooks['wait']=lambda: 37; result=r.run()
        self.assertEqual(result['first_failure'], 'CHILD_EXIT')
        self.assertEqual(result['child_returncode'],37)
        self.assertEqual(r.files['stdout.bin'],r.stdout)
        self.assertNotIn('parse',r.events)
        self.assertNotIn('kill',r.events)

    def test_stderr_is_retained_not_success(self):
        r=Rig(); r.stderr=b'synthetic warning'; result=r.run()
        self.assertEqual(result['first_failure'],'CHILD_STDERR')
        self.assertEqual(r.files['stderr.bin'],r.stderr)

    def test_capture_type_and_both_boundaries(self):
        for stream, value in (('stdout',b'x'*o.CAPTURE_CAP),('stderr',b'x'*o.CAPTURE_CAP),
                              ('stdout','text'),('stderr',False)):
            r=Rig(); setattr(r,stream,value); result=r.run()
            self.assertEqual(result['first_failure'],'CAPTURE')
            self.assertEqual(r.events.count('capture'),1)
            self.assertFalse(result['capture_stable'])
        r=Rig(); r.stdout=b'x'*(o.CAPTURE_CAP-1)
        self.assertEqual(r.run()['status'],'owner_complete_unreviewed')

    def test_launch_error_not_retried(self):
        r=Rig(); r.hooks['launch']=raising(OSError('sensitive path not logged')); result=r.run()
        self.assertEqual(result['first_failure'],'INTERNAL')
        self.assertEqual(result['launch_reservations'],1)
        self.assertFalse(result['child_obtained'])
        self.assertTrue(result['launch_outcome_unknown'])
        self.assertNotIn('sensitive',repr(result))
        self.assertEqual(r.events.count('launch'),1)
        self.assertNotIn('poll',r.events)

    def test_import_refusal_immediately_after_launch_still_reaps(self):
        r=Rig(); r.hooks['launch']=lambda:r.runtime.modules.update(late=module('late'))
        result=r.run()
        self.assertEqual(result['first_failure'],'IMPORT_BOUNDARY')
        self.assertTrue(result['kill_attempted'] and result['child_reaped'])
        self.assertEqual(r.events.count('kill'),1)
        self.assertEqual(r.events.count('wait'),1)
        self.assertEqual(r.events.count('capture'),1)
        self.assertEqual(r.events.count('child_close'),1)

    def test_wait_timeout_does_not_reset_body_deadline_or_launch(self):
        r=Rig(); sequence=iter([o.Refused('DEADLINE'),-9])
        def wait():
            value=next(sequence)
            if isinstance(value,Exception): raise value
            return value
        r.hooks['wait']=wait; result=r.run()
        self.assertEqual(result['first_failure'],'DEADLINE')
        self.assertEqual(result['child_returncode'],-9)
        self.assertTrue(result['child_reaped'] and result['capture_stable'])
        self.assertEqual(r.events.count('launch'),1)
        self.assertEqual(r.events.count('kill'),1)
        self.assertEqual(r.events.count('wait'),2)  # body + sole cleanup wait.
        self.assertIn(('timeout',o.CLEANUP_SECONDS),r.events)

    def test_unreaped_child_never_gets_capture_digest(self):
        r=Rig(); r.hooks['wait']=raising(o.Refused('DEADLINE')); result=r.run()
        self.assertEqual(result['first_failure'],'DEADLINE')
        self.assertTrue(result['cleanup_failure'])
        self.assertFalse(result['child_reaped'] or result['capture_stable'])
        self.assertIsNone(result['raw_stdout'])
        self.assertNotIn('capture',r.events)
        self.assertEqual(r.events.count('child_close'),1)

    def test_already_reaped_child_not_signalled_after_interrupt(self):
        r=Rig()
        def wait():
            r.returncode=0
            raise KeyboardInterrupt()
        r.hooks['wait']=wait; result=r.run()
        self.assertEqual(result['first_failure'],'INTERRUPTED')
        self.assertTrue(result['child_reaped'])
        self.assertNotIn('kill',r.events)

    def test_kill_error_preserves_first_failure_and_still_waits(self):
        r=Rig(); r.hooks['launch']=lambda:r.runtime.modules.update(late=module('late'))
        r.hooks['kill']=raising(OSError('cannot kill')); result=r.run()
        self.assertTrue(result['cleanup_failure'])
        self.assertEqual(result['first_failure'],'IMPORT_BOUNDARY')
        self.assertTrue(result['child_reaped'])
        self.assertEqual(r.events.count('kill'),1)

    def test_poll_error_never_signals_unknown_ownership(self):
        r=Rig(); r.hooks['wait']=raising(o.Refused('DEADLINE'))
        r.hooks['poll']=raising(OSError('unknown')); result=r.run()
        self.assertTrue(result['cleanup_failure'])
        self.assertFalse(result['child_reaped'])
        self.assertNotIn('kill',r.events)

    def test_body_exception_not_overwritten_by_normal_guard_close(self):
        r=Rig(); r.hooks['parse']=raising(ValueError('private state')); result=r.run()
        self.assertEqual(result['boundary']['status'],'closed_unchanged')
        self.assertEqual(result['status'],'refused')
        self.assertEqual(result['first_failure'],'INTERNAL')
        self.assertIsNone(result['instructions'])
        self.assertNotIn('private state',repr(result))

    def test_post_identity_failure_refuses_complete_listing(self):
        r=Rig(); r.hooks['verify:after']=raising(o.Refused('PREPARATION')); result=r.run()
        self.assertEqual(result['first_failure'],'PREPARATION')
        self.assertIsNone(result['instructions'])

    def test_caught_import_error_stays_latched(self):
        r=Rig()
        def caught():
            try:r.owner.boundary.find_spec('unregistered')
            except o.Refused:pass
        r.hooks['parse']=caught; result=r.run()
        self.assertEqual(result['first_failure'],'IMPORT_BOUNDARY')
        self.assertEqual(result['boundary']['reason'],'LATE_IMPORT_OR_RELOAD')

    def test_final_sink_mutation_cannot_keep_body_success(self):
        for event in ('candidate','sink_close','child_close'):
            r=Rig(); r.hooks[event]=lambda:r.runtime.modules.update(late=module('late'))
            result=r.run()
            self.assertEqual(result['status'],'refused')
            self.assertEqual(result['first_failure'],'IMPORT_BOUNDARY')
            self.assertIsNone(result['instructions'])

    def test_sink_failures_preserve_first_reason_and_no_retry(self):
        for event in ('reserve','write:target.bin','write:stdout.bin','candidate','sink_close'):
            with self.subTest(event=event):
                r=Rig(); r.hooks[event]=raising(OSError('private output path')); result=r.run()
                self.assertEqual(result['status'],'refused')
                self.assertEqual(r.events.count(event),1)
                self.assertEqual(r.events.count('sink_close'),1)
                self.assertNotIn('private output path',repr(result))
                if event in ('reserve','write:target.bin'):self.assertNotIn('launch',r.events)

    def test_body_failure_not_overwritten_by_persistence_failure(self):
        r=Rig(); r.hooks['wait']=lambda:37; r.hooks['candidate']=raising(OSError('full'))
        result=r.run()
        self.assertEqual(result['first_failure'],'CHILD_EXIT')
        self.assertTrue(result['persistence_failure'])
        self.assertFalse(result['candidate_written'])

    def test_capture_failure_never_retried_in_finally(self):
        r=Rig(); r.hooks['capture']=raising(OSError('short read')); result=r.run()
        self.assertEqual(r.events.count('capture'),1)
        self.assertTrue(result['capture_failure'])
        self.assertFalse(result['capture_stable'])

    def test_child_close_failure_is_not_completion(self):
        r=Rig(); r.hooks['child_close']=raising(OSError('close')); result=r.run()
        self.assertTrue(result['cleanup_failure'])
        self.assertFalse(result['child_closed'])
        self.assertEqual(result['first_failure'],'CLEANUP')

    def test_checkpoint_deadline_stops_before_launch_and_does_not_wait_again(self):
        for stamp in (12,13,-1,float('nan'),float('inf'),True):
            r=Rig(); r.hooks['write:target.bin']=lambda:setattr(r,'time',stamp)
            result=r.run(); self.assertEqual(result['first_failure'],'DEADLINE')
            self.assertNotIn('launch',r.events)

    def test_initial_bad_clock_touches_no_adapters(self):
        for stamp in (float('nan'),True,None,-1,2**100):
            r=Rig(); r.time=stamp; result=r.run()
            self.assertEqual(result['first_failure'],'DEADLINE')
            self.assertEqual(r.events,[])

    def test_clock_expiry_during_persistence_rejects_candidate(self):
        r=Rig(); r.hooks['candidate']=lambda:setattr(r,'time',13)
        result=r.run()
        self.assertEqual(result['first_failure'],'DEADLINE')
        self.assertEqual(r.snapshots[0]['status'],'body_complete_unreviewed')
        self.assertEqual(result['status'],'refused')

    def test_first_failure_survives_multiple_cleanup_failures(self):
        r=Rig(); r.hooks['wait']=raising(o.Refused('DEADLINE'))
        for event in ('kill','child_close','candidate','sink_close'):
            r.hooks[event]=raising(OSError('failure'))
        result=r.run()
        self.assertEqual(result['first_failure'],'DEADLINE')
        self.assertTrue(result['cleanup_failure'] and result['persistence_failure'])
        self.assertFalse(result['child_reaped'])
        self.assertEqual(r.events.count('kill'),1)

    def test_boolean_child_exit_is_never_zero(self):
        r=Rig(); r.hooks['wait']=lambda:False; result=r.run()
        self.assertEqual(result['first_failure'],'CLEANUP')
        self.assertFalse(result['child_reaped'])

    def test_boundary_cleanup_unconfirmed_stays_failure(self):
        r=Rig()
        def changed():r.runtime.meta_path.extend(object() for _ in range(20))
        r.hooks['sink_close']=changed; result=r.run()
        self.assertFalse(result['boundary']['finder_detached'])
        self.assertEqual(result['status'],'refused')

    def test_baseexceptions_preserved_at_body_and_persistence(self):
        for event in ('verify:before','launch','wait','parse','candidate','sink_close'):
            r=Rig(); r.hooks[event]=raising(KeyboardInterrupt()); result=r.run()
            self.assertEqual(result['status'],'refused')
            self.assertFalse(result['observation_accepted'])
            self.assertEqual(r.events.count(event),1 if event!='wait' else 2)

    def test_unknown_none_child_does_not_succeed(self):
        r=Rig(); result=r.run(launch=lambda _:None)
        self.assertEqual(result['status'],'refused')
        self.assertFalse(result['child_obtained'])
        self.assertEqual(result['launch_reservations'],1)


if __name__=='__main__':unittest.main(verbosity=2)
