#!/usr/bin/env python3
"""Pure contract tests. Generated reports are synthetic, never benchmark scores."""
import copy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from zipfile import ZipFile

import check_external_noop_gate as gate

HERE = Path(__file__).resolve().parent
FIXTURE = HERE / 'fixtures/pr207-external-noop.json'
ORIGINAL = json.loads(FIXTURE.read_text(encoding='utf-8'))


def pairs(a=10, b=10):
    rows = copy.deepcopy(ORIGINAL['paired_samples'])
    aa = a if isinstance(a, list) else [a] * 4
    bb = b if isinstance(b, list) else [b] * 4
    for row, left, right in zip(rows, aa, bb):
        row['baseline_total_ms'] = row['baseline']['total_ms'] = left
        row['candidate_total_ms'] = row['candidate']['total_ms'] = right
    return rows


def report(external=None):
    """Synthetic complete shape; only original fixture row values are historical."""
    rows = pairs() if external is None else external
    value = dict(schema_version=3, pair_count=4, execution_order=gate.ORDER,
                 pairing='alternating baseline-candidate / candidate-baseline',
                 required_scenarios=list(gate.SCENARIOS), paired_samples=[], comparison=[])
    for name in gate.SCENARIOS:
        for row in copy.deepcopy(rows):
            row['scenario'] = name
            value['paired_samples'].append(row)
    return value


