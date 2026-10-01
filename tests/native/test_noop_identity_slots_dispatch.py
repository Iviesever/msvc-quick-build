"""Execute actual workflow Python with fake platform/process dependencies only.

No original MQB, MSVC, workflow dispatch or performance measurement is executed.
"""
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import noop_identity_slots as slots
from test_noop_causal_dispatch import inline_blocks
import noop_identity_slots_workflow_contract as contract

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / contract.WORKFLOW
GUARD, INVOKE = inline_blocks(WORKFLOW.read_text(encoding='utf-8'))


def valid_context(temp):
    return dict(GITHUB_ACTIONS='true', GITHUB_REPOSITORY='Iviesever/msvc-quick-build',
        GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main', GITHUB_SHA='a'*40,
        GITHUB_WORKFLOW_SHA='a'*40,
        GITHUB_WORKFLOW_REF='Iviesever/msvc-quick-build/'+contract.WORKFLOW+'@refs/heads/main',
        GITHUB_RUN_ID='123', GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1',
        RUNNER_ENVIRONMENT='github-hosted', RUNNER_OS='Windows', RUNNER_ARCH='X64',
        RUNNER_TEMP=str(temp), REVIEWED_COMMIT='a'*40, ALLOCATION='pr232-slot-001', EXECUTE_REVIEWED='true')


@contextlib.contextmanager
def workspace():
    previous = Path.cwd()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp); runner = root/'runner'; runner.mkdir()
        context = valid_context(runner)
        try:
            os.chdir(root)
            with patch.dict(os.environ, context, clear=True):
                yield root, context
        finally:
            os.chdir(previous)


def execute(block):
    # Interpret exactly the authored workflow body, not a substitute implementation.
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile(block, str(WORKFLOW), 'exec'), {})


def guard_case(change=None, missing=None):
    with workspace() as (root, context):
        if missing is not None: del os.environ[missing]
        os.environ.update(change or {})
        error = None
        try: execute(GUARD)
        except SystemExit as exc: error = str(exc)
        out = root/'slot-execution-out'
        return json.loads((out/'request.json').read_text()), error, (out/'admission.json').exists()


def invoke_case(*, exit_code=0, source='a'*40, dirty='', download_count=1,
                archive_error=None, launch_error=None, audit_error=None,
                saved_mismatch=False, wrong_provenance=False, missing_pwsh=False,
                save_error=False, mutate_context=False, existing_intent=False):
    """Mock only external archive/process seams; exercise real wrapper IO and control flow."""
    with workspace() as (root, context):
        execute(GUARD)
        out = root/'slot-execution-out'; download = out/'download'; download.mkdir()
        for i in range(download_count): (download/f'{i}.zip').write_bytes(b'SYNTHETIC ORIGINAL')
        repo = root/'checkout'; workflow = repo/contract.WORKFLOW
        workflow.parent.mkdir(parents=True); workflow.write_bytes(WORKFLOW.read_bytes())
        (repo/'tests/native').mkdir(parents=True)
        study = Path(context['RUNNER_TEMP'])/'pr232-slot-001-123-1'
        calls = []
        good = dict(status='complete_diagnostic_uninterpreted', original_decision='HOLD',
            may_clear_hold=False, gate_replacement=False, calls=[], contrasts=[],
            native_provenance=dict(run_id='123', reviewed_commit='a'*40, remote_allocation_verified=False))
        # This is a synthetic auditor return, not an authentic native audit fixture.
        if wrong_provenance: good['native_provenance']['run_id'] = '124'
        def run(argv, **kwargs):
            if argv[0].endswith('git'):
                if argv[-3:] != ['archive', '--format=zip', 'HEAD']:
                    raise AssertionError('Unexpected Git command')
                kwargs['stdout'].write(b'SYNTHETIC SOURCE ARCHIVE')
                return subprocess.CompletedProcess(argv, 0)
            calls.append((argv, kwargs))
            study.mkdir()
            (study/'partial.json').write_text('{"intent_only":true}', encoding='utf-8')
            kwargs['stdout'].write(b'RETAINED STDOUT')
            kwargs['stderr'].write(b'RETAINED STDERR')
            if launch_error: raise launch_error
            saved = copy.deepcopy(good)
            if saved_mismatch: saved['original_decision'] = 'WRONG'
            (study/'audit.json').write_text(json.dumps(saved), encoding='utf-8')
            return subprocess.CompletedProcess(argv, exit_code)
        def git(argv, **kwargs):
            if argv[-2:] == ['rev-parse', 'HEAD']: return source+'\n'
            if argv[-3:] == ['status', '--porcelain', '--untracked-files=no']: return dirty
            raise AssertionError('Unexpected Git query')
        original_open = Path.open
        def opening(path, *args, **kwargs):
            if save_error and path.name == 'wrapper-result.json': raise OSError('SYNTHETIC journal failure')
            return original_open(path, *args, **kwargs)
        if existing_intent: (out/'entry-intent.json').write_bytes(b'KEEP')
        if mutate_context: os.environ['GITHUB_RUN_ATTEMPT'] = '2'
        error = None; stderr = io.StringIO()
        with patch('subprocess.run', side_effect=run), patch('subprocess.check_output', side_effect=git), \
             patch('shutil.which', side_effect=lambda name: None if missing_pwsh and name == 'pwsh' else '/synthetic/'+name), \
             patch.object(slots, 'check_archive', side_effect=archive_error) as checked, \
             patch.object(slots, 'audit_native', side_effect=audit_error, return_value=good), \
             patch.object(Path, 'open', opening), contextlib.redirect_stderr(stderr):
            try: execute(INVOKE)
            except BaseException as exc: error = exc
        files = {p.relative_to(out).as_posix():p.read_bytes() for p in out.rglob('*') if p.is_file()}
        partial = (study/'partial.json').is_file()
        result = json.loads(files['wrapper-result.json']) if 'wrapper-result.json' in files else None
        return dict(calls=calls, error=error, result=result, files=files, partial=partial,
                    archive_checks=checked.call_count, stderr=stderr.getvalue())


