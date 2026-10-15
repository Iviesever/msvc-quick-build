"""Tamper/replay cases against actual joins; controller exits are synthetic facts."""
import copy
import unittest
from unittest import mock

import bcrypt_admission_contract_017 as c
import bcrypt_completion_receipt_017 as r
from bcrypt_guarded_owner_017 import Refused
from test_bcrypt_bound_observation_017 import Fixture, TARGET


class CompletionTests(unittest.TestCase):
    def setUp(self):
        self.f=Fixture();self.addCleanup(self.f.close);self.f.complete()
        self.artifacts=copy.deepcopy(self.f.artifacts)
        self.controller=copy.deepcopy(self.f.controller)

    def assess(self,*,rebind_controller=True):
        # A re-pinned controller is test-only: exercise semantic checks even when
        # the synthetic transport claims the altered bytes were observed intact.
        if rebind_controller:
            self.controller['artifacts']={n:c.fingerprint(raw) for n,raw in self.artifacts.items()}
            raw=c.encode(self.controller,c.RECEIPT_CAP)
        else:raw=self.f.controller_raw
        with self.f.provenance():
            return r.evaluate_completion(self.f.raw,self.f.sha,metadata=self.f.metadata,
                receipts=self.f.receipts,artifacts=self.artifacts,controller_raw=raw,
                expected_controller_sha=c.fingerprint(raw)['sha256'])

    def alter(self,name,fn):
        value=c.decode(self.artifacts[name]);fn(value)
        self.artifacts[name]=c.encode(value,c.REPORT_CAP)

    def test_complete_join_does_not_grant_semantic_or_native_admission(self):
        result=self.assess()
        self.assertTrue(result['transport_complete'])
        self.assertEqual(result['status'],'CONSISTENT_TRANSPORT_REQUIRES_RESULT_REVIEW')
        for key in ('observation_accepted','native_execution_performed','clears_hold'):
            self.assertIs(result[key],False)
        self.assertIsNone(result['cause'])
        self.assertEqual(result['binding']['request_sha256'],self.f.sha)

    def test_wrong_independently_supplied_controller_pin_refused(self):
        with self.f.provenance(),self.assertRaisesRegex(Refused,'RECEIPT_ROOT'):
            r.evaluate_completion(self.f.raw,self.f.sha,metadata=self.f.metadata,
                receipts=self.f.receipts,artifacts=self.artifacts,controller_raw=self.f.controller_raw,
                expected_controller_sha='f'*64)

    def test_old_controller_cannot_authenticate_changed_artifact(self):
        self.artifacts['tool-stdout.bin']+=b'changed'
        with self.assertRaises(Refused):self.assess(rebind_controller=False)

    def test_every_artifact_is_required_and_no_extra_member_is_allowed(self):
        original=copy.deepcopy(self.artifacts)
        for name in original:
            self.artifacts=dict(original);self.artifacts.pop(name)
            with self.subTest(missing=name),self.assertRaises(Refused):self.assess()
        self.artifacts=dict(original,foreign=b'')
        with self.assertRaises(Refused):self.assess()

    def test_artifact_byte_caps_and_exact_target_size(self):
        original=copy.deepcopy(self.artifacts)
        for name,cap in r.ARTIFACT_CAPS.items():
            self.artifacts=dict(original);self.artifacts[name]=b'x'*(cap+1)
            with self.subTest(name=name),self.assertRaises(Refused):self.assess()
        for invalid in (b'',TARGET[:-1],bytearray(TARGET)):
            self.artifacts=dict(original);self.artifacts['target.bin']=invalid
            with self.assertRaises(Refused):self.assess()

    def test_candidate_cannot_impersonate_post_close_record(self):
        self.artifacts['owner-final.json']=self.artifacts['report.json']
        with self.assertRaises(Refused):self.assess()

    def test_post_close_record_cannot_overwrite_immutable_candidate(self):
        self.artifacts['report.json']=self.artifacts['owner-final.json']
        with self.assertRaises(Refused):self.assess()

    def test_containing_zero_exit_required_not_just_child_exit(self):
        for value in (False,1,-9,None,'0'):
            self.controller['owner_exit_code']=value
            with self.subTest(value=value),self.assertRaises(Refused):self.assess()

    def test_containing_reap_and_writer_stability_are_required(self):
        original=copy.deepcopy(self.controller)
        for name in ('owner_reaped','writers_quiescent','readback_verified'):
            for value in (False,1,None):
                self.controller=copy.deepcopy(original);self.controller[name]=value
                with self.subTest(name=name,value=value),self.assertRaises(Refused):self.assess()

    def test_timeout_interruption_and_deadline_never_count_as_complete(self):
        original=copy.deepcopy(self.controller)
        for name,value in [('timed_out',True),('interrupted',True),('timed_out',0),
                           ('elapsed_ns',False),('elapsed_ns',-1),
                           ('elapsed_ns',c.LIMITS['outer_seconds']*1_000_000_000)]:
            self.controller=copy.deepcopy(original);self.controller[name]=value
            with self.subTest(name=name),self.assertRaises(Refused):self.assess()

    def test_different_request_or_controller_program_cannot_be_spliced_in(self):
        self.controller['binding']['request_sha256']='0'*64
        with self.assertRaises(Refused):self.assess()
        self.controller=copy.deepcopy(self.f.controller);self.controller['controller']['sha256']='f'*64
        with self.assertRaises(Refused):self.assess()

    def test_changed_marker_or_target_refused_despite_rebound_controller(self):
        self.alter('consumed.json',lambda v:v.update(request_sha256='0'*64))
        with self.assertRaises(Refused):self.assess()
        self.artifacts=copy.deepcopy(self.f.artifacts);self.artifacts['target.bin']=b'\x90'*103
        with self.assertRaises(Refused):self.assess()

    def test_report_or_boundary_failure_and_success_flags_are_strict(self):
        original=copy.deepcopy(self.artifacts)
        for key in r._TRUE:
            self.artifacts=copy.deepcopy(original)
            self.alter('owner-final.json',lambda v:v.update({key:False}))
            with self.subTest(key=key),self.assertRaises(Refused):self.assess()
        for key in r._FALSE:
            self.artifacts=copy.deepcopy(original)
            self.alter('owner-final.json',lambda v:v.update({key:True}))
            with self.subTest(key=key),self.assertRaises(Refused):self.assess()

    def test_inner_zero_exit_and_one_launch_are_strict_integers(self):
        original=copy.deepcopy(self.artifacts)
        for name,value in [('child_returncode',False),('child_returncode',37),
                           ('launch_reservations',True),('launch_reservations',2)]:
            self.artifacts=copy.deepcopy(original)
            self.alter('owner-final.json',lambda v:v.update({name:value}))
            with self.subTest(name=name),self.assertRaises(Refused):self.assess()

    def test_import_boundary_status_and_finder_detach_are_checked(self):
        original=copy.deepcopy(self.artifacts)
        for name,value in [('status','refused'),('finder_detached',False),('reason','LATE_IMPORT'),
                           ('module_count',0),('execution_admitted',True)]:
            self.artifacts=copy.deepcopy(original)
            self.alter('owner-final.json',lambda v:v['boundary'].update({name:value}))
            with self.subTest(name=name),self.assertRaises(Refused):self.assess()

    def test_final_failure_cannot_be_hidden_by_a_green_candidate(self):
        self.alter('owner-final.json',lambda v:v.update(status='refused',first_failure='DEADLINE',instructions=None))
        with self.assertRaises(Refused):self.assess()
        self.assertEqual(c.decode(self.artifacts['report.json'])['status'],'body_complete_unreviewed')

    def test_capture_fingerprints_must_join_exact_bytes(self):
        original=copy.deepcopy(self.artifacts)
        for key in ('raw_stdout','raw_stderr'):
            self.artifacts=copy.deepcopy(original)
            self.alter('owner-final.json',lambda v:v[key].update(sha256='f'*64))
            with self.assertRaises(Refused):self.assess()

    def test_complete_103_byte_instruction_coverage_is_required(self):
        original=copy.deepcopy(self.artifacts)
        mutations=[lambda rows:rows.pop(),lambda rows:rows[0].update(length=True),
                   lambda rows:rows[0].update(raw_hex='ff'),lambda rows:rows[1].update(rva=0),
                   lambda rows:rows[0].update(assembly='x'*385)]
        for mutation in mutations:
            self.artifacts=copy.deepcopy(original)
            for name in ('owner-final.json','report.json'):
                self.alter(name,lambda v:mutation(v['instructions']))
            with self.assertRaises(Refused):self.assess()

    def test_candidate_and_final_cannot_disagree_in_any_unclosed_field(self):
        self.alter('report.json',lambda v:v['instructions'][0].update(assembly='different text'))
        with self.assertRaises(Refused):self.assess()

    def test_identity_pass_accounting_is_exact_and_not_boolean(self):
        original=copy.deepcopy(self.artifacts)
        for phase in ('before','after'):
            for key,value in [('files',True),('bytes',0),('eof_requests',-1)]:
                self.artifacts=copy.deepcopy(original)
                self.alter('owner-final.json',lambda v:v['verification'][phase].update({key:value}))
                with self.assertRaises(Refused):self.assess()

    def test_unknown_fields_are_rejected_not_silently_ignored(self):
        for name in ('consumed.json','report.json','owner-final.json'):
            self.artifacts=copy.deepcopy(self.f.artifacts)
            self.alter(name,lambda v:v.update(unreviewed_override=True))
            with self.subTest(name=name),self.assertRaises(Refused):self.assess()

    def test_reviewer_receipts_rechecked_at_completion_not_only_at_start(self):
        self.f.receipts['qualification']=b'{}\n'
        with self.assertRaises(Refused):self.assess()

    def test_evaluation_does_not_spawn_or_rehash_host_runtime(self):
        with mock.patch('bcrypt_bound_observation_017.snapshot') as snapshot, \
             mock.patch('bcrypt_owner_backend_017.subprocess.Popen') as popen:
            self.assess()
        snapshot.assert_not_called();popen.assert_not_called()

    def test_report_size_boundary_handles_maximum_allowed_assembly(self):
        for name in ('owner-final.json','report.json'):
            def maximal(v):
                for row in v['instructions']:row['assembly']='x'*384
            self.alter(name,maximal)
            self.assertLessEqual(len(self.artifacts[name]),c.REPORT_CAP)
        self.assertTrue(self.assess()['transport_complete'])


if __name__=='__main__':unittest.main(verbosity=2)
