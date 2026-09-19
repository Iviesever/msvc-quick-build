"""Check the frozen M1b workflow boundary, without running any research tools.

This deliberately checks the literal archived job sections, not arbitrary YAML
or a new workflow interpreter. GitHub evaluates the actual job guards. Changing
an archived matrix requires a new reviewed allocation and an updated contract.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = 'msvc-default-endpoint.yml'
PRIVATE = 'msvc-service-ownership.yml'
GUARD = ('    # Frozen M1b research: no real-tool allocation; see docs/PROCESS_LIFETIME.md.\n'
         '    if: ${{ false }}\n')
CHECK_STEP = ('      - name: Verify research allocation boundary without running tools\n'
              '        shell: pwsh\n'
              '        run: |\n'
              '          python tests/native/test_ownership_research_scope.py\n'
              '          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\n')
ARCHIVED = {
    (DEFAULT, 'build'): '3c2bf2196a253bbfcca852b43dd21bd2e5554c709ff7feb4b811fb29dc9ed310',
    (DEFAULT, 'observe'): '8c4e0c0343f69b3415e1a438b0402a6efa276b954be293ccfb4d243453f12dfe',
    (PRIVATE, 'observe'): 'ebfd8e06f8092563ca3cb72bc663922ca88f56813f19aabea0d2e3ad94ded927',
}
ORIGINAL_CONTRACT = 'c817c0448d383050b91b9bde85c8c5eae95a06fff5bd9b3fea1aa39c12381404'

ORIGINAL_PREAMBLE = {'msvc-default-endpoint.yml': '3e74fbd878c2ab92e830b94280277a1f5ce4b432b400771138229e766541f6f6', 'msvc-service-ownership.yml': '890598deaedbf13b0674ed925b5b91029a6e153d90443574e213801f2320dcca'}


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def sha(text: str) -> str:
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def sections(text: str) -> tuple[str, dict[str, str]]:
    require(text.count('\njobs:\n') == 1, 'expected one literal jobs mapping')
    preamble, body = text.split('\njobs:\n')
    entries = list(re.finditer(r'^  ([A-Za-z0-9_-]+):\s*$', body, re.MULTILINE))
    names = [entry.group(1) for entry in entries]
    require(len(names) == len(set(names)), 'duplicate job')
    result = {entry.group(1): body[entry.start():entries[i+1].start() if i+1 < len(entries) else len(body)].strip()+'\n'
              for i, entry in enumerate(entries)}
    return preamble, result


def validate(texts: dict[str, str]) -> None:
    for name in (DEFAULT, PRIVATE):
        preamble, jobs = sections(texts[name])
        expected = {'contract', 'build', 'observe'} if name == DEFAULT else {'scope', 'observe'}
        require(set(jobs) == expected, 'unreviewed job set')
        require('  pull_request:\n' in preamble and '  workflow_dispatch:' in preamble,
                'keep both contract entry events')
        require("      - 'cpp/**'" in preamble, 'do not filter out product changes from contracts')
        for path in (DEFAULT, PRIVATE):
            require("      - '.github/workflows/"+path+"'" in preamble, 'missing cross-workflow trigger')
        require("      - 'tests/native/test_ownership_research_scope.py'" in preamble, 'missing contract trigger')
        require('  cancel-in-progress: false' in preamble and '${{ github.run_id }}' in preamble,
                'do not supersede evidence by PR number')
        other = PRIVATE if name == DEFAULT else DEFAULT
        original_preamble = preamble.replace("      - '.github/workflows/"+other+"'\n", '').replace(
            "      - 'tests/native/test_ownership_research_scope.py'\n", '')
        original_preamble = original_preamble.replace('${{ github.run_id }}',
            '${{ github.event.pull_request.number || github.run_id }}').replace(
            '  cancel-in-progress: false', '  cancel-in-progress: true')
        require(sha(original_preamble) == ORIGINAL_PREAMBLE[name], 'original contract triggers changed')
        for (workflow, job), expected_sha in ARCHIVED.items():
            if workflow != name:
                continue
            block = jobs[job]
            require(block.startswith(job+':\n'+GUARD), 'missing unconditional job-level research freeze')
            require(block.count(GUARD) == 1, 'duplicate guard')
            require(sha(block.replace(GUARD, '', 1)) == expected_sha,
                    'archived build, collector, failure checks or evidence upload changed')
        if name == DEFAULT:
            contract = jobs['contract']
            require(contract.count(CHECK_STEP) == 1, 'automatic scope check missing')
            contract = contract.replace(CHECK_STEP, '', 1).replace(
                '          path: |\n            .mqb/ownership-contract-tests/**\n            .mqb/ownership-scope/**',
                '          path: .mqb/ownership-contract-tests/**')
            require(sha(contract) == ORIGINAL_CONTRACT, 'original synthetic validation or evidence changed')
        else:
            block = jobs['scope']
            require(sha(block) == '510af29f20166e729cd29a1f894706522525afa50aa9ea9c3dcfee99e4d8b733', 'scope command or evidence policy changed')
            require('    runs-on: ubuntu-latest\n' in block, 'scope runner changed')
            require(re.search(r'^    (if|needs):', block, re.MULTILINE) is None, 'automatic scope check may not be skipped')
            require('        run: python tests/native/test_ownership_research_scope.py\n' in block,
                    'scope command changed')
            require(block.count('        run:') == 1 and 'shell:' not in block,
                    'unexpected executable scope command')
            require('collect_msvc' not in block and 'build_mqb' not in block, 'scope must not invoke native tools')
            require('        if: always()\n' in block and 'continue-on-error:' not in block,
                    'scope failures must retain evidence and fail')


class OwnershipResearchScopeTests(unittest.TestCase):
    def setUp(self):
        self.texts = {n: (ROOT/'.github/workflows'/n).read_text(encoding='utf-8') for n in (DEFAULT, PRIVATE)}

    def rejects(self, name, old, new):
        validate(self.texts)  # A rejecting mutant needs a passing unmodified control.
        self.assertIn(old, self.texts[name])
        changed = {**self.texts, name: self.texts[name].replace(old, new, 1)}
        with self.assertRaises(ValueError):
            validate(changed)

    def test_current_boundary_and_archived_payloads(self):
        validate(self.texts)

    def test_no_event_can_reopen_real_jobs(self):
        # Check the actual literal guard, not an imitation of GitHub's evaluator.
        for name, job in ARCHIVED:
            with self.subTest(workflow=name, job=job):
                _, jobs = sections(self.texts[name])
                self.assertTrue(jobs[job].startswith(job+':\n'+GUARD))
                for condition in ('true', "github.event_name == 'workflow_dispatch'", 'github.run_attempt == 1'):
                    self.rejects(name, '  '+job+':\n'+GUARD,
                                 '  '+job+':\n'+GUARD.replace('false', condition))

    def test_removed_or_step_level_guard_rejected(self):
        for name, job in ARCHIVED:
            with self.subTest(workflow=name, job=job):
                self.rejects(name, '  '+job+':\n'+GUARD, '  '+job+':\n')
                self.rejects(name, '  '+job+':\n'+GUARD, '  '+job+':\n'+GUARD.replace('    if:', '        if:'))

    def test_matrix_and_failure_policy_changes_rejected(self):
        for name in (DEFAULT, PRIVATE):
            with self.subTest(workflow=name):
                self.rejects(name, 'collect_msvc_service_ownership.ps1', 'weakened_collector.ps1')
                self.rejects(name, 'if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }', 'exit 0')
                self.rejects(name, '          if-no-files-found: warn', '          if-no-files-found: ignore')
        self.rejects(DEFAULT, '-EndpointMode default', '-EndpointMode private')
        self.rejects(DEFAULT, '          if ($code -eq 0', '          if ($code -ne 0')

    def test_synthetic_contract_cannot_be_bypassed(self):
        self.rejects(DEFAULT, CHECK_STEP, '')
        self.rejects(DEFAULT, '  contract:\n', '  contract:\n    if: ${{ false }}\n')
        self.rejects(DEFAULT, 'verify_ownership_evidence_contract.ps1\n', 'skip_validation.ps1\n')
        self.rejects(PRIVATE, '  scope:\n', '  scope:\n    if: ${{ false }}\n')
        self.rejects(PRIVATE, '  scope:\n', '  scope:\n    needs: observe\n')

    def test_extra_or_duplicate_jobs_rejected(self):
        for name in (DEFAULT, PRIVATE):
            with self.subTest(workflow=name):
                self.rejects(name, '\njobs:\n', '\njobs:\n  hidden:\n    runs-on: windows-latest\n')
                self.rejects(name, '\njobs:\n', '\njobs:\n  observe:\n    if: true\n')

    def test_contract_triggers_and_evidence_not_cancelled(self):
        for name in (DEFAULT, PRIVATE):
            with self.subTest(workflow=name):
                self.rejects(name, "      - 'cpp/**'\n", '')
                self.rejects(name, '  workflow_dispatch:', '  ignored_dispatch:')
                self.rejects(name, '  cancel-in-progress: false', '  cancel-in-progress: true')
                self.rejects(name, '${{ github.run_id }}', '${{ github.event.pull_request.number }}')

    def test_scope_is_not_a_second_native_runner(self):
        self.rejects(PRIVATE, '        run: python tests/native/test_ownership_research_scope.py',
                     '        run: python tests/native/build_mqb.py')
        self.rejects(PRIVATE, '  scope:\n', '  scope:\n    continue-on-error: true\n')


def main() -> None:
    # Only direct workflow invocation writes an artifact; unittest discovery is pure.
    output = ROOT/'.mqb/ownership-scope'
    output.mkdir(parents=True, exist_ok=False)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(OwnershipResearchScopeTests)
    with (output/'tests.txt').open('w', encoding='utf-8') as stream:
        result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
    evidence = dict(scope_contract_passed=result.wasSuccessful(), tests_run=result.testsRun,
                    failures=len(result.failures), errors=len(result.errors),
                    real_tool_allocation=0, research_executed=False, research_passed=None,
                    historical_failures_cleared=False, native_product_acceptance=False,
                    release_authorized=False, latest_failed_run=35311336307,
                    latest_failed_artifact=10534120991,
                    latest_failed_artifact_sha256='ded51533c9dd889f2bdbdc08840a621157428bfa3816f92b38ecb9cd52c1d6b1',
                    head=os.environ.get('GITHUB_SHA'), run=os.environ.get('GITHUB_RUN_ID'),
                    attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
                    workflows={n:sha((ROOT/'.github/workflows'/n).read_text(encoding='utf-8')) for n in (DEFAULT, PRIVATE)})
    (output/'scope.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    print((output/'tests.txt').read_text(encoding='utf-8'))
    print('Research allocation: 0; original C1041 evidence remains failed; no release authorization.')
    raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == '__main__':
    main()
