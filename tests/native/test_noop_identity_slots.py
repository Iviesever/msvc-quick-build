"""Synthetic journals/bytes only; no execution of an archived or native program."""
from contextlib import contextmanager
import copy
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import noop_identity_slots as d

ROOT = Path(__file__).resolve().parents[2]


def write(path, value):
    path.write_text(json.dumps(value), encoding='utf-8')


def call(row):
    start = row['sequence']*100000
    cold = row['phase'] == 'prime' and row['position'] == 0
    lines = ['[compile] main.cpp', '[compile] helper.cpp', '[link] timing_bench.exe'] if cold else [
        '[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe']
    return dict(row=row, argv=d.b.ARGV.copy(), executable_sha256=row['sha256'], dispatch_requested=True,
                may_clear_hold=False, error=None, exit_code=0, root_times=None, cleanup=None,
                output_format='powershell_merged_lines', output_lines=lines,
                clock=dict(frequency=1000, outer_start=start, native_start=start+2,
                           native_end=start+18, outer_end=start+20))


@contextmanager
def fixture():
    with tempfile.TemporaryDirectory() as temp:
        outer = Path(temp); root = outer/'evidence'; archive=outer/'fake.zip'; archive.write_bytes(b'SYNTHETIC')
        images={name: ('SYNTHETIC '+name).encode() for name in d.IMAGES}
        hashes={name:d.b.digest(value) for name,value in images.items()}
        # The original ZIP verifier is separately tested; this fixture explicitly does NOT impersonate it.
        with patch.dict(d.IMAGES,hashes), patch.object(d,'check_archive',return_value=(b'SYNTHETIC',images)):
            plan=d.prepare(archive,root,ROOT,'a'*40,'pr232-slot-001')
            write(root/'host.json',dict(plan_sha256=d.digest(root/'plan.json'),reviewed_commit='a'*40,
                                       allocation_label='pr232-slot-001',qpc_frequency=1000))
            previous=None
            for index,block in enumerate(plan['blocks']):
                loc=root/'fixtures'/block['id'];loc.mkdir()
                for name,raw in d.b.SOURCES.items(): (loc/name).write_bytes(raw)
                meta=lambda name,raw:dict(path=name,size=len(raw),mtime_ticks=1,sha256=d.b.digest(raw))
                before=[meta(n,v) for n,v in d.b.SOURCES.items()]
                (loc/'.mqb/bin').mkdir(parents=True)
                (loc/'.mqb/bin/timing_bench.exe').write_bytes(b'SYNTHETIC OUTPUT')
                after=before+[meta('.mqb/bin/timing_bench.exe',b'SYNTHETIC OUTPUT')]
                slots={s:dict(size=len(images[block['mapping'][s]]),mtime_ticks=index,creation_ticks=index,
                             sha256=hashes[block['mapping'][s]]) for s in ('L','R')}
                retired={s:None if previous is None else previous['slots_after'][s] for s in ('L','R')}
                for slot in ('L','R'):
                    path=root/'slots'/(slot+'.exe')
                    if index:
                        dest=root/'retired'/plan['blocks'][index-1]['id'];dest.mkdir(exist_ok=True)
                        path.rename(dest/(slot+'.exe'))
                    path.write_bytes(images[block['mapping'][slot]])
                calls=[]
                for row in block['rows']:
                    v=call(row);v['executable']=str(root/'slots'/(row['slot']+'.exe'));v['cwd']=str(loc);calls.append(v)
                record=dict(block=block,calls=calls,pending=None,error=None,close_errors=[],before=before,
                            after_prime=after,after_final=after,slots_before=slots,slots_after=slots,retired=retired,
                            budget_before=dict(free_bytes=2**33,evidence_bytes=100),
                            budget_after=dict(free_bytes=2**33,evidence_bytes=200))
                stem=root/'blocks'/block['id']
                write(Path(str(stem)+'.started.json'),dict(block=block,attempted_before=index*6,intent_only=True))
                write(Path(str(stem)+'.window.json'),dict(rows=block['rows'][2:],intent_only=True,
                      after_prime=after,slots=slots,primes=calls[:2]))
                write(Path(str(stem)+'.result.json'),record);previous=record
            write(root/'completion.json',dict(status='completed_diagnostic_unreviewed',attempted=48,error=None,
                                              close_errors=[],may_clear_hold=False))
            yield root,plan


