"""Same-definition toolchain warm-cache ABBA and real fallback checks.

Exact binaries, common fixtures, externally measured elapsed time, no discarded
samples. Timed work includes all toolchain trust validation, not isolated I/O.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
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
    h.require(os.name == 'nt', 'Windows/MSVC required for product evidence')
    h.require(all([args.baseline, args.candidate, args.output, args.base_sha, args.head_sha]), 'Missing exact identity/paths')
    base, candidate, output = args.baseline.resolve(), args.candidate.resolve(), args.output.resolve()
    h.require(base.is_file() and candidate.is_file() and not output.exists(), 'Missing binaries or existing evidence')
    recorder = h.Recorder(output)
    report = {'base_sha': args.base_sha, 'head_sha': args.head_sha,
              'baseline_binary_sha256': hashlib.sha256(base.read_bytes()).hexdigest(),
              'candidate_binary_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
              'platform': platform.platform(), 'image_version': os.environ.get('ImageVersion'),
              'definition': __doc__, 'scenarios': []}
    def save() -> None:
        (output / 'toolchain-comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    def warm(row, case) -> None:
        h.assert_warm(row, case, candidate=True)
        cache = row['timing']['counter_breakdown']['cache']['toolchain']
        h.require(cache['files_opened'] == 1 and cache['files_written'] == 0,
                  'Expected one reused toolchain cache payload, not discovery/resealing')
    save()
    with tempfile.TemporaryDirectory(prefix='mqb-toolchain-') as tmp:
        root = Path(tmp)
        cases = [h.fixture(root, 'small2', 2), h.fixture(root, 'scale129', 129)]
        for case in cases:
            recorder.run(base, case, case.name + '-prime-A')
            recorder.run(candidate, case, case.name + '-prime-B')
            for timed in (False, True):
                name = f'{case.name}-toolchain-hit-{"timed" if timed else "off"}'
                audits = [recorder.run(binary, case, name + '-pre-' + side, timings=True)
                          for side, binary in [('A', base), ('B', candidate)]]
                for row in audits:
                    warm(row, case)
                h.require(h.semantic_counters(audits[0]) == h.semantic_counters(audits[1]), 'Precondition counters differ')
                before = h.state(case)
                (output / (name + '-before.json')).write_text(json.dumps(before, indent=2), encoding='utf-8')
                pairs = []
                for index in range(24):
                    pair = {'pair': index + 1, 'orientation': 'AB' if index % 2 == 0 else 'BA'}
                    sides = [('baseline', base), ('candidate', candidate)]
                    for side, binary in sides if index % 2 == 0 else sides[::-1]:
                        row = recorder.run(binary, case, f'{name}-{index+1}-{side}', timings=timed)
                        if timed:
                            warm(row, case)
                        text = recorder.human(row, 'stdout')
                        h.require(b'[compile]' not in text and b'[link]' not in text, 'Unexpected build in warm sample')
                        pair[side] = row
                    h.require(h.semantic_counters(pair['baseline']) == h.semantic_counters(pair['candidate']),
                              'Non-output counters or hits differ')
                    for channel in ('stdout', 'stderr'):
                        h.require(pair['baseline'][f'human_{channel}_sha256'] == pair['candidate'][f'human_{channel}_sha256'],
                                  'Toolchain transport changed human output')
                    pairs.append(pair)
                after = h.state(case)
                (output / (name + '-after.json')).write_text(json.dumps(after, indent=2), encoding='utf-8')
                h.require(before == after, 'Warm matrix mutated cache or artifacts')
                for side, binary in [('A', base), ('B', candidate)]:
                    warm(recorder.run(binary, case, name + '-post-' + side, timings=True), case)
                report['scenarios'].append({'name': name, 'summary': h.summarize(pairs), 'pairs': pairs})
                save()
        # Product-level fallback uses real discovery; the native reader test
        # separately forbids processes on hits and requires fallback on misses.
        case = cases[0]
        # Application uses .cache; the standalone locator's default uses .mqbcache.
        files = list((case.root / '.mqb/cache/toolchain').glob('*.cache'))
        h.require(len(files) == 1, 'Expected exactly one toolchain cache fixture')
        cache = files[0]
        report['fallback_contracts'] = []
        for damage in ('empty', 'truncated', 'trailing', 'oversized', 'missing'):
            pair = {'damage': damage}
            source_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in case.root.glob('*.cpp')}
            # Compare the actual baseline behavior, rather than assuming a
            # rediscovery cannot invalidate compiler filesystem evidence.
            # Each side starts with a verified full hit on the same fixture.
            for side, binary in [('baseline', base), ('candidate', candidate)]:
                warm(recorder.run(binary, case, f'pre-repair-{damage}-{side}', timings=True), case)
                if damage == 'empty': cache.write_bytes(b'')
                elif damage == 'truncated': cache.write_bytes(cache.read_bytes()[:20])
                elif damage == 'trailing':
                    with cache.open('ab') as stream: stream.write(b'\0')
                elif damage == 'oversized':
                    with cache.open('r+b') as stream: stream.truncate(1024 * 1024 + 1)
                else: cache.unlink()
                row = recorder.run(binary, case, f'repair-{damage}-{side}', timings=True)
                h.require(row['timing']['counter_breakdown']['cache']['toolchain']['files_written'] == 1,
                          'Unusable toolchain cache was not resealed by discovery')
                pair[side] = row
                warm(recorder.run(binary, case, f'repaired-hit-{damage}-{side}', timings=True), case)
                state_after = h.state(case)
                (output / f'repair-{damage}-{side}-state.json').write_text(
                    json.dumps(state_after, indent=2), encoding='utf-8')
                h.require(source_hashes == {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                          for path in case.root.glob('*.cpp')},
                          'Fallback contract altered source contents')
            h.require(h.semantic_counters(pair['baseline']) == h.semantic_counters(pair['candidate']),
                      'Rediscovery changed baseline build decisions or non-output counters')
            for channel in ('stdout', 'stderr'):
                h.require(pair['baseline'][f'human_{channel}_sha256'] == pair['candidate'][f'human_{channel}_sha256'],
                          'Rediscovery changed baseline diagnostics or rebuild reasons')
            report['fallback_contracts'].append(pair)
            save()
    (output / 'passed.txt').write_text('All 96 pairs and real toolchain fallback contracts completed.\n', encoding='utf-8')
    print(json.dumps([{key: value for key, value in row.items() if key != 'pairs'}
                      for row in report['scenarios']], indent=2))


if __name__ == '__main__':
    main()