class DispatchAdmission(unittest.TestCase):
    def test_valid_synthetic_request_is_not_remote_authorization(self):
        request, error, admitted = guard_case()
        self.assertIsNone(error); self.assertTrue(admitted)
        self.assertEqual('pr232-slot-001', request['ALLOCATION'])
        self.assertIn('remote_allocation_verified=False', GUARD)

    def test_every_platform_and_input_field_is_required(self):
        for key in valid_context(Path('/synthetic')):
            with self.subTest(key=key):
                request, error, admitted = guard_case(missing=key)
                self.assertIsNone(request[key]); self.assertIsNotNone(error); self.assertFalse(admitted)

    def test_false_execute_automatic_events_and_other_hosts_are_refused(self):
        cases = [('EXECUTE_REVIEWED', s) for s in ('false', '', '1', 'True')]
        cases += [('GITHUB_EVENT_NAME', s) for s in ('push', 'pull_request', 'schedule', 'workflow_run')]
        cases += [('RUNNER_ENVIRONMENT','self-hosted'), ('RUNNER_OS','Linux'), ('RUNNER_ARCH','ARM64'),
                  ('GITHUB_REPOSITORY','other/repo'), ('GITHUB_REF','refs/heads/other'),
                  ('GITHUB_WORKFLOW_REF','other.yml@refs/heads/main'), ('GITHUB_ACTIONS','false')]
        for key, value in cases:
            with self.subTest(key=key, value=value):
                _, error, admitted = guard_case({key:value})
                self.assertIsNotNone(error); self.assertFalse(admitted)

    def test_repeated_run_attempt_or_different_allocation_is_refused(self):
        for key, value in [('GITHUB_RUN_NUMBER','2'), ('GITHUB_RUN_NUMBER','01'),
                           ('GITHUB_RUN_ATTEMPT','2'), ('GITHUB_RUN_ID','0'), ('GITHUB_RUN_ID','1\n'),
                           ('ALLOCATION','pr232-slot-002'), ('ALLOCATION','pr232-slot-000')]:
            with self.subTest(key=key, value=value): self.assertIsNotNone(guard_case({key:value})[1])

    def test_exact_source_and_noninterpolated_inputs(self):
        for key, value in [('GITHUB_SHA','b'*40), ('GITHUB_WORKFLOW_SHA','b'*40),
                           ('REVIEWED_COMMIT','main'), ('REVIEWED_COMMIT','A'*40),
                           ('REVIEWED_COMMIT','a'*40+'\n'), ('REVIEWED_COMMIT',"'; Write-Host BAD"),
                           ('RUNNER_TEMP','../unowned')]:
            with self.subTest(key=key): self.assertIsNotNone(guard_case({key:value})[1])
        self.assertNotIn('${{', GUARD); self.assertNotIn('${{', INVOKE)

    def test_allowlisted_record_omits_credentials(self):
        request, _, _ = guard_case({'GH_TOKEN':'SYNTHETIC_SECRET', 'UNRELATED_SECRET':'SYNTHETIC_SECRET'})
        self.assertNotIn('SYNTHETIC_SECRET', json.dumps(request))

    def test_existing_request_and_study_are_never_overwritten(self):
        with workspace() as (root, context):
            execute(GUARD); before = (root/'slot-execution-out/request.json').read_bytes()
            with self.assertRaises(FileExistsError): execute(GUARD)
            self.assertEqual(before, (root/'slot-execution-out/request.json').read_bytes())
        with workspace() as (root, context):
            study = Path(context['RUNNER_TEMP'])/'pr232-slot-001-123-1'; study.mkdir()
            (study/'keep').write_bytes(b'KEEP')
            with self.assertRaisesRegex(SystemExit, 'existing study'): execute(GUARD)
            self.assertEqual(b'KEEP', (study/'keep').read_bytes())