class SlotContracts(unittest.TestCase):
    def test_exact_mappings_balance_and_reverse_order(self):
        p=d.plan();blocks=p['blocks'];rows=[r for x in blocks for r in x['rows']]
        self.assertEqual(['r1-AA','r1-AB','r1-BB','r1-BA','r2-BA','r2-BB','r2-AB','r2-AA'],[x['id'] for x in blocks])
        self.assertEqual(list(range(1,49)),[x['sequence'] for x in rows])
        for image in d.IMAGES:self.assertEqual(16,sum(r['image']==image and r['phase']=='measured' for r in rows))
        for slot in ('L','R'):self.assertEqual(16,sum(r['slot']==slot and r['phase']=='measured' for r in rows))
        for i,block in enumerate(blocks):
            self.assertEqual(list('LR'),[r['slot'] for r in block['rows'][:2]])
            self.assertEqual(list('LRRL' if i<4 else 'RLLR'),[r['slot'] for r in block['rows'][2:]])
        self.assertFalse(p['execution_allocated']);self.assertFalse(p['may_clear_hold']);self.assertFalse(p['gate_replacement'])

    def test_original_launcher_exact_pin_and_no_edits(self):
        self.assertEqual(d.LEGACY_SHA,d.b.digest(d.canonical((ROOT/d.LEGACY).read_bytes())))
        self.assertEqual(d.b.ARGV,['main.cpp','helper.cpp','--output','timing_bench','-j','1'])

    def test_all_call_types_and_unknown_os_fields(self):
        for block in d.plan()['blocks']:
            for row in block['rows']:
                result=d.check_call(row,call(row))
                self.assertIsNone(result['root_lifetime_ms']);self.assertIsNone(result['child_process_count'])

    def test_negative_exit_capture_and_proof_values(self):
        row=d.plan()['blocks'][0]['rows'][2]
        for key,value in [('exit_code',1),('exit_code',None),('exit_code',False),('exit_code','0'),
                          ('error','lost capture'),('root_times',{}),('cleanup',{}),('may_clear_hold',True),
                          ('dispatch_requested',False),('output_format','raw bytes'),('executable_sha256','0'*64)]:
            with self.subTest(key=key,value=value):
                v=call(row);v[key]=value
                with self.assertRaises(ValueError):d.check_call(row,v)

    def test_wrong_order_args_output_and_second_prime_work(self):
        for index in (1,2):
            row=d.plan()['blocks'][0]['rows'][index]
            for lines in ([],['[up-to-date] wrong'],['[compile] extra.cpp'],['[link] extra.exe'],
                          ['{"type":"mqb.timings"}']):
                with self.subTest(index=index,lines=lines):
                    v=call(row);v['output_lines']=lines
                    with self.assertRaises(ValueError):d.check_call(row,v)
        row=d.plan()['blocks'][0]['rows'][0];v=call(row);v['argv']=['--timings=json']
        with self.assertRaises(ValueError):d.check_call(row,v)

    def test_clock_boundaries_and_thirty_second_equality(self):
        row=d.plan()['blocks'][0]['rows'][2]
        for key,value in [('frequency',0),('frequency',True),('native_start',None),('native_end',-1),('outer_end',True)]:
            v=call(row);v['clock'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):d.check_call(row,v)
        v=call(row);v['clock']['outer_end']=v['clock']['outer_start']+30000
        d.check_call(row,v)
        v['clock']['outer_end']+=1
        with self.assertRaises(ValueError):d.check_call(row,v)

    def test_complete_synthetic_journal_retains_all_contrasts_without_gate(self):
        with fixture() as (root,_):
            result=d.audit(root)
            self.assertEqual(48,len(result['calls']));self.assertEqual(16,len(result['contrasts']))
            self.assertEqual('HOLD',result['original_decision']);self.assertFalse(result['may_clear_hold'])
            self.assertFalse(result['gate_replacement'])
            self.assertNotIn('winner',result);self.assertNotIn('adjusted_gate',result)

    def test_no_overwrite_or_resume(self):
        with fixture() as (root,_):
            original=(root/'plan.json').read_bytes()
            with self.assertRaises(ValueError):d.prepare(Path('unused'),root,ROOT,'a'*40,'pr232-slot-001')
            self.assertEqual(original,(root/'plan.json').read_bytes())

    def test_stopped_or_unknown_completion_cannot_be_complete(self):
        for key,value in [('status','stopped'),('attempted',3),('attempted',True),('error','failed'),
                          ('close_errors',['failed close']),('may_clear_hold',True)]:
            with self.subTest(key=key),fixture() as (root,_):
                p=root/'completion.json';v=d.b.load(p);v[key]=value;write(p,v)
                with self.assertRaises(ValueError):d.audit(root)

    def test_missing_extra_dangling_journal_refused(self):
        for name in ('blocks/r1-AA.window.json','blocks/r2-AA.result.json','retired/r1-AA/L.exe'):
            with self.subTest(name=name),fixture() as (root,_):
                (root/name).unlink()
                with self.assertRaises((ValueError,OSError)):d.audit(root)
        with fixture() as (root,_):
            write(root/'blocks/EXTRA.started.json',{})
            with self.assertRaises(ValueError):d.audit(root)

    def test_fixture_or_slot_mutation_refused(self):
        for name in ('fixtures/r1-AA/helper.cpp','slots/L.exe','retired/r1-AA/R.exe','inputs/baseline.exe',
                     'source/tests/native/noop_identity_slots_runtime.psm1'):
            with self.subTest(name=name),fixture() as (root,_):
                with (root/name).open('ab') as f:f.write(b'BAD')
                with self.assertRaises(ValueError):d.audit(root)

    def test_bad_plan_not_admitted(self):
        for key,value in [('prime_calls',15),('may_clear_hold',True),('execution_allocated',True),('images',{})]:
            with self.subTest(key=key),fixture() as (root,_):
                p=root/'plan.json';v=d.b.load(p);v[key]=value;write(p,v)
                with self.assertRaises(ValueError):d.audit(root)

    def test_wrong_call_path_or_clock_refused(self):
        for what in ('path','cwd','frequency','clock','record','retired','pending'):
            with self.subTest(what=what),fixture() as (root,_):
                p=root/'blocks/r1-AB.result.json';v=d.b.load(p)
                if what=='path':v['calls'][2]['executable']='foreign.exe'
                elif what=='cwd':v['calls'][2]['cwd']='foreign'
                elif what=='frequency':v['calls'][2]['clock']['frequency']=2000
                elif what=='clock':v['calls'][2]['clock']['outer_start']=0
                elif what=='record':v['calls'].pop()
                elif what=='pending':v['pending']={}
                else:v['retired']['L']['sha256']='0'*64
                write(p,v)
                with self.assertRaises(ValueError):d.audit(root)

    def test_post_prime_continuity_and_metadata_refused(self):
        for what in ('manifest','slot','budget'):
            with self.subTest(what=what),fixture() as (root,_):
                p=root/'blocks/r1-AA.result.json';v=d.b.load(p)
                if what=='manifest':v['after_final'][-1]['mtime_ticks']+=1
                elif what=='slot':v['slots_after']['L']['creation_ticks']+=1
                else:v['budget_after']['evidence_bytes']=268435457
                write(p,v)
                with self.assertRaises(ValueError):d.audit(root)

    def test_resource_integer_and_exact_bounds(self):
        d.limits(dict(free_bytes=4294967296,evidence_bytes=268435456))
        for value in [dict(free_bytes=4294967295,evidence_bytes=1),dict(free_bytes=2**33,evidence_bytes=True),
                      dict(free_bytes=2**33,evidence_bytes=268435457)]:
            with self.assertRaises(ValueError):d.limits(value)

    def test_wrong_archive_size_or_hash_before_any_output(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'bad.zip';p.write_bytes(b'not zip')
            with self.assertRaises(ValueError):d.check_archive(p)
            with patch.object(d,'ARCHIVE_BYTES',p.stat().st_size):
                with self.assertRaisesRegex(ValueError,'SHA256'):d.check_archive(p)
            out=Path(temp)/'out'
            with self.assertRaises(ValueError):d.prepare(p,out,ROOT,'a'*40,'pr232-slot-001')
            self.assertFalse(out.exists())

    def test_invalid_review_or_label_refused_without_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            for commit,label in [('HEAD','pr232-slot-001'),('a'*40,'reuse-819'),('a'*40,'')]:
                out=Path(temp)/'out'
                with self.assertRaises(ValueError):d.prepare(Path('unused'),out,ROOT,commit,label)
                self.assertFalse(out.exists())

    def test_duplicate_json_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'bad.json';p.write_text('{"exit_code":37,"exit_code":0}')
            with self.assertRaises(ValueError):d.b.load(p)

    def test_plan_cli_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'plan.json'
            args=[sys.executable,'-B',str(ROOT/'tests/native/noop_identity_slots.py'),'plan','--output',str(p)]
            self.assertEqual(0,subprocess.run(args,capture_output=True).returncode)
            before=p.read_bytes();self.assertEqual(2,subprocess.run(args,capture_output=True).returncode)
            self.assertEqual(before,p.read_bytes())

    def test_measured_loop_is_only_original_launcher_and_in_memory_checks(self):
        s=(ROOT/'tests/native/noop_identity_slots_runtime.psm1').read_text()
        loop=s.split("foreach ($row in @($Block.rows | Select-Object -Skip 2)) {")[1].split('}')[0].strip()
        self.assertEqual('Invoke-SlotRow $State $row $record $fixture',loop)
        body=s.split('function Invoke-SlotRow {')[1].split('\nfunction Invoke-SlotBlock')[0]
        for banned in ('Get-Digest','Get-FileManifest','Write-NewJson','Get-SlotBudget','Start-Process','wpr.exe','& python'):
            self.assertNotIn(banned,body)
        self.assertIn('& ($State.services.Launch) $State $path $Fixture ([string[]]$Row.argv)',body)

    def test_literal_io_helpers_have_exact_legacy_bodies(self):
        def body(text, name):
            start=text.index('function '+name+' {')
            tail=text[start:]
            endings=[p for p in (tail.find('\nfunction ',1),tail.find('\nExport-ModuleMember',1)) if p>=0]
            return tail[:min(endings)].strip() if endings else tail.strip()
        legacy=(ROOT/d.LEGACY).read_text()
        runtime=(ROOT/'tests/native/noop_identity_slots_runtime.psm1').read_text()
        for name in ('Write-NewJson','Get-Digest','Get-FileManifest'):
            self.assertEqual(body(legacy,name),body(runtime,name))

    def test_tests_cannot_import_or_default_to_native_launcher(self):
        runtime=(ROOT/'tests/native/noop_identity_slots_runtime.psm1').read_text()
        tests=(ROOT/'tests/native/test_noop_identity_slots_control.ps1').read_text()
        self.assertFalse((ROOT/'tests/native/run_noop_identity_slots.ps1').exists())
        self.assertFalse((ROOT/'tests/native/noop_identity_slots_capture.psm1').exists())
        self.assertFalse(d.plan()['native_entry_available'])
        for text in (runtime,tests):
            for forbidden in ('scriptblock]::Create','Invoke-Expression','Extent.Text','Import-SlotDefinitions'):
                self.assertNotIn(forbidden,text)
        for text in (runtime,tests):
            self.assertNotIn('noop_identity_slots_capture.psm1',text)
            self.assertNotIn('function Invoke-LegacyBoundary',text)
        self.assertIn('An explicit launcher dependency is required.',runtime)
        self.assertIn('${function:Invoke-SyntheticSlotLaunch}',tests)
        self.assertNotIn('function Get-Digest(',tests)
        self.assertNotIn('function Write-NewJson(',tests)
        self.assertIn('-or $tick -lt 0',runtime)
        for text in (runtime,tests):
            self.assertNotIn("Export-ModuleMember -Function '*'",text)

    def test_no_cleanup_or_qualification_trigger_added(self):
        s=(ROOT/'tests/native/noop_identity_slots_runtime.psm1').read_text()
        for banned in ('Remove-Item','::Delete(','.Kill(', 'SetPriority', 'ProcessorAffinity','SetLastWriteTime'):
            self.assertNotIn(banned,s)
        workflow=(ROOT/'.github/workflows/reporting-journal-correctness.yml').read_text()
        self.assertNotIn('-ExecuteReviewedDiagnostic',workflow)
        self.assertIn("'tests/native/test_*.py'",workflow)
        self.assertIn('if: always()',workflow)

    def test_powershell_orchestrator_with_in_process_fakes(self):
        executable=shutil.which('pwsh')
        if executable is None:
            self.skipTest('PowerShell 7 unavailable; native diagnostic not executed')
        # Reporting already owns and uploads journal-checks/. Preserve this first
        # control result there without changing ANY historical workflow contract.
        retained=ROOT/'journal-checks'
        with tempfile.TemporaryDirectory() as temp:
            out=(retained/'slot-controls') if os.environ.get('GITHUB_ACTIONS')=='true' and (retained/'identity.json').is_file() else Path(temp)/'slot-controls'
            out.mkdir(exist_ok=False)
            write(out/'plan.json',d.plan())
            args=[executable,'-NoProfile','-File',str(ROOT/'tests/native/test_noop_identity_slots_control.ps1'),
                  '-OutputRoot',str(out/'synthetic'),'-PlanPath',str(out/'plan.json')]
            with (out/'control.stdout').open('xb') as stdout,(out/'control.stderr').open('xb') as stderr:
                result=subprocess.run(args,stdout=stdout,stderr=stderr,timeout=90)
            self.assertEqual(0,result.returncode,(out/'control.stdout').read_text(errors='replace')+
                             (out/'control.stderr').read_text(errors='replace'))
            data=d.b.load(out/'synthetic/result.json')
            self.assertEqual(20,data['tests']);self.assertEqual(0,data['failures'])
            self.assertTrue(all(row['passed'] for row in data['cases']))
            for key in ('study_mqb_calls','native_launches','etw_sessions'):
                self.assertEqual(0,data[key])
            # Feed the first full fake run to the REAL auditor, rather than only
            # trusting a passed-case count. Only the intentionally synthetic archive
            # authentication is mocked; all clocks, paths, manifests and rows are real.
            evidence=out/'synthetic/case-01'
            hashes={name:d.digest(evidence/'inputs'/(name+'.exe')) for name in d.IMAGES}
            with patch.dict(d.IMAGES,hashes),patch.object(d,'check_archive',return_value=(b'SYNTHETIC',{})):
                sources={n:(ROOT/n).read_bytes() for n in d.SOURCE_FILES}
                for name,raw in sources.items():
                    dest=evidence/'source'/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw)
                p=dict(**d.plan(),root=str(evidence),reviewed_commit='a'*40,allocation_label='pr232-slot-001',
                       source_hashes={n:d.b.digest(raw) for n,raw in sources.items()})
                write(evidence/'plan.json',p)
                write(evidence/'host.json',dict(plan_sha256=d.digest(evidence/'plan.json'),reviewed_commit='a'*40,
                                               allocation_label='pr232-slot-001',qpc_frequency=1000))
                checked=d.audit(evidence)
                self.assertEqual(48,len(checked['calls']));self.assertFalse(checked['may_clear_hold'])
                write(out/'synthetic-orchestrator-audit.json',checked)



if __name__=='__main__': unittest.main(verbosity=2)
