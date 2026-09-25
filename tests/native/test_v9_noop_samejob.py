"""Synthetic admission/state/binding controls; never run the study programs."""
from contextlib import contextmanager
import copy
import hashlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

import v9_noop_samejob as s
from test_v9_noop_validation import journal, save, environment

ROOT=Path(__file__).resolve().parents[2]


def context():
    c={k:'' for k in s.KEYS}
    c.update(GITHUB_REPOSITORY=s.v.REPO,GITHUB_ACTIONS='true',GITHUB_EVENT_NAME='workflow_dispatch',
        GITHUB_REF='refs/heads/main',GITHUB_SHA='c'*40,GITHUB_WORKFLOW_SHA='c'*40,
        GITHUB_WORKFLOW_REF=f'{s.v.REPO}/{s.WORKFLOW}@refs/heads/main',GITHUB_RUN_ID='900',
        GITHUB_RUN_NUMBER='1',GITHUB_RUN_ATTEMPT='1',RUNNER_ENVIRONMENT='github-hosted',
        RUNNER_OS='Windows',RUNNER_ARCH='X64',GITHUB_JOB='validation',REVIEWED_COMMIT='c'*40,
        ALLOCATION=s.ALLOCATION,ACCEPT_AUTOMATIC_GATE='true')
    return c


def execution_zip(root):
    with ZipFile(root/'source-execution.zip','w') as z:
        z.comment=context()['REVIEWED_COMMIT'].encode()
        for name in (*s.PINS,*s.NEW_FILES): z.writestr(name,(ROOT/name).read_bytes())


@contextmanager
def specimen(ticks=90, freeze=True):
    # Reuse exact old synthetic call records, but authenticate a DISTINCT new protocol.
    with journal(ticks) as (old,unused):
        root=old.parent/'samejob';root.mkdir();p=root/'preparation';p.mkdir();(p/'bin').mkdir()
        save(root/'request.json',context());execution_zip(root)
        sources={}
        for side in s.v.SOURCES:
            shutil.copyfile(unused/'bin'/f'{side}.exe',p/'bin'/f'{side}.exe')
            commit=s.v.SOURCES[side]['commit']
            with ZipFile(p/f'source-{side}.zip','w') as z:
                z.comment=commit.encode();z.writestr('SYNTHETIC.txt','NOT PRODUCTION '+side)
            identity,_=s.source_archive(p/f'source-{side}.zip',commit)
            sources[side]=dict(commit=commit,tree=identity['tree'])
        for part in ('before','after'):save(p/f'environment-{part}.json',environment())
        save(p/'seed-identity.json',dict(sha256=s.SEED_SHA))
        save(p/'completion.json',dict(status='prepared_unmeasured',mqb_ceiling_admitted=5,error=None,clears_hold=False))
        for phase,budget in (('seed',1),('baseline',2),('candidate',2)):
            save(p/f'{phase}.started.json',dict(phase=phase,mqb_ceiling=budget))
            save(p/f'{phase}.finished.json',dict(phase=phase,success=True))
        for name in ('seed.log','baseline-build.log','candidate-build.log'):(p/name).write_text('SYNTHETIC no programs executed')
        # ONLY this synthetic fixture substitutes its product source-tree identities.
        with patch.dict(s.v.SOURCES,sources):
            if freeze:
                s.freeze(root,ROOT);plan=s.ready(root,ROOT);m=root/'measurement'
                for folder in ('calls','cache-projections'):
                    for file in (old/folder).iterdir():shutil.copyfile(file,m/folder/file.name)
                for r in s.v.rows():
                    file=m/'calls'/f"{r['sequence']:02d}.started.json";data=s.load(file)
                    data['cwd']=plan['root']+'/fixtures/'+r['fixture']
                    data['executable']=plan['input_root']+'/bin/'+r['side']+'.exe';save(file,data)
                for name in ('completion.json','environment-before.json','environment-after.json'):shutil.copyfile(old/name,m/name)
                save(root/'completion.json',dict(status='same_job_complete_unreviewed',preparation_mqb_ceiling_admitted=5,
                     study_attempted=16,error=None,clears_hold=False))
            yield root


