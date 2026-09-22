"""Admission and offline closure for one retained #701 study; never launches MQB."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from zipfile import ZipFile

import external_noop_boundary as boundary

# Only this new allocation is executable; 001 is immutable historical evidence.
ALLOCATION = '701-boundary-002'
RUN_NUMBER = '2'
PREVIOUS_STUDY = {
    'allocation': '701-boundary-001', 'run_id': '35725316959',
    'run_number': '1', 'attempt': '1',
    'commit': 'ac95a9f4efac785af0e3ac225f8e6db16cbf5f7b',
    'manifest_sha256': '907c49a21e9ea7f7d8d9855aa11f997c8b42717c4982b4153276355be6aba3a8',
    'artifact_id': 10693057419, 'size': 16641257,
    'sha256': 'd75b31e472439426f5cff42abf4f0213b86f140b310ea3e3ae7723a1906da0a7',
    'status': 'consumed_stopped', 'recorded_calls': 2, 'validated_calls': 1,
}

REPOSITORY = 'Iviesever/msvc-quick-build'
WORKFLOW = '.github/workflows/retained-noop-study.yml'
PIN_FILE = 'tests/native/retained_noop_executor.pins.json'
CRITICAL = (
    WORKFLOW,
    'tests/native/run_retained_noop_study.ps1',
    'tests/native/retained_noop_executor.py',
    'tests/native/collect_external_noop_boundary.ps1',
    'tests/native/external_noop_boundary.py',
)
ARTIFACT_ID = 10675079360
ARTIFACT_SIZE = 4880704


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(data):
    # Only the comparison representation changes. Snapshots retain checkout bytes.
    data.decode('utf-8')
    return data.replace(b'\r\n', b'\n')


def file_bytes(path, limit=16 * 1024 * 1024):
    require(not path.is_symlink() and path.is_file(), 'not an ordinary file: ' + str(path))
    require(path.stat().st_size <= limit, 'oversize file: ' + str(path))
    return path.read_bytes()


def admission(context, approved_commit, checkout_commit, allocation):
    require(re.fullmatch('[0-9a-f]{40}', approved_commit) is not None, 'invalid approved commit')
    expected = {
        'GITHUB_REPOSITORY': REPOSITORY,
        'GITHUB_EVENT_NAME': 'workflow_dispatch',
        'GITHUB_REF': 'refs/heads/main',
        'GITHUB_SHA': approved_commit,
        'GITHUB_WORKFLOW_SHA': approved_commit,
        'GITHUB_WORKFLOW_REF': REPOSITORY + '/' + WORKFLOW + '@refs/heads/main',
        'GITHUB_RUN_NUMBER': RUN_NUMBER,
        'GITHUB_RUN_ATTEMPT': '1',
    }
    require(all(context.get(k) == v for k, v in expected.items()), 'dispatch identity/exact allocation run refused')
    require(checkout_commit == approved_commit, 'checkout differs from reviewed commit')
    require(all(isinstance(context.get(k), str) and re.fullmatch('[1-9][0-9]*', context[k])
                for k in ('GITHUB_RUN_ID',)), 'missing run identity')
    require(allocation == ALLOCATION, 'wrong allocation; 001 is consumed')
    require(context['GITHUB_RUN_ID'] != PREVIOUS_STUDY['run_id'], 'historical run cannot be reused')
    return {**expected, 'GITHUB_RUN_ID': context['GITHUB_RUN_ID']}


def verify_sources(root, expected_manifest):
    require(re.fullmatch('[0-9a-f]{64}', expected_manifest) is not None, 'invalid manifest digest')
    raw_manifest = file_bytes(root / PIN_FILE)
    require(sha(canonical(raw_manifest)) == expected_manifest, 'unreviewed source manifest')
    pins = json.loads(raw_manifest.decode('utf-8'), object_pairs_hook=boundary.unique)
    require(set(pins) == {'schema', 'canonical_lf_sha256'} and type(pins['schema']) is int
            and pins['schema'] == 1, 'invalid source manifest')
    mapping = pins['canonical_lf_sha256']
    require(isinstance(mapping, dict) and set(mapping) == set(CRITICAL), 'critical source inventory changed')
    observations = {}
    for name in CRITICAL:
        data = file_bytes(root / name)
        require(sha(canonical(data)) == mapping[name], 'execution source mismatch: ' + name)
        observations[name] = {'size': len(data), 'raw_sha256': sha(data), 'canonical_lf_sha256': mapping[name]}
    return observations


def verify_archive(archive):
    data = file_bytes(archive, 128 * 1024 * 1024)
    require(len(data) == ARTIFACT_SIZE and sha(data) == boundary.ARCHIVE, 'not retained original #701 ZIP')
    # The entire ZIP identity is pinned before opening; never extract or execute it here.
    import io
    with ZipFile(io.BytesIO(data)) as z:
        require(len(z.namelist()) == len(set(z.namelist())) and z.testzip() is None, 'invalid retained ZIP')
        before = json.loads(z.read('provenance/identity-before.json'), object_pairs_hook=boundary.unique)
        after = json.loads(z.read('provenance/identity-after.json'), object_pairs_hook=boundary.unique)
        require(before['run_id'] == '35681224762' and before['attempt'] == '1', 'wrong original run')
        for side, digest in boundary.BINARIES.items():
            require(sha(z.read(side + '/mqb.exe')) == digest == before['inputs'][side]['binary_sha256']
                    == after[side], 'retained binary mismatch')
            require(before['inputs'][side]['sha'] == boundary.REVISIONS[side], 'retained revision mismatch')
            require(sha(z.read('provenance/' + side + '-source.zip')) ==
                    before['inputs'][side]['source_zip_sha256'], 'retained source ZIP mismatch')
    return {'artifact_id': ARTIFACT_ID, 'run_id': '35681224762', 'attempt': '1',
            'size': len(data), 'sha256': sha(data), 'binaries': dict(boundary.BINARIES)}


def verify_previous_study(archive):
    """Read, never extract/rejudge, the exact stopped 001 ZIP required by 002."""
    data = file_bytes(archive, 128 * 1024 * 1024)
    require(len(data) == PREVIOUS_STUDY['size'] and sha(data) == PREVIOUS_STUDY['sha256'],
            'not the retained stopped 001 ZIP')
    import io
    with ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        require(len(names) == len(set(names)) and z.testzip() is None, 'invalid stopped 001 ZIP')
        def read(name):
            return json.loads(z.read(name).decode('utf-8-sig'), object_pairs_hook=boundary.unique)
        requested = read('requested-allocation.json')
        expected = {
            'GITHUB_REPOSITORY': REPOSITORY, 'GITHUB_EVENT_NAME': 'workflow_dispatch',
            'GITHUB_REF': 'refs/heads/main', 'GITHUB_SHA': PREVIOUS_STUDY['commit'],
            'GITHUB_WORKFLOW_SHA': PREVIOUS_STUDY['commit'],
            'GITHUB_WORKFLOW_REF': REPOSITORY + '/' + WORKFLOW + '@refs/heads/main',
            'GITHUB_RUN_ID': PREVIOUS_STUDY['run_id'], 'GITHUB_RUN_NUMBER': '1',
            'GITHUB_RUN_ATTEMPT': '1', 'REVIEWED_COMMIT': PREVIOUS_STUDY['commit'],
            'MANIFEST_SHA256': PREVIOUS_STUDY['manifest_sha256'], 'ALLOCATION': '701-boundary-001',
        }
        require(boundary.same(requested, expected), 'historical allocation identity changed')
        error = 'Call 2 invalid; first failure retained, no refill.'
        require(boundary.same(read('evidence/execution-completion.json'), {
            'schema': 1, 'status': 'stopped', 'collector_invoked': True,
            'error': error, 'clears_hold': False, 'cause': None}), 'historical outer stop changed')
        require(boundary.same(read('evidence/study/completion.json'), {
            'status': 'stopped', 'attempted': 2, 'validated': 1,
            'error': error, 'clears_hold': False}), 'historical call accounting changed')
        prefix = 'evidence/study/calls/'
        expected_calls = {prefix + n + '.' + suffix + '.json'
                          for n in ('01', '02') for suffix in ('started', 'before', 'result', 'after')}
        expected_calls.add(prefix + '01.validated.json')
        require({n for n in names if n.startswith(prefix)} == expected_calls,
                'historical journal changed; no refill or revalidation')
    return dict(PREVIOUS_STUDY)


def interpreter():
    path = Path(sys.executable).resolve(strict=True)
    data = file_bytes(path, 128 * 1024 * 1024)
    return {'path': str(path), 'size': len(data), 'sha256': sha(data)}


def prepare(archive, source_root, output, approved_commit, checkout_commit, manifest_sha, allocation, context, *, previous_archive):
    admitted = admission(context, approved_commit, checkout_commit, allocation)
    sources = verify_sources(source_root, manifest_sha)
    identity = verify_archive(archive)
    previous = verify_previous_study(previous_archive)
    require(not output.exists(), 'new output root required; no resume')
    output.mkdir()
    # No workload begins in this helper. On partial write failure, keep the prefix.
    for name in (*CRITICAL, PIN_FILE):
        dest = output / 'source' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open('xb') as f:
            f.write(file_bytes(source_root / name))
    require(verify_sources(output / 'source', manifest_sha) == sources, 'snapshot differs from inspected source')
    with (output / 'original-701.zip').open('xb') as f:
        f.write(file_bytes(archive, 128 * 1024 * 1024))
    require(verify_archive(output / 'original-701.zip') == identity, 'copied artifact mismatch')
    with (output / 'previous-001.zip').open('xb') as f:
        f.write(file_bytes(previous_archive, 128 * 1024 * 1024))
    require(verify_previous_study(output / 'previous-001.zip') == previous, 'copied 001 changed')
    receipt = {'schema': 2, 'allocation': allocation, 'context': admitted, 'manifest_sha256': manifest_sha,
               'sources': sources, 'artifact': identity, 'interpreter': interpreter(),
               'previous_study': previous, 'cumulative_max_study_calls': 42,
               'max_study_calls': 40, 'step_timeout_minutes': 15, 'job_timeout_minutes': 20,
               'clears_hold': False, 'cause': None}
    boundary.write_new(output / 'execution-before.json', receipt)
    return receipt


def finish(root, *, live_interpreter=False, final=False):
    receipt = boundary.load(root / 'execution-before.json')
    admission(receipt['context'], receipt['context']['GITHUB_SHA'], receipt['context']['GITHUB_SHA'], receipt['allocation'])
    require(type(receipt['schema']) is int and receipt['schema'] == 2 and receipt['clears_hold'] is False
            and receipt['cause'] is None and type(receipt['max_study_calls']) is int
            and receipt['max_study_calls'] == 40 and type(receipt['step_timeout_minutes']) is int
            and receipt['step_timeout_minutes'] == 15 and type(receipt['job_timeout_minutes']) is int
            and receipt['job_timeout_minutes'] == 20 and type(receipt['cumulative_max_study_calls']) is int
            and receipt['cumulative_max_study_calls'] == 42, 'invalid execution receipt')
    previous = verify_previous_study(root / 'previous-001.zip')
    require(boundary.same(previous, receipt['previous_study']), 'historical receipt changed')
    require(verify_sources(root / 'source', receipt['manifest_sha256']) == receipt['sources'], 'snapshot changed')
    require(verify_archive(root / 'original-701.zip') == receipt['artifact'], 'original input changed')
    observed_after = boundary.load(root / 'execution-interpreter-after.json')
    require(boundary.same(observed_after, receipt['interpreter']), 'recorded interpreter identity changed')
    require(set(observed_after) == {'path', 'size', 'sha256'} and isinstance(observed_after['path'], str)
            and type(observed_after['size']) is int and observed_after['size'] > 0
            and re.fullmatch('[0-9a-f]{64}', observed_after['sha256']) is not None, 'invalid interpreter identity')
    if live_interpreter:
        require(interpreter() == observed_after, 'live interpreter changed')
    actual = boundary.audit(root / 'study')
    require(boundary.same(actual, boundary.load(root / 'study' / 'audit.json')), 'saved study audit differs')
    for name in boundary.COLLECTORS:
        require(file_bytes(root / 'study' / 'collector-source' / name) ==
                file_bytes(root / 'source' / 'tests' / 'native' / name), 'study used another collector')
    result = {'schema': 2, 'status': 'complete_diagnostic_only', 'allocation': receipt['allocation'],
            'previous_study': previous, 'cumulative_max_study_calls': 42,
            'recorded_calls_including_previous': previous['recorded_calls'] + len(actual['samples']),
            'run_id': receipt['context']['GITHUB_RUN_ID'], 'calls': len(actual['samples']),
            'manifest_sha256': receipt['manifest_sha256'], 'clears_hold': False, 'cause': None,
            'limits': 'Journal consistency, not atomic provenance, absent unlogged processes, causal attribution or performance clearance.'}
    if final:
        require(boundary.same(boundary.load(root / 'execution-audit.json'), result), 'saved execution audit changed')
        require(boundary.same(boundary.load(root / 'execution-completion.json'), {
            'schema': 1, 'status': 'completed_diagnostic_only', 'collector_invoked': True,
            'error': None, 'clears_hold': False, 'cause': None}), 'outer executor stopped or incomplete')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['prepare', 'finish', 'audit', 'inspect-input'])
    p.add_argument('path', type=Path)
    p.add_argument('--source-root', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--approved-commit'); p.add_argument('--checkout-commit')
    p.add_argument('--manifest-sha'); p.add_argument('--allocation')
    p.add_argument('--previous-archive', type=Path)
    a = p.parse_args()
    try:
        if a.command == 'prepare':
            require(all((a.source_root, a.approved_commit, a.checkout_commit, a.manifest_sha, a.allocation, a.previous_archive)), 'missing admission arguments')
            prepare(a.path, a.source_root, a.output, a.approved_commit, a.checkout_commit, a.manifest_sha, a.allocation, os.environ,
                    previous_archive=a.previous_archive)
        elif a.command == 'finish':
            boundary.write_new(a.path / 'execution-interpreter-after.json', interpreter())
            boundary.write_new(a.output, finish(a.path, live_interpreter=True))
        else:
            boundary.write_new(a.output, finish(a.path, final=True) if a.command == 'audit' else verify_archive(a.path))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as e:
        print('REFUSED: ' + str(e), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
