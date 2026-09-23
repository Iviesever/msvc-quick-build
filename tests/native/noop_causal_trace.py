"""Fixed three-arm causal plan and journal checks. Never launches a process.

A complete journal is NOT a decoded/lossless ETL or a causal/performance verdict.
"""
from __future__ import annotations
import argparse
import io
from pathlib import Path
import re
import sys
from zipfile import ZipFile
import xml.etree.ElementTree as ET

import external_noop_boundary as b
import retained_noop_executor as e

ARMS = ('omit', 'middle_off', 'middle_on')
MAX_CALLS = 32
PROFILE = 'tests/native/noop_causal_trace.wprp'
SOURCES = (PROFILE, 'tests/native/noop_causal_trace.py',
           'tests/native/trace_noop_causal.ps1', 'tests/native/external_noop_boundary.py',
           'tests/native/retained_noop_executor.py',
           'tests/native/collect_external_noop_boundary.ps1')
KEYWORDS = {'ProcessThread', 'Loader', 'CSwitch', 'ReadyThread', 'FileIO', 'FileIOInit', 'Filename'}
COLLECTOR_SHA = '4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf'


def cells():
    result = []
    sequence = 0
    for block in (1, 2):
        for arm in ARMS if block == 1 else ARMS[::-1]:
            for side in ('baseline', 'candidate') if block == 1 else ('candidate', 'baseline'):
                phases = ('prime', 'final') if arm == 'omit' else ('prime', 'middle', 'final')
                rows = []
                for phase in phases:
                    sequence += 1
                    rows.append(dict(sequence=sequence, phase=phase,
                                     argv=b.ARGV + (['--timings=json'] if arm == 'middle_on' and phase == 'middle' else [])))
                result.append(dict(id=f'{block}-{arm}-{side}', block=block, arm=arm, side=side, rows=rows))
    b.require(sequence == MAX_CALLS, 'incorrect implementation budget')
    return result


def profile_check(path):
    root = ET.fromstring(e.file_bytes(path))
    ps = root.find('Profiles')
    b.require(ps is not None, 'missing profile definitions')
    collector = ps.find('SystemCollector')
    b.require(collector is not None, 'missing kernel collector')
    limit = collector.find('MaximumFileSize')
    b.require(limit is not None and limit.attrib == {'Value': '256', 'FileMode': 'Sequential'}, 'trace limit changed')
    provider = ps.find('SystemProvider')
    b.require(provider is not None, 'missing kernel provider')
    keys = provider.findall('Keywords/Keyword')
    b.require(len(keys) == len(KEYWORDS) and {x.get('Value') for x in keys} == KEYWORDS
              and all(x.get('Strict') == 'true' for x in keys), 'required strict events missing')
    b.require({x.get('Value') for x in provider.findall('Stacks/Stack')} == {'CSwitch', 'ReadyThread'}, 'wait stacks changed')
    p = ps.find('Profile')
    b.require(p is not None and p.get('Name') == 'MqbNoopCausal' and p.get('LoggingMode') == 'File', 'wrong profile')
    return dict(keywords=sorted(KEYWORDS), max_kernel_etl_mb=256, mode='Sequential')


def prepare(archive, root, repo, reviewed_commit, profile_sha):
    b.require(re.fullmatch('[0-9a-f]{40}', reviewed_commit or '') is not None, 'bad reviewed commit')
    raw_profile = e.file_bytes(repo/PROFILE)
    b.require(e.sha(e.canonical(raw_profile)) == profile_sha, 'unreviewed profile')
    profile_check(repo/PROFILE)
    b.require(e.sha(e.canonical(e.file_bytes(repo/SOURCES[-1]))) == COLLECTOR_SHA, 'legacy launcher changed')
    identity = e.verify_archive(archive)  # Existing whole-ZIP/A/B/source identity verifier.
    b.require(not root.exists(), 'new evidence directory required; no resume')
    raw = e.file_bytes(archive, 128*1024*1024)
    root.mkdir()
    for name in ('inputs', 'source', 'cells', 'fixtures', 'wpr-temp'):
        (root/name).mkdir()
    (root/'inputs/original-701.zip').write_bytes(raw)
    with ZipFile(io.BytesIO(raw)) as z:
        for side in b.BINARIES:
            (root/'inputs'/f'{side}.exe').write_bytes(z.read(side+'/mqb.exe'))
    hashes = {}
    for name in SOURCES:
        data = e.file_bytes(repo/name); hashes[name] = b.digest(data)
        p = root/'source'/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
    plan = dict(schema=1, purpose='legacy_three_arm_causal_trace', reviewed_commit=reviewed_commit,
                root=str(root.resolve()), profile_sha256=profile_sha, source_hashes=hashes,
                original=identity, max_calls=MAX_CALLS, previous_recorded_calls=42,
                proposed_cumulative_ceiling=74, cells=cells(), clears_hold=False, cause=None)
    for cell in plan['cells']:
        fixture = root/'fixtures'/cell['id']; fixture.mkdir()
        for name, data in b.SOURCES.items():
            (fixture/name).write_bytes(data)
    b.write_new(root/'plan.json', plan)
    return plan


