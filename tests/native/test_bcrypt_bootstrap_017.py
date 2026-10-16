"""Fixed entry/receipt integration, real temp files, synthetic provenance/processes.

No native child, loader probe, original target, or developer smoke is started.
Only tests patch old metadata pins, process observation and interpreter inventory.
"""
import copy
from contextlib import ExitStack
from importlib.machinery import ModuleSpec
from pathlib import Path
from types import ModuleType, SimpleNamespace
import sys
import time
import unittest
from unittest import mock

import bcrypt_admission_contract_017 as c
import bcrypt_bootstrap_plan_017 as p
import bcrypt_fixed_bootstrap_017 as boot
import bcrypt_observation_controller_017 as ctrl
from bcrypt_scoped_transport_017 import ScopedOwnerTransport
from bcrypt_guarded_owner_017 import Refused
from test_bcrypt_bound_observation_017 import Fixture, TARGET, SHA


class BundleFixture(Fixture):
    def __init__(self):
        super().__init__()
        def add(name,file_name=None):
            ref=self.put(file_name or name+'.py',('SYNTHETIC SOURCE '+name).encode())
            m=ModuleType(name);m.__spec__=ModuleSpec(name,object(),origin=ref['path'])
            m.__file__=ref['path'];m.__cached__=None;m.__package__=''
            m.__loader__=m.__spec__.loader
            self.modules[name]=m;self.files[name]=ref
            self.context['modules'][name]=c.module_manifest({name:__import__('bcrypt_import_boundary_017').describe(m)})[name]
            return ref
        add('bcrypt_bootstrap_plan_017')
        self.bootstrap=add('__main__',p.BOOTSTRAP_NAME)
        self.files['controller']=self.put(p.CONTROLLER_NAME,b'SYNTHETIC CONTROLLER')
        self.context['programs']['controller']=self.files['controller']['path']
        self.seal()
        self.plan=dict(schema='bcrypt-bootstrap-plan-017-v1',
            request=self.put('request.json',self.raw),bootstrap=self.bootstrap,
            primary=self.put('primary.bin',TARGET),independent=self.put('independent.bin',TARGET),
            transport_dir=str(self.root/'transport'),receipt_dir=str(self.root/'receipts'))
        self.authority=dict(schema='bcrypt-writer-authority-017-v1',request_sha256=self.sha,
            qualification_sha256=c.fingerprint(self.receipts['qualification'])['sha256'],reviewer='fixture-reviewer',
            outputs=[self.context['output_dir'],self.plan['transport_dir'],self.plan['receipt_dir']],
            premises=p.PREMISES,decision='QUALIFIED_EXCLUSIVE_WRITER_DOMAIN_ONE_ATTEMPT')
        self.plan['authority']=self.put('authority.json',c.encode(self.authority,c.RECEIPT_CAP))
        self.save_plan()
        self.scopes=[];self.native_observation_mutation=None

    def save_plan(self):
        raw=c.encode(self.plan,p.PLAN_CAP)
        self.plan_path=str(self.root/'plan.json');Path(self.plan_path).write_bytes(raw)
        self.plan_sha=c.fingerprint(raw)['sha256']

    def load(self):
        with self.provenance(): return p.BootstrapInputs(self.plan_path,self.plan_sha)

    def environment(self):
        stack=self.execution()
        stack.enter_context(self.provenance())
        stack.enter_context(mock.patch.object(boot,'__file__',self.bootstrap['path']))
        stack.enter_context(mock.patch.object(ctrl,'__file__',self.context['programs']['controller']))
        return stack

    def transport_class(self):
        f=self
        class SyntheticScope:
            def assert_empty(self): f.scopes.append('assert_empty')
        class SyntheticTransport(ScopedOwnerTransport):
            def __init__(self,raw,sha):
                super().__init__(raw,sha)
                self.scope=SyntheticScope()
            def run(self):
                self.start=time.monotonic_ns()
                # Execute the actual fixed body, binding, owner and backend; fake OS only.
                raw,code=boot.run_owner(p.BootstrapInputs(f.plan_path,f.plan_sha))
                output=Path(f.context['output_dir']);output.mkdir()
                for name,data in f.directory.files.items():(output/name).write_bytes(data)
                direct=Path(f.plan['transport_dir']);direct.mkdir()
                (direct/'owner-final.json').write_bytes(raw)
                direct_result=dict(status='owner_transport_complete_unreviewed',reason=None,
                    launch_reserved=True,owner_reaped=True,pipes_eof=True,capture_complete=True,
                    final_json=True,readback_verified=True,output_closed=True,
                    launch_outcome_unknown=False,timed_out=False,interrupted=False,kill_attempted=False,
                    cleanup_failed=False,persistence_failed=False,owner_exit_code=code)
                scope=dict(status='descendant_scope_complete_unreviewed',reason=None,
                    activated=True,descendant_domain_empty=True,policy_restored=True,
                    unexpected_children=False,cleanup_failed=False,signal_attempts=0,
                    children_reaped=0,exit_codes=[])
                self.result=dict(status='scoped_transport_complete_unreviewed',transport=direct_result,scope=scope)
                if f.native_observation_mutation:f.native_observation_mutation(self.result)
                return self.result
        return SyntheticTransport


