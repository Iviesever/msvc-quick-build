"""Cumulative release-source ABBA, not an addition of incremental PR percentages.

Rebuild both unmodified trees with the same seed/toolchain. All scored samples
have timings OFF and use process start-through-completion/pipe-drain elapsed
measurement. Separate audits understand schema 1 and 2 without fabricating old
counters. Fixed sample counts and practical flags; retain every adverse sample.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from copy import deepcopy
from dataclasses import replace
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import shutil
import statistics as stats
import subprocess
import tempfile
import sys
import time

# Preflight rejects untracked source contamination; do not create our own cache.
sys.dont_write_bytecode = True
import compare_reporting as h

RELEASE = '08cdc20a9f9380e18132d21a619288224fd07fd4'
RELEASE_TREE = 'f6ecbed37e6c6caf135d18ec734955a266f491d3'
CANDIDATE = 'cfb774258c9f3256defac38e1bbf8713ab2a1fc1'
CANDIDATE_TREE = 'b29fdf922d14942744a2d1f432661c945aff2230'
# Changing a measured tree is a new reviewed experiment, not a retry override.
PINNED = {'baseline': (RELEASE, RELEASE_TREE), 'candidate': (CANDIDATE, CANDIDATE_TREE)}
PROTOCOL = 'pr188-complete-candidate-20260919'
PREREGISTRATION = 5740142076
# One new complete-candidate allocation, not a retry of the historical data.
CUMULATIVE_PAIR_BUDGET = 584
MAX_INVOCATIONS = 1446
MAX_PROGRAM_CHECKS = 12
PREVIOUS_HARNESS = '3196245ad36e1af7965e7d521cc414186cdae940'
BRANCH = 'codex/v5.6-cumulative-evidence-entry'
EXPECTED_RUN_NUMBER = '5'
LEGACY_CANDIDATE = '1c565ca9f364b0bcb380565c4684ea543b449e04'
LEGACY_HARNESS = '8e9c2882014c84feb8021bcba5151d383b1d39db'
PRODUCT_CPP_TREE = '0dd3b2100ac896de915ebf0c142e0221583d663a'
LEGACY_RISKS = (
    {'id': 'private129', 'status': 'HOLD', 'run': 34756138330,
     'artifact': 10317617795, 'paired_median_ms': 3.4128,
     'reason': 'Original cumulative flag; new product and recorder changes do not prove its historical cause.'},
    {'id': 'pr188-abba630', 'status': 'HOLD', 'run': 34756138336,
     'artifact': 10317871192, 'paired_median_ms': 1.5423,
     'reason': 'Original conjunctive gate failed; actual measured binaries were not retained.'},
    {'id': 'pr192-abba641', 'status': 'HOLD',
     'reason': 'Original gate failure and missing actual measured binary identities remain.'},
    {'id': 'msvc-c1041', 'status': 'HOLD',
     'reason': 'Unresolved M1b compiler-PDB research dependency under merged PR195; no cancellation/lease claim.'},
    {'id': 'cold-tails', 'status': 'HOLD',
     'reason': 'Historical common129 +5.354s/+6.480s and later cold tails remain unexplained.'},
    {'id': 'earlier-abba-flags', 'status': 'HOLD',
     'reason': 'PR170, PR180/599, PR186/625 and 627 adverse evidence is not superseded.'},
)


def release_disposition(scenarios=None):
    return {'release_authorized': False, 'historical_risks_cleared': False,
            'historical_risks': deepcopy(list(LEGACY_RISKS)),
            'current_performance_flags': None if scenarios is None else
                [s['name'] for s in scenarios if s['summary']['practical_regression_flag']],
            'decision': 'HOLD'}


def permitted_sampling(event, env):
    pr = event.get('pull_request', {})
    head = pr.get('head', {})
    return (env.get('GITHUB_EVENT_NAME') == 'pull_request'
            and env.get('GITHUB_REPOSITORY') == 'Iviesever/msvc-quick-build'
            and env.get('GITHUB_RUN_ATTEMPT') == '1'
            and env.get('GITHUB_RUN_NUMBER') == EXPECTED_RUN_NUMBER
            and event.get('action') == 'synchronize' and event.get('number') == 188
            and event.get('before') == PREVIOUS_HARNESS
            and pr.get('base', {}).get('sha') == CANDIDATE
            and head.get('ref') == BRANCH
            and head.get('repo', {}).get('full_name') == 'Iviesever/msvc-quick-build'
            and re.fullmatch(r'[0-9a-f]{40}', head.get('sha', '')) is not None
            and head.get('sha') not in (CANDIDATE, PREVIOUS_HARNESS, LEGACY_HARNESS)
            and event.get('after') == head.get('sha')
            and env.get('MQB_EXPECTED_HARNESS') == head.get('sha'))


def require_sampling_budget(event=None, env=None):
    env = os.environ if env is None else env
    if event is None:
        path = env.get('GITHUB_EVENT_PATH')
        h.require(bool(path), 'No reviewed cumulative sampling event')
        event = json.loads(Path(path).read_text(encoding='utf-8'))
    h.require(CUMULATIVE_PAIR_BUDGET == 584 and permitted_sampling(event, env),
              'No reviewed cumulative sampling budget for this event/run/attempt')


def consume_call(result):
    # Consume before process creation; a failed launch does not create a refill.
    count = result.get('attempted_invocations', 0)
    h.require(count < MAX_INVOCATIONS, 'Cumulative invocation budget exhausted')
    result['attempted_invocations'] = count + 1
    result['not_run'] = MAX_INVOCATIONS - count - 1


def retain_program_check(output, case, side, result):
    checks = result.setdefault('program_checks', [])
    h.require(len(checks) < MAX_PROGRAM_CHECKS, 'Program-check budget exhausted')
    root = output / 'program-checks'
    root.mkdir(exist_ok=True)
    stem = f'{len(checks):02d}-{side}-{case.name}'
    row = dict(index=len(checks), side=side, fixture=case.name,
               argv=[str(case.output)], cwd=str(case.root), exit_code=None,
               stdout_file=f'program-checks/{stem}.stdout',
               stderr_file=f'program-checks/{stem}.stderr')
    checks.append(row)
    out, err = b'', b''
    try:
        completed = subprocess.run(row['argv'], cwd=case.root, capture_output=True,
                                   timeout=30, check=False)
        out, err = completed.stdout, completed.stderr
        row['exit_code'] = completed.returncode
        h.require(completed.returncode == 0, f'{case.name}: built program returned failure')
    except subprocess.TimeoutExpired as error:
        out, err = error.stdout or b'', error.stderr or b''
        row['timeout'] = True
        raise
    finally:
        # Preserve both available streams even if one evidence write fails.
        active = sys.exception()
        failures = []
        for field, data in (('stdout_file', out), ('stderr_file', err)):
            try:
                (output / row[field]).write_bytes(data)
                row[field + '_sha256'] = h.digest(data)
            except OSError as error:
                failures.append(error)
        if failures:
            row['evidence_errors'] = [str(e) for e in failures]
            if active is not None:
                for error in failures:
                    active.add_note(f'Program evidence write failed: {error}')
            else:
                raise failures[0]


WARM_PAIRS = 40
REBUILD_PAIRS = 20
COLD_PAIRS = 6


def dump(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2), encoding='utf-8')


def first_attempt(attempt: str) -> None:
    h.require(attempt == '1', 'A repeated attempt cannot replace the first evidence')


def validate_sources(sources, harness_sha):
    h.require(re.fullmatch(r'[0-9a-f]{40}', harness_sha or '') is not None,
              'Expected harness must be a full commit SHA')
    h.require(harness_sha not in (RELEASE, CANDIDATE), 'Harness and measured trees need separate identities')
    h.require(set(sources) == {'baseline', 'candidate', 'harness'}, 'Incorrect source role set')
    for side, source in sources.items():
        h.require(source['version'] == '5.5.0', f'{side}: frozen development VERSION changed')
        h.require(not source['dirty'], f'{side}: source checkout differs from its commit')
        h.require(re.fullmatch(r'[0-9a-f]{40}', source['tree']) is not None, f'{side}: invalid tree')
    for side, (sha, tree) in PINNED.items():
        h.require((sources[side]['sha'], sources[side]['tree']) == (sha, tree),
                  f'{side}: not the reviewed v5.5.0 baseline / frozen complete candidate')
    h.require(sources['harness']['sha'] == harness_sha, 'Running a different harness commit')


def git(root: Path, *arguments: str) -> str:
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True).strip()


def source_provenance(workspace: Path, harness_sha: str):
    first_attempt(os.environ.get('GITHUB_RUN_ATTEMPT', '1'))
    h.require((workspace / 'harness/tests/native/compare_release_cumulative.py').resolve()
              == Path(__file__).resolve(), 'Script is not from the specified harness checkout')
    sources = {}
    for side in ('baseline', 'candidate', 'harness'):
        root = workspace / side
        sources[side] = {'sha': git(root, 'rev-parse', 'HEAD'),
                         'tree': git(root, 'rev-parse', 'HEAD^{tree}'),
                         'version': (root / 'VERSION').read_text(encoding='utf-8').strip(),
                         'dirty': git(root, 'status', '--porcelain', '--untracked-files=all'),
                         'cpp_tree': git(root, 'rev-parse', 'HEAD:cpp')}
    validate_sources(sources, harness_sha)
    for side in ('candidate', 'harness'):
        h.require(sources[side]['cpp_tree'] == PRODUCT_CPP_TREE, f'{side}: product changed outside frozen complete candidate')
    h.require(git(workspace / 'harness', 'show', '-s', '--format=%P', 'HEAD').split()
              == [PREVIOUS_HARNESS, CANDIDATE], 'Unexpected exact harness parents')
    for parent in (CANDIDATE, LEGACY_HARNESS):
        subprocess.run(['git', '-C', str(workspace / 'harness'), 'merge-base', '--is-ancestor',
                        parent, harness_sha], check=True)
    return {'protocol': PROTOCOL, 'preregistration': PREREGISTRATION,
            'new_cumulative_pair_budget': CUMULATIVE_PAIR_BUDGET,
            'source_lineage': {'legacy_candidate': LEGACY_CANDIDATE,
                                   'previous_harness': PREVIOUS_HARNESS,
                                   'current_candidate': CANDIDATE, 'cpp_tree': PRODUCT_CPP_TREE,
                                   'binary_or_timing_equivalence_proven': False},
            **release_disposition(), 'sources': sources, 'run_id': os.environ.get('GITHUB_RUN_ID'),
            'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
            'python': sys.version, 'platform': platform.platform(),
            'runner_image': os.environ.get('ImageOS'), 'runner_image_version': os.environ.get('ImageVersion')}


def prepare_sources(workspace: Path, output: Path, provenance):
    output.mkdir(parents=True, exist_ok=False)
    dump(output / 'source-plan.json', {**provenance, 'retry_budget': 0,
         'full_matrix_pairs': 584, 'full_matrix_recorded_invocations': 1446,
         'new_cumulative_pair_budget': CUMULATIVE_PAIR_BUDGET, 'binary_build_completed': False,
         'warm_pairs': WARM_PAIRS, 'rebuild_pairs': REBUILD_PAIRS, 'cold_pairs': COLD_PAIRS,
         'bootstrap_resamples': 5000, 'bootstrap_seed': 5500,
         'release_authorized': False, 'historical_risks_cleared': False,
         'binary_origin': 'Planned same-seed rebuild of both source trees; preflight does not build binaries'})
    # Before any build: archives preserve the exact harness and both source trees.
    for side in ('baseline', 'candidate', 'harness'):
        subprocess.run(['git', '-C', str(workspace / side), '-c', 'core.autocrlf=false',
                        'archive', '--format=zip', '--output=' + str(output / (side + '-source.zip')), 'HEAD'],
                       check=True)
    dump(output / 'source-archive-hashes.json', {
        p.name: h.digest(p.read_bytes()) for p in sorted(output.glob('*-source.zip'))})
    dump(output / 'preflight-completed.json', {'status': 'preflight-only',
         'protocol': PROTOCOL, 'new_mqb_invocations': 0, 'new_cumulative_pairs': 0,
         'cumulative_measurement_completed': False, **release_disposition()})


@contextmanager
def retained_fixture(output, result, recorder):
    with tempfile.TemporaryDirectory(prefix='mqb-cumulative-') as temporary:
        try:
            yield temporary
        except Exception as error:
            result.update(status='failed', error=f'{type(error).__name__}: {error}',
                          recorded_invocations=len(recorder.calls))
            # Keep inputs before TemporaryDirectory cleanup; never touch PDB/IDB.
            # Existing Recorder retains raw diagnostics / calls, including failures.
            try:
                root = Path(temporary)
                for source in root.rglob('*'):
                    if source.suffix in ('.cpp', '.hpp', '.ixx') and source.is_file():
                        target = output / 'failure-inputs' / source.relative_to(root)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(source, target)
            except Exception as snapshot_error:
                result['input_snapshot_error'] = str(snapshot_error)
            try:
                dump(output / 'cumulative.json', result)
            except Exception as save_error:
                error.add_note(f'Could not save cumulative failure state: {save_error}')
            raise


def counts(recorder, row):
    text = recorder.human(row, 'stdout')
    return {kind: len(re.findall(rb'^\[' + kind.encode() + rb'\] ', text, re.M))
            for kind in ('compile', 'link', 'archive', 'pch')}


def audit_warm(row, case):
    timing = row['timing']
    h.require(timing is not None and timing['schema_version'] in (1, 2), 'Unknown timing schema')
    h.require(timing['cache']['compile'] == {'hits': case.units + int(case.pch), 'misses': 0},
              f"{row['label']}: not all compile hits: {timing['cache']}")
    stage = 'archive' if case.static else 'link'
    h.require(timing['cache'][stage] == {'hits': 1, 'misses': 0}, 'Not a warm final stage')
    counters = timing.get('counters')
    if timing['schema_version'] == 1:
        h.require(counters is None, 'Historical schema unexpectedly gained counters')
    else:
        h.require(counters is not None and all(counters[k] == 0 for k in (
            'cl_processes_launched', 'link_processes_launched', 'lib_processes_launched',
            'cache_files_written')), 'Candidate warm audit launched tools or wrote caches')
    return {'schema_version': timing['schema_version'], 'cache': timing['cache'],
            'counters': counters, 'attribution': timing.get('attribution'),
            'counter_breakdown': timing.get('counter_breakdown')}


def comparison(pairs, *, warm):
    result = h.summarize(pairs)
    baseline = stats.median(p['baseline']['external_ms'] for p in pairs)
    candidate = stats.median(p['candidate']['external_ms'] for p in pairs)
    values = result['paired_deltas_ms']
    rng = random.Random(5500)
    boot = sorted(stats.median(rng.choices(values, k=len(values))) for _ in range(5000))
    interval = [boot[124], boot[4874]]
    threshold = max(0.5 if warm else 5.0, baseline * 0.03)
    result.update(baseline_median_ms=baseline, candidate_median_ms=candidate,
                  bootstrap_median_interval_ms=interval, practical_threshold_ms=threshold,
                  practical_regression_flag=result['paired_median_delta_ms'] > threshold and interval[0] > 0,
                  minimum_delta_ms=min(values), maximum_delta_ms=max(values))
    return result


def write_later(path: Path, data: bytes):
    previous = path.stat().st_mtime_ns
    deadline = time.monotonic() + 2
    while True:
        path.write_bytes(data)
        if path.stat().st_mtime_ns > previous:
            return
        h.require(time.monotonic() < deadline, 'Actual write timestamp did not advance')
        time.sleep(0.01)


def build_fixtures(root):
    cases = {
        'small': h.fixture(root, 'small', 2),
        'common': h.fixture(root, 'common', 129),
        'private': h.fixture(root, 'private', 129),
        'static': h.fixture(root, 'static', 129, static=True),
        'modules': h.fixture(root, 'modules', 2, modules=True),
        'pch': h.fixture(root, 'pch', 2, pch=True),
        'discovery': h.fixture(root, 'discovery', 2),
    }
    private = cases['private']
    for source in sorted(private.root.glob('*.cpp')):
        header = source.with_suffix('.hpp')
        header.write_bytes((private.root / 'common.hpp').read_bytes())
        source.write_text(source.read_text(encoding='utf-8').replace('common.hpp', header.name), encoding='utf-8')
    discovery = cases['discovery']
    # A real selected two-TU closure, not no-op compilation after rediscovery.
    (discovery.root / 'unit_000.hpp').write_text('#pragma once\nint value_0();\n', encoding='utf-8')
    main = discovery.root / 'main.cpp'
    main.write_text('#include "unit_000.hpp"\nint main() { return value_0() == 42 ? 0 : 1; }\n', encoding='utf-8')
    discovery.arguments = [a for a in discovery.arguments if a not in ('unit_000.cpp', '--no-discover')]
    return cases


def self_test():
    h.self_test()
    pairs = [{'baseline': {'external_ms': 10.0}, 'candidate': {'external_ms': 11.0}} for _ in range(40)]
    h.require(comparison(pairs, warm=True)['practical_regression_flag'], 'Regression flag lost')
    h.require(not comparison(pairs, warm=False)['practical_regression_flag'], 'Rebuild threshold changed')
    case = h.Fixture('test', Path('.'), [], 2)
    old = {'label': 'schema1', 'timing': {'schema_version': 1, 'cache': {
        'compile': {'hits': 2, 'misses': 0}, 'link': {'hits': 1, 'misses': 0}}}}
    h.require(audit_warm(old, case)['counters'] is None, 'Unavailable historical counters became zero')
    valid = {side: {'sha': sha, 'tree': tree, 'version': '5.5.0', 'dirty': ''}
             for side, (sha, tree) in PINNED.items()}
    valid['harness'] = {'sha': 'a' * 40, 'tree': 'b' * 40, 'version': '5.5.0', 'dirty': ''}
    validate_sources(valid, 'a' * 40)
    rejected = 0
    for side in valid:
        for field, bad in (('sha', 'c' * 40), ('tree', 'bad-tree'), ('version', '5.6.0'), ('dirty', 'modified.cpp')):
            changed = {key: dict(value) for key, value in valid.items()}
            changed[side][field] = bad
            try:
                validate_sources(changed, 'a' * 40)
            except RuntimeError:
                rejected += 1
            else:
                raise RuntimeError(f'Accepted incorrect {side}/{field}')
    for wrong in (RELEASE, CANDIDATE, 'main', '', 'c' * 40):
        try:
            validate_sources(valid, wrong)
        except RuntimeError:
            rejected += 1
        else:
            raise RuntimeError('Accepted wrong harness identity')
    first_attempt('1')
    for attempt in ('0', '2', '', 'invalid'):
        try:
            first_attempt(attempt)
        except RuntimeError:
            rejected += 1
        else:
            raise RuntimeError('Accepted repeated/invalid attempt')
    # Failure retention itself is tested without executing MQB.
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / 'evidence'
        out.mkdir()
        recorder = type("FixtureOnlyRecorder", (), {"calls": []})()
        result = {'status': 'incomplete'}
        try:
            with retained_fixture(out, result, recorder) as inputs:
                (Path(inputs) / 'failed.cpp').write_text('#error retained', encoding='utf-8')
                (Path(inputs) / 'compiler.pdb').write_bytes(b'not to be copied')
                raise RuntimeError('original failure')
        except RuntimeError as error:
            h.require(str(error) == 'original failure', 'Original failure replaced')
        h.require(result['status'] == 'failed' and (out / 'failure-inputs/failed.cpp').is_file(),
                  'Partial failure evidence lost')
        h.require(not list(out.rglob('*.pdb')), 'Failure snapshot touched PDB')
        try:
            prepare_sources(Path(tmp), out, {})
        except FileExistsError:
            pass
        else:
            raise RuntimeError('Existing evidence overwritten')
    h.require((WARM_PAIRS, REBUILD_PAIRS, COLD_PAIRS) == (40, 20, 6), 'Sample budget changed')
    print(f'Cumulative entry: valid identities, {rejected} rejecting mutations, first-failure/no-overwrite passed.')
    print('Cumulative schema/statistics tests passed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--preflight', action='store_true')
    parser.add_argument('--workspace', type=Path)
    for name in ('baseline', 'candidate', 'output'):
        parser.add_argument('--' + name, type=Path)
    for name in ('base-sha', 'head-sha', 'base-tree', 'head-tree', 'harness-sha'):
        parser.add_argument('--' + name)
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return
    h.require(args.workspace and args.output and args.harness_sha, 'Missing source workspace/output/harness')
    workspace = args.workspace.resolve()
    provenance = source_provenance(workspace, args.harness_sha)
    if args.preflight:
        prepare_sources(workspace, args.output.resolve(), provenance)
        return
    require_sampling_budget()
    h.require(os.name == 'nt', 'Product evidence requires Windows/MSVC')
    h.require(args.baseline and args.candidate, 'Missing binary paths')
    h.require((args.base_sha, args.base_tree) == PINNED['baseline'], 'Incorrect v5.5.0 baseline identity')
    h.require((args.head_sha, args.head_tree) == PINNED['candidate'], 'Incorrect complete candidate identity')
    plan = json.loads((args.output.resolve().parent / 'provenance/source-plan.json').read_text(encoding='utf-8'))
    h.require(plan.get('protocol') == PROTOCOL and
              plan.get('new_cumulative_pair_budget') == CUMULATIVE_PAIR_BUDGET, 'Sampling plan changed')
    h.require(plan['sources'] == provenance['sources'] and plan['run_id'] == provenance['run_id']
              and plan['run_attempt'] == provenance['run_attempt'], 'Source/run identity changed since preflight')
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    h.require(all(p.is_file() for p in binaries.values()), 'Missing binary')
    before = json.loads((args.output.resolve().parent / 'provenance/binary-before.json').read_text(encoding='utf-8'))
    h.require(before['binary_sha256'] == {s: h.digest(p.read_bytes()) for s, p in binaries.items()},
              'Built input differs from pre-measurement identity')
    output = args.output.resolve()
    h.require(not output.exists(), 'Refusing to overwrite existing evidence')
    with h.Recorder(output) as recorder:
        (output / 'inventories').mkdir()
        result = {'schema_version': 1, 'status': 'incomplete', 'provenance': provenance,
                  **release_disposition(), 'base_sha': args.base_sha, 'head_sha': args.head_sha,
                  'base_tree': args.base_tree, 'head_tree': args.head_tree, 'harness_sha': args.harness_sha,
                  'binary_sha256': {s: h.digest(p.read_bytes()) for s, p in binaries.items()},
                  'created_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
                  'runner_image': os.environ.get('ImageOS'), 'runner_image_version': os.environ.get('ImageVersion'),
                  'definition': __doc__, 'policy': {'warm_pairs': WARM_PAIRS, 'rebuild_pairs': REBUILD_PAIRS,
                  'cold_pairs': COLD_PAIRS, 'bootstrap': '5000 paired-median resamples, seed 5500, percentile interval',
                  'flag': 'paired median > max(0.5ms warm / 5ms rebuild, 3% base median) AND interval lower bound > 0',
                  'scope': 'Rebuilt release source, not the historical packaged executable. Pipes, not a terminal.'},
                  'scenarios': [], 'audits': [], 'behavior_contracts': [],
                  'attempted_invocations': 0, 'not_run': MAX_INVOCATIONS, 'program_checks': []}
        def save():
            dump(output / 'cumulative.json', result)
        def run(side, case, label, **kwargs):
            consume_call(result)
            return recorder.run(binaries[side], case, label + '-' + side, **kwargs)
        def audit(side, case, label, **kwargs):
            row = run(side, case, label, timings=True, **kwargs)
            result['audits'].append({'label': row['label'], **audit_warm(row, case)})
            return row
        def sides(index):
            return ('baseline', 'candidate') if index % 2 == 0 else ('candidate', 'baseline')
        def add(name, kind, pairs):
            result['scenarios'].append({'name': name, 'kind': kind,
                'summary': comparison(pairs, warm=kind == 'warm'), 'pairs': pairs})
            save()
        save()
        with retained_fixture(output, result, recorder) as temporary:
            cases = build_fixtures(Path(temporary))
            for key, case in cases.items():
                for side in binaries:
                    run(side, case, key + '-prime')
                if key == 'discovery':
                    # First builds can change root directory evidence. Seal once
                    # after all artifacts exist, then require zero writes below.
                    for side in binaries:
                        run(side, case, key + '-seal')
            warm = [('small', False, 'auto'), ('small', True, 'auto'),
                    ('common', False, 'auto'), ('common', True, 'auto'), ('common', False, '1'),
                    ('private', False, 'auto'), ('static', False, 'auto'), ('static', True, 'auto'),
                    ('modules', False, 'auto'), ('pch', False, 'auto'), ('discovery', False, 'auto'),
                    ('run', False, 'auto')]
            for key, verbose, jobs in warm:
                case = cases['small'] if key == 'run' else cases[key]
                if key == 'run':
                    case = replace(case, arguments=['run', *case.arguments[1:]])
                name = f'{key}-{"verbose" if verbose else "default"}-j{jobs}'
                print('Cumulative warm:', name, flush=True)
                for side in binaries:
                    audit(side, case, name + '-pre', verbose=verbose, jobs=jobs)
                before = h.state(case)
                dump(output / 'inventories' / (name + '-before.json'), before)
                pairs = []
                for index in range(WARM_PAIRS):
                    pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                    for side in sides(index):
                        row = run(side, case, name + '-' + str(index + 1), verbose=verbose, jobs=jobs)
                        h.require(all(v == 0 for v in counts(recorder, row).values()), 'Measured no-op built an artifact')
                        pair[side] = row
                    h.require(pair['baseline']['human_stderr_sha256'] == pair['candidate']['human_stderr_sha256'],
                              'Warm stderr differs between versions')
                    pairs.append(pair)
                after = h.state(case)
                dump(output / 'inventories' / (name + '-after.json'), after)
                h.require(before == after, 'Warm matrix modified cache/artifact metadata')
                for side in binaries:
                    audit(side, case, name + '-post', verbose=verbose, jobs=jobs)
                add(name, 'warm', pairs)
            rebuilds = [('common', 'single-tu'), ('static', 'single-tu'),
                        ('common', 'public-header'), ('small', 'output-repair')]
            for key, kind in rebuilds:
                case = cases[key]
                name = key + '-' + kind
                path = case.root / ('common.hpp' if kind == 'public-header' else 'unit_000.cpp')
                original = path.read_bytes()
                pairs = []
                print('Cumulative rebuild:', name, flush=True)
                for index in range(REBUILD_PAIRS):
                    pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                    payload = original + f'\n// cumulative pair {index:03d}\n'.encode()
                    pair['mutation_sha256'] = None if kind == 'output-repair' else h.digest(payload)
                    for side in sides(index):
                        if kind == 'output-repair':
                            case.output.unlink()
                        else:
                            write_later(path, payload)
                        row = run(side, case, name + '-' + str(index + 1))
                        expected = 0 if kind == 'output-repair' else case.units if kind == 'public-header' else 1
                        work = counts(recorder, row)
                        h.require(work['compile'] == expected and work['archive' if case.static else 'link'] == 1,
                                  f'{name}: incorrect rebuild scope: {work}')
                        pair[side] = row
                        audit(side, case, name + '-' + str(index + 1) + '-post')
                    pairs.append(pair)
                add(name, 'rebuild', pairs)
            for key in ('small', 'common', 'static', 'modules'):
                case = cases[key]
                pairs = []
                print('Cumulative cold:', key, flush=True)
                for index in range(COLD_PAIRS):
                    pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                    for side in sides(index):
                        shutil.rmtree(case.root / '.mqb')
                        row = run(side, case, key + '-cold-' + str(index + 1))
                        work = counts(recorder, row)
                        h.require(work['compile'] == case.units and work['archive' if case.static else 'link'] == 1,
                                  f'{key}: cold scope incorrect: {work}')
                        pair[side] = row
                        audit(side, case, key + '-cold-' + str(index + 1) + '-post')
                    pairs.append(pair)
                add(key + '-cold', 'cold', pairs)
            case = cases['small']
            unit = case.root / 'unit_000.cpp'
            original = unit.read_bytes()
            for side in binaries:
                write_later(unit, b'#error cumulative_visible_failure\n')
                row = run(side, case, 'compiler-failure', success=False)
                h.require(row['exit_code'] == 4 and b'cumulative_visible_failure' in
                          recorder.human(row, 'stdout') + recorder.human(row, 'stderr'), 'Compiler failure lost')
                write_later(unit, original)
                run(side, case, 'compiler-recovery')
                audit(side, case, 'compiler-recovered')
                for key in ('small', 'common', 'private', 'modules', 'pch', 'discovery'):
                    retain_program_check(output, cases[key], side, result)
                result['behavior_contracts'].append({'side': side, 'compiler_failure_exit': 4,
                                                    'recovery_and_program_runs': True})
            save()
        h.require(len(recorder.calls) == result['attempted_invocations'] == MAX_INVOCATIONS,
                  'Missing or extra original invocation records')
        h.require(len(result['program_checks']) == MAX_PROGRAM_CHECKS, 'Missing program controls')
        h.require(result['binary_sha256'] == {s: h.digest(p.read_bytes()) for s, p in binaries.items()},
                  'Measured binary changed during comparison')
        recorder.finalize()
        result.update(status='completed', recorded_invocations=len(recorder.calls),
                      performance_flags=[s['name'] for s in result['scenarios']
                                         if s['summary']['practical_regression_flag']],
                      **release_disposition(result['scenarios']))
        save()
        (output / 'passed.txt').write_text('Cumulative 584 pairs and behavior contracts completed; performance flags and historical HOLDs require separate disposition.\n', encoding='utf-8')
        print(json.dumps([{k: v for k, v in s.items() if k != 'pairs'} for s in result['scenarios']], indent=2))


if __name__ == '__main__':
    main()
