"""Offline/static window contracts; synthetic logs/ETLs, no MQB or ETW."""
from contextlib import contextmanager
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

import noop_causal_windows as w
from test_noop_causal_trace import record as call_record

ROOT = Path(__file__).resolve().parents[2]


def write(path, value):
    path.write_text(json.dumps(value), encoding='utf-8')


@contextmanager
def specimen(alias=False):
    with tempfile.TemporaryDirectory() as d:
        top = Path(d); archive = top/'synthetic.zip'; out = top/'out'
        if alias:
            (top/'real').mkdir()
            try: (top/'alias').symlink_to(top/'real', target_is_directory=True)
            except OSError as exc: raise unittest.SkipTest('directory alias unavailable: '+str(exc))
            out = top/'alias'/'out'
        payloads = {s: ('SYNTHETIC '+s).encode() for s in w.b.BINARIES}
        with ZipFile(archive, 'w') as z:
            for side, value in payloads.items(): z.writestr(side+'/mqb.exe', value)
        hashes = {s: w.b.digest(v) for s, v in payloads.items()}
        identity = dict(SYNTHETIC=True, binaries=hashes)
        with patch.dict(w.b.BINARIES, hashes), patch.object(w.e, 'verify_archive', return_value=identity):
            plan = w.prepare(archive, out, ROOT, 'a'*40, w.PROFILE_SHA)
            manifest = [dict(path=n, size=len(v), mtime_ticks=1, sha256=w.b.digest(v)) for n,v in w.b.SOURCES.items()]
            after = manifest+[dict(path='.mqb/bin/timing_bench.exe', size=1, mtime_ticks=1, sha256='f'*64)]
            for index, cell in enumerate(w.c.cells()):
                calls = [dict(call_record(cell,r), host_tid_before=1, host_tid_after=1) for r in cell['rows']]
                first = index*5+1; instance = 'MQB-NoopWindow-'+f'{index:032x}'
                window = dict(instance=instance, first_control=first, last_control=first+4,
                    start_qpc=calls[0]['clock']['outer_end']+1, ready_qpc=calls[0]['clock']['outer_end']+2,
                    calls_end_qpc=calls[-1]['clock']['outer_end']+1, stop_qpc=calls[-1]['clock']['outer_end']+2,
                    stopped_qpc=calls[-1]['clock']['outer_end']+3,
                    stop_attempted=True, stop_error=None, owned_after_stop=False)
                write(out/'cells'/(cell['id']+'.json'), dict(cell=cell,before=manifest,after_prime=after,
                    after_final=after,calls=calls,error=None,window=window))
                write(out/'primes'/(cell['id']+'.json'), dict(cell=cell,before=manifest,after_prime=after,call=calls[0]))
                segment=out/'traces'/cell['id']; segment.mkdir(); (segment/'temp').mkdir()
                (segment/'trace.etl').write_bytes(b'SYNTHETIC NOT AN ETL')
                label='MQB_NOOP_WINDOW|'+cell['id']
                args=[['-start',str(out/'source'/w.c.PROFILE)+'!MqbNoopCausal.Verbose','-filemode','-recordtempto',str(segment/'temp')],
                      ['-marker',label+'|begin'],['-marker',label+'|end'],['-status','collectors','-details'],
                      ['-stop',str(segment/'trace.etl'),'MQB post-prime causal window; not a score','-skipPdbGen']]
                for i, argv in enumerate(args, first):
                    write(out/f'wpr-{i:02}.started.json',dict(argv=argv,instance=instance))
                    write(out/f'wpr-{i:02}.json',dict(argv=argv,instance=instance,exit_code=0,
                        command_error=None,journal_errors=[],lines=['SYNTHETIC']))
            write(out/'host.json',dict(qpc_frequency=1000,reviewed_commit='a'*40))
            write(out/'completion.json',dict(status='captured_windows_unreviewed',attempted=32,error=None,
                trace_stop_error=None,clears_hold=False,cause=None))
            yield out, plan


