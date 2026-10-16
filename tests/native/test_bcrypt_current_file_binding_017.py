"""Real temporary-file revalidation; no decoder, process launch or qualification."""
import copy
from contextlib import contextmanager
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import check_bcrypt_entry_binding_017 as b
from test_bcrypt_entry_binding_017 import specimen, proposal, packed


@contextmanager
def current_fixture():
    with tempfile.TemporaryDirectory(prefix='mqb-current-files-') as directory:
        root = Path(directory)
        def relocate(value):
            if type(value) is str: return value.replace('/synthetic', directory)
            if type(value) is list: return [relocate(v) for v in value]
            if type(value) is dict: return {relocate(k):relocate(v) for k,v in value.items()}
            return value
        evidence, _ = specimen(); evidence = relocate(evidence)
        runtime = proposal(evidence)
        for ref in runtime['files'].values():
            path = Path(ref['path']); path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'SYNTHETIC' if ref['bytes'] else b'')
        for alias in runtime['aliases']:
            Path(alias['path']).symlink_to(alias['target'])
        args = packed(evidence, runtime)
        with mock.patch.object(b.sys, 'executable', runtime['programs']['python']), \
             mock.patch.dict(b.os.environ, b.ENV, clear=True), \
             mock.patch.object(b, '_single_native_thread', return_value=None):
            yield root, evidence, runtime, args


