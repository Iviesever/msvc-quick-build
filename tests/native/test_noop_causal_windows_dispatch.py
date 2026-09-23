"""Post-prime wrapper: synthetic admission, structure and verdict-function controls.

No Actions dispatch, trace entry, WPR command or archived executable is executed.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
import shutil
import subprocess
from test_noop_causal_dispatch import inline_blocks
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/noop-causal-windows-study.yml'
PROFILE_SHA = '7ef59ac788bfc554273d4a3ad028288e79fec428889be395765fd176c79406ec'


def valid_context():
    return dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build', GITHUB_EVENT_NAME='workflow_dispatch',
                GITHUB_REF='refs/heads/main', GITHUB_SHA='a'*40, GITHUB_WORKFLOW_SHA='a'*40,
                GITHUB_WORKFLOW_REF='Iviesever/msvc-quick-build/.github/workflows/noop-causal-windows-study.yml@refs/heads/main',
                GITHUB_RUN_ID='123456789', GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1',
                RUNNER_ENVIRONMENT='github-hosted', RUNNER_OS='Windows', RUNNER_ARCH='X64',
                REVIEWED_COMMIT='a'*40, PROFILE_SHA256=PROFILE_SHA, ALLOCATION='701-causal-windows-001')


def run_guard(context):
    """Return recorded allowlisted request and refusal, using a fresh synthetic directory."""
    guard, _ = inline_blocks(WORKFLOW.read_text(encoding='utf-8'))
    old = Path.cwd()
    with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, context, clear=True):
        try:
            os.chdir(tmp)
            error = None
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    exec(compile(guard, str(WORKFLOW), 'exec'), {})
            except SystemExit as exc:
                error = str(exc)
            request = json.loads(Path('causal-windows-execution-out/request.json').read_text(encoding='utf-8'))
            return request, error
        finally:
            os.chdir(old)


class WindowDispatchContracts(unittest.TestCase):
    def test_valid_synthetic_first_request(self):
        request, error = run_guard(valid_context())
        self.assertIsNone(error); self.assertEqual(request, valid_context())

    def test_every_required_context_field_is_required(self):
        for key in valid_context():
            c = valid_context(); del c[key]
            with self.subTest(key=key):
                request, error = run_guard(c)
                self.assertIsNotNone(error); self.assertIsNone(request[key])

    def test_automatic_events_and_wrong_repo_ref_or_workflow_rejected(self):
        for field, value in [('GITHUB_EVENT_NAME', 'push'), ('GITHUB_EVENT_NAME', 'pull_request'),
                             ('GITHUB_EVENT_NAME', 'schedule'), ('GITHUB_REPOSITORY', 'other/repo'),
                             ('GITHUB_REF', 'refs/heads/review'), ('GITHUB_WORKFLOW_REF', 'other.yml@refs/heads/main')]:
            c=valid_context(); c[field]=value
            with self.subTest(field=field, value=value): self.assertIsNotNone(run_guard(c)[1])

    def test_exact_commit_and_profile_identity_required(self):
        for field, value in [('REVIEWED_COMMIT', 'main'), ('REVIEWED_COMMIT', 'A'*40),
                             ('REVIEWED_COMMIT', 'b'*40), ('GITHUB_SHA', 'b'*40),
                             ('GITHUB_WORKFLOW_SHA', 'b'*40), ('PROFILE_SHA256', 'f'*64)]:
            c=valid_context(); c[field]=value
            with self.subTest(field=field): self.assertIsNotNone(run_guard(c)[1])

    def test_no_retry_or_next_allocation(self):
        for field, value in [('GITHUB_RUN_NUMBER', '2'), ('GITHUB_RUN_ATTEMPT', '2'),
                             ('GITHUB_RUN_NUMBER', '01'), ('GITHUB_RUN_ID', '0'),
                             ('GITHUB_RUN_ID', '-1'), ('ALLOCATION', '701-boundary-002'), ('ALLOCATION', '701-causal-001'),
                             ('ALLOCATION', '701-causal-windows-002')]:
            c=valid_context(); c[field]=value
            with self.subTest(field=field, value=value): self.assertIsNotNone(run_guard(c)[1])

    def test_no_self_hosted_or_non_windows_runner(self):
        for field,value in [('RUNNER_ENVIRONMENT','self-hosted'), ('RUNNER_OS','Linux'), ('RUNNER_ARCH','ARM64')]:
            c=valid_context(); c[field]=value
            with self.subTest(field=field): self.assertIsNotNone(run_guard(c)[1])

    def test_refusal_keeps_allowlisted_request_without_secrets(self):
        c=valid_context(); c.update(GITHUB_TOKEN='SYNTHETIC_SECRET', UNRELATED_SECRET='NOT_REAL', ALLOCATION='refused')
        request,error=run_guard(c)
        self.assertIsNotNone(error); self.assertEqual(set(valid_context()),set(request))
        self.assertNotIn('SYNTHETIC_SECRET',json.dumps(request)); self.assertEqual('refused',request['ALLOCATION'])

    def test_guard_never_overwrites_an_existing_request(self):
        guard,_=inline_blocks(WORKFLOW.read_text(encoding='utf-8')); old=Path.cwd()
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ,valid_context(),clear=True):
            try:
                os.chdir(tmp); exec(compile(guard,str(WORKFLOW),'exec'),{})
                before=Path('causal-windows-execution-out/request.json').read_bytes()
                with self.assertRaises(FileExistsError): exec(compile(guard,str(WORKFLOW),'exec'),{})
                self.assertEqual(before,Path('causal-windows-execution-out/request.json').read_bytes())
            finally: os.chdir(old)

    def test_only_manual_trigger_and_single_host_job(self):
        s=WORKFLOW.read_text(encoding='utf-8')
        trigger=s.split('\non:\n',1)[1].split('\npermissions:',1)[0]
        self.assertEqual(['workflow_dispatch'],re.findall(r'^  ([a-z_]+):',trigger,re.M))
        self.assertEqual(1,s.count('    runs-on: windows-latest'))
        self.assertNotIn('self-hosted',s); self.assertNotIn('strategy:',s)
        self.assertIn('  cancel-in-progress: false',s)
        self.assertEqual({'contents','actions'},set(re.findall(r'^  (\w+): read$',s,re.M)))
        self.assertIn('          persist-credentials: false',s)
        self.assertNotIn('secrets.',s)

    def test_fixed_original_input_and_owned_output_directory(self):
        s=WORKFLOW.read_text(encoding='utf-8')
        self.assertIn("artifact-ids: '10675079360'",s); self.assertIn("run-id: '35681224762'",s)
        self.assertIn('          skip-decompress: true',s); self.assertIn('          digest-mismatch: error',s)
        self.assertEqual(1,s.count('actions/download-artifact@'))
        self.assertIn('          path: causal-windows-execution-out/',s)
        self.assertIn('          include-hidden-files: true',s); self.assertIn('          overwrite: false',s)
        self.assertIn('        if: always()',s); self.assertIn('          if-no-files-found: error',s)
        self.assertNotIn('path: checkout\n          include-hidden-files',s)

    def test_one_entry_invocation_and_external_limits(self):
        s=WORKFLOW.read_text(encoding='utf-8'); _,invoke=inline_blocks(s)
        self.assertEqual(1,invoke.count('& ./checkout/tests/native/trace_noop_causal_windows.ps1 '))
        self.assertEqual(1,invoke.count('-ExecuteReviewedWindows'))
        self.assertIn('    timeout-minutes: 20',s); self.assertIn('        timeout-minutes: 15',s)
        self.assertIn('$free -lt 4GB',invoke); self.assertIn('$files[0].Length -ne 4880704',invoke)
        self.assertIn('940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3',invoke)
        self.assertNotIn('${{',invoke) # Untrusted input is data via environment, never script interpolation.
        for bad in ('wpr.exe -','-cancel','Remove-Item','Start-Process','run_retained_noop_study','continue-on-error'):
            self.assertNotIn(bad,s)

    def test_capture_failure_propagates_and_etl_health_stays_unknown(self):
        _,invoke=inline_blocks(WORKFLOW.read_text(encoding='utf-8'))
        for required in ('throw $original','execution-error.log','$LASTEXITCODE -ne 0',
                         'window_journal_complete_trace_unreviewed',"@('calls',32)","@('traced_calls',20)","@('windows',12)",
                         '$audit.trace_health_verified -isnot [bool]','$audit.clears_hold -isnot [bool]',
                         '$null -ne $audit.cause'):
            self.assertIn(required,invoke)
        self.assertNotIn('clears_hold=$true',invoke)

    def test_contract_ci_only_tests_and_parses_wrapper(self):
        s=(ROOT/'.github/workflows/noop-causal-contracts.yml').read_text(encoding='utf-8')
        self.assertIn("'.github/workflows/noop-causal-windows-study.yml'",s)
        self.assertIn('tests/native/test_noop_causal_windows_dispatch.py',s)
        self.assertIn('windows-dispatch-inline.ps1',s); self.assertNotIn('-ExecuteReviewedWindows',s)
        self.assertNotIn('workflow_dispatch:',s)

    def test_original_consumed_workflow_and_recorder_stay_unchanged(self):
        import hashlib
        for name, expected in {
            '.github/workflows/noop-causal-study.yml': 'efbca97f05fdb8cb5101f37a68384540115c7f9005f6c5fd61b46a0c4fee4b3e',
            'tests/native/trace_noop_causal_windows.ps1': 'dd73f3b6e563207c8bbe0967f01455c22b5df739bc9ebdc9f0cca31a01dc96b9',
        }.items():
            data=(ROOT/name).read_bytes().replace(b'\r\n', b'\n')
            self.assertEqual(expected,hashlib.sha256(data).hexdigest(),name)

    def test_new_protocol_keeps_history_and_call_accounting(self):
        s=WORKFLOW.read_text(encoding='utf-8')
        for item in ('previous_recorded_calls=72','maximum_new_mqb_calls=32',
                     'planned_windows=12','maximum_traced_mqb_calls=20','maximum_wpr_controls=60'):
            self.assertIn(item,s)
        self.assertNotIn('& ./checkout/tests/native/trace_noop_causal.ps1',s)
        self.assertNotIn('Start-Sleep',s)

    def test_script_injection_is_data_and_is_refused(self):
        for value in ('a'*40+"'; Write-Host BAD; '", '${{ github.token }}', '../main', 'a'*40+'\n'):
            c=valid_context(); c['REVIEWED_COMMIT']=value
            self.assertIsNotNone(run_guard(c)[1])

    @unittest.skipUnless(shutil.which('pwsh'), 'PowerShell 7 required for in-process verdict controls')
    def test_actual_powershell_verdict_with_synthetic_mutations(self):
        import copy
        _,invoke=inline_blocks(WORKFLOW.read_text(encoding='utf-8'))
        function='function Assert-WindowJournal('+invoke.split('function Assert-WindowJournal(',1)[1].split('\n}\n',1)[0]+'\n}'
        base=dict(status='window_journal_complete_trace_unreviewed',calls=32,traced_calls=20,
                  windows=12,trace_files=[dict(cell=str(i)) for i in range(12)],
                  trace_health_verified=False,clears_hold=False,cause=None)
        cases=[dict(name='complete synthetic journal',accept=True,journal=base)]
        for field in base:
            v=copy.deepcopy(base);del v[field]
            cases.append(dict(name='missing '+field,accept=False,journal=v))
        for field,value in [('status','journal_complete_trace_unreviewed'),('status','stopped'),
                            ('calls',30),('calls','32'),('calls',True),('calls',32.5),
                            ('traced_calls',32),('windows',11),('trace_files',[]),
                            ('trace_health_verified',True),('trace_health_verified','false'),
                            ('clears_hold',True),('clears_hold','false'),('cause','assumed noise')]:
            v=copy.deepcopy(base);v[field]=value
            cases.append(dict(name='wrong '+field+':'+str(value),accept=False,journal=v))
        harness="$ErrorActionPreference='Stop'\nSet-StrictMode -Version 2.0\n"+function+"""
$cases=Get-Content -LiteralPath $args[0] -Raw | ConvertFrom-Json
foreach ($case in $cases) {
    $accepted=$true
    try { Assert-WindowJournal $case.journal } catch { $accepted=$false }
    if ($accepted -ne $case.accept) { throw ('Unexpected verdict: '+$case.name) }
}
@{cases=@($cases).Count;native_wpr_invocations=0;study_mqb_calls=0} | ConvertTo-Json -Compress
"""
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'cases.json').write_text(json.dumps(cases),encoding='utf-8')
            (root/'test.ps1').write_text(harness,encoding='utf-8')
            p=subprocess.run([shutil.which('pwsh'),'-NoLogo','-NoProfile','-NonInteractive',
                              '-File',str(root/'test.ps1'),str(root/'cases.json')],
                             capture_output=True,text=True,timeout=30)
            self.assertEqual(0,p.returncode,p.stdout+'\n'+p.stderr)
            result=json.loads(p.stdout)
            self.assertEqual(dict(cases=len(cases),native_wpr_invocations=0,study_mqb_calls=0),result)


if __name__ == '__main__': unittest.main(verbosity=2)
