"""One fixed, external-timing decision experiment; never modifies product code.

Forty alternating pairs per warm scenario; six independently reset cold pairs.
Read alongside the earlier evidence, not as a replacement or a search for green.
A practical warm regression is flagged when its median exceeds max(0.5 ms, 3%)
and its bootstrap median interval excludes zero. Inconclusive is not proof of
non-regression. No automatic merge is performed. Binary identities and all raw
observations are retained, including adverse pairs and partial failures.
"""
from __future__ import annotations
import argparse
import importlib.util
import hashlib
import json
import random
import shutil
import statistics
import sys
import tempfile
from pathlib import Path


def check(value, message):
    if not value:
        raise RuntimeError(message)


def ci(values):
    rng = random.Random(157158159)
    medians = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(5000))
    return [medians[124], medians[4874]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('report_harness', args.harness)
    h = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = h
    spec.loader.exec_module(h)
    h.self_test()
    output = args.output.resolve()
    check(not output.exists(), 'refusing to replace previous evidence')
    output.mkdir(parents=True)
    identities = {}
    binaries = {}
    for name in ['main', 'pr157', 'pr158', 'pr159']:
        root = args.binaries.resolve() / name
        binaries[name] = root / 'mqb.exe'
        identities[name] = json.loads((root / 'identity.json').read_text(encoding='utf-8-sig'))
        check(identities[name]['binary_sha256'].lower() == hashlib.sha256(binaries[name].read_bytes()).hexdigest(), 'binary identity mismatch')
    result = {'identities': identities, 'harness_sha256': hashlib.sha256(args.harness.read_bytes()).hexdigest(),
              'definition': __doc__, 'measurement': 'external process start through completion and pipe drain, timings OFF',
              'terminal': 'captured pipes; not interactive terminal', 'experiments': []}
    def save():
        (output / 'decisions.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    save()
    with tempfile.TemporaryDirectory(prefix='mqb-review-') as temporary:
        fixtures = Path(temporary)
        for number, baseline_name, candidate_name in [(157, 'main', 'pr157'), (158, 'pr157', 'pr158'), (159, 'main', 'pr159')]:
            base, candidate = binaries[baseline_name], binaries[candidate_name]
            root = fixtures / str(number)
            root.mkdir()
            recorder = h.Recorder(output / str(number))
            exp = {'pr': number, 'baseline': baseline_name, 'candidate': candidate_name, 'scenarios': []}
            result['experiments'].append(exp)
            small = h.fixture(root, 'small', 2)
            scale = h.fixture(root, 'common129', 129)
            static = h.fixture(root, 'static129', 129, static=True)
            pch = h.fixture(root, 'pch', 2, pch=True)
            discovery = h.fixture(root, 'discovery', 2)
            discovery.arguments = ['build', 'main.cpp', '--env', 'vs', '--release', '--std', 'latest', '-o', 'report']
            # .mqb exists before the first index snapshot; transcripts are always
            # outside the fixture. A true persistent discovery hit is required.
            (discovery.root / '.mqb').mkdir()
            modes = [(small, False, 'auto'), (scale, False, 'auto'), (scale, False, '1'),
                     (scale, True, 'auto'), (static, False, 'auto'), (static, True, 'auto'),
                     (pch, False, 'auto'), (discovery, False, 'auto')]
            for case, verbose, jobs in modes:
                name = f'{case.name}-{"verbose" if verbose else "default"}-j{jobs}'
                def run(binary, label, timed=False):
                    return recorder.run(binary, case, label, verbose=verbose, timings=timed, jobs=jobs)
                run(base, name + '-prime-A')
                run(candidate, name + '-prime-B')
                def audit(binary, label):
                    row = run(binary, label, True)
                    t = row['timing']
                    check(t is not None, label + ': missing timing record')
                    c = t['counters']
                    check(all(c[k] == 0 for k in ['cl_processes_launched', 'link_processes_launched', 'lib_processes_launched', 'cache_files_written']), label + ': not a cache-preserving no-op')
                    check(t['cache']['compile']['hits'] > 0 and t['cache']['compile']['misses'] == 0, label + ': not a compile hit')
                    return h.semantic_counters(row)
                check(audit(base, name + '-audit-A') == audit(candidate, name + '-audit-B'), name + ': semantic counters differ')
                before = h.state(case)
                rows = []
                for i in range(40):
                    pair = {'pair': i + 1, 'orientation': 'AB' if i % 2 == 0 else 'BA'}
                    order = [('baseline', base), ('candidate', candidate)]
                    if i % 2:
                        order.reverse()
                    for side, binary in order:
                        row = run(binary, f'{name}-{i+1}-{side}')
                        text = recorder.human(row, 'stdout')
                        check(b'[compile]' not in text and b'[link]' not in text and b'[archive]' not in text, name + ': unexpected execution')
                        pair[side] = row
                    check(pair['baseline']['human_stderr_sha256'] == pair['candidate']['human_stderr_sha256'], name + ': diagnostics changed')
                    if number != 159 or verbose:
                        check(pair['baseline']['human_stdout_sha256'] == pair['candidate']['human_stdout_sha256'], name + ': detailed output changed')
                    rows.append(pair)
                check(before == h.state(case), name + ': project artifacts/caches mutated')
                check(audit(base, name + '-post-A') == audit(candidate, name + '-post-B'), name + ': postcondition differs')
                summary = h.summarize(rows)
                summary['median_ci95_ms'] = ci(summary['paired_deltas_ms'])
                threshold = max(0.5, statistics.median(p['baseline']['external_ms'] for p in rows) * 0.03)
                summary['practical_threshold_ms'] = threshold
                summary['material_regression_flag'] = summary['paired_median_delta_ms'] > threshold and summary['median_ci95_ms'][0] > 0
                exp['scenarios'].append({'name': name, 'summary': summary, 'pairs': rows})
                save()
                print(number, name, json.dumps(summary), flush=True)
            # Cold cost is measured independently, never pooled with warm paths.
            rows = []
            for i in range(6):
                pair = {'pair': i + 1, 'orientation': 'AB' if i % 2 == 0 else 'BA'}
                order = [('baseline', base), ('candidate', candidate)]
                if i % 2:
                    order.reverse()
                for side, binary in order:
                    shutil.rmtree(scale.root / '.mqb')
                    pair[side] = recorder.run(binary, scale, f'cold-{i+1}-{side}')
                    check(scale.output.is_file(), 'cold output missing')
                rows.append(pair)
            exp['scenarios'].append({'name': 'common129-cold', 'summary': h.summarize(rows), 'pairs': rows})
            save()
    (output / 'passed.txt').write_text('All three fixed comparisons completed; no dropped pairs. Review numbers before merging.\n')

if __name__ == '__main__':
    main()
