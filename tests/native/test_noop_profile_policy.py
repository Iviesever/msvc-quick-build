"""Synthetic policy/absence/dispatch contracts. Never executes MQB or starts ETW."""
from contextlib import contextmanager
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile
import noop_profile_policy as p
import test_noop_causal_dispatch as dispatch
from test_noop_causal_trace import record as make_call

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT/'.github/workflows/noop-profile-policy-study.yml'

def write(path, value): path.write_text(json.dumps(value), encoding='utf-8')

@contextmanager
def specimen(alias=False):
    with tempfile.TemporaryDirectory() as d:
        top=Path(d); archive=top/'synthetic.zip'; out=top/'out'
        if alias:
            (top/'real').mkdir()
            try: (top/'alias').symlink_to(top/'real',target_is_directory=True)
            except OSError as exc: raise unittest.SkipTest('alias unavailable: '+str(exc))
            out=top/'alias'/'out'
        payloads={s:('SYNTHETIC '+s).encode() for s in p.b.BINARIES}
        with ZipFile(archive,'w') as z:
            for s,data in payloads.items(): z.writestr(s+'/mqb.exe',data)
        hashes={s:p.b.digest(v) for s,v in payloads.items()}; identity=dict(SYNTHETIC=True,binaries=hashes)
        with patch.dict(p.b.BINARIES,hashes), patch.object(p.e,'verify_archive',return_value=identity):
            plan=p.prepare(archive,out,ROOT,'a'*40,p.PROFILE_SHA)
            before=[dict(path=n,size=len(v),mtime_ticks=1,sha256=p.b.digest(v)) for n,v in p.b.SOURCES.items()]
            after=before+[dict(path='.mqb/bin/timing_bench.exe',size=1,mtime_ticks=1,sha256='f'*64)]
            control=1
            for index,cell in enumerate(p.cells()):
                calls=[dict(make_call(cell,r),host_tid_before=1,host_tid_after=1) for r in cell['rows']]
                win=None; size=0
                if cell['policy']=='P':
                    instance='MQB-NoopPolicy-'+f'{index:032x}'
                    win=dict(instance=instance,first_control=control,last_control=control+4,
                        start_qpc=calls[0]['clock']['outer_end']+1,ready_qpc=calls[0]['clock']['outer_end']+2,
                        calls_end_qpc=calls[1]['clock']['outer_end']+1,stop_qpc=calls[1]['clock']['outer_end']+2,
                        stopped_qpc=calls[1]['clock']['outer_end']+3,
                        stop_attempted=True,stop_error=None,owned_after_stop=False)
                    segment=out/'traces'/cell['id']; segment.mkdir(); (segment/'temp').mkdir()
                    raw=b'SYNTHETIC NOT ETL'; (segment/'trace.etl').write_bytes(raw); size=len(raw)
                    for i,argv in enumerate(p.control_args(str(out),cell),control):
                        write(out/f'wpr-{i:02}.started.json',dict(argv=argv,instance=instance))
                        write(out/f'wpr-{i:02}.json',dict(argv=argv,instance=instance,exit_code=0,
                            command_error=None,journal_errors=[],lines=['SYNTHETIC']))
                    control+=5
                budget=dict(observed_trace_bytes=100,segment_etl_bytes=size,free_bytes=4294967296)
                write(out/'cells'/(cell['id']+'.json'),dict(cell=cell,before=before,after_prime=after,
                    after_final=after,calls=calls,error=None,window=win,budget_before=budget,budget_after=budget))
                write(out/'primes'/(cell['id']+'.json'),dict(cell=cell,before=before,after_prime=after,call=calls[0]))
            for phase,t in [('before',1),('after',10000)]:
                write(out/f'profile-interval-{phase}.started.json',dict(phase=phase,argv=['-profint']))
                write(out/f'profile-interval-{phase}.json',dict(phase=phase,argv=['-profint'],exit_code=0,error=None,
                    lines=['SYNTHETIC Current Profile Interval = 10000 [1.0000ms]'],start_qpc=t,end_qpc=t+1,changes_interval=False))
            write(out/'host.json',dict(qpc_frequency=1000,reviewed_commit='a'*40))
            write(out/'completion.json',dict(status='policy_calls_complete_trace_unreviewed',attempted=16,error=None,
                trace_stop_error=None,clears_hold=False,cause=None))
            yield out,plan


