"""Post-prime trace windows: fixed plan and offline journals, never execution.

Neither a complete journal nor nonempty ETLs certify event health or clear HOLD.
The original 001 collector/profile/auditor remain unchanged.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import ntpath
from pathlib import Path
import re
import sys
from zipfile import ZipFile

import noop_causal_trace as c

b, e = c.b, c.e
SOURCES = c.SOURCES + ('tests/native/noop_causal_windows.py',
                       'tests/native/trace_noop_causal_windows.ps1')
CONTROL_SOURCE_SHA = '546206fa04fa50314fa7017738f4cc8801461c0b9d91c273ecca91ea053c0bdb'
PROFILE_SHA = '7ef59ac788bfc554273d4a3ad028288e79fec428889be395765fd176c79406ec'
LIMITS = dict(kernel_file_bytes=268435456, segment_etl_bytes=268435456,
              total_trace_bytes=536870912, minimum_free_bytes=4294967296,
              max_windows=12, max_wpr_commands=60)


def specification():
    return dict(schema=2, purpose='post_prime_causal_windows', cells=c.cells(),
                max_calls=32, traced_calls=20, previous_recorded_calls=72,
                proposed_cumulative_ceiling=104, limits=LIMITS.copy(),
                execution_allocated=False, cause=None, clears_hold=False)


def file_hash(path):
    b.require(path.is_file() and not path.is_symlink(), 'not an ordinary trace file')
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def trace_bytes(root):
    """Observe retained ETL and temp bytes. Not a filesystem quota or atomic snapshot."""
    total = 0
    b.require(root.is_dir() and not root.is_symlink(), 'missing trace directory')
    for p in root.rglob('*'):
        b.require(not p.is_symlink(), 'trace symlink refused')
        if p.is_file():
            size = p.stat().st_size
            b.require('temp' not in p.relative_to(root).parts or size < LIMITS['kernel_file_bytes'],
                      'kernel trace checkpoint reached')
            total += size
        else:
            b.require(p.is_dir(), 'special trace member refused')
    b.require(total <= LIMITS['total_trace_bytes'], 'total trace checkpoint exceeded')
    return total


def prepare(archive, root, repo, reviewed_commit, profile_sha):
    b.require(re.fullmatch('[0-9a-f]{40}', reviewed_commit or '') is not None, 'bad reviewed commit')
    b.require(profile_sha == PROFILE_SHA and
              e.sha(e.canonical(e.file_bytes(repo/c.PROFILE))) == PROFILE_SHA, 'profile changed')
    c.profile_check(repo/c.PROFILE)
    b.require(e.sha(e.canonical(e.file_bytes(repo/c.SOURCES[-1]))) == c.COLLECTOR_SHA,
              'legacy launcher changed')
    b.require(e.sha(e.canonical(e.file_bytes(repo/'tests/native/trace_noop_causal.ps1'))) == CONTROL_SOURCE_SHA,
              'owned control source changed')
    identity = e.verify_archive(archive)
    snapshots = {name: e.file_bytes(repo/name) for name in SOURCES}
    raw = e.file_bytes(archive, 128 * 1024 * 1024)
    b.require(not root.exists(), 'new evidence directory required; no resume')
    root.mkdir()
    for name in ('inputs', 'source', 'cells', 'primes', 'fixtures', 'traces'):
        (root/name).mkdir()
    (root/'inputs/original-701.zip').write_bytes(raw)
    with ZipFile(io.BytesIO(raw)) as z:
        for side in b.BINARIES:
            (root/'inputs'/f'{side}.exe').write_bytes(z.read(side+'/mqb.exe'))
    for name, data in snapshots.items():
        p = root/'source'/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
    plan = dict(**specification(), reviewed_commit=reviewed_commit, root=str(root.resolve()),
                profile_sha256=profile_sha, source_hashes={n: b.digest(v) for n, v in snapshots.items()},
                original=identity)
    for cell in plan['cells']:
        fixture = root/'fixtures'/cell['id']; fixture.mkdir()
        for name, data in b.SOURCES.items():
            (fixture/name).write_bytes(data)
    b.write_new(root/'plan.json', plan)
    return plan


def same_path(actual, expected):
    return isinstance(actual, str) and ntpath.normpath(actual) == ntpath.normpath(expected)


def check_controls(root, plan, cell, window, first):
    instance = window['instance']
    b.require(isinstance(instance, str) and re.fullmatch('MQB-NoopWindow-[0-9a-f]{32}', instance),
              'bad window instance')
    b.require(b.same(window['first_control'], first) and b.same(window['last_control'], first+4),
              'window control sequence changed')
    base = plan['root']; label = 'MQB_NOOP_WINDOW|'+cell['id']
    expected = [
        ['-start', base+'/source/'+c.PROFILE+'!MqbNoopCausal.Verbose', '-filemode',
         '-recordtempto', base+'/traces/'+cell['id']+'/temp'],
        ['-marker', label+'|begin'], ['-marker', label+'|end'],
        ['-status', 'collectors', '-details'],
        ['-stop', base+'/traces/'+cell['id']+'/trace.etl', 'MQB post-prime causal window; not a score', '-skipPdbGen'],
    ]
    for offset, argv in enumerate(expected):
        stem = f'wpr-{first+offset:02}'
        start, result = (b.load(root/(stem+suffix)) for suffix in ('.started.json', '.json'))
        actual = start['argv']
        b.require(isinstance(actual, list) and len(actual) == len(argv), 'wrong WPR argv length')
        paths = {1, 4} if offset == 0 else {1} if offset == 4 else set()
        b.require(all(same_path(a, v) if i in paths else a == v
                      for i, (a, v) in enumerate(zip(actual, argv))), 'wrong WPR command/target')
        b.require(b.same(start, dict(argv=actual, instance=instance)), 'bad WPR start journal')
        b.require(result['instance'] == instance and b.same(result['argv'], actual) and
                  b.same(result['exit_code'], 0) and result['command_error'] is None and
                  result['journal_errors'] == [], 'WPR control failed or mismatched')
    ticks = [window[k] for k in ('start_qpc', 'ready_qpc', 'calls_end_qpc', 'stop_qpc', 'stopped_qpc')]
    b.require(all(type(x) is int and x > 0 for x in ticks) and ticks == sorted(ticks), 'bad window clock')
    b.require(window['stop_attempted'] is True and window['stop_error'] is None and
              window['owned_after_stop'] is False, 'window not released')
    return ticks


def audit(root):
    plan = b.load(root/'plan.json')
    for key, value in specification().items():
        b.require(b.same(plan[key], value), 'plan changed: '+key)
    b.require(plan['profile_sha256'] == PROFILE_SHA, 'profile identity changed')
    b.require(set(plan['source_hashes']) == set(SOURCES), 'source inventory changed')
    for name, digest in plan['source_hashes'].items():
        b.require(b.digest(e.file_bytes(root/'source'/name)) == digest, 'source changed: '+name)
    b.require(e.sha(e.canonical(e.file_bytes(root/'source'/c.PROFILE))) == PROFILE_SHA, 'profile bytes changed')
    b.require(e.sha(e.canonical(e.file_bytes(root/'source/tests/native/trace_noop_causal.ps1'))) == CONTROL_SOURCE_SHA,
              'control source bytes changed')
    b.require(e.verify_archive(root/'inputs/original-701.zip') == plan['original'], 'original input changed')
    for side, digest in b.BINARIES.items():
        b.require(b.digest(e.file_bytes(root/'inputs'/f'{side}.exe')) == digest, 'binary changed')
    ids = {x['id'] for x in c.cells()}
    for folder in ('cells', 'primes'):
        b.require({p.name for p in (root/folder).iterdir()} == {x+'.json' for x in ids}, 'missing/extra '+folder)
    b.require({p.name for p in (root/'traces').iterdir()} == ids, 'missing/extra trace windows')
    expected_controls = {f'wpr-{i:02}{suffix}' for i in range(1, 61) for suffix in ('.started.json', '.json')}
    b.require({p.name for p in root.glob('wpr-*')} == expected_controls, 'missing/extra WPR control journal')
    finish = b.load(root/'completion.json')
    b.require(b.same(finish, dict(status='captured_windows_unreviewed', attempted=32, error=None,
              trace_stop_error=None, clears_hold=False, cause=None)), 'window capture stopped')
    host = b.load(root/'host.json'); frequency = host['qpc_frequency']
    b.require(type(frequency) is int and frequency > 0 and host['reviewed_commit'] == plan['reviewed_commit'],
              'invalid host clock/source')
    previous_end = 0; previous_stop = 0; instances = set(); traces = []
    for index, cell in enumerate(c.cells()):
        record = b.load(root/'cells'/(cell['id']+'.json'))
        checkpoint = b.load(root/'primes'/(cell['id']+'.json'))
        b.require(b.same(record['cell'], cell) and record['error'] is None and
                  len(record['calls']) == len(cell['rows']), 'cell stopped')
        b.require(b.same(checkpoint, dict(cell=cell, before=record['before'], after_prime=record['after_prime'],
                  call=record['calls'][0])), 'prime checkpoint mismatch')
        b.require(set(b.manifest(record['before'])) == set(b.SOURCES), 'fixture not fresh')
        after = b.manifest(record['after_final'])
        b.require(b.manifest(record['after_prime']) == after and '.mqb/bin/timing_bench.exe' in after, 'fixture changed')
        w = record['window']; start, ready, end, stop, stopped = check_controls(root, plan, cell, w, index*5+1)
        b.require(w['instance'] not in instances, 'trace instance reused'); instances.add(w['instance'])
        for row, call in zip(cell['rows'], record['calls']):
            ticks = c.check_record(cell, row, call)
            b.require(call['clock']['frequency'] == frequency and ticks[0] >= previous_end, 'call clock/order changed')
            b.require(all(type(call[k]) is int and call[k] > 0 for k in ('host_tid_before', 'host_tid_after')), 'missing host thread')
            if row['phase'] == 'prime':
                b.require(ticks[0] > previous_stop and ticks[-1] < start, 'prime overlaps trace boundary')
            else:
                b.require(ready <= ticks[0] <= ticks[-1] <= end, 'call outside its window')
            previous_end = ticks[-1]
        previous_stop = stopped
        trace = root/'traces'/cell['id']/'trace.etl'
        b.require(trace.is_file() and 0 < trace.stat().st_size <= LIMITS['segment_etl_bytes'], 'segment ETL limit/missing')
        traces.append(dict(cell=cell['id'], size=trace.stat().st_size, sha256=file_hash(trace)))
    observed = trace_bytes(root/'traces')
    return dict(status='window_journal_complete_trace_unreviewed', calls=32, traced_calls=20, windows=12,
                trace_files=traces, observed_trace_bytes=observed, trace_health_verified=False,
                cause=None, clears_hold=False,
                limitation='Requires native ETL loss/schema, 24 markers and 20 measured process/thread/I-O correlations; primes are untraced.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('plan', 'prepare', 'audit'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--root', type=Path); p.add_argument('--repo', type=Path)
    p.add_argument('--archive', type=Path); p.add_argument('--reviewed-commit'); p.add_argument('--profile-sha')
    a = p.parse_args()
    try:
        if a.command == 'plan':
            result = specification()
        elif a.command == 'prepare':
            b.require(all((a.archive, a.root, a.repo, a.reviewed_commit, a.profile_sha)), 'missing preparation args')
            result = prepare(a.archive, a.root, a.repo, a.reviewed_commit, a.profile_sha)
        else:
            b.require(a.root is not None, 'missing root'); result = audit(a.root)
        b.write_new(a.output, result)
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print('REFUSED: '+str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