class CurrentFileBindingTests(unittest.TestCase):
    def test_full_two_passes_include_real_empty_file_and_eof(self):
        with current_fixture() as (_, _, runtime, args):
            result = b.verify_current_files(*args)
            self.assertEqual(result['status'], 'CURRENT_RUNTIME_FILES_MATCHED_NOT_QUALIFIED')
            self.assertTrue(result['current_files_verified'] and result['interpreter_identity_checked'])
            self.assertEqual(result['completed_passes'], 2)
            self.assertEqual(result['reads']['passes'], 2*len(runtime['files']))
            self.assertEqual(result['reads']['eof_requests'], 2*len(runtime['files']))
            self.assertEqual(result['reads']['declared_content_bytes'], 2*sum(r['bytes'] for r in runtime['files'].values()))
            self.assertEqual(result['passes'][0], result['passes'][1])
            for field in ('snapshots_atomic','current_host_qualified','runtime_module_closure_verified',
                          'writer_domain_qualified','preparations_verified','independent_review_signed',
                          'execution_admitted','clears_hold'):
                self.assertIs(result[field], False)

    def test_all_roots_checked_before_any_current_file_read(self):
        with current_fixture() as (_, _, _, args):
            for role in args[2]:
                bad=copy.deepcopy(args); bad[2][role]['sha256']='0'*64
                with self.subTest(role=role), mock.patch.object(b, 'snapshot') as snap, \
                     mock.patch.object(b.os,'lstat') as lst, self.assertRaises(b.Refused):
                    b.verify_current_files(*bad)
                snap.assert_not_called(); lst.assert_not_called()

    def test_same_size_changed_bytes_refused_not_refrozen(self):
        with current_fixture() as (_, _, runtime, args):
            Path(runtime['programs']['decoder']).write_bytes(b'CHANGED!!')
            result=b.verify_current_files(*args)
            self.assertFalse(result['current_files_verified']); self.assertEqual(result['failure_stage'],'files')
            self.assertIsNotNone(result['last_file'])

    def test_missing_file_does_not_become_empty(self):
        with current_fixture() as (root, _, _, args):
            (root/'empty/__init__.py').unlink()
            self.assertEqual(b.verify_current_files(*args)['failure_stage'],'files')

    def test_empty_file_growth_refused_before_snapshot(self):
        with current_fixture() as (root, _, _, args):
            (root/'empty/__init__.py').write_bytes(b'x')
            self.assertEqual(b.verify_current_files(*args)['failure_stage'],'files')

    def test_canonical_file_cannot_be_replaced_by_symlink(self):
        with current_fixture() as (root, _, runtime, args):
            decoder=Path(runtime['programs']['decoder']); decoder.unlink()
            alternate=root/'alternate'; alternate.write_bytes(b'SYNTHETIC'); decoder.symlink_to(alternate)
            self.assertEqual(b.verify_current_files(*args)['failure_stage'],'files')
            self.assertTrue(decoder.is_symlink())

    def test_missing_alias_is_not_silently_skipped(self):
        with current_fixture() as (_, _, runtime, args):
            Path(runtime['aliases'][0]['path']).unlink()
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'aliases'); self.assertEqual(result['reads']['passes'],0)

    def test_retargeted_alias_with_same_content_is_refused(self):
        with current_fixture() as (root, _, runtime, args):
            alias=Path(runtime['aliases'][0]['path']); alias.unlink()
            other=root/'other'; other.write_bytes(b'SYNTHETIC'); alias.symlink_to(other)
            self.assertEqual(b.verify_current_files(*args)['failure_stage'],'aliases')

    def test_absent_path_present_is_refused_and_retained(self):
        with current_fixture() as (_, _, runtime, args):
            path=Path(runtime['absent_paths'][0]); path.write_bytes(b'foreign')
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'absence'); self.assertEqual(path.read_bytes(),b'foreign')

    def test_dangling_symlink_violates_absence(self):
        with current_fixture() as (root, _, runtime, args):
            path=Path(runtime['absent_paths'][0]); path.symlink_to(root/'missing-destination')
            self.assertFalse(path.exists()); self.assertTrue(path.is_symlink())
            self.assertEqual(b.verify_current_files(*args)['failure_stage'],'absence')
            self.assertTrue(path.is_symlink())

    def test_snapshot_permission_failure_stops_once(self):
        with current_fixture() as (_, _, _, args), \
             mock.patch.object(b,'snapshot',side_effect=PermissionError('private path')) as snap:
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'files'); self.assertEqual(snap.call_count,1)
            self.assertNotIn('private path',repr(result))

    def test_content_drift_between_passes_refused(self):
        with current_fixture() as (_, _, runtime, args):
            real=b.snapshot; count=0
            first=Path(next(iter(runtime['files'].values()))['path'])
            def read(*a,**kw):
                nonlocal count
                record=real(*a,**kw); count+=1
                if count==len(runtime['files']): first.write_bytes(b'CHANGED!!')
                return record
            with mock.patch.object(b,'snapshot',side_effect=read): result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'files'); self.assertEqual(result['completed_passes'],1)

    def test_same_bytes_replacement_state_drift_refused(self):
        with current_fixture() as (root, _, runtime, args):
            real=b.snapshot; count=0
            first=Path(next(iter(runtime['files'].values()))['path'])
            def read(*a,**kw):
                nonlocal count
                record=real(*a,**kw); count+=1
                if count==len(runtime['files']):
                    temp=root/'replacement'; temp.write_bytes(first.read_bytes()); temp.replace(first)
                return record
            with mock.patch.object(b,'snapshot',side_effect=read): result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'files'); self.assertEqual(result['completed_passes'],1)

    def test_historical_inode_differences_do_not_block_new_host(self):
        with current_fixture() as (_, evidence, runtime, _):
            for role, number in [('owner',1),('controller',2)]:
                for record in evidence[role]['files'].values(): record['state']={'ino':number,'dev':number}
            result=b.verify_current_files(*packed(evidence,runtime))
            self.assertTrue(result['current_files_verified']); self.assertFalse(result['snapshots_atomic'])

    def test_wrong_interpreter_stops_before_hashing(self):
        with current_fixture() as (root, _, _, args), mock.patch.object(b.sys,'executable',str(root/'other')):
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'runtime'); self.assertEqual(result['reads']['passes'],0)

    def test_wrong_environment_stops_before_hashing(self):
        with current_fixture() as (_, _, _, args), mock.patch.dict(b.os.environ,{'UNREGISTERED':'yes'}):
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'runtime'); self.assertEqual(result['reads']['passes'],0)

    def test_native_thread_refusal_is_preserved(self):
        with current_fixture() as (_, _, _, args), mock.patch.object(b,'_single_native_thread',side_effect=ValueError('threads')):
            result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'runtime'); self.assertEqual(result['reads']['passes'],0)

    def test_environment_drift_after_reads_is_refused(self):
        with current_fixture() as (_, _, runtime, args):
            real=b.snapshot; count=0
            def read(*a,**kw):
                nonlocal count
                record=real(*a,**kw); count+=1
                if count==len(runtime['files']): b.os.environ['LATE']='yes'
                return record
            with mock.patch.object(b,'snapshot',side_effect=read): result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'runtime'); self.assertFalse(result['current_files_verified'])

    def test_deadline_does_not_expand_or_restart_budget(self):
        with current_fixture() as (_, _, _, args), mock.patch.object(b.Budget,'checkpoint',side_effect=ValueError('deadline')) as check:
            result=b.verify_current_files(*args)
            self.assertEqual(check.call_count,1); self.assertEqual(result['reads']['passes'],0)
            self.assertEqual(result['failure_stage'],'runtime')

    def test_incorrect_eof_accounting_prevents_success(self):
        with current_fixture() as (_, _, _, args):
            real=b.snapshot
            def read(path,budget):
                value=real(path,budget); budget.eof_requests-=1; return value
            with mock.patch.object(b,'snapshot',side_effect=read): result=b.verify_current_files(*args)
            self.assertEqual(result['failure_stage'],'accounting'); self.assertFalse(result['current_files_verified'])

    def test_interrupt_preserved_no_retry(self):
        with current_fixture() as (_, _, _, args), mock.patch.object(b,'snapshot',side_effect=KeyboardInterrupt()) as snap:
            result=b.verify_current_files(*args)
            self.assertTrue(result['interrupted']); self.assertEqual(snap.call_count,1)
            self.assertFalse(result['current_files_verified'])

    def test_output_does_not_alias_roots_or_create_authority(self):
        with current_fixture() as (_, _, _, args):
            old=copy.deepcopy(args); result=b.verify_current_files(*args)
            result['passes'][0]['files']=999
            self.assertEqual(args,old)
            self.assertNotIn('modules',result); self.assertNotIn('request',result)


if __name__=='__main__': unittest.main(verbosity=2)