class DispatchInvocation(unittest.TestCase):
    def test_single_existing_entry_and_fixed_arguments(self):
        value = invoke_case()
        self.assertIsNone(value['error']); self.assertEqual(1, len(value['calls']))
        argv, kw = value['calls'][0]
        self.assertEqual(1, argv.count('-ExecuteReviewedDiagnostic'))
        self.assertEqual(('tests','native','run_noop_identity_slots.ps1'), Path(argv[5]).parts[-3:])
        self.assertEqual('pr232-slot-001', argv[argv.index('-AllocationLabel')+1])
        self.assertEqual('1', kw['env']['MQB_SLOT_DISPOSABLE']); self.assertFalse(kw['check'])
        self.assertNotIn('shell', kw); self.assertEqual(1, value['archive_checks'])
        self.assertEqual('diagnostic_recorded_unreviewed', value['result']['status'])
        self.assertFalse(value['result']['may_clear_hold'])
        self.assertEqual('HOLD', value['result']['original_decision'])

    def test_nonzero_exit_preserves_prefix_and_fails_without_retry(self):
        value = invoke_case(exit_code=37)
        self.assertIsInstance(value['error'], RuntimeError); self.assertEqual(1, len(value['calls']))
        self.assertEqual(37, value['result']['entry_exit_code']); self.assertTrue(value['partial'])
        self.assertEqual(b'RETAINED STDOUT', value['files']['entry.stdout'])
        self.assertEqual(b'RETAINED STDERR', value['files']['entry.stderr'])
        self.assertNotIn('audit-readback.json', value['files'])

    def test_launch_error_retains_unknown_exit_and_intent(self):
        value = invoke_case(launch_error=OSError('SYNTHETIC launch error'))
        self.assertIsInstance(value['error'], OSError); self.assertIsNone(value['result']['entry_exit_code'])
        self.assertEqual(1, len(value['calls'])); self.assertIn('entry-intent.json', value['files'])

    def test_zero_exit_does_not_accept_incomplete_native_audit(self):
        value = invoke_case(audit_error=ValueError('incomplete/stopped diagnostic'))
        self.assertIsInstance(value['error'], ValueError); self.assertEqual(0, value['result']['entry_exit_code'])
        self.assertEqual('failed_or_incomplete', value['result']['status'])

    def test_changed_saved_audit_and_wrong_run_provenance_are_rejected(self):
        for options in (dict(saved_mismatch=True), dict(wrong_provenance=True)):
            with self.subTest(options=options):
                value = invoke_case(**options)
                self.assertIsInstance(value['error'], ValueError); self.assertTrue(value['partial'])

    def test_preparation_failures_never_request_the_entry(self):
        for options in (dict(source='b'*40), dict(dirty=' M changed'), dict(download_count=0),
                        dict(download_count=2), dict(archive_error=ValueError('wrong archive SHA')),
                        dict(missing_pwsh=True), dict(mutate_context=True)):
            with self.subTest(options=str(options)):
                value = invoke_case(**options)
                self.assertIsNotNone(value['error']); self.assertEqual([], value['calls'])
                self.assertFalse(value['result']['entry_requested'])

    def test_no_overwrite_or_retry_after_existing_intent(self):
        value = invoke_case(existing_intent=True)
        self.assertIsInstance(value['error'], FileExistsError); self.assertEqual([], value['calls'])
        self.assertEqual(b'KEEP', value['files']['entry-intent.json'])

    def test_secondary_journal_failure_does_not_mask_primary_failure(self):
        value = invoke_case(exit_code=37, save_error=True)
        self.assertIsInstance(value['error'], RuntimeError)
        self.assertIn('exit code 37', str(value['error'])); self.assertIn('Could not save', value['stderr'])
        self.assertEqual(1, len(value['calls'])); self.assertTrue(value['partial'])

    def test_outcome_write_failure_is_not_reported_as_success(self):
        value = invoke_case(save_error=True)
        self.assertIsInstance(value['error'], OSError); self.assertEqual(1, len(value['calls']))


