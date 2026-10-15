"""Synthetic detached receipts and real temp-file identity passes; no real decoder.

Only these tests patch the fixed historical metadata pins/semantic preflight.
The public binding API has no synthetic-mode or alternate-pin parameter.
"""
import copy
from contextlib import ExitStack
from importlib.machinery import ModuleSpec
from pathlib import Path
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest import mock

import bcrypt_admission_contract_017 as c
import bcrypt_bound_observation_017 as b
import bcrypt_completion_receipt_017 as receipt
import bcrypt_guarded_owner_017 as owner
import bcrypt_owner_backend_017 as backend_module
from test_bcrypt_owner_backend_017 import Directory, Process, parse
from bcrypt_import_boundary_017 import describe

TARGET=b'\x90'*102+b'\xc3'
SHA=c.fingerprint(TARGET)['sha256']
CHECKS=[dict(file_offset=0xDB84,bytes=5,raw_hex='e8f3350000'),
        dict(file_offset=0x22E28,bytes=12,raw_hex='7c110100e31101004cdd0100'),
        dict(file_offset=0x1DD4C,bytes=8,raw_hex='0104010004420000')]


class Fixture:
    def __init__(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mqb017-bind-')
        self.root=Path(self.temp.name)
        self.files={}; self.modules={}
        def put(name,raw):
            path=self.root/name; path.write_bytes(raw)
            return dict(path=str(path),**c.fingerprint(raw))
        self.put=put
        for name in c.SOURCES:
            ref=put(name+'.py',('synthetic source '+name).encode())
            self.files[name]=ref
            module=ModuleType(name); module.__spec__=ModuleSpec(name,object(),origin=ref['path'])
            module.__file__=ref['path']; module.__cached__=None; module.__package__=''
            module.__loader__=module.__spec__.loader; self.modules[name]=module
        programs={}
        for name in ('python','decoder','loader','controller'):
            self.files[name]=put(name+'.fixture',('synthetic '+name).encode())
            programs[name]=self.files[name]['path']
        self.metadata={role:c.encode(dict(synthetic_role=role)) for role in c.provenance.PINS}
        self.pins={role:(len(raw),c.fingerprint(raw)['sha256']) for role,raw in self.metadata.items()}
        self.context=dict(call_id='017-static-1',source_commit='a'*40,output_dir=str(self.root/'out'),
            target=c.target(SHA),metadata={r:put(r+'.meta',raw) for r,raw in self.metadata.items()},
            files=self.files,programs=programs,modules={n:describe(m) for n,m in self.modules.items()},
            aliases=[],absent_paths=[],environment=dict(backend_module.ENV),limits=dict(c.LIMITS))
        self.runtime=SimpleNamespace(modules=self.modules,meta_path=[object()])
        self.directory=Directory(self.context['output_dir'])
        self.factory=lambda path:self.directory
        self.seal()

    def close(self): self.temp.cleanup()

    def seal(self, change_preparations=None, change_qualification=None):
        scope=c.fingerprint(c.encode(self.context))['sha256']
        preps={role:dict(schema='bcrypt-preparation-receipt-017-v1',role=role,context_sha256=scope,
            producer=role+'-producer',attempt_id=role+'-attempt',
            image=dict(bytes=c.provenance.IMAGE_BYTES,sha256=c.provenance.IMAGE_SHA256),
            target=self.context['target'],metadata={r:dict(bytes=v[0],sha256=v[1]) for r,v in self.pins.items()},
            snapshot_checks=copy.deepcopy(CHECKS),complete_image_passes=1) for role in ('primary','independent')}
        if change_preparations: change_preparations(preps)
        self.receipts={r:c.encode(v,c.RECEIPT_CAP) for r,v in preps.items()}
        q=dict(schema='bcrypt-execution-qualification-017-v1',context_sha256=scope,
            author='fixture-author',reviewer='fixture-reviewer',
            preparations={r:c.fingerprint(raw) for r,raw in self.receipts.items()},
            checks=list(c.CHECKS),attempts=1,decision='APPROVE_ONE_STATIC_OBSERVATION')
        if change_qualification: change_qualification(q)
        self.receipts['qualification']=c.encode(q,c.RECEIPT_CAP)
        self.request=dict(schema='bcrypt-bound-request-017-v1',context=copy.deepcopy(self.context),
            receipts={r:self.put(r+'.receipt',raw) for r,raw in self.receipts.items()})
        self.raw=c.encode(self.request); self.sha=c.fingerprint(self.raw)['sha256']

    def provenance(self):
        stack=ExitStack()
        stack.enter_context(mock.patch.object(c.provenance,'PINS',self.pins))
        stack.enter_context(mock.patch.object(c.provenance,'preflight',
                     return_value={'required_future_snapshot_checks':copy.deepcopy(CHECKS)}))
        return stack

    def bound(self):
        with self.provenance():
            return b.BoundObservation(self.raw,self.sha,metadata=self.metadata,receipts=self.receipts)

    def backend(self):
        return backend_module.PreparedNativeBackend(self.context['output_dir'],
            self.context['programs']['decoder'],SHA,self.sha,output_factory=self.factory,
            encode_json=c.encode,parse_listing=parse)

    def execution(self,popen_hook=None):
        stack=ExitStack()
        stack.enter_context(mock.patch.dict(b.os.environ,backend_module.ENV,clear=True))
        stack.enter_context(mock.patch.object(b.sys,'executable',self.context['programs']['python']))
        stack.enter_context(mock.patch.object(b.threading,'active_count',return_value=1))
        stack.enter_context(mock.patch.object(b,'GuardedPreparedOwner',
                side_effect=lambda expected:owner.GuardedPreparedOwner(expected,runtime=self.runtime)))
        # The legacy dependency objects below are test doubles, not imported code.
        stack.enter_context(mock.patch.dict(sys.modules,{
            'bcrypt_disassembly_io_014':SimpleNamespace(OutputDirectory=self.factory),
            'bcrypt_disassembly_listing_014':SimpleNamespace(parse_listing=parse)}))
        stack.enter_context(mock.patch.object(backend_module.signal,'getsignal',
                                             return_value=backend_module.signal.SIG_DFL))
        def popen(*args,**kw):
            if popen_hook: popen_hook()
            self.directory.files['tool-stdout.bin']=b'synthetic listing; not GNU output\n'
            return Process()
        self.popen=stack.enter_context(mock.patch.object(backend_module.subprocess,'Popen',side_effect=popen))
        return stack

    def complete(self,hook=None):
        gate=self.bound()
        with self.execution(hook): final=gate.run(self.backend(),TARGET,TARGET)
        self.artifacts=dict(self.directory.files,**{'owner-final.json':final})
        final_value=c.decode(final)
        self.controller=dict(schema='bcrypt-outer-completion-017-v1',binding=final_value['binding'],
            controller=self.context['files']['controller'],
            artifacts={n:c.fingerprint(raw) for n,raw in self.artifacts.items()},
            owner_exit_code=0,owner_reaped=True,timed_out=False,interrupted=False,
            writers_quiescent=True,readback_verified=True,elapsed_ns=1_000_000)
        self.controller_raw=c.encode(self.controller,c.RECEIPT_CAP)
        return final_value


class BoundObservationTests(unittest.TestCase):
    def setUp(self): self.f=Fixture(); self.addCleanup(self.f.close)

    def invalid_context(self,change):
        value=copy.deepcopy(self.f.request); change(value['context']); raw=c.encode(value)
        with self.f.provenance(), self.assertRaises(owner.Refused):
            c.validate_request(raw,c.fingerprint(raw)['sha256'])

    def test_root_checked_before_metadata_parse_or_file_io(self):
        with mock.patch.object(c.provenance,'preflight') as preflight, \
             mock.patch.object(b,'snapshot') as read, self.assertRaises(owner.Refused):
            b.BoundObservation(self.f.raw,'f'*64,metadata=self.f.metadata,receipts=self.f.receipts)
        preflight.assert_not_called(); read.assert_not_called()

    def test_actual_metadata_pins_have_no_synthetic_fallback(self):
        with self.assertRaises(owner.Refused):
            b.BoundObservation(self.f.raw,self.f.sha,metadata=self.f.metadata,receipts=self.f.receipts)
        self.assertEqual(c.provenance.PINS['index'][0],271920)
        self.assertEqual(c.provenance.PINS['caller'][1],
            '8a8513ad908a2db62f816639c0a08ed6b6d2413fde181ea8aab39937faec951c')

    def test_metadata_and_receipt_hashes_all_precede_semantics(self):
        for group in ('metadata','receipts'):
            inputs=copy.deepcopy(getattr(self.f,group)); key=next(iter(inputs))
            raw=inputs[key]; inputs[key]=bytes([raw[0]^1])+raw[1:]
            args=dict(metadata=self.f.metadata,receipts=self.f.receipts);args[group]=inputs
            with self.f.provenance(),mock.patch.object(c.provenance,'preflight') as semantic, \
                 self.assertRaises(owner.Refused):
                b.BoundObservation(self.f.raw,self.f.sha,**args)
            semantic.assert_not_called()

    def test_missing_extra_detached_input_is_not_ignored(self):
        for group in ('metadata','receipts'):
            for op in ('missing','extra'):
                args=dict(metadata=self.f.metadata,receipts=self.f.receipts)
                args[group]=dict(args[group])
                if op=='missing':args[group].pop(next(iter(args[group])))
                else:args[group]['foreign']=b'{}\n'
                with self.f.provenance(),self.assertRaises(owner.Refused):
                    b.BoundObservation(self.f.raw,self.f.sha,**args)

    def test_original_preflight_rejection_is_not_an_empty_callback_success(self):
        with self.f.provenance(),mock.patch.object(c.provenance,'preflight',side_effect=ValueError('private')), \
             self.assertRaisesRegex(owner.Refused,'BINDING_METADATA'):
            b.BoundObservation(self.f.raw,self.f.sha,metadata=self.f.metadata,receipts=self.f.receipts)

    def test_geometry_digest_commit_and_modes_are_fixed(self):
        mutations=[lambda v:v.update(call_id='018-static-1'),lambda v:v.update(source_commit='Z'*40),
            lambda v:v.update(source_commit=False),lambda v:v.update(target=[]),
            lambda v:v['target'].update(sha256=None),lambda v:v['target'].update(rva_end=0x111E4),
            lambda v:v['target'].update(section_index=False),lambda v:v['target'].update(runtime_function_index=301),
            lambda v:v['environment'].update(LD_PRELOAD='/foreign'),
            lambda v:v['limits'].update(identity_passes=True)]
        for i,change in enumerate(mutations):
            with self.subTest(i=i):self.invalid_context(change)

    def test_module_bytes_and_required_source_members_cannot_be_omitted(self):
        source=c.SOURCES[0]
        for change in (lambda v:v['modules'].pop(source),
                       lambda v:v['files'].pop(source),
                       lambda v:v['modules'][source].update(origin='/unlisted/file'),
                       lambda v:v['modules'][source].update(cached='/unlisted/cache')):
            self.invalid_context(change)

    def test_unknown_program_duplicate_files_and_output_overlap(self):
        src=next(iter(self.f.files))
        for change in (lambda v:v['programs'].update(loader='/absent'),
            lambda v:v['files'].update(alias=copy.deepcopy(v['files'][src])),
            lambda v:v.update(output_dir=self.f.files[src]['path']),
            lambda v:v.update(output_dir=str(self.f.root))):self.invalid_context(change)

    def test_byte_and_file_count_caps_are_enforced_before_reads(self):
        self.invalid_context(lambda v:v['files']['python'].update(bytes=c.FILE_CAP+1))
        self.invalid_context(lambda v:v['files']['python'].update(bytes=True))
        def excessive(v):
            for n in range(c.FILE_COUNT+1):
                v['files']['f'+str(n)]=dict(path='/fixture/'+str(n),bytes=1,sha256='a'*64)
        self.invalid_context(excessive)
        def total(v):
            for n in ('python','decoder'):v['files'][n]['bytes']=c.FILE_CAP
        self.invalid_context(total)

    def test_reused_preparer_or_attempt_receipts_refused(self):
        for key in ('producer','attempt_id','role'):
            self.f.seal(lambda p:p['independent'].update({key:p['primary'][key]}))
            with self.assertRaises(owner.Refused):self.f.bound()

    def test_preparation_snapshot_context_and_lineage_mismatch(self):
        for key,value in [('context_sha256','0'*64),('complete_image_passes',True),
                           ('image',dict(bytes=170984,sha256='0'*64)),('snapshot_checks',[])]:
            self.f.seal(lambda p:p['primary'].update({key:value}))
            with self.assertRaises(owner.Refused):self.f.bound()

    def test_qualification_not_self_review_or_preparer_review(self):
        for who in ('fixture-author','primary-producer','independent-producer'):
            self.f.seal(change_qualification=lambda q:q.update(reviewer=who))
            with self.assertRaises(owner.Refused):self.f.bound()

    def test_qualification_must_cover_exact_request_and_both_preparations(self):
        for key,value in [('context_sha256','0'*64),('preparations',{}),('checks',[]),
                           ('attempts',True),('attempts',2),('decision','APPROVED')]:
            self.f.seal(change_qualification=lambda q:q.update({key:value}))
            with self.assertRaises(owner.Refused):self.f.bound()

    def test_complete_composition_runs_real_bounded_file_snapshots(self):
        with mock.patch.object(b,'snapshot',wraps=b.snapshot) as snapshot:
            final=self.f.complete()
        self.assertEqual(final['status'],'owner_complete_unreviewed')
        self.assertEqual(self.f.popen.call_count,1)
        count=len(self.f.files)+8
        self.assertEqual(snapshot.call_count,2*count)
        self.assertEqual(final['verification']['before']['files'],count)
        self.assertEqual(final['verification']['before'],final['verification']['after'])
        self.assertEqual(final['binding']['request_sha256'],self.f.sha)
        candidate=c.decode(self.f.artifacts['report.json'])
        self.assertEqual(candidate['status'],'body_complete_unreviewed')
        self.assertFalse(final['observation_accepted'])
        self.assertFalse(candidate['output_closed'])

    def test_same_length_module_file_drift_stops_before_launch(self):
        gate=self.f.bound(); ref=self.f.files[c.SOURCES[0]]
        Path(ref['path']).write_bytes(b'x'*ref['bytes'])
        with self.f.execution(): result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(self.f.popen.call_count,0)
        self.assertEqual(result['first_failure'],'PREPARATION')
        self.assertEqual(result['verification']['failure'],'IDENTITY_PASS_REFUSED')

    def test_archived_metadata_on_disk_is_rechecked(self):
        gate=self.f.bound(); ref=self.f.context['metadata']['caller']
        Path(ref['path']).write_bytes(b'x'*ref['bytes'])
        with self.f.execution(): result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(self.f.popen.call_count,0)
        self.assertEqual(result['status'],'refused')

    def test_post_launch_dependency_drift_invalidates_candidate(self):
        ref=self.f.files['loader']
        result=self.f.complete(lambda:Path(ref['path']).write_bytes(b'x'*ref['bytes']))
        self.assertEqual(result['status'],'refused')
        self.assertEqual(result['first_failure'],'PREPARATION')
        self.assertIsNotNone(result['verification']['before'])
        self.assertIsNone(result['verification']['after'])
        self.assertEqual(c.decode(self.f.artifacts['report.json'])['status'],'refused')

    def test_absence_and_alias_drift_and_stable_positive(self):
        alias=str(self.f.root/'python-alias'); target=self.f.files['python']['path']
        Path(alias).symlink_to(target)
        missing=str(self.f.root/'not-present')
        self.f.context['aliases']=[dict(path=alias,target=target)]
        self.f.context['absent_paths']=[missing];self.f.seal()
        self.assertEqual(self.f.complete()['status'],'owner_complete_unreviewed')
        self.f.directory=Directory(self.f.context['output_dir'])
        Path(missing).symlink_to(self.f.root/'dangling')
        gate=self.f.bound()
        with self.f.execution(): result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(result['status'],'refused');self.assertEqual(self.f.popen.call_count,0)

    def test_backend_field_mismatch_never_reserves_output(self):
        for attr,value in [('output_path','/wrong'),('tool_path','/wrong'),('target_sha','f'*64),
                            ('request_sha','e'*64),('used',True),('encoder',lambda *_:b'')]:
            gate=self.f.bound(); backend=self.f.backend();setattr(backend,attr,value)
            with self.f.execution(),self.assertRaises(owner.Refused):gate.run(backend,TARGET,TARGET)
            self.assertEqual(self.f.directory.events,[])
            self.assertEqual(self.f.popen.call_count,0)

    def test_foreign_factory_or_parser_is_not_a_qualified_dependency(self):
        for attr in ('factory','parser'):
            gate=self.f.bound(); backend=self.f.backend();setattr(backend,attr,lambda *_:None)
            with self.f.execution(),self.assertRaises(owner.Refused):gate.run(backend,TARGET,TARGET)
            self.assertEqual(self.f.directory.events,[])

    def test_target_disagreement_prevents_any_launch_or_sink(self):
        for other in (b'\0'*103,TARGET[:-1],bytearray(TARGET)):
            gate=self.f.bound()
            with self.f.execution(),self.assertRaises(owner.Refused):gate.run(self.f.backend(),TARGET,other)
            self.assertEqual(self.f.directory.events,[])

    def test_second_run_and_failed_second_pass_do_not_read_again(self):
        gate=self.f.bound()
        with self.f.execution():gate.run(self.f.backend(),TARGET,TARGET)
        with mock.patch.object(b,'snapshot') as read,self.assertRaises(owner.Refused):
            gate.run(self.f.backend(),TARGET,TARGET)
        read.assert_not_called()
        with mock.patch.object(b,'snapshot') as read,self.assertRaises(owner.Refused):gate._verify('after')
        read.assert_not_called()

    def test_environment_interpreter_and_thread_mismatch(self):
        for mode in ('environment','python','threads'):
            gate=self.f.bound()
            with self.f.execution(),ExitStack() as stack:
                if mode=='environment':stack.enter_context(mock.patch.dict(b.os.environ,{'LD_PRELOAD':'bad'}))
                elif mode=='python':stack.enter_context(mock.patch.object(b.sys,'executable','/wrong'))
                else:stack.enter_context(mock.patch.object(b.threading,'active_count',return_value=2))
                result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
            self.assertEqual(result['status'],'refused');self.assertEqual(self.f.popen.call_count,0)

    def test_interrupt_during_identity_pass_preserves_interrupt_classification(self):
        gate=self.f.bound()
        with self.f.execution(),mock.patch.object(b,'snapshot',side_effect=KeyboardInterrupt()):
            result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(result['first_failure'],'INTERRUPTED')
        self.assertEqual(result['verification']['failure'],'INTERRUPTED')

    def test_bound_request_does_not_retain_callers_mutable_objects(self):
        gate=self.f.bound()
        self.f.request['context']['limits']['owner_seconds']=999
        self.f.metadata.clear();self.f.receipts.clear()
        with self.f.execution(): result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(result['status'],'owner_complete_unreviewed')


    def test_backend_identity_is_rechecked_after_launch(self):
        gate=self.f.bound();backend=self.f.backend()
        with self.f.execution(lambda:setattr(backend,'tool_path','/drifted')):
            result=c.decode(gate.run(backend,TARGET,TARGET))
        self.assertEqual(result['status'],'refused')
        self.assertEqual(result['first_failure'],'PREPARATION')
        self.assertEqual(self.f.popen.call_count,1)

    def test_shadowed_backend_method_is_refused_before_reservation(self):
        gate=self.f.bound();backend=self.f.backend()
        backend.launch=lambda target:Process()
        with self.f.execution(),self.assertRaises(owner.Refused):gate.run(backend,TARGET,TARGET)
        self.assertEqual(self.f.directory.events,[])
        self.assertEqual(self.f.popen.call_count,0)

    def test_late_backend_drift_cannot_keep_candidate_success(self):
        gate=self.f.bound();backend=self.f.backend();close=self.f.directory.close
        def drift():
            close();backend.target_sha='f'*64
        with self.f.execution(),mock.patch.object(self.f.directory,'close',side_effect=drift):
            result=c.decode(gate.run(backend,TARGET,TARGET))
        self.assertEqual(result['status'],'refused')
        self.assertEqual(result['first_failure'],'PREPARATION')
        self.assertIsNone(result['instructions'])
        self.assertEqual(c.decode(self.f.directory.files['report.json'])['status'],'body_complete_unreviewed')

    def test_post_close_binding_interrupt_keeps_interrupt_reason(self):
        gate=self.f.bound();check=gate._check_backend
        def interrupt_after_close():
            if self.f.directory.closed: raise KeyboardInterrupt()
            check()
        with self.f.execution(),mock.patch.object(gate,'_check_backend',side_effect=interrupt_after_close):
            result=c.decode(gate.run(self.f.backend(),TARGET,TARGET))
        self.assertEqual(result['first_failure'],'INTERRUPTED')
        self.assertEqual(result['verification']['failure'],'INTERRUPTED')
        self.assertEqual(result['status'],'refused')


class CodecTests(unittest.TestCase):
    def test_canonical_roundtrip_and_unicode_byte_accounting(self):
        for value in [None,False,True,0,-1,2**64-1,{'a':'\u4e2d\U0001f600\n\t\\"'},[1,2]]:
            raw=c.encode(value);self.assertEqual(c.encode(c.decode(raw)),raw)
            self.assertEqual(c.encode(value,len(raw)),raw)
            with self.assertRaises(owner.Refused):c.encode(value,len(raw)-1)

    def test_ambiguous_json_forms_are_refused(self):
        for raw in [b'{"a":1,"a":2}\n',b'{"a":NaN}\n',b'{"a":1.0}\n',b'{"a":1e500}\n',
                    b'{"a":9999999999999999999999999999999}\n',b'{}',b'{} \n',b'\xff\n',
                    b'{"a":"\\ud800"}\n',b'{ "a":1}\n']:
            with self.subTest(raw=raw),self.assertRaises(owner.Refused):c.decode(raw)

    def test_graph_limits_precede_encoding_or_parsing(self):
        values=[float('nan'),object(),{True:1}];cyclic=[];cyclic.append(cyclic);values.append(cyclic)
        for value in values:
            with mock.patch.object(c.json,'dumps') as dump,self.assertRaises(owner.Refused):c.encode(value)
            dump.assert_not_called()
        for raw in [b'['*17+b'0'+b']'*17+b'\n', b' '*(c.REQUEST_CAP+1)]:
            with mock.patch.object(c.json,'loads') as load,self.assertRaises(owner.Refused):c.decode(raw)
            load.assert_not_called()

    def test_equality_does_not_conflate_json_boolean_and_integer(self):
        for left,right in [(True,1),(False,0),({'x':[True]},{'x':[1]})]:
            with self.assertRaises(owner.Refused):c.equal(left,right)


if __name__=='__main__':unittest.main(verbosity=2)