class NumericContract(unittest.TestCase):
    def test_historical_four_pairs_still_hold(self):
        result = gate.evaluate_pairs(ORIGINAL['paired_samples'])
        self.assertEqual(result['decision'], 'HOLD')
        self.assertEqual(Decimal(result['median_delta_ms']), Decimal('3.0963'))
        self.assertAlmostEqual(float(result['median_delta_pct']), 26.700241785250636, places=12)
        self.assertFalse(result['overall_acceptance_proven'])

    def test_both_conditions_are_strict(self):
        for a, b, hold in [(10,11,False),(20,22,False),(1,1.5,False),
                           (20,21.5,False),(10,12,True),(10,10,False),
                           (10,8,False),(10,0,False)]:
            with self.subTest(a=a,b=b):
                self.assertEqual(gate.evaluate_pairs(pairs(a,b))['crossed'],hold)

    def test_values_just_over_boundary_are_not_rounded(self):
        self.assertTrue(gate.evaluate_pairs(pairs(10,Decimal('11.00000000000000000001')))['crossed'])

    def test_exact_percent_equality_is_not_a_failure(self):
        # Differences >1, but EXACT median percentage is10, not float epsilon >10.
        self.assertFalse(gate.evaluate_pairs(pairs([30,30,30,30],[32,32,34,34]))['crossed'])

    def test_pair_medians_not_ratio_of_separate_medians(self):
        value = gate.evaluate_pairs(pairs([1,10,100,1000],[3,12,90,900]))
        self.assertEqual(Decimal(value['median_delta_ms']),Decimal('-4'))
        self.assertEqual(Decimal(value['median_delta_pct']),Decimal('5'))
        self.assertFalse(value['crossed'])

    def test_wrong_deltas_and_rounded_summaries_cannot_hide_hold(self):
        r = report(pairs(10,12));r['comparison']=[{'paired_median_total_delta_ms':-1000}]
        for row in r['paired_samples']:row['total_delta_ms']=-1000;row['total_delta_pct']=-1000
        self.assertTrue(gate.evaluate_report(r)['crossed'])

    def test_bad_numeric_values_fail_closed(self):
        for bad in (None,True,False,'12','abc',float('nan'),float('inf'),
                    Decimal('-Infinity'),Decimal('1e100001'),-1):
            with self.subTest(bad=str(bad)),self.assertRaises(ValueError):
                gate.evaluate_pairs(pairs(10,bad))
        for bad in (0,-1):
            with self.subTest(baseline=bad),self.assertRaises(ValueError):
                gate.evaluate_pairs(pairs(bad,1))

    def test_pair_ids_or_orientations_must_be_exact(self):
        for bad in (True,1.0,0,5,'1',None,2):
            r=pairs();r[0]['pair']=bad
            with self.subTest(pair=bad),self.assertRaises(ValueError):gate.evaluate_pairs(r)
        r=pairs();r[0]['orientation']='candidate-baseline'
        with self.assertRaises(ValueError):gate.evaluate_pairs(r)
        with self.assertRaises(ValueError):gate.evaluate_pairs(pairs()[:3])

    def test_raw_external_identity_and_unknowns(self):
        mutations=[('measurement_source','internal'),('total_ms',11),('iteration',2),
                   ('timing_schema_version',True),('scenario','no-op'),('counters',{}),
                   ('attribution',{}),('counter_breakdown',{}),('compile_hits',0)]
        for key,value in mutations:
            r=pairs();r[0]['candidate'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.evaluate_pairs(r)
        r=pairs();del r[0]['candidate']['counters']
        with self.assertRaises(ValueError):gate.evaluate_pairs(r)

    def test_grid_requires_all_registered_scenarios_and_unique_rows(self):
        r=report();self.assertEqual(gate.evaluate_report(r)['grid_rows_checked'],76)
        mutations=[]
        missing=copy.deepcopy(r);missing['paired_samples'].pop(0);mutations.append(missing)
        dup=copy.deepcopy(r);dup['paired_samples'][0]=dup['paired_samples'][1];mutations.append(dup)
        for k,v in [('pair_count',True),('pair_count',3),('schema_version',4),
                    ('execution_order',[]),('required_scenarios',[gate.SCENARIO]),
                    ('pairing','ABBA'),('paired_samples',[None]*76)]:
            m=copy.deepcopy(r);m[k]=v;mutations.append(m)
        for m in mutations:
            with self.subTest(keys=list(m)),self.assertRaises(ValueError):gate.evaluate_report(m)

    def test_shuffled_rows_preserve_pair_identity(self):
        r=report(pairs(10,12));r['paired_samples'].reverse()
        self.assertTrue(gate.evaluate_report(r)['crossed'])

    def test_json_duplicates_nonfinite_and_malformed_refused(self):
        for raw in (b'{"schema_version":3,"schema_version":3}',b'{"x":NaN}',
                    b'{"x":Infinity}',b'{',b'\xff'):
            with self.subTest(raw=raw),self.assertRaises(ValueError):gate.strict_json(raw)


class CommandContract(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def invoke(self, path, output, *args):
        return subprocess.run([sys.executable,str(HERE/'check_external_noop_gate.py'),str(path),
            '--output',str(output),*args],capture_output=True,text=True,check=False)
    def save(self,value,name='comparison.json'):
        p=self.root/name;p.write_text(json.dumps(value),encoding='utf-8');return p

    def test_cli_hold_is_nonzero_and_keeps_complete_input(self):
        p=self.save(report(ORIGINAL['paired_samples']));before=p.read_bytes();out=self.root/'decision.json'
        ran=self.invoke(p,out)
        self.assertEqual(ran.returncode,1,ran.stderr)
        self.assertEqual(json.loads(out.read_text())['decision'],'HOLD')
        self.assertEqual(p.read_bytes(),before)

    def test_cli_pass_is_only_threshold_not_crossed(self):
        p=self.save(report());out=self.root/'decision.json';ran=self.invoke(p,out)
        self.assertEqual(ran.returncode,0,ran.stderr)
        self.assertFalse(json.loads(out.read_text())['overall_acceptance_proven'])

    def test_missing_and_partial_inputs_are_invalid_not_skipped(self):
        for label,p in [('missing',self.root/'absent.json'),('partial',self.save({'schema_version':3})),
                        ('corrupt',self.root/'bad.json')]:
            if label=='corrupt':p.write_bytes(b'{')
            out=self.root/(label+'.decision.json');ran=self.invoke(p,out)
            self.assertEqual(ran.returncode,2,ran.stderr)
            self.assertEqual(json.loads(out.read_text())['decision'],'INVALID')

    def test_output_never_overwrites_inputs_or_an_earlier_decision(self):
        p=self.save(report());before=p.read_bytes()
        self.assertEqual(self.invoke(p,p).returncode,2);self.assertEqual(p.read_bytes(),before)
        out=self.root/'old.json';out.write_bytes(b'original evidence')
        self.assertEqual(self.invoke(p,out).returncode,2);self.assertEqual(out.read_bytes(),b'original evidence')

    def test_archive_replay_is_pinned_and_executes_no_binaries(self):
        p=self.root/'synthetic.zip'
        with ZipFile(p,'w') as z:
            z.writestr('benchmark-comparison.json',json.dumps(report(pairs(10,12))))
            z.writestr('candidate/mqb.exe',b'NOT AN EXECUTABLE: do not execute archive payloads')
        before=p.read_bytes();digest=hashlib.sha256(before).hexdigest()
        out=self.root/'hold.json';ran=self.invoke(p,out,'--artifact-sha256',digest)
        self.assertEqual(ran.returncode,1,ran.stderr)
        self.assertFalse(json.loads(out.read_text())['archived_tools_executed'])
        self.assertEqual(p.read_bytes(),before)
        self.assertEqual(self.invoke(p,self.root/'mismatch.json','--artifact-sha256','0'*64).returncode,2)

    def test_duplicate_or_missing_archived_report_refused(self):
        for label,names in [('missing',['other.json']),('ambiguous',['benchmark-comparison.json','x/benchmark-comparison.json'])]:
            p=self.root/(label+'.zip')
            with ZipFile(p,'w') as z:
                for n in names:z.writestr(n,json.dumps(report()))
            with self.subTest(label=label):
                self.assertEqual(self.invoke(p,self.root/(label+'.json'),'--artifact-sha256',
                    hashlib.sha256(p.read_bytes()).hexdigest()).returncode,2)

    def test_fixed_manifest_matches_existing_collector(self):
        src=(HERE/'compare_mqb_benchmarks.ps1').read_text()
        block=src.split('$RequiredScenarios = @(',1)[1].split('\n)',1)[0]
        import re
        self.assertEqual(re.findall(r"'([^']+)'",block),list(gate.SCENARIOS))


if __name__=='__main__':unittest.main(verbosity=2)
