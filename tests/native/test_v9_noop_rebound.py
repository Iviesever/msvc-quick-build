"""Synthetic binding controls; real fixed input is separately read in contract CI.
Only test fixtures replace frozen() metadata. No admission patch runs in production.
"""
from contextlib import contextmanager
import ast
import copy
import hashlib
import inspect
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

import v9_noop_rebound as b
import v9_noop_validation as v
from test_v9_noop_validation import journal, save

ROOT = Path(__file__).resolve().parents[2]


def context():
    c = {k:'' for k in b.KEYS}
    c.update(GITHUB_REPOSITORY=v.REPO,GITHUB_ACTIONS='true',GITHUB_EVENT_NAME='workflow_dispatch',
             GITHUB_REF='refs/heads/main',GITHUB_SHA='c'*40,GITHUB_WORKFLOW_SHA='c'*40,
             GITHUB_WORKFLOW_REF=f'{v.REPO}/{b.WORKFLOW}@refs/heads/main',GITHUB_RUN_ID='400',
             GITHUB_RUN_NUMBER='1',GITHUB_RUN_ATTEMPT='1',RUNNER_ENVIRONMENT='github-hosted',
             RUNNER_OS='Windows',RUNNER_ARCH='X64',REVIEWED_COMMIT='c'*40,ALLOCATION=b.ALLOCATION)
    return c


def execution_archive(root, c):
    with ZipFile(root/'source-execution.zip','w') as z:
        z.comment = c['REVIEWED_COMMIT'].encode()
        for name in set((*v.PINS,*b.CORE_PINS,b.IDENTITY,b.FAILURE,b.WORKFLOW,
                         'tests/native/v9_noop_rebound.py','tests/native/run_v9_noop_rebound.ps1')):
            z.writestr(name,(ROOT/name).read_bytes())


@contextmanager
def rebound(ticks=90):
    with journal(ticks) as (root,inputs):
        prior = v.audit(root,inputs)
        c = context()
        f = dict(run='100',artifact='300',harness_commit='a'*40,
                 archive_sha256='b'*64,archive_bytes=1,expanded_bytes=1,
                 members={p.relative_to(inputs).as_posix():v.file_sha(p) for p in inputs.rglob('*') if p.is_file()})
        # Dependency seam used ONLY with synthetic input bytes. The real exact ZIP
        # is independently consumed by the no-execution contract step.
        with patch.object(b,'frozen',return_value=f):
            save(root/'request.json',c)
            execution_archive(root,c)
            h = b.execution_source(root,c)
            save(root/'plan.json',b.expected_plan(c,inputs,str(root.absolute()),h))
            save(root/'binding.json',b.binding(c,h))
            shutil.copyfile(inputs/'request.json',root/'preparation-request.json')
            shutil.copyfile(inputs/'manifest.json',root/'preparation-manifest.json')
            shutil.copyfile(ROOT/b.FAILURE,root/'consumed-measurement.zip')
            yield root,inputs,f,prior


