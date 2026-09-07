"""Independent archive transport ABBA; reuse the existing external recorder.

No product freshness changes, no dropped pairs, no synthetic future timestamps.
Cold/mutation rows are separate from no-op rows; overlapping read work is not an
elapsed I/O percentage. Captured pipes are not an interactive terminal.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import tempfile
import compare_reporting as h


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--base-sha')
    parser.add_argument('--head-sha')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    h.self_test()
    if args.self_test:
        return
    h.require(os.name == 'nt', 'Product evidence requires Windows/MSVC')
    h.require(all([args.baseline, args.candidate, args.output, args.base_sha, args.head_sha]),
              'Missing exact binary/source identity or output directory')
    base, candidate, output = args.baseline.resolve(), args.candidate.resolve(), args.output.resolve()
    h.require(base.is_file() and candidate.is_file(), 'Exact binaries missing')
    h.require(not output.exists(), 'Refusing to overwrite earlier evidence')
    recorder = h.Recorder(output)
    result = {'base_sha': args.base_sha, 'head_sha': args.head_sha,
              'baseline_binary_sha256': hashlib.sha256(base.read_bytes()).hexdigest(),
              'candidate_binary_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
              'platform': platform.platform(), 'image_version': os.environ.get('ImageVersion'),
              'definition': __doc__, 'scenarios': []}
    def save() -> None:
        (output / 'archive-comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    def add(name, pairs) -> None:
        result['scenarios'].append({'name': name, 'summary': h.summarize(pairs), 'pairs': pairs})
        save()
    def order(index):
        rows = [('baseline', base), ('candidate', candidate)]
        return rows if index % 2 == 0 else list(reversed(rows))
    save()
    with tempfile.TemporaryDirectory(prefix='mqb-archive-') as temporary:
        root = Path(temporary)
        cases = [h.fixture(root, 'static2', 2, static=True), h.fixture(root, 'static129', 129, static=True)]
        for case in cases:
            recorder.run(base, case, case.name + '-prime-A')
            recorder.run(candidate, case, case.name + '-prime-B')
            for verbose, timed, jobs in [(False, False, 'auto'), (True, False, 'auto'),
                                         (False, True, 'auto'), (False, False, '1')]:
                name = f'{case.name}-{"verbose" if verbose else "default"}-{"timed" if timed else "off"}-j{jobs}'
                def run(binary, label, audit=False):
                    return recorder.run(binary, case, label, verbose=verbose, timings=timed or audit, jobs=jobs)
                audit_a = run(base, name + '-pre-A', True)
                audit_b = run(candidate, name + '-pre-B', True)
                h.assert_warm(audit_a, case, candidate=False)
                h.assert_warm(audit_b, case, candidate=True)
                h.require(h.semantic_counters(audit_a) == h.semantic_counters(audit_b), 'Warm precondition differs')
                before = h.state(case)
                pairs = []
                for index in range(24):
                    pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                    for side, binary in order(index):
                        row = run(binary, f'{name}-{index + 1}-{side}')
                        if timed:
                            h.assert_warm(row, case, candidate=side == 'candidate')
                        human = recorder.human(row, 'stdout')
                        h.require(b'[compile]' not in human and b'[archive]' not in human,
                                  'A measured warm invocation executed build work')
                        pair[side] = row
                    for channel in ['stdout', 'stderr']:
                        h.require(pair['baseline'][f'human_{channel}_sha256'] == pair['candidate'][f'human_{channel}_sha256'],
                                  'Archive transport changed the human transcript')
                    h.require(h.semantic_counters(pair['baseline']) == h.semantic_counters(pair['candidate']),
                              'Archive transport changed measured counters/hits')
                    pairs.append(pair)
                h.require(before == h.state(case), 'Warm measurement modified cache/artifact metadata')
                post_a = run(base, name + '-post-A', True)
                post_b = run(candidate, name + '-post-B', True)
                h.assert_warm(post_a, case, candidate=False)
                h.assert_warm(post_b, case, candidate=True)
                h.require(h.semantic_counters(post_a) == h.semantic_counters(post_b), 'Warm postcondition differs')
                add(name, pairs)
        case = cases[1]
        for kind in ['cold', 'single-tu']:
            pairs = []
            for index in range(6):
                pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                for side, binary in order(index):
                    if kind == 'cold':
                        shutil.rmtree(case.root / '.mqb')
                    else:
                        unit = case.root / 'unit_000.cpp'
                        before = unit.stat().st_mtime_ns
                        unit.write_text(unit.read_text(encoding='utf-8') + '\n// mutation\n', encoding='utf-8')
                        h.require(unit.stat().st_mtime_ns > before, 'Actual mutation timestamp did not advance')
                    row = recorder.run(binary, case, f'{kind}-{index + 1}-{side}', timings=True)
                    expected_misses = case.units if kind == 'cold' else 1
                    h.require(row['timing']['cache']['compile'] == {'hits': case.units - expected_misses, 'misses': expected_misses},
                              'Unexpected rebuild scope')
                    h.require(row['timing']['cache']['archive'] == {'hits': 0, 'misses': 1}, 'Required archive was skipped')
                    h.require(case.output.is_file(), 'Static-library output missing')
                    pair[side] = row
                h.require(h.semantic_counters(pair['baseline']) == h.semantic_counters(pair['candidate']),
                          'Rebuild counters differ')
                pairs.append(pair)
            add('static129-' + kind, pairs)
        case = cases[0]
        cache = case.root / '.mqb/cache/archive/report.archivecache'
        h.require(cache.is_file(), 'Archive cache fixture missing')
        for damage in ['empty', 'truncated', 'trailing', 'oversized', 'missing']:
            if damage == 'empty':
                cache.write_bytes(b'')
            elif damage == 'truncated':
                cache.write_bytes(cache.read_bytes()[:-3])
            elif damage == 'trailing':
                with cache.open('ab') as stream:
                    stream.write(b'\x00')
            elif damage == 'oversized':
                with cache.open('r+b') as stream:
                    stream.truncate(64 * 1024 * 1024 + 1)
            else:
                cache.unlink()
            repaired = recorder.run(candidate, case, 'repair-' + damage, timings=True)
            h.require(repaired['timing']['cache']['compile'] == {'hits': case.units, 'misses': 0},
                      'Bad archive cache must not force compilation')
            h.require(repaired['timing']['cache']['archive'] == {'hits': 0, 'misses': 1},
                      'Unusable archive cache must cause a real re-archive')
            if damage != 'missing':
                h.require(b'warning:' in recorder.human(repaired, 'stderr'), 'Cache failure warning was suppressed')
            warm = recorder.run(candidate, case, 'repaired-warm-' + damage, timings=True)
            h.assert_warm(warm, case, candidate=True)
    (output / 'passed.txt').write_text('All archive contracts and 204 independent ABBA pairs passed.\n', encoding='utf-8')
    print(json.dumps([{k: v for k, v in row.items() if k != 'pairs'} for row in result['scenarios']], indent=2))


if __name__ == '__main__':
    main()
