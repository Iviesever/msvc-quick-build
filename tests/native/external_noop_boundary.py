"""Prepare/audit the fixed #207 diagnostic; never launches MQB or clears HOLD."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys
from zipfile import ZipFile

ARCHIVE = '940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3'
BINARIES = {
    'baseline': '9c23cde3450a7c10c2edca75838923601c9a4fd300617928b04263d128d19b58',
    'candidate': '76b55ca6176eb816131dad7de624170bcc3b21631b13190f0e943034d00655a9',
}
REVISIONS = {'baseline': 'f32be264b9862f1ba0682535ae808d4fb52c3fa6',
             'candidate': 'bd12a59e9894068384d1a9b56b052ce5fec86098'}
ARGV = ['main.cpp', 'helper.cpp', '--output', 'timing_bench', '-j', '1']
SOURCES = {'helper.cpp': b'int timing_helper() { return 42; }\r\n',
           'main.cpp': b'int timing_helper(); int main() { return timing_helper() == 42 ? 0 : 1; }\r\n'}
COLLECTORS = ['external_noop_boundary.py', 'collect_external_noop_boundary.ps1']
MAX_CALLS = 40


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    result = {}
    for k, v in pairs:
        require(k not in result, 'duplicate JSON member: ' + k)
        result[k] = v
    return result


def load(path):
    require(path.stat().st_size <= 16 * 1024 * 1024, 'oversize JSON')
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))


def write_new(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, indent=2, allow_nan=False)
        f.write('\n')


def schedule():
    rows = []
    def pair(mode, number, sides, control):
        for position, side in enumerate(sides):
            fixture = f'{control}-{number}-{mode}-{position}-{side}'
            for phase in ('prime', 'noop'):
                rows.append(dict(sequence=len(rows)+1, mode=mode, pair=number,
                                 side=side, position=position, control=control,
                                 fixture=fixture, phase=phase))
    for n in range(1, 5):
        modes = ('legacy', 'process') if n % 2 else ('process', 'legacy')
        sides = ('baseline', 'candidate') if n % 2 else ('candidate', 'baseline')
        for mode in modes:
            pair(mode, n, sides, 'AB')
    for mode in ('legacy', 'process'):
        pair(mode, 1, ('candidate', 'candidate'), 'BB')
    require(len(rows) == MAX_CALLS, 'implementation budget mismatch')
    return rows


def prepare(archive, root):
    # All input checks precede creation of the new evidence root.
    require(archive.stat().st_size <= 128*1024*1024, 'oversize ZIP')
    data = archive.read_bytes()
    require(digest(data) == ARCHIVE, 'not the original #701 artifact')
    import io
    with ZipFile(io.BytesIO(data)) as z:
        require(len(z.namelist()) == len(set(z.namelist())), 'duplicate ZIP member')
        require(z.testzip() is None, 'ZIP CRC error')
        identity = json.loads(z.read('provenance/identity-before.json'), object_pairs_hook=unique)
        after = json.loads(z.read('provenance/identity-after.json'), object_pairs_hook=unique)
        require(identity['run_id'] == '35681224762' and identity['attempt'] == '1', 'wrong run')
        binaries = {}
        for side, expected in BINARIES.items():
            b = z.read(side+'/mqb.exe')
            require(digest(b) == expected == after[side] == identity['inputs'][side]['binary_sha256'],
                    'original executable identity mismatch')
            require(identity['inputs'][side]['sha'] == REVISIONS[side], 'source revision mismatch')
            require(digest(z.read('provenance/'+side+'-source.zip')) ==
                    identity['inputs'][side]['source_zip_sha256'], 'source archive mismatch')
            binaries[side] = b
    collector_bytes = {n: Path(__file__).with_name(n).read_bytes() for n in COLLECTORS}
    root.mkdir(exist_ok=False)  # No overwrite, cleanup, resume, or replacement directory.
    for name in ('inputs', 'calls', 'fixtures', 'collector-source'):
        (root/name).mkdir()
    with (root/'inputs/original-701.zip').open('xb') as f:
        f.write(data)
    for side, b in binaries.items():
        (root/'inputs'/side).mkdir()
        with (root/'inputs'/side/'mqb.exe').open('xb') as f:
            f.write(b)
    for n, b in collector_bytes.items():
        with (root/'collector-source'/n).open('xb') as f:
            f.write(b)
    plan = dict(schema=1, source_run='35681224762', archive_sha256=ARCHIVE,
                binaries=BINARIES, revisions=REVISIONS, argv=ARGV, max_calls=MAX_CALLS,
                collector_hashes={n: digest(b) for n, b in collector_bytes.items()},
                source_hashes={n: digest(b) for n, b in SOURCES.items()},
                root=str(root.resolve()), rows=schedule(), clears_hold=False)
    write_new(root/'plan.json', plan)
    return plan


def same(a, b):
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


def validate_plan(plan):
    require(type(plan['schema']) is int and plan['schema'] == 1 and plan['source_run'] == '35681224762', 'wrong plan schema/run')
    require(plan['archive_sha256'] == ARCHIVE and plan['binaries'] == BINARIES and
            plan['revisions'] == REVISIONS, 'wrong plan identity')
    require(plan['source_hashes'] == {n: digest(b) for n, b in SOURCES.items()}, 'wrong fixture hashes')
    require(plan['argv'] == ARGV and type(plan['max_calls']) is int and plan['max_calls'] == MAX_CALLS and
            same(plan['rows'], schedule()) and plan['clears_hold'] is False, 'changed fixed plan')


def manifest(value):
    require(isinstance(value, list) and 2 <= len(value) <= 2048, 'invalid file manifest')
    result = {}
    for f in value:
        p = f['path']
        require(isinstance(p, str) and not p.startswith('/') and '\\' not in p and
                ':' not in p and all(x not in ('', '.', '..') for x in p.split('/')), 'bad relative path')
        require(p.casefold() not in {x.casefold() for x in result}, 'duplicate path')
        require(type(f['size']) is int and f['size'] >= 0 and
                type(f['mtime_ticks']) is int and f['mtime_ticks'] >= 0, 'bad file value')
        require(isinstance(f['sha256'], str) and len(f['sha256']) == 64 and
                all(c in '0123456789abcdef' for c in f['sha256']), 'bad digest')
        result[p] = f
    for n, b in SOURCES.items():
        require(n in result and result[n]['sha256'] == digest(b) and result[n]['size'] == len(b),
                'fixture source changed')
    return result


def validate_call(row, record, before, after, output_lines):
    require(same(record['row'], row) and record['argv'] == ARGV, 'call not in fixed plan')
    require(record['executable_sha256'] == BINARIES[row['side']], 'wrong executable')
    require(record['error'] is None and type(record['exit_code']) is int and record['exit_code'] == 0,
            'original call failed or observation incomplete')
    require(record['dispatch_attempted'] is True and record['clears_hold'] is False, 'invalid dispatch/proof')
    require(record['cleanup'] is None, 'successful call contains failure cleanup')
    q = record['clock']; freq = q['frequency']
    require(type(freq) is int and freq > 0, 'invalid QPC frequency')
    keys = ('outer_start', 'native_start', 'native_end', 'outer_end')
    ticks = [q[k] for k in keys]
    require(all(type(t) is int and t >= 0 for t in ticks) and ticks == sorted(ticks), 'invalid QPC boundaries')
    root = record['root_times']
    if row['mode'] == 'legacy':
        require(root is None and record['output_format'] == 'powershell_merged_lines', 'invented legacy process time')
    else:
        require(record['output_format'] == 'separate_bytes' and isinstance(root, dict), 'missing process observation')
        require(type(root['pid']) is int and root['pid'] > 0, 'missing root PID')
        for k in ('created_100ns', 'exited_100ns', 'kernel_100ns', 'user_100ns'):
            require(type(root[k]) is int and root[k] >= 0, 'missing OS time')
        require(0 < root['created_100ns'] <= root['exited_100ns'], 'invalid process lifetime')
        start, returned, exited, drained = [q[k] for k in ('native_start', 'start_return', 'wait_return', 'native_end')]
        require(all(type(x) is int for x in (start, returned, exited, drained)) and
                start <= returned <= exited <= drained, 'invalid launch/drain boundary')
    require(isinstance(output_lines, list) and all(isinstance(x, str) for x in output_lines), 'invalid output')
    require(not any(x.startswith('{"type":"mqb.timings') for x in output_lines), 'unexpected internal timing')
    compiles = sum(x.startswith('[compile] ') for x in output_lines)
    links = sum(x.startswith('[link] ') for x in output_lines)
    b, a = manifest(before), manifest(after)
    require('.mqb/bin/timing_bench.exe' in a, 'missing fixture output')
    if row['phase'] == 'noop':
        # Fixed ARGV is non-verbose: Diagnostics.cpp aggregates reused TUs.
        # Check their count and the target, not an arbitrary number of lines.
        progress = [x for x in output_lines if x.startswith('[up-to-date] ')]
        require(compiles == links == 0 and progress == [
                    '[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe'],
                'not the registered compact two-source no-op')
        require(b == a, 'no-op changed observed file bytes/metadata')
    else:
        require(set(b) == set(SOURCES) and compiles == 2 and links == 1, 'not a fresh two-source priming')
    return {'row': row, 'outer_ms': (ticks[3]-ticks[0])*1000/freq,
            'native_envelope_ms': (ticks[2]-ticks[1])*1000/freq,
            'root_lifetime_ms': None if root is None else (root['exited_100ns']-root['created_100ns'])/10000,
            'root_cpu_ms': None if root is None else (root['kernel_100ns']+root['user_100ns'])/10000}


def audit_call(root, row):
    prefix = root/'calls'/f"{row['sequence']:02d}"
    started = load(Path(str(prefix)+'.started.json'))
    require(same(started['row'], row) and started['argv'] == ARGV, 'missing/wrong started record')
    plan = load(root/'plan.json')
    norm = lambda x: str(x).replace('\\', '/').rstrip('/').casefold()
    require(norm(started['executable']) == norm(plan['root']+'/inputs/'+row['side']+'/mqb.exe') and
            norm(started['cwd']) == norm(plan['root']+'/fixtures/'+row['fixture']) and
            started['executable_sha256'] == BINARIES[row['side']], 'wrong recorded invocation path')
    if row['phase'] == 'noop':
        previous = root/'calls'/f"{row['sequence']-1:02d}.after.json"
        require(load(previous) == load(Path(str(prefix)+'.before.json')), 'fixture changed between prime and no-op')
    record = load(Path(str(prefix)+'.result.json'))
    if row['mode'] == 'legacy':
        lines = record['output_lines']
    else:
        blobs = [Path(str(prefix)+suffix).read_bytes() for suffix in ('.stdout.bin', '.stderr.bin')]
        require(all(len(b) <= 16*1024*1024 for b in blobs), 'unexpected output size')
        lines = [line for b in blobs for line in b.decode('utf-8', errors='replace').splitlines()]
    return validate_call(row, record, load(Path(str(prefix)+'.before.json')),
                         load(Path(str(prefix)+'.after.json')), lines)



def validate_call_inventory(root, rows):
    """A completed study must account for every dispatch and its evidence.

    A dangling started marker is evidence of an unfinished attempt, not a file
    that can be ignored merely because the expected result count was reached.
    This checks retained journal names, not the existence of unlogged processes.
    """
    expected = set()
    for row in rows:
        prefix = f"{row['sequence']:02d}"
        suffixes = ['.started.json', '.before.json', '.result.json',
                    '.after.json', '.validated.json']
        if row['mode'] == 'process':
            suffixes += ['.stdout.bin', '.stderr.bin']
        expected.update(prefix + suffix for suffix in suffixes)
    actual = set()
    for entry in (root/'calls').iterdir():
        require(not entry.is_symlink() and entry.is_file(), 'non-file call evidence')
        require(entry.name in expected, 'unplanned call evidence: ' + entry.name)
        actual.add(entry.name)
    missing = expected - actual
    if missing:
        raise FileNotFoundError('missing call evidence: ' + ', '.join(sorted(missing)))


def audit(root):
    plan = load(root/'plan.json'); validate_plan(plan)
    require(load(root/'host.json')['original_plan_sha256'] == digest((root/'plan.json').read_bytes()), 'plan changed')
    require(digest((root/'inputs/original-701.zip').read_bytes()) == ARCHIVE, 'archive changed')
    for side, h in BINARIES.items():
        require(digest((root/'inputs'/side/'mqb.exe').read_bytes()) == h, 'executable changed')
    for n in COLLECTORS:
        require(digest((root/'collector-source'/n).read_bytes()) == plan['collector_hashes'][n], 'collector snapshot changed')
    completion = load(root/'completion.json')
    require(same(completion, dict(status='completed', attempted=40, validated=40, error=None, clears_hold=False)),
            'study stopped/incomplete; retain first failure and unused slots')
    rows = schedule()
    validate_call_inventory(root, rows)
    samples = []
    for row in rows:
        sample = audit_call(root, row)
        verdict = root/'calls'/f"{row['sequence']:02d}.validated.json"
        require(same(load(verdict), sample), 'per-call validation does not match raw evidence')
        samples.append(sample)
    pairs = []
    for mode in ('legacy', 'process'):
        for number in range(1, 5):
            cell = [s for s in samples if s['row']['mode'] == mode and s['row']['control'] == 'AB' and
                    s['row']['pair'] == number and s['row']['phase'] == 'noop']
            a = next(s for s in cell if s['row']['side'] == 'baseline')
            b = next(s for s in cell if s['row']['side'] == 'candidate')
            pairs.append(dict(mode=mode, pair=number, outer_delta_ms=b['outer_ms']-a['outer_ms'],
                              native_delta_ms=b['native_envelope_ms']-a['native_envelope_ms'],
                              root_lifetime_delta_ms=None if mode == 'legacy' else b['root_lifetime_ms']-a['root_lifetime_ms']))
    return dict(schema=1, status='complete_diagnostic_only', samples=samples, pairs=pairs,
                mode_medians={mode: {k: statistics.median(p[k] for p in pairs if p['mode'] == mode)
                              for k in ('outer_delta_ms', 'native_delta_ms')} for mode in ('legacy', 'process')},
                clears_hold=False, cause=None,
                limits='No cross-clock subtraction, BB cost subtraction, historical score replacement or automatic causal classification.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['prepare', 'audit', 'check-call']); p.add_argument('path', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--sequence', type=int)
    a = p.parse_args()
    try:
        if a.command == 'prepare':
            prepare(a.path, a.output)
            print(a.output/'plan.json')
        elif a.command == 'check-call':
            require(a.sequence in range(1, MAX_CALLS+1), 'invalid call number')
            validate_plan(load(a.path/'plan.json'))
            write_new(a.output, audit_call(a.path, schedule()[a.sequence-1]))
        else:
            write_new(a.output, audit(a.path))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as e:
        print(f'INVALID: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
