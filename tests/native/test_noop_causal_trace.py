"""Synthetic/static controls only. No MQB launch or ETW session."""
import copy
from collections import Counter
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

import noop_causal_trace as c

ROOT = Path(__file__).resolve().parents[2]


def record(cell, row):
    prime = row['phase'] == 'prime'
    lines = ['[compile] main.cpp', '[compile] helper.cpp', '[link] timing_bench.exe'] if prime else [
        '[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe']
    if row['argv'][-1] == '--timings=json':
        lines.append(json.dumps(dict(type='mqb.timings', cache=dict(compile=dict(hits=2, misses=0),
                                link=dict(hits=1, misses=0))), separators=(',', ':')))
    n = row['sequence']*100
    return dict(row=row, argv=row['argv'], executable_sha256=c.b.BINARIES[cell['side']],
                error=None, exit_code=0, cleanup=None, root_times=None,
                output_format='powershell_merged_lines', dispatch_attempted=True, clears_hold=False,
                clock=dict(frequency=1000, outer_start=n, native_start=n+1, native_end=n+3, outer_end=n+4),
                output_lines=lines)


class Contracts(unittest.TestCase):
    def test_fixed_budget_counts_every_prime_middle_final(self):
        cells = c.cells(); rows = [r for x in cells for r in x['rows']]
        self.assertEqual(12, len(cells)); self.assertEqual(32, len(rows))
        self.assertEqual(list(range(1, 33)), [r['sequence'] for r in rows])
        self.assertEqual(Counter(prime=12, middle=8, final=12), Counter(r['phase'] for r in rows))

    def test_three_arms_keep_extra_call_control(self):
        self.assertEqual({'omit': 8, 'middle_off': 12, 'middle_on': 12},
                         {a: sum(len(x['rows']) for x in c.cells() if x['arm'] == a) for a in c.ARMS})
        for x in c.cells():
            for r in x['rows']:
                self.assertEqual(c.b.ARGV+(['--timings=json'] if x['arm']=='middle_on' and r['phase']=='middle' else []), r['argv'])

    def test_counterbalance_sides_and_conditions(self):
        cells = c.cells()
        self.assertEqual(list(c.ARMS), [x['arm'] for x in cells[:6:2]])
        self.assertEqual(list(c.ARMS[::-1]), [x['arm'] for x in cells[6::2]])
        self.assertEqual(['baseline','candidate']*3, [x['side'] for x in cells[:6]])
        self.assertEqual(['candidate','baseline']*3, [x['side'] for x in cells[6:]])
        self.assertEqual(12, len({x['id'] for x in cells}))

    def test_profile_is_strict_bounded_and_scheduler_aware(self):
        self.assertEqual(256, c.profile_check(ROOT/c.PROFILE)['max_kernel_etl_mb'])

    def test_profile_rejects_missing_scheduler_or_circular_overwrite(self):
        text = (ROOT/c.PROFILE).read_text()
        for bad in (text.replace('Value="ReadyThread" Strict="true"', 'Value="Timer" Strict="true"'),
                    text.replace('FileMode="Sequential"','FileMode="Circular"'), text.replace('Strict="true"','Strict="false"')):
            with tempfile.TemporaryDirectory() as d:
                p=Path(d)/'bad.wprp'; p.write_text(bad)
                with self.assertRaises(ValueError): c.profile_check(p)

    def test_original_launcher_unchanged(self):
        self.assertEqual(c.COLLECTOR_SHA, c.e.sha(c.e.canonical((ROOT/c.SOURCES[-1]).read_bytes())))

    def test_all_synthetic_call_phases_validated(self):
        for cell in c.cells():
            for row in cell['rows']: c.check_record(cell, row, record(cell, row))

    def test_call_errors_wrong_argv_clocks_or_claims_refused(self):
        cell=c.cells()[0]; row=cell['rows'][-1]
        for field,value in [('exit_code',True), ('exit_code',37), ('error','failed'),
                            ('argv',[]), ('root_times',{}), ('cleanup',{}), ('clears_hold',True),
                            ('executable_sha256','0'*64), ('clock',dict(frequency=0))]:
            r=record(cell,row); r[field]=value
            with self.subTest(field=field), self.assertRaises((ValueError,KeyError)): c.check_record(cell,row,r)

    def test_compact_status_is_content_not_line_count(self):
        cell=c.cells()[0]; row=cell['rows'][-1]
        for lines in (['[up-to-date] 3 translation units','[up-to-date] timing_bench.exe'],
                      ['[up-to-date] 2 translation units','[up-to-date] wrong.exe'],
                      ['[compile] main.cpp','[up-to-date] timing_bench.exe']):
            r=record(cell,row); r['output_lines']=lines
            with self.assertRaises(ValueError): c.check_record(cell,row,r)

    def test_timed_middle_requires_exact_noop_cache_outcome(self):
        cell=next(x for x in c.cells() if x['arm']=='middle_on'); row=cell['rows'][1]
        r=record(cell,row); r['output_lines'][-1]=r['output_lines'][-1].replace('"hits":2','"hits":1')
        with self.assertRaises(ValueError): c.check_record(cell,row,r)

    def test_bad_input_creates_no_output(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'out'
            with self.assertRaises((ValueError,FileNotFoundError)):
                c.prepare(Path(d)/'missing.zip',out,ROOT,'a'*40,c.e.sha(c.e.canonical((ROOT/c.PROFILE).read_bytes())))
            self.assertFalse(out.exists())

    def test_prepare_and_audit_synthetic_journal_never_claim_etl_health(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); archive=root/'synthetic.zip'; out=root/'out'
            payloads={s:('SYNTHETIC '+s).encode() for s in c.b.BINARIES}
            with ZipFile(archive,'w') as z:
                for s,data in payloads.items(): z.writestr(s+'/mqb.exe',data)
            hashes={s:c.b.digest(data) for s,data in payloads.items()}
            identity={'SYNTHETIC':True,'binaries':hashes}
            with patch.dict(c.b.BINARIES,hashes), patch.object(c.e,'verify_archive',return_value=identity):
                c.prepare(archive,out,ROOT,'a'*40,c.e.sha(c.e.canonical((ROOT/c.PROFILE).read_bytes())))
                manifest=[dict(path=n,size=len(data),mtime_ticks=1,sha256=c.b.digest(data)) for n,data in c.b.SOURCES.items()]
                after=manifest+[dict(path='.mqb/bin/timing_bench.exe',size=1,mtime_ticks=1,sha256='f'*64)]
                for cell in c.cells():
                    c.b.write_new(out/'cells'/(cell['id']+'.json'),dict(cell=cell,before=manifest,
                        after_prime=after,after_final=after,calls=[record(cell,r) for r in cell['rows']],error=None))
                completion=dict(status='captured_trace_unreviewed',attempted=32,error=None,
                                trace_stop_error=None,clears_hold=False,cause=None)
                c.b.write_new(out/'completion.json',completion)
                (out/'trace.etl').write_bytes(b'SYNTHETIC NOT AN ETL')
                result=c.audit(out)
                self.assertEqual('journal_complete_trace_unreviewed',result['status'])
                self.assertFalse(result['trace_health_verified']); self.assertFalse(result['clears_hold'])
                self.assertIsNone(result['cause'])
                (out/'cells'/'EXTRA.json').write_text('{}')
                with self.assertRaises(ValueError): c.audit(out)

    def test_runner_reuses_legacy_and_never_cancels_foreign_session(self):
        s=(ROOT/'tests/native/trace_noop_causal.ps1').read_text()
        self.assertIn('$matches[0].Extent.Text',s); self.assertIn('Invoke-LegacyBoundary $executable',s)
        self.assertIn('if ($owned)',s); self.assertNotIn("'-cancel'",s); self.assertNotIn('Remove-Item',s)
        self.assertIn('-instancename $instance',s); self.assertIn("'-skipPdbGen'",s)
        self.assertIn('if (-not $ExecuteReviewedTrace)',s); self.assertIn("$env:GITHUB_EVENT_NAME -ne 'workflow_dispatch'",s)
        self.assertIn('if ($attempted -ge 32',s)

    def test_ci_is_parse_only_no_trace_or_archive_execution(self):
        s=(ROOT/'.github/workflows/noop-causal-contracts.yml').read_text()
        self.assertNotIn('workflow_dispatch:',s)
        self.assertNotIn('-ExecuteReviewedTrace',s)
        self.assertNotIn("'-start'",s); self.assertNotIn('download-artifact',s)
        self.assertIn('-profiles $profile',s); self.assertIn('if: always()',s)

    def test_plan_cli_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'plan.json'
            args=[sys.executable,'-B',str(ROOT/'tests/native/noop_causal_trace.py'),'plan','--output',str(p)]
            self.assertEqual(0,subprocess.run(args,capture_output=True).returncode)
            before=p.read_bytes(); value=json.loads(before)
            self.assertFalse(value['execution_allocated'])
            self.assertEqual(2,subprocess.run(args,capture_output=True).returncode)
            self.assertEqual(before,p.read_bytes())


if __name__ == '__main__': unittest.main(verbosity=2)
