"""Synthetic fixed-protocol tests. No binaries, seed builds, timers, or ETW run."""
from contextlib import contextmanager
import copy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

import v9_noop_validation as v

ROOT=Path(__file__).resolve().parents[2]

def context(phase='prepare'):
    c=dict.fromkeys(v.KEYS,'')
    c.update(GITHUB_REPOSITORY=v.REPO,GITHUB_ACTIONS='true',GITHUB_EVENT_NAME='workflow_dispatch',
        GITHUB_REF='refs/heads/main',GITHUB_SHA='a'*40,GITHUB_WORKFLOW_SHA='a'*40,
        GITHUB_WORKFLOW_REF=f'{v.REPO}/{v.WORKFLOW}@refs/heads/main',GITHUB_RUN_ID='100',
        GITHUB_RUN_NUMBER='1',GITHUB_RUN_ATTEMPT='1',RUNNER_ENVIRONMENT='github-hosted',
        RUNNER_OS='Windows',RUNNER_ARCH='X64',REVIEWED_COMMIT='a'*40,PHASE=phase,
        ALLOCATION=v.protocol()['allocations'][phase])
    if phase=='measure':
        c.update(GITHUB_RUN_ID='200',GITHUB_RUN_NUMBER='2',PREP_RUN='100',PREP_ARTIFACT='300',
                 PREP_SHA256='b'*64,MANIFEST_SHA256='c'*64)
    return c

def save(path,value):
    path.write_text(json.dumps(value,allow_nan=False),encoding='utf-8')

def environment():
    return dict(image='SYNTHETIC',powershell='7.5.0',ambient_sha256='d'*64,sdk_versions=['SYNTHETIC'],
                tools=[dict(root='C:/SYNTHETIC',files={k:'f'*64 for k in ('cl.exe','link.exe','lib.exe','c1xx.dll','c2.dll')})])

@contextmanager
def journal(candidate_ticks=90):
    with tempfile.TemporaryDirectory() as d:
        top=Path(d);inputs=top/'input';root=top/'out';inputs.mkdir();root.mkdir();(inputs/'bin').mkdir()
        for folder in ('calls','cache-projections'): (root/folder).mkdir()
        prep=context();save(inputs/'request.json',prep)
        m=dict(kind='matched_builds_unmeasured',protocol=v.protocol(),harness_commit='a'*40,prepare_run='100',
            preparation_mqb_calls=5,environment=environment(),seed_exe_sha256='1'*64,
            binaries={},source_archives={},clears_hold=False,performance_verified=False)
        for side in v.SOURCES:
            binary=inputs/'bin'/f'{side}.exe';binary.write_bytes(b'SYNTHETIC NOT AN EXE '+side.encode())
            source=inputs/f'source-{side}.zip';source.write_bytes(b'SYNTHETIC SOURCE '+side.encode())
            m['binaries'][side]=v.file_sha(binary);m['source_archives'][side]=v.file_sha(source)
        for phase,budget in (('seed',1),('baseline',2),('candidate',2)):
            save(inputs/f'{phase}.started.json',dict(phase=phase,mqb_ceiling=budget))
            save(inputs/f'{phase}.finished.json',dict(phase=phase,success=True))
        for phase in ('before','after'):save(inputs/f'environment-{phase}.json',environment())
        save(inputs/'completion.json',dict(status='prepared_unmeasured',mqb_ceiling_admitted=5,error=None,clears_hold=False))
        save(inputs/'manifest.json',m)
        request=context('measure');request['MANIFEST_SHA256']=v.file_sha(inputs/'manifest.json');save(root/'request.json',request)
        plan=dict(protocol=v.protocol(),root=str(root.absolute()),input_root=str(inputs.absolute()),binaries=m['binaries'],
                  input_manifest_sha256=request['MANIFEST_SHA256'],harness_commit='a'*40)
        save(root/'plan.json',plan)
        for phase in ('before','after'):save(root/f'environment-{phase}.json',environment())
        before=[dict(path=n,size=len(data),mtime_ticks=1,sha256=v.sha(data)) for n,data in v.boundary.SOURCES.items()]
        after=before+[dict(path='.mqb/bin/timing_bench.exe',size=1,mtime_ticks=1,sha256='e'*64),
                      dict(path=v.CACHE,size=123,mtime_ticks=1,sha256='f'*64)]
        for r in v.rows():
            seq=r['sequence'];start=seq*10000;ticks=candidate_ticks if r['side']=='candidate' else 100
            record=dict(row=r,argv=v.boundary.ARGV,executable_sha256=m['binaries'][r['side']],dispatch_attempted=True,
                clears_hold=False,error=None,exit_code=0,cleanup=None,root_times=None,output_format='powershell_merged_lines',
                clock=dict(frequency=10000,outer_start=start,native_start=start+1,native_end=start+1+ticks,outer_end=start+2+ticks),
                output_lines=['[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe'] if r['phase']=='prime' else
                             ['[up-to-date] 2 translation units','[up-to-date] timing_bench.exe'])
            prefix=root/'calls'/f'{seq:02d}'
            for suffix,value in dict(before=before if r['phase']=='prime' else after,after=after,result=record,
                started=dict(row=r,argv=v.boundary.ARGV,executable_sha256=m['binaries'][r['side']],
                             executable=str(inputs/'bin'/f"{r['side']}.exe"),cwd=str(root/'fixtures'/r['fixture']))).items():
                save(Path(str(prefix)+f'.{suffix}.json'),value)
            if r['phase']=='prime':save(root/'cache-projections'/(r['fixture']+'.json'),
                dict(schema='MQB_TOOLCHAIN_CACHE_V9',root='C:/SYNTHETIC',sha256='f'*64,bytes=123))
        save(root/'completion.json',dict(status='calls_complete_unreviewed',attempted=16,error=None,clears_hold=False))
        yield root,inputs