class BindingControls(unittest.TestCase):
    def test_exact_original_identity_and_helpers(self):
        f = b.frozen()
        self.assertEqual(('36106155413','10852045267',4744328,7040330,19),
                         (f['run'],f['artifact'],f['archive_bytes'],f['expanded_bytes'],len(f['members'])))
        self.assertEqual('194f3cad63a963c944b8b44f81a41a0ef7b22cb3',f['harness_commit'])
        b.check_repo(ROOT)

    def test_old_and_new_histories_are_distinct_not_allocations(self):
        old,new = v.protocol(),b.protocol()
        self.assertEqual((120,136),(old['previous_study_calls'],old['proposed_study_total']))
        self.assertEqual((121,137,16,0),(new['previous_study_calls'],new['proposed_study_total'],new['study_mqb_ceiling'],new['new_preparation_calls']))
        self.assertFalse(new['measurement_allocated']); self.assertFalse(new['clears_hold'])
        self.assertEqual(old['rows'],new['rows']); self.assertEqual(old['argv'],new['argv'])

    def test_new_admission_does_not_rewrite_original(self):
        c = context(); original = copy.deepcopy(c)
        self.assertEqual(c,b.admit(c)); self.assertEqual(original,c)
        with self.assertRaises(ValueError): v.admit(c)

    def test_every_missing_or_extra_field_refused(self):
        for key in b.KEYS:
            c = context(); del c[key]
            with self.subTest(key=key),self.assertRaises(ValueError): b.admit(c)
        for key in ('PHASE','PREP_RUN','TOKEN'):
            c = context(); c[key]='unexpected'
            with self.subTest(key=key),self.assertRaises(ValueError): b.admit(c)

    def test_wrong_execution_identity_and_opportunity(self):
        for key,value in [('GITHUB_RUN_NUMBER','2'),('GITHUB_RUN_ATTEMPT','2'),('GITHUB_RUN_ID',b.FAILURE_RUN),
                          ('GITHUB_RUN_ID',b.frozen()['run']),('GITHUB_EVENT_NAME','push'),('RUNNER_ENVIRONMENT','self-hosted'),
                          ('RUNNER_OS','Linux'),('RUNNER_ARCH','ARM64'),('GITHUB_REF','refs/heads/other'),
                          ('GITHUB_SHA','d'*40),('GITHUB_WORKFLOW_SHA','d'*40),('ALLOCATION','v9-reader-noop-001'),
                          ('GITHUB_WORKFLOW_REF',f'{v.REPO}/{v.WORKFLOW}@refs/heads/main')]:
            c = context(); c[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): b.admit(c)

    def test_old_preparation_cannot_be_claimed_as_corrected_execution(self):
        c = context()
        for key in ('REVIEWED_COMMIT','GITHUB_SHA','GITHUB_WORKFLOW_SHA'): c[key]=b.frozen()['harness_commit']
        with self.assertRaisesRegex(ValueError,'Old preparation'): b.admit(c)

    def test_complete_binding_leaves_old_metadata_unchanged(self):
        with rebound() as (root,inputs,f,prior):
            before = {p.relative_to(inputs).as_posix():p.read_bytes() for p in inputs.rglob('*') if p.is_file()}
            r = b.audit(root,inputs)
            self.assertEqual('a'*40,r['binding']['protocol']['preparation_harness'])
            self.assertEqual('c'*40,r['binding']['execution_commit'])
            self.assertEqual(prior['pairs'],r['pairs']); self.assertEqual(prior['samples'],r['samples'])
            self.assertEqual(137,r['completed_study_total']); self.assertFalse(r['clears_hold']); self.assertIsNone(r['cause'])
            self.assertEqual(before,{p.relative_to(inputs).as_posix():p.read_bytes() for p in inputs.rglob('*') if p.is_file()})

    def test_shared_gate_exact_boundary_and_crossing(self):
        for ticks,decision in ((90,'threshold_not_crossed'),(100,'threshold_not_crossed'),(110,'threshold_not_crossed'),(111,'HOLD')):
            with self.subTest(ticks=ticks),rebound(ticks) as (root,inputs,_,prior):
                result = b.audit(root,inputs)
                self.assertEqual(decision,result['decision'])
                for key in ('pairs','median_delta_ms','median_delta_pct','crossed','status'):
                    self.assertEqual(prior[key],result[key])

    def test_each_original_member_change_is_refused(self):
        with rebound() as (root,inputs,f,_):
            for name in f['members']:
                path = inputs/name; raw = path.read_bytes()
                path.write_bytes(raw+b'changed')
                with self.subTest(name=name),self.assertRaises(ValueError): b.audit(root,inputs)
                path.write_bytes(raw)

    def test_missing_or_extra_prepared_files_refused(self):
        with rebound() as (root,inputs,_,__):
            (inputs/'extra.log').write_text('unplanned')
            with self.assertRaisesRegex(ValueError,'inventory'): b.audit(root,inputs)
        with rebound() as (root,inputs,_,__):
            (inputs/'bin/baseline.exe').unlink()
            with self.assertRaisesRegex(ValueError,'inventory'): b.audit(root,inputs)

    def test_original_copies_and_failure_cannot_be_relabelled(self):
        for name in ('preparation-request.json','preparation-manifest.json','consumed-measurement.zip'):
            with self.subTest(name=name),rebound() as (root,inputs,_,__):
                (root/name).write_bytes(b'relabelled')
                with self.assertRaises(ValueError): b.audit(root,inputs)

    def test_plan_and_binding_tampering_refused(self):
        for name,key,value in [('plan.json','execution_harness_commit','a'*40),
                               ('plan.json','preparation_harness_commit','c'*40),
                               ('plan.json','input_manifest_sha256','0'*64),
                               ('binding.json','execution_commit','d'*40)]:
            with self.subTest(key=key),rebound() as (root,inputs,_,__):
                path=root/name; data=v.load(path); data[key]=value; save(path,data)
                with self.assertRaises(ValueError): b.audit(root,inputs)

    def test_changed_plan_budget_or_rows_not_accepted(self):
        for key,value in [('study_mqb_ceiling',32),('previous_study_calls',120),('measurement_allocated',True)]:
            with self.subTest(key=key),rebound() as (root,inputs,_,__):
                path=root/'plan.json'; data=v.load(path); data['protocol'][key]=value; save(path,data)
                with self.assertRaises(ValueError): b.audit(root,inputs)

    def test_incomplete_prefix_never_reaches_gate(self):
        with rebound() as (root,inputs,_,__):
            save(root/'completion.json',dict(status='stopped',attempted=1,error='original failure',clears_hold=False))
            with patch.object(v.gate,'evaluate_pairs') as gate:
                with self.assertRaisesRegex(ValueError,'Stopped/incomplete'): b.audit(root,inputs)
                gate.assert_not_called()

    def test_real_stopped_request_cannot_be_admitted_as_new(self):
        path=ROOT/b.FAILURE
        self.assertEqual(b.FAILURE_SHA,v.file_sha(path))
        with ZipFile(path) as z:
            request=json.loads(z.read('request.json')); done=json.loads(z.read('completion.json'))
        self.assertEqual(1,done['attempted']); self.assertEqual('stopped',done['status'])
        with patch.object(v.gate,'evaluate_pairs') as gate:
            with self.assertRaises(ValueError): b.admit(request)
            gate.assert_not_called()

    def test_missing_and_extra_call_records_refused(self):
        for suffix in ('before','started','result','after'):
            with self.subTest(suffix=suffix),rebound() as (root,inputs,_,__):
                (root/'calls'/f'16.{suffix}.json').unlink()
                with self.assertRaises((ValueError,OSError)): b.audit(root,inputs)
        with rebound() as (root,inputs,_,__):
            save(root/'calls/17.result.json',{})
            with self.assertRaises(ValueError): b.audit(root,inputs)

    def test_missing_environment_projection_or_binding_refused(self):
        for name in ('binding.json','environment-before.json','environment-after.json','cache-projections/1-baseline.json'):
            with self.subTest(name=name),rebound() as (root,inputs,_,__):
                (root/name).unlink()
                with self.assertRaises((ValueError,OSError)): b.audit(root,inputs)

    def test_fixture_source_byte_claim_must_match_original(self):
        with rebound() as (root,inputs,_,__):
            path=root/'calls/01.before.json'; data=v.load(path); data[0]['sha256']='0'*64; save(path,data)
            with self.assertRaisesRegex(ValueError,'fixture source changed'): b.audit(root,inputs)

    def test_source_missing_after_prime_never_reaches_gate(self):
        with rebound() as (root,inputs,_,__):
            path=root/'calls/01.after.json'; data=v.load(path)
            save(path,[x for x in data if x['path']!='main.cpp'])
            with patch.object(v.gate,'evaluate_pairs') as gate:
                with self.assertRaisesRegex(ValueError,'fixture source changed'): b.audit(root,inputs)
                gate.assert_not_called()

    def test_final_compile_and_link_are_refused(self):
        for line in ('[compile] main.cpp','[link] timing_bench.exe'):
            with self.subTest(line=line),rebound() as (root,inputs,_,__):
                path=root/'calls/02.result.json'; data=v.load(path); data['output_lines'].append(line); save(path,data)
                with self.assertRaisesRegex(ValueError,'Not the fixed no-op'): b.audit(root,inputs)

    def test_old_cache_path_refused_in_rebound(self):
        with rebound() as (root,inputs,_,__):
            path=root/'calls/01.after.json'; data=v.load(path)
            for item in data:
                if item['path']==v.CACHE: item['path']='.mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache'
            save(path,data)
            with self.assertRaisesRegex(ValueError,'Missing output or V9 cache'): b.audit(root,inputs)

    def test_changed_image_or_tool_refuses_comparison(self):
        for key,value in [('image','OTHER'),('powershell','OTHER')]:
            with self.subTest(key=key),rebound() as (root,inputs,_,__):
                for phase in ('before','after'):
                    path=root/f'environment-{phase}.json'; data=v.load(path); data[key]=value; save(path,data)
                with self.assertRaisesRegex(ValueError,'mismatch'): b.audit(root,inputs)

    def test_bad_archive_fails_before_extraction(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); archive=root/'wrong.zip'; archive.write_bytes(b'wrong')
            with self.assertRaises(ValueError): b.unpack(archive,root/'inputs')
            self.assertFalse((root/'inputs').exists())

    def test_actual_unpack_on_synthetic_bytes_and_no_resume(self):
        with rebound() as (root,inputs,f,_):
            archive=root/'synthetic-prep.zip'
            with ZipFile(archive,'w') as z:
                for name in f['members']: z.writestr(name,(inputs/name).read_bytes())
            f.update(archive_bytes=archive.stat().st_size,archive_sha256=v.file_sha(archive),
                     expanded_bytes=sum((inputs/n).stat().st_size for n in f['members']))
            out=root/'fresh-inputs'; b.unpack(archive,out)
            with self.assertRaisesRegex(ValueError,'Fresh extraction'): b.unpack(archive,out)

    def test_fresh_plan_copies_do_not_modify_input(self):
        with rebound() as (root,inputs,f,_):
            fresh=root/'fresh-plan'; fresh.mkdir(); c=context(); save(fresh/'request.json',c); execution_archive(fresh,c)
            plan=b.prepare_measure(fresh,inputs,ROOT)
            self.assertEqual('a'*40,plan['preparation_harness_commit'])
            self.assertEqual('c'*40,plan['execution_harness_commit'])
            self.assertEqual((inputs/'manifest.json').read_bytes(),(fresh/'preparation-manifest.json').read_bytes())
            with self.assertRaisesRegex(ValueError,'Fresh execution'): b.prepare_measure(fresh,inputs,ROOT)

    def test_missing_wrong_or_changed_execution_snapshot_refused(self):
        with rebound() as (root,inputs,_,__):
            (root/'source-execution.zip').unlink()
            with self.assertRaises(OSError): b.audit(root,inputs)
        with rebound() as (root,inputs,_,__):
            with ZipFile(root/'source-execution.zip','a') as z: z.comment=b'a'*40
            with self.assertRaisesRegex(ValueError,'snapshot'): b.audit(root,inputs)

    def test_actual_writer_and_cli_contract_is_not_a_fallback_mock(self):
        app=(ROOT/'cpp/src/app/Application.cpp').read_text()
        self.assertIn('std::string toolchain_cache_name = "vs-";',app)
        writer=(ROOT/'cpp/src/msvc/toolchain/VisualStudioToolchainCache.cpp').read_text()
        self.assertIn("stream << label << ' ' << std::quoted(value) << '\\n';",writer)
        self.assertIn('std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};',writer)
        self.assertIn('write_quoted(stream, "vc_tools_root", detail::path_to_utf8(record.vc_tools_root))',writer)
        ps=(ROOT/'tests/native/run_v9_noop_rebound.ps1').read_text()
        self.assertIn("'Get-V9CacheProjection'",ps)
        self.assertNotRegex(ps,r'function\s+Get-V9CacheProjection\s*\(')

    def test_no_old_entry_or_preparation_import_and_no_environment_spoof(self):
        ps=(ROOT/'tests/native/run_v9_noop_rebound.ps1').read_text()
        for forbidden in ('Invoke-V9Preparation','Start-Process','Start-Sleep','-ExecuteReviewedPhase','wpr.exe'):
            self.assertNotIn(forbidden,ps)
        self.assertNotRegex(ps,r'\$env:\w+\s*=')
        py=(ROOT/'tests/native/v9_noop_rebound.py').read_text()
        self.assertNotIn('unittest.mock',py); self.assertNotIn('os.environ[',py)

    def test_workflow_is_independent_pinned_and_manual_only(self):
        text=(ROOT/b.WORKFLOW).read_text()
        for word in ('workflow_dispatch:','persist-credentials: false',"artifact-ids: '10852045267'", "run-id: '36106155413'"):
            self.assertIn(word,text)
        for forbidden in ('  push:','  pull_request:','  schedule:','  workflow_run:', 'phase:', '-ExecuteReviewedPhase'):
            self.assertNotIn(forbidden,text)
        self.assertEqual(1,text.count('-ExecuteReviewedRebound'))
        self.assertEqual('35f37126c30ce91320af2fa35c8f9046f7265551e63bde3d36188a2562feac53',v.sha(v.canonical((ROOT/v.WORKFLOW).read_bytes())))

    def test_shared_audit_body_is_verbatim_not_a_second_gate(self):
        body=inspect.getsource(v.audit_calls).split('    # Shared evidence-only audit. Caller must authenticate its own request/plan/preparation.\n',1)[1]
        self.assertEqual('300d6fe78717046d4540b444600e1468b3ba6783778d553aa8c25dbc94c85f75',hashlib.sha256(body.rstrip().encode()).hexdigest())
        self.assertIn('return audit_calls(root, plan, m)',inspect.getsource(v.audit))

    def test_plan_cli_is_write_once_and_allocates_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json'; args=[sys.executable,'-B',str(ROOT/'tests/native/v9_noop_rebound.py'),'plan','--output',str(path)]
            self.assertEqual(0,subprocess.run(args,capture_output=True).returncode)
            self.assertFalse(v.load(path)['measurement_allocated']); before=path.read_bytes()
            self.assertEqual(2,subprocess.run(args,capture_output=True).returncode)
            self.assertEqual(before,path.read_bytes())

if __name__ == '__main__': unittest.main(verbosity=2)