def sample_summary():
    return dict(schema=1,windows=[dict(cell=x['id'],events_lost=0,buffers_lost=0,schema_supported=True,
        timebase_verified=True,interval_verified=True,process_identity_verified=True,thread_coverage_verified=True,
        threads=[dict(tid=1,process_create_qpc=100,samples=1,stack_matched_samples=1)]) for x in p.cells() if x['policy']=='P'])


def context():
    c=dispatch.valid_context(); c.update(PROFILE_SHA256=p.PROFILE_SHA,ALLOCATION='701-noop-profile-policy-001',
        GITHUB_WORKFLOW_REF='Iviesever/msvc-quick-build/.github/workflows/noop-profile-policy-study.yml@refs/heads/main')
    return c


def guard(ctx):
    # Reuse the test-only harness; only the selected literal Python guard is executed.
    original=dispatch.inline_blocks
    def blocks(text):
        py,ps=original(text)
        return py.replace("pathlib.Path('profile-policy-execution-out')", "pathlib.Path('causal-execution-out')"),ps
    with patch.object(dispatch,'WORKFLOW',WORKFLOW),patch.object(dispatch,'inline_blocks',blocks):
        return dispatch.run_guard(ctx)


class PolicyContracts(unittest.TestCase):
    def test_plan_exact_counterbalance_and_budget(self):
        s=p.specification(); self.assertEqual(16,s['max_calls']); self.assertEqual(4,s['traced_calls'])
        self.assertEqual(104,s['previous_recorded_calls']); self.assertEqual(120,s['proposed_cumulative_ceiling'])
        self.assertEqual(['1-N-baseline','1-N-candidate','1-P-baseline','1-P-candidate',
                          '2-P-candidate','2-P-baseline','2-N-candidate','2-N-baseline'],[c['id'] for c in s['cells']])
        rows=[r for c in s['cells'] for r in c['rows']]
        self.assertEqual(list(range(1,17)),[r['sequence'] for r in rows])
        self.assertTrue(all(r['argv']==p.b.ARGV for r in rows)); self.assertFalse(s['execution_allocated'])
        self.assertEqual(22,s['limits']['max_all_wpr_invocations'])

    def test_pinned_profile_and_all_reused_definitions(self):
        p.profile_check(ROOT/p.PROFILE)
        for name,h in p.PINS.items(): self.assertEqual(h,p.e.sha(p.e.canonical((ROOT/name).read_bytes())))

    def test_no_sample_or_interrupt_profile_downgrade(self):
        for value in ('SampledProfile','DPC','Interrupt','Sequential'):
            with self.subTest(value=value),tempfile.TemporaryDirectory() as d:
                path=Path(d)/'bad.wprp'; path.write_text((ROOT/p.PROFILE).read_text().replace(value,'WRONG'))
                with self.assertRaises(ValueError): p.profile_check(path)

    def test_bad_input_does_not_prepare_output(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'out'
            with self.assertRaises((ValueError,OSError)): p.prepare(Path(d)/'none',out,ROOT,'a'*40,p.PROFILE_SHA)
            self.assertFalse(out.exists())

    def test_complete_fake_journal_stays_unreviewed(self):
        with specimen() as (out,_):
            r=p.audit(out); self.assertEqual((16,4,4,2),(r['calls'],r['traced_calls'],r['windows'],r['interval_queries']))
            self.assertFalse(r['trace_health_verified']); self.assertFalse(r['clears_hold']); self.assertIsNone(r['cause'])

    def test_alias_spelling_is_preserved(self):
        with specimen(alias=True) as (out,plan):
            self.assertEqual(str(out.absolute()),plan['root']); p.audit(out)

    def test_missing_and_extra_outputs_rejected(self):
        for name in ('cells/1-N-baseline.json','primes/2-N-baseline.json','wpr-20.json',
                     'traces/1-P-baseline/trace.etl','profile-interval-before.json'):
            with self.subTest(name=name),specimen() as (out,_):
                (out/name).unlink()
                with self.assertRaises((ValueError,OSError)): p.audit(out)
        for name in ('cells/EXTRA.json','wpr-21.json','traces/1-N-baseline'):
            with self.subTest(name=name),specimen() as (out,_):
                (out/name).write_text('{}')
                with self.assertRaises(ValueError): p.audit(out)

    def test_plan_or_source_changes_refused(self):
        for key,value in [('max_calls',32),('execution_allocated',True),('cause','noise')]:
            with self.subTest(key=key),specimen() as (out,_):
                path=out/'plan.json'; data=p.b.load(path); data[key]=value; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)
        with specimen() as (out,_):
            (out/'source'/p.PROFILE).write_text('changed')
            with self.assertRaises(ValueError): p.audit(out)

    def test_failed_completion_or_false_clearance_refused(self):
        for key,value in [('attempted',True),('attempted',15),('status','stopped'),('error','failed'),
                          ('trace_stop_error','unknown'),('clears_hold',True),('cause','noise')]:
            with self.subTest(key=key),specimen() as (out,_):
                path=out/'completion.json'; data=p.b.load(path); data[key]=value; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)

    def test_wrong_control_target_or_failure_refused(self):
        for name in ('wpr-01.started.json','wpr-05.json'):
            with self.subTest(name=name),specimen() as (out,_):
                path=out/name; data=p.b.load(path); data['argv'][1]='WRONG'; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)
        with specimen() as (out,_):
            path=out/'wpr-05.json'; data=p.b.load(path); data['exit_code']=37; write(path,data)
            with self.assertRaises(ValueError): p.audit(out)

    def test_unsafe_trace_window_and_budget_refused(self):
        for key,value in [('start_qpc',1),('ready_qpc',10000),('stop_error','unknown'),('owned_after_stop',True)]:
            with self.subTest(key=key),specimen() as (out,_):
                path=out/'cells/1-P-baseline.json'; data=p.b.load(path); data['window'][key]=value; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)
        with specimen() as (out,_):
            path=out/'cells/1-N-baseline.json'; data=p.b.load(path); data['budget_after']['free_bytes']=1; write(path,data)
            with self.assertRaises(ValueError): p.audit(out)

    def test_n_window_and_changed_final_are_refused(self):
        for field,value in [('window',{}),('error','failed')]:
            with self.subTest(field=field),specimen() as (out,_):
                path=out/'cells/1-N-baseline.json'; data=p.b.load(path); data[field]=value; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)
        with specimen() as (out,_):
            path=out/'cells/1-N-baseline.json'; data=p.b.load(path); data['calls'][1]['output_lines']=['[compile] wrong.cpp']; write(path,data)
            with self.assertRaises(ValueError): p.audit(out)

    def test_interval_failure_mutation_or_overlap_refused(self):
        for key,value in [('exit_code',1),('changes_interval',True),('lines',[]),('start_qpc',1),('argv',['-setprofint','10000'])]:
            with self.subTest(key=key),specimen() as (out,_):
                path=out/'profile-interval-after.json'; data=p.b.load(path); data[key]=value; write(path,data)
                with self.assertRaises(ValueError): p.audit(out)

    def test_one_sample_is_description_not_precision_or_causation(self):
        r=p.assess_samples(sample_summary()); self.assertEqual('descriptive_samples_only',r['status'])
        self.assertFalse(r['trace_health_verified']); self.assertFalse(r['clears_hold']); self.assertIsNone(r['cause'])

    def test_zero_or_missing_samples_remain_inconclusive(self):
        for threads in ([],None,[dict(tid=1,process_create_qpc=100,samples=0,stack_matched_samples=0)]):
            s=sample_summary(); s['windows'][0]['threads']=threads
            self.assertEqual('inconclusive',p.assess_samples(s)['status'])

    def test_missing_schema_loss_clock_or_identity_cannot_be_promoted(self):
        for field in ('schema_supported','timebase_verified','interval_verified','process_identity_verified',
                      'thread_coverage_verified','events_lost','buffers_lost'):
            for value in (None, False if field.endswith('verified') or field=='schema_supported' else 1):
                s=sample_summary(); s['windows'][0][field]=value
                self.assertEqual('inconclusive',p.assess_samples(s)['status'])

    def test_bad_sample_types_duplicate_threads_and_unmatched_stacks(self):
        for k,v in [('tid',0),('samples',True),('samples',-1),('stack_matched_samples',2)]:
            s=sample_summary(); s['windows'][0]['threads'][0][k]=v
            with self.assertRaises(ValueError): p.assess_samples(s)
        s=sample_summary(); s['windows'][0]['threads']*=2
        with self.assertRaises(ValueError): p.assess_samples(s)
        s=sample_summary(); s['windows'][0]['threads'][0]['stack_matched_samples']=0
        self.assertEqual('inconclusive',p.assess_samples(s)['status'])

    def test_missing_or_reordered_profiled_windows_refused(self):
        s=sample_summary(); s['windows'].reverse()
        with self.assertRaises(ValueError): p.assess_samples(s)

    def test_exact_manual_guard_and_no_secret_dump(self):
        request,err=guard(context()); self.assertIsNone(err); self.assertEqual(context(),request)
        ctx=context();ctx.update(GITHUB_TOKEN='SYNTHETIC-SECRET',ALLOCATION='WRONG')
        request,err=guard(ctx);self.assertIsNotNone(err);self.assertNotIn('GITHUB_TOKEN',request)

    def test_manual_guard_refuses_every_missing_field(self):
        for key in context():
            ctx=context();del ctx[key]
            with self.subTest(key=key): self.assertIsNotNone(guard(ctx)[1])

    def test_manual_guard_rejects_old_runs_hosts_sources_and_allocations(self):
        for k,v in [('GITHUB_RUN_NUMBER','2'),('GITHUB_RUN_ATTEMPT','2'),('GITHUB_EVENT_NAME','push'),
                    ('RUNNER_ENVIRONMENT','self-hosted'),('RUNNER_ARCH','ARM64'),('RUNNER_OS','Linux'),
                    ('GITHUB_REF','refs/heads/other'),('GITHUB_SHA','b'*40),('GITHUB_WORKFLOW_SHA','b'*40),
                    ('PROFILE_SHA256',p.w.PROFILE_SHA),('ALLOCATION','701-causal-windows-001')]:
            ctx=context();ctx[k]=v
            with self.subTest(key=k): self.assertIsNotNone(guard(ctx)[1])

    def test_entry_imports_unchanged_helpers_and_has_no_rate_or_console_change(self):
        s=(ROOT/'tests/native/trace_noop_profile_policy.ps1').read_text()
        for name in ('Invoke-LegacyBoundary','Invoke-OwnedWpr','Invoke-CausalWindowRow','Assert-CausalWindowBudget'):
            self.assertIn("'"+name+"'",s)
        for forbidden in ('-setprofint','-resetprofint','-cancel','Remove-Item','Start-Sleep','ProcessStartInfo'):
            self.assertNotIn(forbidden,s)
        self.assertEqual(1,s.count("Save-ProfileInterval 'before'"));self.assertEqual(1,s.count("Save-ProfileInterval 'after'"))
        workflow=WORKFLOW.read_text();self.assertIn('workflow_dispatch:',workflow)
        for forbidden in ('  push:', '  pull_request:', '  schedule:'):self.assertNotIn(forbidden,workflow)
        self.assertIn('persist-credentials: false',workflow);self.assertIn('timeout-minutes: 20',workflow)
        self.assertIn('timeout-minutes: 15',workflow);self.assertEqual(1,workflow.count('-ExecuteReviewedPolicy'))

    def test_plan_cli_never_overwrites(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'plan.json';args=[sys.executable,'-B',str(ROOT/'tests/native/noop_profile_policy.py'),'plan','--output',str(out)]
            self.assertEqual(0,subprocess.run(args,capture_output=True).returncode);raw=out.read_bytes()
            self.assertEqual(2,subprocess.run(args,capture_output=True).returncode);self.assertEqual(raw,out.read_bytes())

if __name__=='__main__':unittest.main(verbosity=2)