def mutate(path,key,value):
    data=v.load(path);data[key]=value;save(path,data)

class Contracts(unittest.TestCase):
    def test_fixed_order_budget_not_allocated(self):
        p=v.protocol();self.assertEqual((5,16,120,136),(p['preparation_mqb_ceiling'],p['study_mqb_ceiling'],p['previous_study_calls'],p['proposed_study_total']))
        self.assertEqual(['baseline','candidate','candidate','baseline']*2,[r['side'] for r in v.rows() if r['phase']=='prime'])
        self.assertEqual(list(range(1,17)),[r['sequence'] for r in v.rows()])
        self.assertFalse(p['measurement_allocated']);self.assertFalse(p['preparation_allocated'])

    def test_retained_helpers_are_byte_pinned(self): v.check_repo(ROOT)

    def test_preparation_and_measure_admission(self):
        for phase in ('prepare','measure'):self.assertEqual(context(phase),v.admit(context(phase)))

    def test_missing_context_keys(self):
        for key in v.KEYS:
            c=context();del c[key]
            with self.subTest(key=key),self.assertRaises(ValueError):v.admit(c)

    def test_extra_context_secret_not_allowed(self):
        c=context();c['TOKEN']='SYNTHETIC'
        with self.assertRaises(ValueError):v.admit(c)

    def test_refuse_wrong_phase_retry_host_and_commit(self):
        for key,value in [('PHASE','other'),('GITHUB_RUN_NUMBER','3'),('GITHUB_RUN_ATTEMPT','2'),
            ('GITHUB_EVENT_NAME','push'),('RUNNER_ENVIRONMENT','self-hosted'),('RUNNER_ARCH','ARM64'),
            ('GITHUB_REF','refs/heads/other'),('GITHUB_SHA','b'*40),('GITHUB_WORKFLOW_SHA','b'*40),
            ('ALLOCATION','701-noop-profile-policy-001'),('REVIEWED_COMMIT','A'*40)]:
            c=context();c[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.admit(c)

    def test_prepare_cannot_consume_input_or_measure_without_hashes(self):
        c=context();c['PREP_RUN']='100'
        with self.assertRaises(ValueError):v.admit(c)
        for key in ('PREP_RUN','PREP_ARTIFACT','PREP_SHA256','MANIFEST_SHA256'):
            c=context('measure');c[key]=''
            with self.subTest(key=key),self.assertRaises(ValueError):v.admit(c)

    def test_protocol_source_and_budget_mutation_is_refused(self):
        for field,value in [('study_mqb_ceiling',32),('measurement_allocated',True)]:
            with self.subTest(field=field),journal() as (root,inputs):
                p=root/'plan.json';data=v.load(p);data['protocol'][field]=value;save(p,data)
                with self.assertRaises(ValueError):v.audit(root,inputs)
        p=v.protocol();p['sources']['baseline']['commit']='b'*40
        self.assertNotEqual(p['sources'],v.protocol()['sources'])

    def test_qpc_exact_conversion_and_no_rounding(self):
        self.assertEqual(Decimal('0.0001'),v.milliseconds(1,10000000))
        self.assertEqual(Decimal('1'),v.milliseconds(10000,10000000))
        for args in ((1,3),(True,1000),(1,False),(1,0),(-1,1000)):
            with self.subTest(args=args),self.assertRaises(ValueError):v.milliseconds(*args)

    def test_complete_fake_journal_never_clears_old_hold(self):
        with journal() as (root,inputs):
            r=v.audit(root,inputs);self.assertEqual('limited_observed_improvement',r['status'])
            self.assertEqual('-1',r['median_delta_ms']);self.assertEqual('-10',r['median_delta_pct'])
            self.assertEqual(16,len(r['samples']));self.assertFalse(r['clears_hold']);self.assertIsNone(r['cause'])

    def test_strict_threshold_exact_and_just_over(self):
        for ticks,decision in ((110,'threshold_not_crossed'),(111,'HOLD'),(100,'threshold_not_crossed')):
            with self.subTest(ticks=ticks),journal(ticks) as (root,inputs):
                result=v.audit(root,inputs);self.assertEqual(decision,result['decision'])
                if ticks==100:self.assertEqual('benefit_unproven',result['status'])

    def test_gate_is_called_not_replaced_with_new_median_rule(self):
        with journal() as (root,inputs),patch.object(v.gate,'evaluate_pairs',wraps=v.gate.evaluate_pairs) as call:
            v.audit(root,inputs);self.assertEqual(1,call.call_count);self.assertEqual(4,len(call.call_args.args[0]))

    def test_missing_and_extra_call_prefix_are_refused(self):
        for suffix in ('before','started','result','after'):
            with self.subTest(suffix=suffix),journal() as (root,inputs):
                (root/'calls'/f'16.{suffix}.json').unlink()
                with self.assertRaises(ValueError):v.audit(root,inputs)
        with journal() as (root,inputs):
            save(root/'calls'/'17.started.json',{})
            with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_wrong_or_stopped_completion(self):
        for key,value in [('attempted',True),('attempted',15),('status','stopped'),('error','failed'),('clears_hold',True)]:
            with self.subTest(key=key),journal() as (root,inputs):
                mutate(root/'completion.json',key,value)
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_wrong_binary_source_or_manifest_hash(self):
        for name in ('bin/baseline.exe','source-candidate.zip','manifest.json'):
            with self.subTest(name=name),journal() as (root,inputs):
                with (inputs/name).open('ab') as f:f.write(b'CHANGED')
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_changed_environment_and_missing_key_tool(self):
        for key,value in [('image','OTHER'),('ambient_sha256','a'*64),('tools',[])]:
            with self.subTest(key=key),journal() as (root,inputs):
                mutate(root/'environment-after.json',key,value)
                with self.assertRaises(ValueError):v.audit(root,inputs)
        with journal() as (root,inputs):
            for phase in ('before','after'):mutate(root/f'environment-{phase}.json','image','OTHER')
            with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_host_local_path_hash_may_differ_but_not_within_measurement(self):
        with journal() as (root,inputs):
            for phase in ('before','after'):mutate(root/f'environment-{phase}.json','ambient_sha256','a'*64)
            self.assertEqual(16,v.audit(root,inputs)['calls'])

    def test_noop_recompile_and_missing_output_are_refused(self):
        for value in (['[compile] main.cpp'],[],['{"type":"mqb.timings"}']):
            with self.subTest(value=value),journal() as (root,inputs):
                mutate(root/'calls/02.result.json','output_lines',value)
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_original_failure_cannot_be_ignored(self):
        for key,value in [('error','failed'),('exit_code',37),('exit_code',False),('clears_hold',True),('root_times',{})]:
            with self.subTest(key=key),journal() as (root,inputs):
                mutate(root/'calls/02.result.json',key,value)
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_wrong_argv_and_paths(self):
        with journal() as (root,inputs):
            mutate(root/'calls/02.result.json','argv',v.boundary.ARGV+['--timings'])
            with self.assertRaises(ValueError):v.audit(root,inputs)
        for key in ('cwd','executable'):
            with self.subTest(key=key),journal() as (root,inputs):
                mutate(root/'calls/02.started.json',key,'C:/OTHER')
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_clock_overlap_bool_and_changed_frequency(self):
        for key,value in [('outer_start',1),('native_start',True),('frequency',20000)]:
            with self.subTest(key=key),journal() as (root,inputs):
                p=root/'calls/02.result.json';data=v.load(p);data['clock'][key]=value;save(p,data)
                with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_cache_identity_mismatch_missing_and_unknown_root(self):
        for key,value in [('sha256','1'*64),('bytes',124),('schema','V8'),('root','C:/OTHER')]:
            with self.subTest(key=key),journal() as (root,inputs):
                mutate(root/'cache-projections/1-baseline.json',key,value)
                with self.assertRaises(ValueError):v.audit(root,inputs)
        with journal() as (root,inputs):
            (root/'cache-projections/4-candidate.json').unlink()
            with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_changed_manifest_between_prime_and_noop(self):
        with journal() as (root,inputs):
            for suffix in ('before','after'):
                p=root/f'calls/02.{suffix}.json';data=v.load(p);data[-1]['mtime_ticks']=2;save(p,data)
            with self.assertRaises(ValueError):v.audit(root,inputs)

    def test_changed_preparation_budget_or_prefix(self):
        with journal() as (_,inputs):
            mutate(inputs/'completion.json','mqb_ceiling_admitted',True)
            with self.assertRaises(ValueError):v.check_prepared(inputs)
        with journal() as (_,inputs):
            (inputs/'candidate.finished.json').unlink()
            with self.assertRaises(FileNotFoundError):v.check_prepared(inputs)

    def test_safe_zip_rejects_escape_normalization_alias_and_symlink(self):
        for names in (['../x'],['/x'],['a\\b'],['C:/x'],['A','a'],['a//b'],['a.'],['NUL.txt'],['a?b']):
            # Inspect original ZipInfo directly: a Windows writer would already
            # normalize a backslash before serializing it into an actual archive.
            infos=[ZipInfo(n) for n in names]
            with self.subTest(names=names),self.assertRaises(ValueError):
                v.safe_members(SimpleNamespace(infolist=lambda:infos))
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x.zip';i=ZipInfo('link');i.external_attr=0o120777<<16
            with ZipFile(p,'w') as z:z.writestr(i,b'x')
            with ZipFile(p) as z,self.assertRaises(ValueError):v.safe_members(z)

    def test_unpack_hash_before_extraction_and_fresh_destination(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x.zip';out=Path(d)/'out'
            with ZipFile(p,'w') as z:z.writestr('manifest.json',b'{}')
            with self.assertRaises(ValueError):v.unpack(p,'0'*64,out)
            self.assertFalse(out.exists());v.unpack(p,v.file_sha(p),out)
            with self.assertRaises(ValueError):v.unpack(p,v.file_sha(p),out)

    def test_workflow_separates_manual_and_contracts_and_excludes_private_values(self):
        wf=(ROOT/v.WORKFLOW).read_text();ci=(ROOT/'.github/workflows/v9-noop-validation-contracts.yml').read_text()
        for text in ('  push:','  pull_request:','  workflow_run:','  schedule:'):self.assertNotIn(text,wf)
        self.assertIn('workflow_dispatch:',wf);self.assertEqual(2,wf.count('-ExecuteReviewedPhase'))
        self.assertNotIn('-ExecuteReviewedPhase',ci);self.assertNotIn('acquire_seed',ci)
        self.assertNotIn('v9-noop-out/fixtures',wf);self.assertNotIn('wpr.exe',wf)
        self.assertNotIn('compare_mqb_benchmarks',wf);self.assertIn('cancel-in-progress: false',wf)

    def test_inline_admission_and_python_admission_agree(self):
        wf=(ROOT/v.WORKFLOW).read_text();block=wf.split('        run: |\n',1)[1].split('      - uses:',1)[0]
        block='\n'.join(line[10:] if line.startswith('          ') else line for line in block.splitlines())
        contexts=[context(),context('measure')]
        for key in v.KEYS:
            c=context();c[key]='WRONG';contexts.append(c)
        for c in contexts:
            try:v.admit(c);expected=True
            except ValueError:expected=False
            with tempfile.TemporaryDirectory() as d,patch.dict('os.environ',c,clear=True):
                text=block.replace("pathlib.Path('v9-noop-out')",f'pathlib.Path({str(Path(d)/"out")!r})')
                try:exec(compile(text,'actual-inline','exec'),{});actual=True
                except (AssertionError,ValueError):actual=False
                self.assertEqual(expected,actual,c)

if __name__=='__main__':unittest.main(verbosity=2)