class WorkflowStructure(unittest.TestCase):
    def test_manual_only_minimum_permissions_and_no_retries(self):
        text = WORKFLOW.read_text()
        triggers = text.split('\non:\n')[1].split('\npermissions:')[0]
        self.assertEqual(['workflow_dispatch'], re.findall(r'^  ([a-z_]+):', triggers, re.M))
        self.assertIn('        default: false', triggers)
        self.assertEqual({'contents','actions'}, set(re.findall(r'^  (\w+): read$',text,re.M)))
        self.assertIn('  cancel-in-progress: false', text); self.assertNotIn('secrets.', text)
        self.assertEqual(1, text.count('    runs-on: windows-latest'))
        for bad in ('strategy:', 'continue-on-error', 'Start-Sleep', 'Remove-Item', 'shell=True'):
            self.assertNotIn(bad, text)

    def test_original_artifact_no_unpack_and_pinned_actions(self):
        text = WORKFLOW.read_text()
        self.assertIn("artifact-ids: '11083055313'", text)
        self.assertIn("run-id: '36682255779'", text)
        self.assertIn('          skip-decompress: true', text)
        self.assertIn('          digest-mismatch: error', text)
        self.assertEqual(3, len(re.findall(r'uses: actions/[a-z-]+@[0-9a-f]{40}', text)))
        self.assertIn('          persist-credentials: false', text)
        self.assertIn('slots.check_archive(files[0])', INVOKE)

    def test_failure_upload_covers_only_request_and_fixed_study(self):
        text = WORKFLOW.read_text(); upload = text.split('      - name: Preserve this request')[1]
        self.assertIn('        if: always()', upload)
        self.assertIn('            slot-execution-out/', upload)
        self.assertIn('            ${{ runner.temp }}/pr232-slot-001-${{ github.run_id }}-1/', upload)
        self.assertIn('          overwrite: false', upload)
        self.assertIn('          include-hidden-files: true', upload)
        self.assertIn('          if-no-files-found: error', upload)
        self.assertNotIn('inputs.', upload); self.assertNotIn('checkout/', upload)
        self.assertIn('    timeout-minutes: 20', text); self.assertIn('        timeout-minutes: 15', text)

    def test_unchanged_five_entry_dependencies_and_legacy_workflows(self):
        values = {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_text(encoding='utf-8').encode()).hexdigest()
                  for p in (ROOT/'.github/workflows').glob('*') if p.is_file()}
        old = contract.legacy_workflow_view(values)
        self.assertEqual(50,len(old)); self.assertEqual(51,len(values))
        for path, digest in contract.ENTRY_PINS.items():
            self.assertEqual(digest, hashlib.sha256((ROOT/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest(), path)

    def test_workflow_extension_does_not_hide_missing_changed_or_extra_workflows(self):
        values = {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_text(encoding='utf-8').encode()).hexdigest()
                  for p in (ROOT/'.github/workflows').glob('*') if p.is_file()}
        for name in (contract.WORKFLOW, '.github/workflows/native-ci.yml'):
            for mutation in ('change','delete'):
                bad = values.copy()
                if mutation == 'change': bad[name]='f'*64
                else: del bad[name]
                with self.subTest(name=name,mutation=mutation),self.assertRaises(ValueError):contract.legacy_workflow_view(bad)
        bad=values.copy();bad['.github/workflows/foreign.yml']='f'*64
        with self.assertRaises(ValueError):contract.legacy_workflow_view(bad)

    def test_contract_ci_discovers_tests_without_dispatching_study(self):
        text=(ROOT/'.github/workflows/reporting-journal-correctness.yml').read_text()
        self.assertIn("'tests/native/test_*.py'", text)
        self.assertIn("'-p', 'test_*.py'", text)
        self.assertNotIn('workflow_dispatch:', text)
        self.assertNotIn('-ExecuteReviewedDiagnostic', text)


if __name__ == '__main__': unittest.main(verbosity=2)