class BootstrapTests(unittest.TestCase):
    def setUp(self):self.f=BundleFixture();self.addCleanup(self.f.close)

    def test_loads_explicit_existing_inputs_not_new_preparation(self):
        inputs=self.f.load()
        self.assertEqual(inputs.primary,TARGET);self.assertEqual(inputs.independent,TARGET)
        self.assertEqual(inputs.binding['request_sha256'],self.f.sha)
        self.assertLess(inputs.reader.bytes,p.LOAD_CAP)
        self.assertEqual(inputs.reader.files,14)

    def test_wrong_root_prevents_request_or_target_read(self):
        calls=[];original=p.InputReader.read
        def track(reader,path,*a,**kw):calls.append(path);return original(reader,path,*a,**kw)
        with self.f.provenance(),mock.patch.object(p.InputReader,'read',track),self.assertRaisesRegex(Refused,'BOOT_ROOT'):
            p.BootstrapInputs(self.f.plan_path,'f'*64)
        self.assertEqual(calls,[self.f.plan_path])

    def test_plan_canonical_and_unknown_fields_rejected(self):
        self.f.plan['allow_unverified']=True;self.f.save_plan()
        with self.assertRaises(Refused):self.f.load()

    def test_authority_cannot_be_replaced_by_boolean_or_different_scope(self):
        for key,value in [('reviewer','other'),('request_sha256','f'*64),('outputs',[]),
                          ('premises',True),('decision','APPROVED')]:
            with self.subTest(key=key):
                authority=copy.deepcopy(self.f.authority);authority[key]=value
                self.f.plan['authority']=self.f.put('authority.json',c.encode(authority,c.RECEIPT_CAP))
                self.f.save_plan()
                with self.assertRaises(Refused):self.f.load()

    def test_authority_is_checked_before_any_derived_target_is_read(self):
        self.f.authority['premises']=[]
        self.f.plan['authority']=self.f.put('authority.json',c.encode(self.f.authority,c.RECEIPT_CAP));self.f.save_plan()
        calls=[];original=p.InputReader.read
        def track(reader,path,*a,**kw):calls.append(path);return original(reader,path,*a,**kw)
        with mock.patch.object(p.InputReader,'read',track),self.assertRaises(Refused):self.f.load()
        self.assertNotIn(self.f.plan['primary']['path'],calls)

    def test_output_and_input_overlap_rejected(self):
        for path in (self.f.context['output_dir'],str(self.f.root/'out'/'nested'),self.f.plan['request']['path']):
            self.f.plan['transport_dir']=path;self.f.save_plan()
            with self.assertRaises(Refused):self.f.load()

    def test_same_preparation_path_is_not_independent_input(self):
        self.f.plan['independent']=self.f.plan['primary'];self.f.save_plan()
        with self.assertRaises(Refused):self.f.load()

    def test_primary_length_and_hash_exact(self):
        Path(self.f.plan['primary']['path']).write_bytes(TARGET[:-1])
        with self.assertRaises(Refused):self.f.load()

    def test_changed_bootstrap_not_imported_as_replacement(self):
        Path(self.f.bootstrap['path']).write_bytes(b'changed')
        with self.assertRaises(Refused):self.f.load()

    def test_no_alternate_cli_or_environment(self):
        spec=self.f.load().launch_spec()
        self.assertEqual(spec['argv'],[self.f.context['programs']['python'],'-I','-S','-B',
            self.f.bootstrap['path'],self.f.plan_path,self.f.plan_sha])
        self.assertEqual(spec['environment'],self.f.context['environment'])
        self.assertEqual(spec['wall_ms'],14000)
        self.assertNotIn('-c',spec['argv']);self.assertNotIn('-m',spec['argv'])

    def test_recheck_rejects_plan_change(self):
        inputs=self.f.load();Path(self.f.plan_path).write_bytes(b'{}\n')
        with self.assertRaises(Refused):inputs.recheck()

    def test_file_links_and_oversize_refused(self):
        path=Path(self.f.plan['primary']['path']);saved=path.with_suffix('.saved');path.rename(saved);path.symlink_to(saved)
        with self.assertRaises(Refused):self.f.load()

    def test_ordinary_file_read_and_budget(self):
        r=p.InputReader();raw=r.read(self.f.plan_path,p.PLAN_CAP)
        self.assertEqual(raw,Path(self.f.plan_path).read_bytes())
        r.bytes=p.LOAD_CAP
        with self.assertRaises(Refused):r.read(self.f.plan_path,1)

    def test_bootstrap_calls_real_binding_with_fixed_legacy_dependencies(self):
        with self.f.environment():
            raw,code=boot.run_owner(p.BootstrapInputs(self.f.plan_path,self.f.plan_sha))
        self.assertEqual(code,0);self.assertEqual(c.decode(raw)['status'],'owner_complete_unreviewed')
        self.assertEqual(self.f.popen.call_count,1)  # fake OS creation, not native execution
        self.assertFalse(c.decode(raw)['observation_accepted'])

    def test_bootstrap_mismatched_file_identity_suppresses_launch(self):
        with self.f.environment(),mock.patch.object(boot,'__file__','/not-registered.py'),self.assertRaises(Refused):
            boot.run_owner(p.BootstrapInputs(self.f.plan_path,self.f.plan_sha))
        self.f.popen.assert_not_called()

    def test_bootstrap_main_refusal_has_only_bounded_diagnostic(self):
        with mock.patch.object(boot.os,'write',return_value=18) as write:
            self.assertEqual(boot.main([]),1)
        write.assert_called_once_with(2,b'BOOTSTRAP_REFUSED\n')

    def test_short_stdout_does_not_exit_zero(self):
        with mock.patch.object(boot,'BootstrapInputs',return_value=object()), \
             mock.patch.object(boot,'run_owner',return_value=(b'{}\n',0)), \
             mock.patch.object(boot.os,'write',return_value=1):
            self.assertEqual(boot.main([boot.__file__,self.f.plan_path,self.f.plan_sha]),1)


