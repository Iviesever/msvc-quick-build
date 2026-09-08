"""Cumulative release-source ABBA, not an addition of incremental PR percentages.

Rebuild both unmodified trees with the same seed/toolchain. All scored samples
have timings OFF and use process start-through-completion/pipe-drain elapsed
measurement. Separate audits understand schema 1 and 2 without fabricating old
counters. Fixed sample counts and practical flags; retain every adverse sample.
"""
from __future__ import annotations

import argparse
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
import time

import compare_reporting as h

RELEASE = 'd041668de836b9eb9a36e2d6b96ff2114c5c358a'
WARM_PAIRS = 40
REBUILD_PAIRS = 20
COLD_PAIRS = 6


def dump(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2), encoding='utf-8')


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
    print('Cumulative schema/statistics tests passed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    for name in ('baseline', 'candidate', 'output'):
        parser.add_argument('--' + name, type=Path)
    for name in ('base-sha', 'head-sha', 'base-tree', 'head-tree', 'harness-sha'):
        parser.add_argument('--' + name)
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return
    h.require(os.name == 'nt', 'Product evidence requires Windows/MSVC')
    h.require(all(vars(args)[key] for key in vars(args) if key != 'self_test'), 'Missing exact identity/paths')
    h.require(args.base_sha == RELEASE, 'Baseline must be the immutable released v5.4.0 source')
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    h.require(all(p.is_file() for p in binaries.values()), 'Missing binary')
    output = args.output.resolve()
    h.require(not output.exists(), 'Refusing to overwrite existing evidence')
    recorder = h.Recorder(output)
    (output / 'inventories').mkdir()
    result = {'schema_version': 1, 'base_sha': args.base_sha, 'head_sha': args.head_sha,
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
    with tempfile.TemporaryDirectory(prefix='mqb-cumulative-') as temporary:
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
        save()
    (output / 'passed.txt').write_text('Cumulative 584 pairs and behavior contracts completed.\n', encoding='utf-8')
    print(json.dumps([{k: v for k, v in s.items() if k != 'pairs'} for s in result['scenarios']], indent=2))


if __name__ == '__main__':
    main()
