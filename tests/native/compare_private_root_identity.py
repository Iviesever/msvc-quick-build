"""One private129 product comparison after (never inside) the unchanged PR ABBA.

Same freshly built main/candidate EXEs; 2 primes + 4 audits + 40 OFF pairs.
This is a new product contrast, not a replacement for the historical release
comparison. No retry, tail removal, timer adjustment, or new release authority.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import traceback

import compare_reporting as h
from compare_release_cumulative import comparison, counts

BASE = '43cadecfb1ac26d88829c319f0adff95fff9b59a'
BRANCH = 'codex/private129-root-identity-20260918'
PAIRS = 40
MAX_CALLS = 86


def permitted(event, env):
    pr = event.get('pull_request', {})
    return (env.get('GITHUB_EVENT_NAME') == 'pull_request'
            and env.get('GITHUB_RUN_ATTEMPT') == '1'
            and env.get('GITHUB_REPOSITORY') == 'Iviesever/msvc-quick-build'
            and event.get('action') == 'opened'
            and pr.get('head', {}).get('ref') == BRANCH
            and pr.get('base', {}).get('sha') == BASE)


def self_test():
    env = {'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_RUN_ATTEMPT': '1',
           'GITHUB_REPOSITORY': 'Iviesever/msvc-quick-build'}
    event = {'action': 'opened', 'pull_request': {'head': {'ref': BRANCH}, 'base': {'sha': BASE}}}
    h.require(permitted(event, env), 'registered initial request refused')
    for action in ('synchronize', 'edited', 'reopened', 'ready_for_review'):
        h.require(not permitted({**event, 'action': action}, env), 'additional measurement authorized')
    h.require(not permitted(event, {**env, 'GITHUB_RUN_ATTEMPT': '2'}), 'retry authorized')
    h.require(2 + 4 + PAIRS * 2 == MAX_CALLS, 'budget changed')
    for delta, flag in ((2.0, True), (-2.0, False), (0.0, False)):
        pairs = [{'baseline': {'external_ms': 20.0}, 'candidate': {'external_ms': 20.0 + delta}}
                 for _ in range(PAIRS)]
        h.require(comparison(pairs, warm=True)['practical_regression_flag'] == flag,
                  'original private warm flag changed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--workspace', type=Path)
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print('Private root-identity budget/order/statistics contracts passed; no MQB executed.')
        return
    h.require(os.name == 'nt' and args.workspace is not None, 'Windows workspace required')
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    h.require(permitted(event, os.environ), 'not the initial authorized product experiment')
    workspace = args.workspace.resolve()
    perf = workspace / 'performance-out'
    provenance = json.loads((perf/'provenance/identity-before.json').read_text(encoding='utf-8'))
    h.require(provenance['inputs']['baseline']['sha'] == BASE, 'wrong actual baseline')
    h.require(provenance['inputs']['candidate']['sha'] == event['pull_request']['head']['sha'], 'wrong head')
    binaries = {side: perf/side/'mqb.exe' for side in ('baseline', 'candidate')}
    hashes = {side: h.digest(p.read_bytes()) for side, p in binaries.items()}
    h.require(all(hashes[s] == provenance['inputs'][s]['binary_sha256'] for s in binaries), 'binary drift')
    output = perf/'private-root-identity'
    h.require(not output.exists(), 'refuse overwritten or resumed evidence')
    attempted = 0
    with h.Recorder(output) as recorder:
        result = dict(base=BASE, head=event['pull_request']['head']['sha'],
                      run=os.environ['GITHUB_RUN_ID'], attempt=1, binary_sha256=hashes,
                      source_identity=provenance['inputs'], maximum_calls=MAX_CALLS,
                      planned_pairs=PAIRS, completed=False, historical_risks_cleared=False,
                      release_authorized=False, audits=[], pairs=[])
        def save():
            result['attempted'] = attempted
            result['not_run'] = MAX_CALLS - attempted
            (output/'result.json').write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf-8')
        def save_budget():
            budget = dict(maximum=MAX_CALLS, attempted=attempted, not_run=MAX_CALLS-attempted, retry=0)
            (output/'budget.json').write_text(json.dumps(budget), encoding='utf-8')
        save()
        save_budget()
        try:
            with tempfile.TemporaryDirectory(prefix='mqb-cumulative-') as temp:
                h.require(Path(temp).drive.upper() == 'C:', 'unexpected temporary volume; do not substitute')
                case = h.fixture(Path(temp), 'private', 129)
                for source in sorted(case.root.glob('*.cpp')):
                    header = source.with_suffix('.hpp')
                    header.write_bytes((case.root/'common.hpp').read_bytes())
                    source.write_text(source.read_text(encoding='utf-8').replace('common.hpp', header.name),
                                      encoding='utf-8')
                result['fixture_root'] = str(case.root)
                def run(side, label, *, prime=False, audit=False):
                    nonlocal attempted
                    h.require(attempted < MAX_CALLS, 'spent call budget')
                    attempted += 1
                    save_budget()  # Full calls use the accepted append-only journal.
                    row = recorder.run(binaries[side], case, label+'-'+side, timings=audit)
                    activity = counts(recorder, row)
                    expected = 129 if prime and side == 'baseline' else 0
                    h.require(activity == dict(compile=expected, link=int(expected > 0), archive=0, pch=0),
                              'unexpected compilation/linking; retain first sample and stop')
                    if not prime:
                        h.require(b'[up-to-date] 129 translation units\n' in recorder.human(row, 'stdout'),
                                  'missing full warm summary')
                    if audit:
                        h.assert_warm(row, case, candidate=side == 'candidate')
                        result['audits'].append(row)
                    return row
                for side in binaries:
                    run(side, 'prime', prime=True)
                for side in binaries:
                    run(side, 'pre', audit=True)
                before = h.state(case)  # Metadata only: no full-content preread.
                for i in range(PAIRS):
                    order = ('baseline', 'candidate') if i % 2 == 0 else ('candidate', 'baseline')
                    pair = {'pair': i + 1}
                    for side in order:
                        pair[side] = run(side, f'private-{i + 1}')
                    result['pairs'].append(pair)
                after_scores = h.state(case)
                h.require(after_scores == before, 'warm scoring changed cache/output metadata')
                for side in binaries:
                    run(side, 'post', audit=True)
                h.require(h.state(case) == before, 'warm audits changed cache/output metadata')
                for i in (0, 2):
                    h.require(h.semantic_counters(result['audits'][i]) ==
                              h.semantic_counters(result['audits'][i+1]), 'audit A/B semantics differ')
                result['summary'] = comparison(result['pairs'], warm=True)
                result['metadata_before'] = before
                result['metadata_after_scores'] = after_scores
                # Preserve generated inputs only after every scored/audit invocation.
                inputs = output/'fixture-inputs'
                inputs.mkdir()
                for p in sorted(case.root.iterdir()):
                    if p.suffix in ('.cpp', '.hpp'):
                        shutil.copyfile(p, inputs/p.name)
                result['input_hashes'] = {p.name:h.digest(p.read_bytes()) for p in sorted(inputs.iterdir())}
                h.require(len(result['input_hashes']) == 259, 'fixture input inventory incomplete')
                h.require(attempted == MAX_CALLS, 'incomplete measurement')
            h.require(all(h.digest(p.read_bytes()) == hashes[s] for s,p in binaries.items()), 'binary mutated')
        except BaseException:
            (output/'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
            save()
            raise
        recorder.finalize()
        result['completed'] = True
        save()


if __name__ == '__main__':
    main()