class ControllerTests(unittest.TestCase):
    def setUp(self):self.f=BundleFixture();self.addCleanup(self.f.close)

    def run_controller(self):
        with self.f.environment(),mock.patch.object(ctrl,'ScopedOwnerTransport',self.f.transport_class()):
            return ctrl.ObservationController(self.f.plan_path,self.f.plan_sha).run()

    def test_actual_fixed_body_through_six_readback_and_completion_consumer(self):
        result=self.run_controller()
        self.assertEqual(result['status'],'bootstrap_transport_complete_unreviewed',result)
        self.assertTrue(result['assessment']['transport_complete']);self.assertTrue(result['output_closed'])
        self.assertFalse(result['assessment']['observation_accepted']);self.assertFalse(result['clears_hold'])
        root=Path(self.f.plan['receipt_dir'])
        self.assertEqual({x.name for x in root.iterdir()},{'reservation.json','scope.json','completion.json','assessment.json'})
        raw=(root/'completion.json').read_bytes()
        self.assertEqual(result['receipt_sha256'],c.fingerprint(raw)['sha256'])
        self.assertTrue(c.decode((root/'scope.json').read_bytes())['authority_is_external_premise'])
        candidate=c.decode((Path(self.f.context['output_dir'])/'report.json').read_bytes())
        final=c.decode((Path(self.f.plan['transport_dir'])/'owner-final.json').read_bytes())
        self.assertFalse(candidate['output_closed']);self.assertTrue(final['output_closed'])
        self.assertGreaterEqual(len(self.f.scopes),5)

    def test_unqualified_plan_launches_nothing_and_invents_no_receipt(self):
        Path(self.f.plan['authority']['path']).write_bytes(b'{}\n')
        result=self.run_controller()
        self.assertEqual(result['status'],'refused');self.assertFalse(result['launch_attempted'])
        self.assertFalse(Path(self.f.plan['receipt_dir']).exists());self.f.popen.assert_not_called()

    def test_observed_failed_exit_not_converted_from_final_json(self):
        self.f.native_observation_mutation=lambda r:r['transport'].update(owner_exit_code=37)
        result=self.run_controller()
        self.assertEqual(result['status'],'refused');self.assertIsNone(result['receipt_sha256'])
        self.assertTrue((Path(self.f.plan['receipt_dir'])/'failure.json').exists())
        self.assertFalse((Path(self.f.plan['receipt_dir'])/'completion.json').exists())

    def test_leftover_descendant_cleanup_is_still_failure(self):
        self.f.native_observation_mutation=lambda r:r['scope'].update(unexpected_children=True,children_reaped=1)
        result=self.run_controller()
        self.assertEqual(result['status'],'refused');self.assertIsNone(result['assessment'])

    def test_boolean_exit_cannot_impersonate_zero(self):
        self.f.native_observation_mutation=lambda r:r['transport'].update(owner_exit_code=False)
        self.assertEqual(self.run_controller()['status'],'refused')

    def test_changed_inputs_after_owner_close_refuse_receipt(self):
        def mutate(r):Path(self.f.plan['independent']['path']).write_bytes(b'x'*103)
        self.f.native_observation_mutation=mutate
        self.assertEqual(self.run_controller()['status'],'refused')
        self.assertFalse((Path(self.f.plan['receipt_dir'])/'completion.json').exists())

    def test_capture_changed_after_owner_reap_is_detected_by_consumer(self):
        def mutate(r): (Path(self.f.context['output_dir'])/'tool-stdout.bin').write_bytes(b'changed')
        self.f.native_observation_mutation=mutate
        result=self.run_controller();self.assertEqual(result['status'],'refused')

    def test_close_failure_cannot_upgrade_saved_candidate(self):
        original=ctrl.ControllerRecords.close
        def bad(records):original(records);raise OSError('synthetic close error')
        with mock.patch.object(ctrl.ControllerRecords,'close',bad):result=self.run_controller()
        self.assertEqual(result['status'],'refused');self.assertTrue(result['persistence_failed'])
        self.assertIsNone(result['assessment']);self.assertFalse(result['output_closed'])
        self.assertTrue((Path(self.f.plan['receipt_dir'])/'completion.json').is_file())

    def test_assessment_save_error_is_not_retried(self):
        original=ctrl.ControllerRecords.save;calls=[]
        def bad(records,name,raw):
            calls.append(name)
            if name=='assessment.json':raise OSError('synthetic disk error')
            return original(records,name,raw)
        with mock.patch.object(ctrl.ControllerRecords,'save',bad):result=self.run_controller()
        self.assertEqual(result['status'],'refused');self.assertEqual(calls.count('assessment.json'),1)
        self.assertTrue((Path(self.f.plan['receipt_dir'])/'completion.json').is_file())

    def test_cannot_resume_preexisting_controller_directory(self):
        Path(self.f.plan['receipt_dir']).mkdir()
        result=self.run_controller();self.assertEqual(result['status'],'refused')
        self.f.popen.assert_not_called()

    def test_changed_interpreter_bytes_refused_before_owner_process_launch(self):
        Path(self.f.context['programs']['python']).write_bytes(b'changed interpreter')
        result=self.run_controller()
        self.assertEqual(result['status'],'refused')
        self.assertFalse(result['launch_attempted'])
        self.f.popen.assert_not_called()

    def test_initial_dependency_bytes_refused_before_owner_launch(self):
        Path(self.f.files['bcrypt_import_boundary_017']['path']).write_bytes(b'changed dependency')
        result=self.run_controller()
        self.assertFalse(result['launch_attempted'])

    def test_persistence_failure_flag_records_first_save_error(self):
        original=ctrl.ControllerRecords.save
        def bad(records,name,raw):
            if name=='scope.json':raise OSError('synthetic save error')
            return original(records,name,raw)
        with mock.patch.object(ctrl.ControllerRecords,'save',bad):result=self.run_controller()
        self.assertEqual(result['status'],'refused')
        self.assertTrue(result['persistence_failed'])

    def test_single_use_even_when_first_input_refused(self):
        controller=ctrl.ObservationController(self.f.plan_path,'f'*64)
        self.assertEqual(controller.run()['status'],'refused')
        with self.assertRaisesRegex(Refused,'REUSED'):controller.run()

    def test_observer_strictly_requires_all_positive_and_negative_facts(self):
        # A test-only fixture result from an already-run synthetic body.
        result=self.run_controller()['scope']
        for section,field in [('transport','owner_reaped'),('transport','output_closed'),('scope','policy_restored')]:
            mutated=copy.deepcopy(result);mutated[section][field]=False
            with self.assertRaises(Refused):ctrl._observed_success(mutated)
        for field in ('launch_outcome_unknown','timed_out','interrupted','cleanup_failed','persistence_failed'):
            mutated=copy.deepcopy(result);mutated['transport'][field]=True
            with self.assertRaises(Refused):ctrl._observed_success(mutated)

    def test_receipt_directory_exact_files_and_immutable_readback(self):
        path=self.f.root/'records';r=ctrl.ControllerRecords(str(path))
        r.save('reservation.json',b'{}\n')
        with self.assertRaises(FileExistsError):r.save('reservation.json',b'changed')
        with self.assertRaises(Refused):r.save('other.json',b'{}\n')
        with self.assertRaises(Refused):r.save('failure.json',b'x'*(c.RECEIPT_CAP+1))
        r.close()
        with self.assertRaises(Refused):r.save('failure.json',b'{}\n')
        self.assertEqual((path/'reservation.json').read_bytes(),b'{}\n')

    def test_interrupt_returns_refusal_not_success(self):
        with mock.patch.object(ctrl,'BootstrapInputs',side_effect=KeyboardInterrupt()):
            result=ctrl.ObservationController(self.f.plan_path,self.f.plan_sha).run()
        self.assertEqual(result['status'],'refused');self.assertEqual(result['reason'],'INTERRUPTED')


if __name__=='__main__':unittest.main(verbosity=2)