def check_record(cell, row, record):
    b.require(b.same(record['row'], row) and record['argv'] == row['argv'], 'wrong row/argv')
    b.require(record['executable_sha256'] == b.BINARIES[cell['side']], 'wrong binary')
    b.require(record['error'] is None and type(record['exit_code']) is int and record['exit_code'] == 0,
              'original call failed')
    b.require(record['cleanup'] is None and record['root_times'] is None and
              record['output_format'] == 'powershell_merged_lines', 'invented legacy root observation')
    b.require(record['dispatch_attempted'] is True and record['clears_hold'] is False, 'invalid proof flags')
    q = record['clock']; ticks = [q[k] for k in ('outer_start', 'native_start', 'native_end', 'outer_end')]
    b.require(type(q['frequency']) is int and q['frequency'] > 0 and
              all(type(x) is int and x >= 0 for x in ticks) and ticks == sorted(ticks), 'invalid clock')
    lines = record['output_lines']
    b.require(isinstance(lines, list) and all(isinstance(x, str) for x in lines), 'invalid output')
    compiles = sum(x.startswith('[compile] ') for x in lines)
    links = sum(x.startswith('[link] ') for x in lines)
    timing = [x for x in lines if x.startswith('{"type":"mqb.timings"')]
    enabled = row['argv'][-1] == '--timings=json'
    b.require(len(timing) == int(enabled), 'wrong timing output mode')
    if timing:
        import json
        t = json.loads(timing[0], object_pairs_hook=b.unique)
        b.require(t['type'] == 'mqb.timings' and t['cache']['compile'] == {'hits': 2, 'misses': 0}
                  and t['cache']['link'] == {'hits': 1, 'misses': 0}, 'middle timing call not a no-op')
    if row['phase'] == 'prime':
        b.require(compiles == 2 and links == 1, 'not fresh prime')
    else:
        b.require(compiles == links == 0 and [x for x in lines if x.startswith('[up-to-date] ')] ==
                  ['[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe'], 'not compact no-op')
    return ticks


def audit(root):
    plan = b.load(root/'plan.json')
    b.require(b.same(plan['cells'], cells()) and type(plan['max_calls']) is int and plan['max_calls'] == 32
              and plan['clears_hold'] is False and plan['cause'] is None, 'plan changed')
    b.require(b.same(plan['previous_recorded_calls'], 42) and b.same(plan['proposed_cumulative_ceiling'], 74), 'budget changed')
    b.require(set(plan['source_hashes']) == set(SOURCES), 'source inventory changed')
    for name, digest in plan['source_hashes'].items():
        b.require(b.digest(e.file_bytes(root/'source'/name)) == digest, 'source changed: '+name)
    b.require(e.verify_archive(root/'inputs/original-701.zip') == plan['original'], 'original input changed')
    for side, digest in b.BINARIES.items():
        b.require(b.digest(e.file_bytes(root/'inputs'/f'{side}.exe')) == digest, 'binary changed')
    expected = {cell['id']+'.json' for cell in cells()}
    b.require({p.name for p in (root/'cells').iterdir()} == expected, 'missing/extra cell evidence')
    previous_end = 0; count = 0
    for cell in cells():
        record = b.load(root/'cells'/(cell['id']+'.json'))
        b.require(b.same(record['cell'], cell) and record['error'] is None and
                  len(record['calls']) == len(cell['rows']), 'cell stopped')
        b.require(set(b.manifest(record['before'])) == set(b.SOURCES), 'fixture was not fresh')
        prime, after = b.manifest(record['after_prime']), b.manifest(record['after_final'])
        b.require('.mqb/bin/timing_bench.exe' in after and prime == after, 'no-op sequence changed fixture')
        for row, call in zip(cell['rows'], record['calls']):
            ticks = check_record(cell, row, call)
            b.require(ticks[0] >= previous_end, 'overlapping/out-of-order calls')
            previous_end = ticks[-1]; count += 1
    finish = b.load(root/'completion.json')
    b.require(b.same(finish, dict(status='captured_trace_unreviewed', attempted=32,
                  error=None, trace_stop_error=None, clears_hold=False, cause=None)), 'capture stopped')
    etl = root/'trace.etl'
    b.require(etl.is_file() and not etl.is_symlink() and etl.stat().st_size > 0, 'missing ETL')
    return dict(status='journal_complete_trace_unreviewed', calls=count, cells=12,
                trace_etl_sha256=b.digest(etl.read_bytes()), trace_health_verified=False,
                cause=None, clears_hold=False,
                limitation='ETL schema/loss/process correlation/CSwitch/ReadyThread/I-O and wait stacks require separate event review.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['plan', 'prepare', 'audit'])
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--archive', type=Path); p.add_argument('--root', type=Path)
    p.add_argument('--repo', type=Path); p.add_argument('--reviewed-commit'); p.add_argument('--profile-sha')
    a = p.parse_args()
    try:
        if a.command == 'plan':
            result = dict(cells=cells(), max_calls=32, execution_allocated=False, clears_hold=False)
        elif a.command == 'prepare':
            b.require(all((a.archive, a.root, a.repo, a.reviewed_commit, a.profile_sha)), 'missing preparation arguments')
            result = prepare(a.archive, a.root, a.repo, a.reviewed_commit, a.profile_sha)
        else:
            b.require(a.root is not None, 'missing evidence root'); result = audit(a.root)
        b.write_new(a.output, result)
    except Exception as error:
        print('REFUSED: '+str(error), file=sys.stderr); return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
