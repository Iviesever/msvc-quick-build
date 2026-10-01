"""Restricted entry contracts; no original MQB/Windows diagnostic is executed."""
from contextlib import contextmanager
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import noop_identity_slots as d
import noop_identity_slots_workflow_contract as workflow_contract
from test_noop_identity_slots import fixture, write

ROOT = Path(__file__).resolve().parents[2]


@contextmanager
def prepared():
    with tempfile.TemporaryDirectory() as temp:
        outer=Path(temp); root=outer/'evidence'; archive=outer/'fake.zip'; archive.write_bytes(b'SYNTHETIC')
        images={n: ('SYNTHETIC '+n).encode() for n in d.IMAGES}
        hashes={n:d.b.digest(v) for n,v in images.items()}
        with patch.dict(d.IMAGES,hashes),patch.object(d,'check_archive',return_value=(b'SYNTHETIC',images)):
            p=d.prepare(archive,root,ROOT,'a'*40,'pr232-slot-001')
            yield root,p


class NativeEntryContracts(unittest.TestCase):
    def test_plan_declares_real_entry_but_never_allocates(self):
        p=d.plan()
        self.assertEqual(2,p['schema']);self.assertTrue(p['native_entry_available'])
        self.assertFalse(p['execution_allocated']);self.assertFalse(p['may_clear_hold'])
        self.assertEqual(5,len(d.SOURCE_FILES))
        for name in d.SOURCE_FILES:self.assertTrue((ROOT/name).is_file())

    def test_preflight_binds_all_native_sources_without_executing(self):
        with prepared() as (root,p):
            out=d.native_preflight(root,ROOT,'a'*40,'pr232-slot-001')
            self.assertEqual(p['source_hashes'],out['source_hashes'])
            self.assertEqual(0,out['mqb_calls']);self.assertFalse(out['may_clear_hold'])
            self.assertFalse((root/'completion.json').exists())

    def test_preflight_rejects_changed_plan_schedule(self):
        for field in ('sha256','slot','argv'):
            with self.subTest(field=field),prepared() as (root,p):
                p['blocks'][0]['rows'][0][field]='changed'
                write(root/'plan.json',p)
                with self.assertRaisesRegex(ValueError,'fixed plan changed'):d.native_preflight(root,ROOT,'a'*40,'pr232-slot-001')

    def test_preflight_rejects_wrong_root_review_and_allocation(self):
        for key,value in [('root','foreign'),('reviewed_commit','b'*40),('allocation_label','pr232-slot-002')]:
            with self.subTest(key=key),prepared() as (root,p):
                p[key]=value;write(root/'plan.json',p)
                with self.assertRaisesRegex(ValueError,'native request differs'):d.native_preflight(root,ROOT,'a'*40,'pr232-slot-001')

    def test_preflight_rejects_mutated_entry_or_images(self):
        for name in ['source/tests/native/run_noop_identity_slots.ps1','source/tests/native/noop_identity_slots_capture.psm1','inputs/candidate.exe']:
            with self.subTest(name=name),prepared() as (root,_):
                (root/name).write_bytes(b'CHANGED')
                with self.assertRaises(ValueError):d.native_preflight(root,ROOT,'a'*40,'pr232-slot-001')

    def test_preflight_rejects_any_preexisting_study_state(self):
        for name in ('blocks','fixtures','retired','slots'):
            with self.subTest(name=name),prepared() as (root,_):
                (root/name/'foreign').write_bytes(b'KEEP')
                with self.assertRaisesRegex(ValueError,'not fresh'):d.native_preflight(root,ROOT,'a'*40,'pr232-slot-001')
                self.assertEqual(b'KEEP',(root/name/'foreign').read_bytes())

    def test_native_audit_requires_entry_provenance(self):
        with fixture() as (root,_):
            with self.assertRaises((KeyError,OSError,ValueError)):d.audit_native(root)

    def test_synthetic_native_provenance_is_not_remote_approval(self):
        with fixture() as (root,p):
            host=d.b.load(root/'host.json')
            host.update(native_entry='run_noop_identity_slots.ps1',protocol='pinned_819_merged_lines_v1',
                        repository='Iviesever/msvc-quick-build',event='workflow_dispatch',run_attempt='1',run_id='123',
                        execution_allocated_by_plan=False,may_clear_hold=False)
            write(root/'host.json',host)
            write(root/'native-preflight.json',dict(status='prepared_inputs_verified_not_execution_authority',
                  reviewed_commit=p['reviewed_commit'],allocation_label=p['allocation_label'],
                  plan_sha256=d.digest(root/'plan.json'),source_hashes=p['source_hashes'],mqb_calls=0,may_clear_hold=False))
            result=d.audit_native(root)
            self.assertFalse(result['native_provenance']['remote_allocation_verified'])
            self.assertEqual('HOLD',result['original_decision'])
            for key,value in [('run_attempt','2'),('event','pull_request'),('run_id',True),('may_clear_hold',True)]:
                bad=copy.deepcopy(host);bad[key]=value;write(root/'host.json',bad)
                with self.subTest(key=key),self.assertRaises(ValueError):d.audit_native(root)

    def test_no_automatic_measurement_workflow_or_shell_evaluation(self):
        entry=(ROOT/'tests/native/run_noop_identity_slots.ps1').read_text()
        capture=(ROOT/'tests/native/noop_identity_slots_capture.psm1').read_text()
        for text in (entry,capture):
            for banned in ('Invoke-Expression','scriptblock]::Create','EncodedCommand','DllImport','Remove-Item','.Kill('):
                self.assertNotIn(banned,text)
        for path in (ROOT/'.github/workflows').glob('*.yml'):
            if path.relative_to(ROOT).as_posix() == workflow_contract.WORKFLOW:
                self.assertEqual(workflow_contract.WORKFLOW_SHA256,
                    d.b.digest(path.read_text(encoding='utf-8').encode()))
            else:
                self.assertNotIn('run_noop_identity_slots.ps1',path.read_text())
        self.assertLess(entry.index('Assert-SlotAdmission'),entry.index('& $python -B $tool prepare'))
        self.assertIn('Export-ModuleMember -Function @(\'Invoke-PinnedSlotCapture\')',capture)

    def test_powershell_capture_with_real_harmless_children(self):
        pwsh=shutil.which('pwsh')
        if pwsh is None:self.skipTest('PowerShell unavailable; real capture contracts not run')
        retained=ROOT/'journal-checks'
        with tempfile.TemporaryDirectory() as temp:
            out=(retained/'slot-native-controls') if os.environ.get('GITHUB_ACTIONS')=='true' and (retained/'identity.json').is_file() else Path(temp)/'controls'
            out.mkdir(exist_ok=False)
            write(out/'plan.json',d.plan())
            args=[pwsh,'-NoProfile','-File',str(ROOT/'tests/native/test_noop_identity_slots_capture.ps1'),
                  '-OutputRoot',str(out/'native'),'-PlanPath',str(out/'plan.json')]
            with (out/'control.stdout').open('xb') as stdout,(out/'control.stderr').open('xb') as stderr:
                run=subprocess.run(args,stdout=stdout,stderr=stderr,timeout=45)
            self.assertEqual(0,run.returncode,(out/'control.stdout').read_text(errors='replace')+(out/'control.stderr').read_text(errors='replace'))
            result=d.b.load(out/'native/result.json')
            self.assertEqual(11,result['tests']);self.assertEqual(0,result['failures'])
            self.assertTrue(all(c['passed'] for c in result['cases']))
            self.assertEqual(5,result['harmless_child_launches'])
            for key in ('study_mqb_calls','msvc_calls','etw_sessions'):self.assertEqual(0,result[key])
            self.assertEqual(37,d.b.load(out/'native/nonzero.json')['exit_code'])
            self.assertEqual(128,len(d.b.load(out/'native/many.json')['output_lines']))


if __name__=='__main__':unittest.main(verbosity=2)