class SameJobContracts(unittest.TestCase):
    def test_plan_preserves_order_and_history(self):
        p=s.protocol();self.assertEqual((5,16,137,10),(p['preparation_mqb_ceiling'],p['study_mqb_ceiling'],p['proposed_study_total'],p['proposed_preparation_total']))
        self.assertEqual(s.v.rows(),p['rows']);self.assertFalse(p['execution_allocated']);self.assertFalse(p['clears_hold'])
        self.assertEqual({'baseline':'A2','candidate':'B2'},p['binary_labels'])
        self.assertEqual(121,p['history']['study_calls']);self.assertTrue(p['automatic_intermediate_gate'])
        p['history']['measurement001']['calls']=99;self.assertEqual(1,s.protocol()['history']['measurement001']['calls'])

    def test_original_complete_helpers_unchanged(self):s.check_repo(ROOT)

    def test_exact_admission_and_explicit_consent(self):self.assertEqual(context(),s.admit(context()))

    def test_every_missing_or_extra_field(self):
        for key in s.KEYS:
            c=context();del c[key]
            with self.subTest(key=key),self.assertRaises(ValueError):s.admit(c)
        c=context();c['TOKEN']='SYNTHETIC'
        with self.assertRaises(ValueError):s.admit(c)

    def test_wrong_host_retry_source_job_or_consent(self):
        for key,value in [('GITHUB_RUN_NUMBER','2'),('GITHUB_RUN_ATTEMPT','2'),('GITHUB_EVENT_NAME','push'),
            ('GITHUB_REF','refs/heads/other'),('GITHUB_JOB','other'),('GITHUB_SHA','b'*40),('GITHUB_WORKFLOW_SHA','b'*40),
            ('RUNNER_ENVIRONMENT','self-hosted'),('RUNNER_ARCH','ARM64'),('RUNNER_OS','Linux'),
            ('ACCEPT_AUTOMATIC_GATE','false'),('ACCEPT_AUTOMATIC_GATE',True),('ALLOCATION','v9-reader-noop-002')]:
            c=context();c[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):s.admit(c)
        for run in ('0','36106155413','36107949309','36156676599'):
            c=context();c['GITHUB_RUN_ID']=run
            with self.subTest(run=run),self.assertRaises(ValueError):s.admit(c)

    def test_complete_synthetic_new_pair_is_not_human_acceptance(self):
        with specimen() as root:
            result=s.audit(root,ROOT)
            self.assertEqual('limited_observed_improvement',result['status']);self.assertEqual('-1',result['median_delta_ms'])
            self.assertEqual(137,result['completed_study_total']);self.assertEqual(10,result['completed_preparation_total'])
            self.assertEqual(16,len(result['samples']));self.assertTrue(result['human_acceptance_pending'])
            self.assertFalse(result['clears_hold']);self.assertIsNone(result['cause'])

    def test_strict_gate_is_retained(self):
        for ticks,crossed in ((100,False),(110,False),(111,True)):
            with self.subTest(ticks=ticks),specimen(ticks) as root:
                with patch.object(s.v.gate,'evaluate_pairs',wraps=s.v.gate.evaluate_pairs) as gate:
                    result=s.audit(root,ROOT);self.assertEqual(crossed,result['crossed']);gate.assert_called_once()

    def test_incomplete_preparation_never_freezes(self):
        for filename in ('candidate.finished.json','seed-identity.json','source-candidate.zip','bin/baseline.exe','baseline-build.log'):
            with self.subTest(filename=filename),specimen(freeze=False) as root:
                (root/'preparation'/filename).unlink()
                with self.assertRaises((ValueError,OSError)):s.freeze(root,ROOT)
                self.assertFalse((root/'preparation/manifest.json').exists())

    def test_preparation_failure_environment_and_seed_refuse(self):
        for filename,key,value in [('completion.json','mqb_ceiling_admitted',True),('completion.json','error','failed'),
            ('environment-after.json','image','OTHER'),('environment-after.json','ambient_sha256','0'*64),
            ('seed-identity.json','sha256','0'*64)]:
            with self.subTest(filename=filename,key=key),specimen(freeze=False) as root:
                path=root/'preparation'/filename;data=s.load(path);data[key]=value;save(path,data)
                with self.assertRaises(ValueError):s.freeze(root,ROOT)

    def test_missing_empty_or_altered_log(self):
        with specimen(freeze=False) as root:
            (root/'preparation/seed.log').write_bytes(b'')
            with self.assertRaises(ValueError):s.freeze(root,ROOT)
        with specimen() as root:
            (root/'preparation/baseline-build.log').write_text('changed')
            with self.assertRaises(ValueError):s.check_frozen(root,ROOT)

    def test_manifest_and_binary_cannot_be_replaced(self):
        for filename in ('bin/baseline.exe','bin/candidate.exe','source-baseline.zip','manifest.json'):
            with self.subTest(filename=filename),specimen() as root:
                with (root/'preparation'/filename).open('ab') as f:f.write(b'changed')
                with self.assertRaises((ValueError,OSError)):s.audit(root,ROOT)

    def test_new_freeze_never_overwrites_and_ready_never_resumes(self):
        with specimen() as root:
            raw=(root/'preparation/manifest.json').read_bytes()
            with self.assertRaises(FileExistsError):s.freeze(root,ROOT)
            with self.assertRaises(ValueError):s.ready(root,ROOT)
            self.assertEqual(raw,(root/'preparation/manifest.json').read_bytes())

    def test_execution_manifest_mismatch_and_claimed_clearance(self):
        for key,value in [('kind','matched_builds_unmeasured'),('performance_verified',True),('clears_hold',True)]:
            with self.subTest(key=key),specimen() as root:
                path=root/'preparation/manifest.json';data=s.load(path);data[key]=value;save(path,data)
                with self.assertRaises(ValueError):s.audit(root,ROOT)

    def test_missing_ready_and_bad_plan_refuse_before_gate(self):
        with specimen() as root:
            (root/'ready.json').unlink()
            with patch.object(s.v.gate,'evaluate_pairs') as gate:
                with self.assertRaises(OSError):s.audit(root,ROOT)
                gate.assert_not_called()
        with specimen() as root:
            path=root/'measurement/plan.json';data=s.load(path);data['execution_job']='different';save(path,data)
            with self.assertRaises(ValueError):s.audit(root,ROOT)

    def test_stopped_prefix_is_never_a_measurement(self):
        for count in (0,1,15,True):
            with self.subTest(count=count),specimen() as root:
                save(root/'completion.json',dict(status='stopped',preparation_mqb_ceiling_admitted=5,study_attempted=count,error='stopped',clears_hold=False))
                with patch.object(s.v.gate,'evaluate_pairs') as gate:
                    with self.assertRaisesRegex(ValueError,'Stopped/incomplete'):s.audit(root,ROOT)
                    gate.assert_not_called()

    def test_prepare_measure_and_final_environment_drift(self):
        for name in ('environment-before.json','environment-after.json'):
            for key in ('image','powershell','ambient_sha256'):
                with self.subTest(name=name,key=key),specimen() as root:
                    path=root/'measurement'/name;data=s.load(path);data[key]='changed';save(path,data)
                    with self.assertRaises(ValueError):s.audit(root,ROOT)

    def test_missing_extra_call_and_missing_projection(self):
        for name in ('calls/01.started.json','calls/16.result.json','cache-projections/4-baseline.json'):
            with self.subTest(name=name),specimen() as root:
                (root/'measurement'/name).unlink()
                with self.assertRaises((ValueError,OSError)):s.audit(root,ROOT)
        with specimen() as root:
            save(root/'measurement/calls/17.result.json',{})
            with self.assertRaises(ValueError):s.audit(root,ROOT)

    def test_fixture_mutation_and_locator_fallback_rejected(self):
        for field in ('source','fallback'):
            with self.subTest(field=field),specimen() as root:
                path=root/'measurement/calls/01.after.json';data=s.load(path)
                if field=='source':data[0]['sha256']='0'*64
                else:next(x for x in data if x['path']==s.v.CACHE)['path']='.mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache'
                save(path,data)
                with self.assertRaises(ValueError):s.audit(root,ROOT)

    def test_native_cli_is_still_invalid_on_the_real_stopped_001(self):
        with ZipFile(ROOT/'tests/native/v9_noop_validation_failed_measurement.zip') as z:
            done=__import__('json').loads(z.read('completion.json'));self.assertEqual((1,'stopped'),(done['attempted'],done['status']))
        with specimen() as root:
            save(root/'measurement/completion.json',done)
            with patch.object(s.v.gate,'evaluate_pairs') as gate:
                with self.assertRaisesRegex(ValueError,'Stopped/incomplete'):s.audit(root,ROOT)
                gate.assert_not_called()

    def test_git_tree_matches_independent_git(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);work=root/'repo';work.mkdir()
            data={'a.txt':b'x\n','a/z.txt':b'y\n','install.bat':b'@echo off\r\n'}
            for name,raw in data.items():
                p=work/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
            subprocess.run(['git','init','-q',str(work)],check=True)
            subprocess.run(['git','-C',str(work),'-c','core.autocrlf=false','add','.'],check=True)
            expected=subprocess.check_output(['git','-C',str(work),'write-tree'],text=True).strip()
            path=root/'source.zip'
            with ZipFile(path,'w') as z:
                z.comment=b'a'*40
                for name,raw in data.items():z.writestr(name,raw if name=='install.bat' else raw.replace(b'\n',b'\r\n'))
            self.assertEqual(expected,s.source_archive(path,'a'*40,expected)[0]['tree'])

    def test_wrong_archive_comment_tree_and_windows_alias(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'x.zip'
            with ZipFile(path,'w') as z:z.comment=b'a'*40;z.writestr('a','x')
            for commit,tree in [('b'*40,None),('a'*40,'0'*40)]:
                with self.assertRaises(ValueError):s.source_archive(path,commit,tree)
            for name in ('../x','CON','a/../x','/x'):
                with ZipFile(path,'w') as z:z.comment=b'a'*40;z.writestr(name,'x')
                with self.subTest(name=name),self.assertRaises(ValueError):s.source_archive(path,'a'*40)

    def test_raw_backslash_and_nul_names_survive_test_construction(self):
        # ZipInfo normalizes backslashes on Windows BEFORE writing. Mutate the
        # actual local+central filename bytes instead, without changing lengths.
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'raw.zip'
            for name in (b'a\\b',b'a\x00b'):
                with ZipFile(path,'w') as z:
                    z.comment=b'a'*40;z.writestr('axb','x')
                raw=path.read_bytes();self.assertEqual(2,raw.count(b'axb'))
                path.write_bytes(raw.replace(b'axb',name))
                self.assertEqual(2,path.read_bytes().count(name))
                with self.subTest(name=name),self.assertRaises(ValueError):s.source_archive(path,'a'*40)

    def test_manual_workflow_has_only_one_job_and_no_implicit_execution(self):
        text=(ROOT/s.WORKFLOW).read_text();self.assertIn('workflow_dispatch:',text)
        self.assertEqual(1,text.count('runs-on:'));self.assertIn('default: false',text)
        self.assertIn('accept_new_binaries_and_automatic_gate',text)
        for forbidden in ('  push:','  pull_request:','workflow_run:','download-artifact','prepared_artifact_id'):
            self.assertNotIn(forbidden,text)
        self.assertEqual(1,text.count('-ExecuteReviewedSameJob'))
        self.assertNotIn('fixtures/',text);self.assertIn('persist-credentials: false',text)

    def test_entry_reuses_original_helpers_no_new_timer_or_retry(self):
        text=(ROOT/'tests/native/run_v9_noop_samejob.ps1').read_text()
        for name in ('Invoke-V9Preparation','Invoke-V9Calls','Get-V9CacheProjection','Invoke-LegacyBoundary'):self.assertIn("'"+name+"'",text)
        for forbidden in ('Start-Sleep','wpr.exe','ProcessStartInfo','-ExecuteReviewedPhase','-ExecuteReviewedRebound'):self.assertNotIn(forbidden,text)
        self.assertLess(text.index("Invoke-SameJobCheck 'freeze'"),text.index("Invoke-SameJobCheck 'ready'"))
        self.assertLess(text.index("Invoke-SameJobCheck 'ready'"),text.index('Invoke-V9Calls $plan'))

if __name__=='__main__':unittest.main(verbosity=2)