class WindowContracts(unittest.TestCase):
    def test_plan_reuses_original_cells_and_accounts_history(self):
        p=w.specification()
        self.assertEqual(w.c.cells(),p['cells']); self.assertEqual(32,p['max_calls'])
        self.assertEqual(20,p['traced_calls']); self.assertEqual(72,p['previous_recorded_calls'])
        self.assertEqual(104,p['proposed_cumulative_ceiling']); self.assertFalse(p['execution_allocated'])

    def test_original_control_profile_and_consumed_dispatch_unchanged(self):
        self.assertEqual(w.CONTROL_SOURCE_SHA,w.e.sha(w.e.canonical((ROOT/'tests/native/trace_noop_causal.ps1').read_bytes())))
        self.assertEqual(w.PROFILE_SHA,w.e.sha(w.e.canonical((ROOT/w.c.PROFILE).read_bytes())))
        self.assertEqual('efbca97f05fdb8cb5101f37a68384540115c7f9005f6c5fd61b46a0c4fee4b3e',
            w.e.sha(w.e.canonical((ROOT/'.github/workflows/noop-causal-study.yml').read_bytes())))

    def test_complete_synthetic_journal_never_claims_event_health(self):
        with specimen() as (out,_):
            r=w.audit(out)
            self.assertEqual(12,len(r['trace_files'])); self.assertEqual(20,r['traced_calls'])
            self.assertFalse(r['trace_health_verified']); self.assertFalse(r['clears_hold']); self.assertIsNone(r['cause'])

    def test_single_recording_root_spelling_survives_directory_alias(self):
        with specimen(alias=True) as (out, plan):
            self.assertEqual(str(out.absolute()), plan['root'])
            self.assertEqual(12, w.audit(out)['windows'])

    def test_missing_extra_journals_and_windows_refused(self):
        for name in ['cells/1-omit-baseline.json','primes/1-omit-baseline.json','wpr-01.json','wpr-60.started.json','traces/1-omit-baseline/trace.etl']:
            with self.subTest(name=name), specimen() as (out,_):
                (out/name).unlink()
                with self.assertRaises((ValueError,OSError)): w.audit(out)
        for name in ['cells/EXTRA.json','primes/EXTRA.json','wpr-61.started.json','traces/EXTRA']:
            with self.subTest(name=name), specimen() as (out,_):
                (out/name).write_text('{}')
                with self.assertRaises(ValueError): w.audit(out)

    def test_failure_and_false_proof_flags_refused(self):
        for key,value in [('status','stopped'),('attempted',30),('attempted',True),('error','failed'),
                          ('trace_stop_error','stop failed'),('clears_hold',True),('cause','noise')]:
            with self.subTest(key=key), specimen() as (out,_):
                p=out/'completion.json'; data=w.b.load(p); data[key]=value; write(p,data)
                with self.assertRaises(ValueError): w.audit(out)

    def test_windows_cannot_contain_prime_or_exclude_final(self):
        for key,value in [('start_qpc',100),('ready_qpc',205),('calls_end_qpc',200),('stop_qpc',1),
                          ('stopped_qpc',False),('stop_attempted',False),('owned_after_stop',True),('stop_error','bad')]:
            with self.subTest(key=key), specimen() as (out,_):
                p=out/'cells/1-omit-baseline.json'; data=w.b.load(p); data['window'][key]=value; write(p,data)
                with self.assertRaises(ValueError): w.audit(out)

    def test_control_errors_foreign_instance_and_wrong_marker_refused(self):
        for name,key,value in [('wpr-01.json','exit_code',37),('wpr-05.json','exit_code',True),
              ('wpr-02.json','journal_errors',['disk']),('wpr-05.json','instance','foreign'),
              ('wpr-03.started.json','argv',['-marker','wrong']),('wpr-05.json','command_error','unknown')]:
            with self.subTest(name=name,key=key), specimen() as (out,_):
                p=out/name; data=w.b.load(p); data[key]=value; write(p,data)
                with self.assertRaises(ValueError): w.audit(out)

    def test_source_checkpoint_plan_and_clock_corruption_refused(self):
        for what in ('source','checkpoint','plan','frequency','thread','fixture'):
            with self.subTest(what=what), specimen() as (out,_):
                if what=='source': (out/'source'/w.SOURCES[-1]).write_text('changed')
                elif what=='checkpoint':
                    p=out/'primes/1-omit-baseline.json'; v=w.b.load(p); v['call']['exit_code']=37; write(p,v)
                elif what=='plan':
                    p=out/'plan.json'; v=w.b.load(p); v['previous_recorded_calls']=42; write(p,v)
                else:
                    p=out/'cells/1-omit-baseline.json'; v=w.b.load(p)
                    if what=='frequency': v['calls'][-1]['clock']['frequency']=10
                    elif what=='thread': v['calls'][-1]['host_tid_before']=None
                    else: v['after_final'][-1]['size']=2
                    write(p,v)
                with self.assertRaises(ValueError): w.audit(out)

    def test_segment_and_total_limits_preserve_oversized_files(self):
        with specimen() as (out,_):
            p=out/'traces/1-omit-baseline/trace.etl'
            with p.open('wb') as f: f.truncate(w.LIMITS['segment_etl_bytes']+1)
            with self.assertRaises(ValueError): w.audit(out)
            self.assertEqual(w.LIMITS['segment_etl_bytes']+1,p.stat().st_size)
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'retained.bin'
            with p.open('wb') as f: f.truncate(w.LIMITS['total_trace_bytes']+1)
            with self.assertRaises(ValueError): w.trace_bytes(Path(d))
            self.assertTrue(p.exists())

    def test_symlink_trace_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); target=root/'file'; target.write_bytes(b'fake')
            try: (root/'alias').symlink_to(target)
            except OSError: self.skipTest('symlink creation unavailable')
            with self.assertRaises(ValueError): w.trace_bytes(root)

    def test_invalid_input_never_creates_root(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'out'
            with self.assertRaises(ValueError): w.prepare(Path(d)/'none',out,ROOT,'a'*40,'0'*64)
            self.assertFalse(out.exists())

    def test_plan_cli_never_overwrites(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'plan.json'; args=[sys.executable,'-B',str(ROOT/'tests/native/noop_causal_windows.py'),'plan','--output',str(p)]
            self.assertEqual(0,subprocess.run(args,capture_output=True).returncode)
            original=p.read_bytes(); self.assertEqual(2,subprocess.run(args,capture_output=True).returncode)
            self.assertEqual(original,p.read_bytes())

    def test_production_call_body_retains_legacy_validation(self):
        old=(ROOT/'tests/native/trace_noop_causal.ps1').read_text()
        body=old[old.index('                if ($attempted -ge 32'):old.index('                # No Python process')]
        body='\n'.join(x[16:] for x in body.rstrip().splitlines()).replace('++$attempted','++$script:attempted')
        body=body.replace('    $cellRecord.after_prime = Get-FileManifest $fixture\n','')
        new=(ROOT/'tests/native/trace_noop_causal_windows.ps1').read_text()
        actual=new.split('function Invoke-CausalWindowRow {\n    param($cell, $row, $cellRecord, [string]$fixture)\n')[1].split('\n}\nfunction Get-CausalFreeSpace')[0]
        self.assertEqual(body,'\n'.join(x[4:] for x in actual.splitlines()))
        self.assertNotIn('Get-FileManifest',actual); self.assertNotIn('Invoke-OwnedWpr',actual)
        self.assertNotIn('Write-NewJson',actual); self.assertNotIn('& $python',actual)

    def test_critical_loop_has_only_call_and_no_new_execution_trigger(self):
        s=(ROOT/'tests/native/trace_noop_causal_windows.ps1').read_text()
        loop=s.split('foreach ($row in @($cell.rows | Select-Object -Skip 1)) {')[1].split('}')[0]
        self.assertEqual('Invoke-CausalWindowRow $cell $row $cellRecord $fixture',loop.strip())
        self.assertNotIn("'-cancel'",s); self.assertNotIn('Remove-Item',s)
        self.assertIn('$controls[0].Extent.Text',s)
        workflow=(ROOT/'.github/workflows/noop-causal-contracts.yml').read_text()
        self.assertNotIn('workflow_dispatch:',workflow)
        self.assertNotIn('-ExecuteReviewedWindows',workflow)
        self.assertIn('test_noop_causal_windows_control.ps1',workflow)


if __name__=='__main__': unittest.main(verbosity=2)
