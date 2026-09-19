#!/usr/bin/env python3
"""Portable structural regressions for the performance workflow's admission policy.

No dependencies, compiler, benchmarks, network, or workflow dispatch. These tests
check the deliberately fixed YAML scalar/indentation contract, not GitHub's
scheduler implementation. A real metadata-only event must be reviewed separately.
"""
from pathlib import Path
import re
import unittest

WORKFLOW = Path(__file__).resolve().parents[2] / '.github/workflows/performance-evidence.yml'
ADMISSION = ("github.event_name == 'workflow_dispatch' || "
             "(startsWith(github.event.pull_request.title, 'perf:') && "
             "(github.event.action != 'edited' || github.event.changes.title.from != null))")
GROUP = 'performance-evidence-${{ github.event.pull_request.number || github.run_id }}'


def concurrency_policy(source: str) -> tuple[str, str]:
    """Read only the expected plain scalar block; reject ambiguous/repeated keys."""
    if re.search(r'^concurrency:', source, re.MULTILINE):
        raise ValueError('workflow-level concurrency lets skipped metadata events cancel evidence')
    blocks = re.findall(
        r'^    concurrency:\n      group: ([^\n]+)\n      cancel-in-progress: ([^\n]+)$',
        source, re.MULTILINE)
    if len(blocks) != 1 or source.count('    concurrency:') != 1:
        raise ValueError('exactly one compare-job concurrency block is required')
    job = source.split('\njobs:\n', 1)
    if len(job) != 2 or not job[1].startswith('  compare:\n'):
        raise ValueError('unexpected compare job boundary')
    if re.search(r'^  [A-Za-z_][A-Za-z_0-9-]*:', job[1][len('  compare:\n'):], re.MULTILINE):
        raise ValueError('review new job scopes before changing this policy contract')
    group, cancel = blocks[0]
    if group != GROUP or cancel != 'false':
        raise ValueError('admitted comparisons must serialize without cancelling started evidence')
    return group, cancel


class WorkflowPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = WORKFLOW.read_text(encoding='utf-8')

    def test_concurrency_is_job_scoped_and_non_cancelling(self):
        self.assertEqual(concurrency_policy(self.source), (GROUP, 'false'))

    def test_original_event_admission_is_unchanged(self):
        conditions = re.findall(r'^    if: (.+)$', self.source, re.MULTILINE)
        self.assertEqual(conditions, [ADMISSION])
        self.assertIn('    types: [opened, synchronize, reopened, ready_for_review, edited]\n', self.source)

    def test_policy_runs_before_expensive_builds(self):
        call = 'python candidate/tests/native/verify_performance_workflow.py'
        self.assertEqual(self.source.count(call), 1)
        self.assertLess(self.source.index('Checkout exact candidate head'), self.source.index(call))
        self.assertLess(self.source.index(call), self.source.index('Build exact baseline Release MQB'))

    def test_fixed_budget_and_timeout_are_unchanged(self):
        self.assertEqual(self.source.count('-Iterations 4'), 1)
        self.assertIn('    timeout-minutes: 45\n', self.source)
        self.assertIn('tests/native/compare_mqb_benchmarks.ps1', self.source)

    def test_exact_head_and_base_are_still_checked_out(self):
        self.assertIn('ref: ${{ github.event.pull_request.head.sha || github.sha }}', self.source)
        self.assertIn('ref: ${{ github.event.pull_request.base.sha }}', self.source)

    def test_actual_inputs_and_failure_upload_are_preserved(self):
        self.assertIn('Preserve exact measured inputs before instrumentation', self.source)
        self.assertIn('Verify measured binary identities after comparison\n        if: always()', self.source)
        self.assertIn('Upload benchmark evidence\n        if: always()', self.source)
        self.assertIn('          if-no-files-found: error\n', self.source)
        self.assertIn('          retention-days: 30\n', self.source)

    def test_regression_to_workflow_scope_is_rejected(self):
        mutated = 'concurrency:\n  group: unsafe\n  cancel-in-progress: true\n' + self.source
        with self.assertRaisesRegex(ValueError, 'workflow-level'):
            concurrency_policy(mutated)

    def test_automatic_cancellation_is_rejected(self):
        mutated = self.source.replace('      cancel-in-progress: false', '      cancel-in-progress: true')
        with self.assertRaises(ValueError):
            concurrency_policy(mutated)

    def test_missing_or_duplicate_concurrency_is_rejected(self):
        block = f'    concurrency:\n      group: {GROUP}\n      cancel-in-progress: false'
        for mutated in (self.source.replace(block, ''), self.source.replace(block, block + '\n' + block)):
            with self.subTest(source=mutated[:30]), self.assertRaises(ValueError):
                concurrency_policy(mutated)

    def test_separate_pull_requests_and_manual_runs_keep_distinct_keys(self):
        group, _ = concurrency_policy(self.source)
        self.assertEqual(group, GROUP)
        # This checks the maintained key contract, not scheduler event execution.
        keys = {f'performance-evidence-{identifier}' for identifier in (199, 200, 35451305991)}
        self.assertEqual(len(keys), 3)


if __name__ == '__main__':
    unittest.main(verbosity=2)
