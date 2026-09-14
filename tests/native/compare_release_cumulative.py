"""Cumulative release-source ABBA, not an addition of incremental PR percentages.

Rebuild both unmodified trees with the same seed/toolchain. All scored samples
have timings OFF and use process start-through-completion/pipe-drain elapsed
measurement. Separate audits understand schema 1 and 2 without fabricating old
counters. Fixed sample counts and practical flags; retain every adverse sample.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
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

import compare_reporting as h

RELEASE = '08cdc20a9f9380e18132d21a619288224fd07fd4'
RELEASE_TREE = 'f6ecbed37e6c6caf135d18ec734955a266f491d3'
CANDIDATE = '1c565ca9f364b0bcb380565c4684ea543b449e04'
CANDIDATE_TREE = '96629b0d75061027ca0eacd4a5522806215b95f7'
# Changing a measured tree is a new reviewed experiment, not a retry override.
PINNED = {'baseline': (RELEASE, RELEASE_TREE), 'candidate': (CANDIDATE, CANDIDATE_TREE)}
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
    for side, source in sources.items():
        h.require(source['version'] == '5.5.0', f'{side}: frozen development VERSION changed')
        h.require(not source['dirty'], f'{side}: tracked source differs from its commit')
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
                         'dirty': git(root, 'diff', '--name-only', 'HEAD')}
    validate_sources(sources, harness_sha)
    return {'sources': sources, 'run_id': os.environ.get('GITHUB_RUN_ID'),
            'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
            'python': sys.version, 'platform': platform.platform(),
            'runner_image': os.environ.get('ImageOS'), 'runner_image_version': os.environ.get('ImageVersion')}


def prepare_sources(workspace: Path, output: Path, provenance):
    output.mkdir(parents=True, exist_ok=False)
    dump(output / 'source-plan.json', {**provenance, 'retry_budget': 0,
         'expected_pairs': 584, 'expected_recorded_invocations': 1446,
         'warm_pairs': WARM_PAIRS, 'rebuild_pairs': REBUILD_PAIRS, 'cold_pairs': COLD_PAIRS,
         'bootstrap_resamples': 5000, 'bootstrap_seed': 5500,
         'release_authorized': False, 'historical_risks_cleared': False,
         'binary_origin': 'Both unmodified release-source trees rebuilt with one pinned seed'})
    # Before any build: archives preserve the exact harness and both source trees.
    for side in ('baseline', 'candidate', 'harness'):
        subprocess.run(['git', '-C', str(workspace / side), '-c', 'core.autocrlf=false',
                        'archive', '--format=zip', '--output=' + str(output / (side + '-source.zip')), 'HEAD'],
                       check=True)
    dump(output / 'source-archive-hashes.json', {
        p.name: h.digest(p.read_bytes()) for p in sorted(output.glob('*-source.zip'))})


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
                    if source.is_file() and source.suffix in ('.cpp', '.hpp', '.ixx'):
                        target = output / 'failure-inputs' / source.relative_to(root)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(source, target)
            except Exception as snapshot_error:
                result['input_snapshot_error'] = str(snapshot_error)
            dump(output / 'cumulative.json', result)
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
        recorder = h.Recorder(out)
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
    h.require(os.name == 'nt', 'Product evidence requires Windows/MSVC')
    h.require(args.baseline and args.candidate, 'Missing binary paths')
    h.require((args.base_sha, args.base_tree) == PINNED['baseline'], 'Incorrect v5.5.0 baseline identity')
    h.require((args.head_sha, args.head_tree) == PINNED['candidate'], 'Incorrect complete candidate identity')
    plan = json.loads((args.output.resolve().parent / 'provenance/source-plan.json').read_text(encoding='utf-8'))
    h.require(plan['sources'] == provenance['sources'] and plan['run_id'] == provenance['run_id']
              and plan['run_attempt'] == provenance['run_attempt'], 'Source/run identity changed since preflight')
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    h.require(all(p.is_file() for p in binaries.values()), 'Missing binary')
    output = args.output.resolve()
    h.require(not output.exists(), 'Refusing to overwrite existing evidence')
    recorder = h.Recorder(output)
    (output / 'inventories').mkdir()
    result = {'schema_version': 1, 'status': 'incomplete', 'provenance': provenance,
              'release_authorized': False, 'historical_risks_cleared': False, 'base_sha': args.base_sha, 'head_sha': args.head_sha,
              'base_tree': args.base_tree, 'head_tree': args.head_tree, 'harness_sha': args.harness_sha,
              'binary_sha256': {s: h.digest(p.read_bytes()) for s, p in binaries.items()},
              'created_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'runner_image': os.environ.get('ImageOS'), 'runner_image_version': os.environ.get('ImageVersion'),
              'definition': __doc__, 'policy': {'warm_pairs': WARM_PAIRS, 'rebuild_pairs': REBUILD_PAIRS,
              'cold_pairs': COLD_PAIRS, 'bootstrap': '5000 paired-median resamples, seed 5500, percentile interval',
              'flag': 'paired median > max(0.5ms warm / 5ms rebuild, 3% base median) AND interval lower bound > 0',
              'scope': 'Rebuilt release source, not the historical packaged executable. Pipes, not a terminal.'},
              'scenarios': [], 'audits': [], 'behavior_contracts': []}
    def save():
        dump(output / 'cumulative.json', result)
    def run(side, case, label, **kwargs):
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
                check = subprocess.run([str(cases[key].output)], cwd=cases[key].root,
                                       capture_output=True, timeout=30, check=False)
                h.require(check.returncode == 0, f'{key}: built program returned failure')
            result['behavior_contracts'].append({'side': side, 'compiler_failure_exit': 4,
                                                'recovery_and_program_runs': True})
        h.require(len(recorder.calls) == 1446, 'Missing or extra original invocation records')
        result.update(status='completed', recorded_invocations=len(recorder.calls),
                      performance_flags=[s['name'] for s in result['scenarios']
                                         if s['summary']['practical_regression_flag']])
        save()
    (output / 'passed.txt').write_text('Cumulative 584 pairs and behavior contracts completed; performance flags require separate disposition.\n', encoding='utf-8')
    print(json.dumps([{k: v for k, v in s.items() if k != 'pairs'} for s in result['scenarios']], indent=2))


if __name__ == '__main__':
    main()
