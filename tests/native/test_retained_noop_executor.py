"""Synthetic admission, pin and closure tests. No MQB or study launcher is run."""
import ast
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import retained_noop_executor as e

ROOT = Path(__file__).resolve().parents[2]
COMMIT = 'a' * 40


def context():
    return {'GITHUB_REPOSITORY': e.REPOSITORY, 'GITHUB_EVENT_NAME': 'workflow_dispatch',
            'GITHUB_REF': 'refs/heads/main', 'GITHUB_SHA': COMMIT, 'GITHUB_WORKFLOW_SHA': COMMIT,
            'GITHUB_WORKFLOW_REF': e.REPOSITORY + '/' + e.WORKFLOW + '@refs/heads/main',
            'GITHUB_RUN_ID': '123', 'GITHUB_RUN_NUMBER': '1', 'GITHUB_RUN_ATTEMPT': '1'}


def pin_hash():
    return e.sha(e.canonical((ROOT / e.PIN_FILE).read_bytes()))


def synthetic_tree(root):
    for name in (*e.CRITICAL, e.PIN_FILE):
        p = root / name; p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes((ROOT / name).read_bytes())


class Contracts(unittest.TestCase):
    def test_first_reviewed_manual_allocation_only(self):
        result = e.admission(context(), COMMIT, COMMIT, e.ALLOCATION)
        self.assertEqual('123', result['GITHUB_RUN_ID'])

    def test_every_context_mismatch_refused(self):
        for key in context():
            with self.subTest(key=key):
                c = context(); c[key] = 'invalid'
                with self.assertRaises(ValueError): e.admission(c, COMMIT, COMMIT, e.ALLOCATION)
                del c[key]
                with self.assertRaises(ValueError): e.admission(c, COMMIT, COMMIT, e.ALLOCATION)

    def test_rerun_and_second_dispatch_refused(self):
        for key in ('GITHUB_RUN_NUMBER', 'GITHUB_RUN_ATTEMPT'):
            for value in ('2', '01', 1, True, '1.0', ''):
                c = context(); c[key] = value
                with self.assertRaises(ValueError): e.admission(c, COMMIT, COMMIT, e.ALLOCATION)

    def test_unreviewed_branch_commit_and_allocation_refused(self):
        for approved, checkout, allocation in ((COMMIT, 'b'*40, e.ALLOCATION),
                ('A'*40, COMMIT, e.ALLOCATION), (COMMIT, COMMIT, 'other')):
            with self.assertRaises(ValueError): e.admission(context(), approved, checkout, allocation)
        for event in ('push', 'pull_request', 'workflow_run', 'schedule'):
            c = context(); c['GITHUB_EVENT_NAME'] = event
            with self.assertRaises(ValueError): e.admission(c, COMMIT, COMMIT, e.ALLOCATION)

    def test_frozen_sources_and_original_collector(self):
        observed = e.verify_sources(ROOT, pin_hash())
        self.assertEqual(set(e.CRITICAL), set(observed))
        self.assertEqual('4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf',
                         observed['tests/native/collect_external_noop_boundary.ps1']['canonical_lf_sha256'])
        self.assertEqual(40, len(e.boundary.schedule()))
        self.assertEqual(20, len({r['fixture'] for r in e.boundary.schedule()}))

    def test_lf_comparison_does_not_overwrite_raw_crlf(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic_tree(root)
            for p in root.rglob('*'):
                if p.is_file(): p.write_bytes(e.canonical(p.read_bytes()).replace(b'\n', b'\r\n'))
            before = {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            e.verify_sources(root, pin_hash())
            self.assertEqual(before, {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_any_execution_source_mutation_refused(self):
        for name in e.CRITICAL:
            with tempfile.TemporaryDirectory() as d:
                root = Path(d); synthetic_tree(root)
                p = root / name; p.write_bytes(p.read_bytes() + b'\n# SYNTHETIC mutation\n')
                with self.assertRaises(ValueError): e.verify_sources(root, pin_hash())

    def test_manifest_hash_inventory_duplicates_and_type_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic_tree(root); path = root / e.PIN_FILE
            value = json.loads(path.read_bytes())
            with self.assertRaises(ValueError): e.verify_sources(root, '0'*64)
            for wrong in (dict(value, schema=True), dict(value, schema=1.0), dict(value, extra=True),
                          dict(value, canonical_lf_sha256={} )):
                path.write_text(json.dumps(wrong)); h = e.sha(e.canonical(path.read_bytes()))
                with self.assertRaises(ValueError): e.verify_sources(root, h)
            path.write_text('{"schema":1,"schema":1}')
            with self.assertRaises(ValueError): e.verify_sources(root, e.sha(path.read_bytes()))

    def test_missing_source_is_not_repaired(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic_tree(root)
            missing = root / e.CRITICAL[0]; missing.unlink()
            with self.assertRaises(ValueError): e.verify_sources(root, pin_hash())
            self.assertFalse(missing.exists())

    def test_wrong_original_archive_is_not_extracted(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); archive = root / 'synthetic.zip'; archive.write_bytes(b'not original')
            with self.assertRaises(ValueError): e.verify_archive(archive)
            self.assertEqual([archive], list(root.iterdir()))

    def test_refused_admission_creates_no_output(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / 'never-created'
            c = context(); c['GITHUB_RUN_ATTEMPT'] = '2'
            with self.assertRaises(ValueError):
                e.prepare(Path('absent.zip'), ROOT, output, COMMIT, COMMIT, pin_hash(), e.ALLOCATION, c)
            self.assertFalse(output.exists())

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); marker = root/'retained'; marker.write_text('failure')
            with patch.object(e, 'verify_archive', return_value={'synthetic': True}):
                with self.assertRaises(ValueError):
                    e.prepare(Path('synthetic'), ROOT, root, COMMIT, COMMIT, pin_hash(), e.ALLOCATION, context())
            self.assertEqual('failure', marker.read_text())

    def test_workflow_has_no_automatic_study_trigger_or_retry(self):
        text = (ROOT / e.WORKFLOW).read_text()
        trigger = text.split('on:\n', 1)[1].split('permissions:', 1)[0]
        self.assertIn('  workflow_dispatch:', trigger)
        for name in ('pull_request:', 'push:', 'schedule:', 'workflow_run:', 'workflow_call:'):
            self.assertNotIn(name, trigger)
        active = '\n'.join(x for x in text.splitlines() if not x.lstrip().startswith('#'))
        for forbidden in ('continue-on-error', 'actions: write', 'contents: write', 'rerun', 'acquire_seed'):
            self.assertNotIn(forbidden, active)
        for expected in ('timeout-minutes: 20', 'timeout-minutes: 15', 'if: always()',
                         "artifact-ids: '10675079360'", "run-id: '35681224762'",
                         'skip-decompress: true', 'digest-mismatch: error', 'persist-credentials: false',
                         "context['GITHUB_RUN_NUMBER'] == context['GITHUB_RUN_ATTEMPT'] == '1'"):
            self.assertIn(expected, active)
        self.assertLess(active.index('Record and enforce'), active.index('uses: actions/checkout@'))
        self.assertLess(active.index('Download only'), active.index('Invoke frozen collector'))
        self.assertEqual(1, active.count('& ./checkout/tests/native/run_retained_noop_study.ps1'))

    def test_wrapper_calls_existing_collector_exactly_once(self):
        text = (ROOT/'tests/native/run_retained_noop_study.ps1').read_text()
        self.assertEqual(1, text.count('& $collector -ArtifactPath'))
        for name in ('Invoke-ProcessBoundary', 'Invoke-LegacyBoundary', 'Invoke-RegisteredStudy'):
            self.assertNotIn('function '+name, text)
        self.assertIn('Set-Alias -Name python -Value $pythonExe -Scope Local -Force', text)
        self.assertIn('Get-Command python -CommandType Application | Select-Object -First 1', text)
        self.assertIn('[IO.FileMode]::CreateNew', text)
        self.assertLess(text.index('$checker prepare'), text.index('& $collector -ArtifactPath'))
        self.assertIn("if ($LASTEXITCODE -ne 0) { throw", text)

    def test_checker_has_no_process_or_network_launch(self):
        tree = ast.parse(Path(e.__file__).read_text())
        imports = {n.name for x in ast.walk(tree) if isinstance(x, ast.Import) for n in x.names}
        self.assertFalse({'subprocess', 'ctypes', 'multiprocessing', 'urllib', 'requests'} & imports)
        self.assertNotIn('os.system(', Path(e.__file__).read_text())

    def test_preparation_snapshots_and_receipt_without_dispatch(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); archive = root/'SYNTHETIC.zip'; archive.write_bytes(b'SYNTHETIC input, not an executable')
            output = root/'evidence'
            with patch.object(e, 'verify_archive', return_value={'synthetic': True}):
                receipt = e.prepare(archive, ROOT, output, COMMIT, COMMIT, pin_hash(), e.ALLOCATION, context())
            self.assertEqual(40, receipt['max_study_calls'])
            self.assertFalse(receipt['clears_hold'])
            self.assertIsNone(receipt['cause'])
            self.assertFalse((output/'study').exists())
            self.assertEqual(set((*e.CRITICAL, e.PIN_FILE)),
                {str(p.relative_to(output/'source')).replace('\\', '/') for p in (output/'source').rglob('*') if p.is_file()})

    def test_complete_live_and_offline_closure_and_corruption(self):
        # Deliberately synthetic inner study: tests outer closure, not Windows execution.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); archive = root/'SYNTHETIC.zip'; archive.write_bytes(b'SYNTHETIC input')
            output = root/'evidence'
            with patch.object(e, 'verify_archive', return_value={'synthetic': True}):
                receipt = e.prepare(archive, ROOT, output, COMMIT, COMMIT, pin_hash(), e.ALLOCATION, context())
                e.boundary.write_new(output/'execution-interpreter-after.json', receipt['interpreter'])
                study = output/'study'; (study/'collector-source').mkdir(parents=True)
                for name in e.boundary.COLLECTORS:
                    (study/'collector-source'/name).write_bytes((output/'source/tests/native'/name).read_bytes())
                inner = {'samples': ['SYNTHETIC']*40, 'clears_hold': False, 'cause': None}
                e.boundary.write_new(study/'audit.json', inner)
                with patch.object(e.boundary, 'audit', return_value=inner):
                    result = e.finish(output, live_interpreter=True)
                    self.assertEqual(40, result['calls']); self.assertFalse(result['clears_hold'])
                    e.boundary.write_new(output/'execution-audit.json', result)
                    completion = dict(schema=1, status='completed_diagnostic_only', collector_invoked=True,
                                      error=None, clears_hold=False, cause=None)
                    e.boundary.write_new(output/'execution-completion.json', completion)
                    self.assertEqual(result, e.finish(output, final=True))
                    for field, value in [('schema', 1.0), ('collector_invoked', 1), ('status', 'stopped'),
                                         ('error', 'SYNTHETIC failure'), ('clears_hold', True)]:
                        bad = dict(completion); bad[field] = value
                        (output/'execution-completion.json').write_text(json.dumps(bad))
                        with self.assertRaises(ValueError): e.finish(output, final=True)
                    (output/'execution-completion.json').write_text(json.dumps(completion))
                    bad = dict(result); bad['calls'] = 40.0
                    (output/'execution-audit.json').write_text(json.dumps(bad))
                    with self.assertRaises(ValueError): e.finish(output, final=True)

    def test_workflow_guard_executes_with_synthetic_context_before_download(self):
        import os
        import textwrap
        text = (ROOT/e.WORKFLOW).read_text()
        block = text.split('        run: |\n', 1)[1].split('      - uses:', 1)[0]
        code = compile(textwrap.dedent(block), 'SYNTHETIC workflow guard', 'exec')
        base = dict(context(), REVIEWED_COMMIT=COMMIT, MANIFEST_SHA256=pin_hash(), ALLOCATION=e.ALLOCATION)
        cases = [(None, None), ('GITHUB_RUN_NUMBER', '2'), ('GITHUB_RUN_ATTEMPT', '2'),
                 ('GITHUB_EVENT_NAME', 'pull_request'), ('GITHUB_REF', 'refs/heads/other'),
                 ('GITHUB_SHA', 'b'*40), ('MANIFEST_SHA256', 'bad'), ('ALLOCATION', 'other')]
        old = Path.cwd()
        for key, value in cases:
            with tempfile.TemporaryDirectory() as d:
                try:
                    os.chdir(d); env = dict(base)
                    if key: env[key] = value
                    with patch.dict(os.environ, env, clear=True):
                        if key:
                            with self.assertRaises(SystemExit): exec(code, {})
                        else: exec(code, {})
                    request = Path('execution-out/requested-allocation.json')
                    self.assertTrue(request.is_file())
                    self.assertEqual(1, len(list(Path('execution-out').iterdir())))
                finally: os.chdir(old)

    def test_failure_in_inner_audit_is_not_replaced_with_success(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            receipt = {'schema': 1, 'allocation': e.ALLOCATION, 'context': context(), 'manifest_sha256': pin_hash(),
                       'sources': {}, 'artifact': {}, 'max_study_calls': 40, 'step_timeout_minutes': 15,
                       'job_timeout_minutes': 20, 'clears_hold': False, 'cause': None,
                       'interpreter': {'path': 'SYNTHETIC', 'size': 1, 'sha256': 'f'*64}}
            e.boundary.write_new(root/'execution-before.json', receipt)
            e.boundary.write_new(root/'execution-interpreter-after.json', receipt['interpreter'])
            with patch.object(e, 'verify_sources', return_value={}), patch.object(e, 'verify_archive', return_value={}), \
                 patch.object(e.boundary, 'audit', side_effect=ValueError('SYNTHETIC stopped study')):
                with self.assertRaisesRegex(ValueError, 'stopped study'): e.finish(root)
            self.assertFalse((root/'execution-audit.json').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
