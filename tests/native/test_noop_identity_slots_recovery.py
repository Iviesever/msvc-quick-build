"""Synthetic permit/platform tests plus authentic, read-only failed-prefix checks."""
import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import noop_identity_slots_recovery as r

ROOT = Path(__file__).resolve().parents[2]


def failed_input():
    value = os.environ.get('MQB_SLOT_FAILED_ZIP')
    if not value:
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            raise AssertionError('CI must supply the fixed failed-prefix fixture')
        raise unittest.SkipTest('Set MQB_SLOT_FAILED_ZIP to the original failed-prefix ZIP')
    return Path(value)


def context():
    return dict(GITHUB_RUN_ID='40000000000', GITHUB_RUN_NUMBER='2', GITHUB_RUN_ATTEMPT='1',
        RECOVERY_ID=r.RECOVERY, PERMIT_COMMENT_ID='123', REVIEWED_COMMIT='a'*40,
        ALLOCATION='pr232-slot-001', EXECUTE_REVIEWED='true')


def history(c):
    def run(old):
        return dict(id=r.FAILED_RUN if old else int(c['GITHUB_RUN_ID']), workflow_id=r.WORKFLOW_ID,
            path=r.WORKFLOW, event='workflow_dispatch', head_branch='main', run_attempt=1,
            run_number=1 if old else 2, head_sha=r.FAILED_HEAD if old else c['REVIEWED_COMMIT'],
            status='completed' if old else 'in_progress', conclusion='failure' if old else None,
            created_at='2026-10-02T01:02:05Z' if old else '2026-10-04T00:00:00Z')
    return dict(total_count=2, workflow_runs=[run(False), run(True)])


def comment(c, tree='b'*40, sha='c'*64):
    return dict(id=123, issue_url=r.API+'/issues/198', author_association='OWNER', user=dict(login='Iviesever'),
        created_at='2026-10-03T01:00:00Z', updated_at='2026-10-03T01:00:00Z',
        body=r.PERMIT_PREFIX+json.dumps(r.expected_permit(c['REVIEWED_COMMIT'], tree, sha)))


