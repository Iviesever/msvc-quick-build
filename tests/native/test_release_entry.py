"""Frozen complete-candidate cumulative entry: no MQB/MSVC executions or timing samples."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import compare_release_cumulative as c
import compare_reporting as h


class EntryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.valid = {side: {'sha': sha, 'tree': tree, 'version': '5.5.0', 'dirty': '',
                            'cpp_tree': c.PRODUCT_CPP_TREE}
                      for side, (sha, tree) in c.PINNED.items()}
        self.valid['harness'] = dict(sha='a' * 40, tree='b' * 40,
                                    version='5.5.0', dirty='', cpp_tree=c.PRODUCT_CPP_TREE)

    def test_direct_self_test_does_not_create_untracked_bytecode(self):
        for source in (Path(c.__file__), Path(h.__file__)):
            (self.root / source.name).write_bytes(source.read_bytes())
        environment = dict(os.environ)
        environment.pop('PYTHONDONTWRITEBYTECODE', None)
        completed = subprocess.run([sys.executable, str(self.root / Path(c.__file__).name), '--self-test'],
                                   env=environment, capture_output=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr.decode(errors='replace'))
        self.assertFalse(list(self.root.rglob('*.pyc')))

    def test_current_and_historical_identities_are_separate(self):
        self.assertEqual(c.RELEASE, '08cdc20a9f9380e18132d21a619288224fd07fd4')
        self.assertEqual(c.CANDIDATE, 'cfb774258c9f3256defac38e1bbf8713ab2a1fc1')
        self.assertNotEqual(c.CANDIDATE, c.LEGACY_CANDIDATE)
        self.assertNotEqual(c.CANDIDATE, c.LEGACY_HARNESS)
        c.validate_sources(self.valid, 'a' * 40)

    def test_old_candidate_cannot_be_relabelled_current(self):
        self.valid['candidate']['sha'] = c.LEGACY_CANDIDATE
        with self.assertRaisesRegex(RuntimeError, 'frozen complete candidate'):
            c.validate_sources(self.valid, 'a' * 40)

    def test_missing_role_rejected(self):
        del self.valid['candidate']
        with self.assertRaisesRegex(RuntimeError, 'role set'):
            c.validate_sources(self.valid, 'a' * 40)

    def test_extra_role_rejected(self):
        self.valid['extra'] = self.valid['candidate']
        with self.assertRaisesRegex(RuntimeError, 'role set'):
            c.validate_sources(self.valid, 'a' * 40)

    def test_invalid_harness_and_source_fields_rejected(self):
        for side in self.valid:
            for field, bad in (('sha', 'c' * 40), ('tree', 'invalid'),
                               ('version', '5.6.0'), ('dirty', ' M tracked.py')):
                value = copy.deepcopy(self.valid)
                value[side][field] = bad
                with self.subTest(side=side, field=field), self.assertRaises(RuntimeError):
                    c.validate_sources(value, 'a' * 40)

    def test_repeat_budget_is_zero(self):
        c.first_attempt('1')
        for value in ('0', '2', '', '01', 'invalid'):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                c.first_attempt(value)

    def allocated_request(self):
        env = dict(GITHUB_EVENT_NAME='pull_request', GITHUB_REPOSITORY='Iviesever/msvc-quick-build',
                   GITHUB_RUN_ATTEMPT='1', GITHUB_RUN_NUMBER='5', MQB_EXPECTED_HARNESS='a' * 40)
        event = dict(action='synchronize', number=188, before=c.PREVIOUS_HARNESS, after='a' * 40,
                     pull_request=dict(base=dict(sha=c.CANDIDATE),
                     head=dict(ref=c.BRANCH, sha='a' * 40, repo=dict(full_name='Iviesever/msvc-quick-build'))))
        return event, env

    def test_measurement_requires_exact_new_reviewed_budget(self):
        self.assertEqual(c.CUMULATIVE_PAIR_BUDGET, 584)
        event, env = self.allocated_request()
        c.require_sampling_budget(event, env)
        with self.assertRaisesRegex(RuntimeError, 'No reviewed'):
            c.require_sampling_budget({}, env)
        self.assertEqual((c.WARM_PAIRS, c.REBUILD_PAIRS, c.COLD_PAIRS), (40, 20, 6))
        self.assertEqual(12 * c.WARM_PAIRS + 4 * c.REBUILD_PAIRS + 4 * c.COLD_PAIRS, 584)

    def test_other_event_attempt_or_run_never_reuses_budget(self):
        event, env = self.allocated_request()
        for key in env:
            for value in ('', '0', '2', '6', '01', 'workflow_dispatch'):
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    c.require_sampling_budget(event, {**env, key: value})
        for key, value in [('action', 'opened'), ('action', 'reopened'), ('action', 'edited'),
                           ('number', 194), ('before', c.CANDIDATE), ('after', 'b' * 40)]:
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                c.require_sampling_budget({**event, key: value}, env)
        for field, value in [('ref', 'other'), ('sha', 'bad'), ('sha', c.CANDIDATE),
                             ('repo', {'full_name': 'another/repo'})]:
            changed = copy.deepcopy(event)
            changed['pull_request']['head'][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                c.require_sampling_budget(changed, env)
        changed = copy.deepcopy(event)
        changed['pull_request']['base']['sha'] = c.PREVIOUS_HARNESS
        with self.assertRaises(RuntimeError):
            c.require_sampling_budget(changed, env)

    def test_real_event_file_is_required_outside_explicit_models(self):
        event, env = self.allocated_request()
        with self.assertRaises(RuntimeError):
            c.require_sampling_budget(env=env)
        path = self.root / 'event.json'
        path.write_text(json.dumps(event), encoding='utf-8')
        c.require_sampling_budget(env={**env, 'GITHUB_EVENT_PATH': str(path)})

    def test_call_budget_consumed_before_dispatch_and_never_refilled(self):
        result = {}
        for count in range(c.MAX_INVOCATIONS):
            c.consume_call(result)
            self.assertEqual(result['attempted_invocations'], count + 1)
        self.assertEqual(result['not_run'], 0)
        with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
            c.consume_call(result)
        self.assertEqual(result['attempted_invocations'], 1446)

    def test_program_controls_preserve_both_streams_and_nonzero(self):
        case = h.Fixture('small', self.root, [], 2)
        result = {}
        with patch.object(c.subprocess, 'run', return_value=subprocess.CompletedProcess([], 7, b'out', b'err')):
            with self.assertRaisesRegex(RuntimeError, 'program returned failure'):
                c.retain_program_check(self.root, case, 'baseline', result)
        row = result['program_checks'][0]
        self.assertEqual(row['exit_code'], 7)
        self.assertEqual((self.root / row['stdout_file']).read_bytes(), b'out')
        self.assertEqual((self.root / row['stderr_file']).read_bytes(), b'err')

    def test_program_timeout_preserves_available_diagnostics(self):
        case = h.Fixture('small', self.root, [], 2)
        result = {}
        with patch.object(c.subprocess, 'run', side_effect=subprocess.TimeoutExpired('program', 30, b'partial', b'error')):
            with self.assertRaises(subprocess.TimeoutExpired):
                c.retain_program_check(self.root, case, 'baseline', result)
        row = result['program_checks'][0]
        self.assertTrue(row['timeout'])
        self.assertIsNone(row['exit_code'])
        self.assertEqual((self.root / row['stdout_file']).read_bytes(), b'partial')

    def test_no_thirteenth_program_execution(self):
        case = h.Fixture('small', self.root, [], 2)
        with patch.object(c.subprocess, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
                c.retain_program_check(self.root, case, 'baseline', {'program_checks': [{}] * 12})
            run.assert_not_called()

    def test_preflight_has_no_current_performance_result(self):
        state = c.release_disposition()
        self.assertIsNone(state['current_performance_flags'])
        self.assertFalse(state['release_authorized'])
        self.assertFalse(state['historical_risks_cleared'])
        self.assertEqual(state['decision'], 'HOLD')

    def test_new_green_cannot_clear_old_failures(self):
        state = c.release_disposition([{'name': 'new-green', 'summary': {'practical_regression_flag': False}}])
        self.assertEqual(state['current_performance_flags'], [])
        self.assertTrue(all(r['status'] == 'HOLD' for r in state['historical_risks']))
        self.assertEqual({r['id'] for r in state['historical_risks']},
                         {'private129', 'pr188-abba630', 'pr192-abba641',
                          'msvc-c1041', 'cold-tails', 'earlier-abba-flags'})
        self.assertFalse(state['release_authorized'])

    def test_new_adverse_flags_are_additional_not_replacements(self):
        state = c.release_disposition([{'name': 'new-flag', 'summary': {'practical_regression_flag': True}}])
        self.assertEqual(state['current_performance_flags'], ['new-flag'])
        self.assertEqual(len(state['historical_risks']), 6)
        self.assertFalse(state['historical_risks_cleared'])

    def test_disposition_is_not_mutable_global_state(self):
        state = c.release_disposition()
        state['historical_risks'][0]['status'] = 'cleared'
        self.assertEqual(c.release_disposition()['historical_risks'][0]['status'], 'HOLD')

    def observed_sources(self, changed=None, ancestry_error=False):
        """Mock only Git reads; the workflow separately checks real checkouts."""
        values = copy.deepcopy(self.valid)
        if changed:
            values[changed[0]][changed[1]] = changed[2]
        script = self.root / 'harness/tests/native/compare_release_cumulative.py'
        for side in values:
            (self.root / side).mkdir(exist_ok=True)
            (self.root / side / 'VERSION').write_text(values[side]['version'], encoding='utf-8')
        def read_git(root, *arguments):
            source = values[root.name]
            return {('rev-parse', 'HEAD'): source['sha'],
                    ('rev-parse', 'HEAD^{tree}'): source['tree'],
                    ('rev-parse', 'HEAD:cpp'): source['cpp_tree'],
                    ('status', '--porcelain', '--untracked-files=all'): source['dirty'],
                    ('show', '-s', '--format=%P', 'HEAD'): c.PREVIOUS_HARNESS + ' ' + c.CANDIDATE}[arguments]
        with patch.object(c, '__file__', str(script)), patch.object(c, 'git', side_effect=read_git), \
             patch.object(c.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'git')
                          if ancestry_error else None) as run, \
             patch.dict(os.environ, {'GITHUB_RUN_ATTEMPT': '1', 'GITHUB_RUN_ID': 'test-run'}):
            result = c.source_provenance(self.root, 'a' * 40)
            ancestry = [call for call in run.call_args_list if call.args[0][0] == 'git']
            self.assertEqual(len(ancestry), 2)
            self.assertTrue(all(call.kwargs['check'] for call in ancestry))
            return result

    def test_provenance_keeps_holds_and_source_only_equivalence(self):
        value = self.observed_sources()
        self.assertEqual(value['new_cumulative_pair_budget'], 584)
        self.assertEqual(value['source_lineage']['legacy_candidate'], c.LEGACY_CANDIDATE)
        self.assertFalse(value['source_lineage']['binary_or_timing_equivalence_proven'])
        self.assertFalse(value['release_authorized'])

    def test_missing_preserved_parent_fails_closed(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.observed_sources(ancestry_error=True)

    def test_untracked_harness_file_is_contamination(self):
        with self.assertRaisesRegex(RuntimeError, 'source checkout differs'):
            self.observed_sources(('harness', 'dirty', '?? injected.py'))

    def test_modified_product_cpp_tree_is_not_migration(self):
        for side in ('candidate', 'harness'):
            with self.subTest(side=side), self.assertRaisesRegex(RuntimeError, 'product changed'):
                self.observed_sources((side, 'cpp_tree', 'd' * 40))

    def test_wrong_script_workspace_rejected_before_git(self):
        with patch.object(c, 'git') as git, self.assertRaisesRegex(RuntimeError, 'Script is not'):
            c.source_provenance(self.root, 'a' * 40)
        git.assert_not_called()

    def test_source_archives_contain_actual_git_committed_bytes(self):
        sources = {}
        for side in ('baseline', 'candidate', 'harness'):
            root = self.root / side
            root.mkdir()
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            for name, value in [('user.name', 'Entry test'), ('user.email', 'test@example.invalid'),
                                ('core.autocrlf', 'false')]:
                subprocess.run(['git', '-C', str(root), 'config', name, value], check=True)
            (root / 'VERSION').write_bytes(b'5.5.0\n')
            (root / 'input.txt').write_bytes((side + '\n').encode())
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(root), 'commit', '-qm', 'Synthetic preflight input'], check=True)
            sources[side] = dict(sha=c.git(root, 'rev-parse', 'HEAD'),
                                 tree=c.git(root, 'rev-parse', 'HEAD^{tree}'))
        out = self.root / 'provenance'
        provenance = dict(protocol=c.PROTOCOL, sources=sources, **c.release_disposition())
        c.prepare_sources(self.root, out, provenance)
        hashes = json.loads((out / 'source-archive-hashes.json').read_text())
        for side in sources:
            path = out / (side + '-source.zip')
            self.assertEqual(hashes[path.name], h.digest(path.read_bytes()))
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.read('input.txt'), (side + '\n').encode())
                self.assertEqual(archive.comment.decode(), sources[side]['sha'])
        completed = json.loads((out / 'preflight-completed.json').read_text())
        self.assertEqual(completed['status'], 'preflight-only')
        self.assertFalse(completed['cumulative_measurement_completed'])
        self.assertEqual(completed['new_mqb_invocations'], 0)
        self.assertFalse((out / 'passed.txt').exists())
        with self.assertRaises(FileExistsError):
            c.prepare_sources(self.root, out, provenance)

    def test_archive_failure_cannot_emit_preflight_completion(self):
        out = self.root / 'provenance'
        with patch.object(c.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'git')):
            with self.assertRaises(subprocess.CalledProcessError):
                c.prepare_sources(self.root, out, {'sources': self.valid})
        self.assertTrue((out / 'source-plan.json').is_file())
        self.assertFalse((out / 'preflight-completed.json').exists())

    def retained_error(self, *, snapshot_failure=False, save_failure=False):
        out = self.root / 'failure'
        result = dict(status='incomplete', **c.release_disposition())
        with h.Recorder(out) as recorder:
            with self.assertRaisesRegex(RuntimeError, '^primary failure$') as caught:
                with c.retained_fixture(out, result, recorder) as temporary:
                    root = Path(temporary)
                    (root / 'failed.cpp').write_text('#error original', encoding='utf-8')
                    (root / 'locked.pdb').write_bytes(b'do not inspect')
                    (root / 'locked.idb').write_bytes(b'do not inspect')
                    if snapshot_failure:
                        self.enterContext(patch.object(c.shutil, 'copy2', side_effect=OSError('snapshot unavailable')))
                    if save_failure:
                        self.enterContext(patch.object(c, 'dump', side_effect=OSError('state unavailable')))
                    raise RuntimeError('primary failure')
            recorder.finalize()
        self.assertEqual(result['status'], 'failed')
        self.assertFalse(result['release_authorized'])
        self.assertEqual(len(result['historical_risks']), 6)
        self.assertFalse(list(out.rglob('*.pdb')) or list(out.rglob('*.idb')))
        return result, caught.exception, out

    def test_first_failure_keeps_inputs_and_holds(self):
        _, _, out = self.retained_error()
        self.assertEqual((out / 'failure-inputs/failed.cpp').read_text(), '#error original')
        self.assertEqual(json.loads((out / 'calls.json').read_text()), [])

    def test_snapshot_secondary_error_does_not_replace_primary(self):
        result, _, _ = self.retained_error(snapshot_failure=True)
        self.assertIn('snapshot unavailable', result['input_snapshot_error'])

    def test_state_save_secondary_error_does_not_replace_primary(self):
        _, error, _ = self.retained_error(save_failure=True)
        self.assertTrue(any('state unavailable' in note for note in error.__notes__))


if __name__ == '__main__':
    unittest.main()