class RecoveryContracts(unittest.TestCase):
    def test_exact_original_failed_prefix_authenticates_without_any_execution(self):
        path=failed_input(); before=hashlib.sha256(path.read_bytes()).hexdigest()
        value=r.inspect_failure(path)
        self.assertEqual('consumed_failed_before_measurement',value['status'])
        self.assertEqual(r.FAILED_RUN,value['run_id']); self.assertEqual(r.FAILED_SHA,before)
        self.assertEqual(before,hashlib.sha256(path.read_bytes()).hexdigest())

    def test_changed_and_truncated_original_failure_rejected(self):
        raw=failed_input().read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'changed.zip'
            for data in (raw[:-1], bytes([raw[0]^1])+raw[1:]):
                path.write_bytes(data)
                with self.assertRaises(ValueError): r.inspect_failure(path)

    def test_permit_requires_all_exact_fields_no_extras_and_strict_types(self):
        c=context(); original=comment(c)
        self.assertEqual(48,r.validate_permit(original,c,'b'*40,'c'*64,'2026-10-04T00:00:00Z')['maximum_mqb_calls'])
        fields=r.expected_permit('a'*40,'b'*40,'c'*64)
        for key in fields:
            for operation in ('missing','changed'):
                value=fields.copy()
                if operation=='missing': del value[key]
                else: value[key]='SYNTHETIC WRONG'
                bad=copy.deepcopy(original); bad['body']=r.PERMIT_PREFIX+json.dumps(value)
                with self.subTest(key=key,operation=operation),self.assertRaises(ValueError):
                    r.validate_permit(bad,c,'b'*40,'c'*64,'2026-10-04T00:00:00Z')
        for extra in ({**fields,'extra':True},{**fields,'run_attempt':True}):
            bad=copy.deepcopy(original);bad['body']=r.PERMIT_PREFIX+json.dumps(extra)
            with self.assertRaises(ValueError):r.validate_permit(bad,c,'b'*40,'c'*64,'2026-10-04T00:00:00Z')

    def test_wrong_author_issue_id_edited_or_posthoc_permit_rejected(self):
        c=context()
        for key,value in [('id',124),('issue_url',r.API+'/issues/232'),('author_association','CONTRIBUTOR'),
            ('user',dict(login='other')),('updated_at','2026-10-03T01:00:01Z'),('body','{}')]:
            bad=comment(c);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):
                r.validate_permit(bad,c,'b'*40,'c'*64,'2026-10-04T00:00:00Z')
        with self.assertRaises(ValueError):r.validate_permit(comment(c),c,'b'*40,'c'*64,'2026-10-03T01:00:00Z')

    def test_duplicate_json_fields_are_not_an_authorization(self):
        with self.assertRaises(ValueError):r.loads('{"run_attempt":1,"run_attempt":2}')

    def test_history_requires_original_failure_and_only_current_successor(self):
        c=context();h=history(c)
        self.assertEqual(int(c['GITHUB_RUN_ID']),r.validate_history(h,c)['id'])
        for change in ('extra','missing','duplicate','counter'):
            bad=copy.deepcopy(h)
            if change=='extra': bad['workflow_runs'].append(dict(id=99));bad['total_count']=3
            if change=='missing': bad['workflow_runs'].pop();bad['total_count']=1
            if change=='duplicate':bad['workflow_runs'][1]=bad['workflow_runs'][0].copy()
            if change=='counter':bad['total_count']=100
            with self.subTest(change=change),self.assertRaises(ValueError):r.validate_history(bad,c)

    def test_history_rejects_reruns_other_source_workflow_and_changed_old_state(self):
        c=context()
        changes=[('workflow_id',1),('path','other'),('event','push'),('head_branch','other'),
                 ('run_attempt',2),('run_number',3),('head_sha','d'*40),('status','queued'),('conclusion','success')]
        for index in (0,1):
            for key,value in changes:
                bad=history(c);bad['workflow_runs'][index][key]=value
                with self.subTest(index=index,key=key),self.assertRaises(ValueError):r.validate_history(bad,c)

    def test_context_rejects_unallocated_ids_attempts_and_old_dispatch(self):
        for key,value in [('GITHUB_RUN_NUMBER','1'),('GITHUB_RUN_NUMBER','3'),('GITHUB_RUN_ATTEMPT','2'),
            ('GITHUB_RUN_ID',str(r.FAILED_RUN)),('PERMIT_COMMENT_ID',''),('RECOVERY_ID','other'),
            ('ALLOCATION','pr232-slot-002'),('EXECUTE_REVIEWED','false')]:
            c=context();c[key]=value
            with self.subTest(key=key),patch.object(r,'inspect_failure') as evidence,patch.object(r,'read_api') as api:
                with self.assertRaises(ValueError):r.authorize(c,ROOT,'b'*40,Path('absent'),Path('unused'))
                evidence.assert_not_called();api.assert_not_called()

    def test_authorization_saves_raw_server_proof_but_does_not_execute(self):
        c=context();sha=hashlib.sha256(r.slots.canonical((ROOT/r.WORKFLOW).read_bytes())).hexdigest()
        replies=[json.dumps(v).encode() for v in (history(c),comment(c,sha=sha),dict(object=dict(sha='a'*40)))]
        with tempfile.TemporaryDirectory() as temp,patch.object(r,'read_api',side_effect=replies) as api:
            out=Path(temp);value=r.authorize(c,ROOT,'b'*40,failed_input(),out,token='SYNTHETIC TOKEN')
            self.assertEqual('owner_permit_and_failed_prefix_checked',value['status'])
            self.assertFalse(value['may_clear_hold']);self.assertFalse(value['remote_allocation_verified'])
            self.assertEqual(3,api.call_count)
            for name,raw in zip(('history','permit','main'),replies):self.assertEqual(raw,(out/f'recovery-{name}-response.json').read_bytes())
            self.assertNotIn('SYNTHETIC TOKEN',(out/'recovery-admission.json').read_text())
            self.assertEqual('',api.call_args_list[1].args[1]) # public permit GET gets no token

    def test_unavailable_api_and_moving_main_preserve_prefix_no_receipt(self):
        c=context();sha=hashlib.sha256(r.slots.canonical((ROOT/r.WORKFLOW).read_bytes())).hexdigest()
        replies=[json.dumps(v).encode() for v in (history(c),comment(c,sha=sha),dict(object=dict(sha='d'*40)))]
        for values in ([OSError('offline')],replies):
            with tempfile.TemporaryDirectory() as temp,patch.object(r,'read_api',side_effect=values):
                out=Path(temp)
                with self.assertRaises((ValueError,OSError)):r.authorize(c,ROOT,'b'*40,failed_input(),out)
                self.assertFalse((out/'recovery-admission.json').exists())

    def test_existing_proof_is_not_overwritten(self):
        c=context()
        with tempfile.TemporaryDirectory() as temp,patch.object(r,'read_api',return_value=b'{}'):
            out=Path(temp);saved=out/'recovery-history-response.json';saved.write_bytes(b'KEEP')
            with self.assertRaises(FileExistsError):r.authorize(c,ROOT,'b'*40,failed_input(),out)
            self.assertEqual(b'KEEP',saved.read_bytes())

    def test_network_surface_is_fixed_get_only_and_redirects_rejected(self):
        for path in ('https://other.invalid','/actions/runs/1/rerun','/issues/comments/1?x=2','/issues/comments/../1'):
            with self.assertRaises(ValueError):r.read_api(path)
        with self.assertRaises(ValueError):r.NoRedirect().redirect_request(None,None,302,'',{},'https://other.invalid')
        source=(ROOT/'tests/native/noop_identity_slots_recovery.py').read_text()
        for forbidden in ('import subprocess','import socket','method=\'POST\'','urlopen('):self.assertNotIn(forbidden,source)


if __name__=='__main__':unittest.main(verbosity=2)
