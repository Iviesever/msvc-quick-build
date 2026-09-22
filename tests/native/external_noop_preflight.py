"""Fixed helper-child preflight plan and offline audit; never launches a process."""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path, PureWindowsPath
import sys

import external_noop_boundary as h
from external_noop_preflight_child import stream_bytes

MAX_DISPATCHES = 9
MAX_CHILDREN = 7
COLLECTOR_SHA256 = '4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf'
SOURCE_NAMES = ('collect_external_noop_boundary.ps1', 'external_noop_boundary.py',
                'preflight_external_noop_boundary.ps1', 'external_noop_preflight.py',
                'external_noop_preflight_child.py')
PAYLOAD = ['space argument', 'quoted"value', '\u65e5\u672c\u8a9e', 'tail\\', '']


def schedule():
    rows = []
    for scenario in ('echo', 'streams', 'nonzero', 'missing'):
        for mode in ('legacy', 'process'):
            rows.append(dict(sequence=len(rows)+1, mode=mode, scenario=scenario))
    rows.append(dict(sequence=9, mode='process', scenario='timeout'))
    return rows


def norm(path):
    return str(path).replace('\\', '/').rstrip('/').casefold()


def original_path(path):
    text = str(path)
    return PureWindowsPath(text) if len(text) > 2 and text[1] == ':' else Path(text)


def expected_argv(root, row):
    root = original_path(root)
    return ['-I', '-u', str(root/'source'/'external_noop_preflight_child.py'),
            row['scenario'], str(root/'calls'/f"{row['sequence']:02d}.child.json"), *PAYLOAD]


def expected_executable(plan, row):
    return str(original_path(plan['root'])/'missing-helper.exe') if row['scenario'] == 'missing' else plan['executable']


def validate_plan(plan):
    h.require(h.same(plan['rows'], schedule()) and h.same(plan['payload'], PAYLOAD), 'changed preflight schedule/payload')
    h.require(h.same(plan['max_dispatches'], MAX_DISPATCHES) and
              h.same(plan['max_helper_processes'], MAX_CHILDREN), 'changed helper budget')
    h.require(h.same(plan['schema'], 1) and plan['purpose'] == 'non_mqb_launcher_preflight', 'wrong purpose')
    h.require(plan['clears_hold'] is False and h.same(plan['study_mqb_calls'], 0), 'invented study authority')
    h.require(set(plan['sources']) == set(SOURCE_NAMES), 'incomplete source identity')
    h.require(plan['collector_canonical_sha256'] == COLLECTOR_SHA256,
              'collector differs from reviewed launch functions')
    h.require(isinstance(plan['root'], str) and isinstance(plan['executable'], str), 'missing paths')
    for value in [plan['executable_sha256'], *plan['sources'].values()]:
        h.require(isinstance(value, str) and len(value) == 64 and
                  all(c in '0123456789abcdef' for c in value), 'invalid source digest')


def make_plan(root, executable):
    h.require(root.is_absolute() and executable.is_absolute(), 'absolute preflight paths required')
    value = dict(schema=1, purpose='non_mqb_launcher_preflight', root=str(root),
                 executable=str(executable), executable_sha256=h.digest(executable.read_bytes()),
                 sources={n: h.digest((root/'source'/n).read_bytes()) for n in SOURCE_NAMES},
                 collector_canonical_sha256=h.digest((root/'source'/SOURCE_NAMES[0]).read_bytes().replace(b'\r\n', b'\n')),
                 max_dispatches=MAX_DISPATCHES, max_helper_processes=MAX_CHILDREN,
                 study_mqb_calls=0, clears_hold=False, payload=PAYLOAD, rows=schedule())
    validate_plan(value)
    return value


def check_identity(root, plan, live_interpreter=False):
    validate_plan(plan)
    h.require(h.digest((root/'source'/SOURCE_NAMES[0]).read_bytes().replace(b'\r\n', b'\n')) ==
              COLLECTOR_SHA256, 'changed collector content')
    for name, digest in plan['sources'].items():
        p = root/'source'/name
        h.require(p.is_file() and not p.is_symlink() and h.digest(p.read_bytes()) == digest,
                  'source changed: ' + name)
    # Collection rehashes the live executable. Relocated offline audits check
    # saved before/after identities, not unavailable bytes on the old runner.
    if live_interpreter:
        exe = Path(plan['executable'])
        h.require(exe.is_file() and not exe.is_symlink() and
                  h.digest(exe.read_bytes()) == plan['executable_sha256'], 'helper interpreter changed')


def validate_observation(row, record, child, stdout, stderr, lines, plan):
    """Validate synthetic or real observations without launching anything."""
    h.require(h.same(record['row'], row) and record['clears_hold'] is False, 'wrong result row/proof')
    q = record['clock']
    h.require(type(q['frequency']) is int and q['frequency'] > 0, 'invalid clock frequency')
    times = [q[k] for k in ('outer_start', 'native_start', 'native_end', 'outer_end')]
    h.require(all(type(t) is int and t >= 0 for t in times) and times == sorted(times), 'invalid QPC order')
    scenario, mode = row['scenario'], row['mode']
    rt = record['root_times']
    h.require(record['output_format'] == ('separate_bytes' if mode == 'process' else 'powershell_merged_lines'),
              'wrong output mode')
    if mode == 'legacy':
        h.require(rt is None and record['cleanup'] is None, 'invented legacy root timing/cleanup')
        h.require(isinstance(lines, list) and all(isinstance(s, str) for s in lines), 'invalid legacy output')
    else:
        h.require(record['output_lines'] is None and isinstance(stdout, bytes) and isinstance(stderr, bytes),
                  'missing raw process streams')
    if scenario == 'missing':
        h.require(child is None and record['exit_code'] is None and rt is None and record['cleanup'] is None,
                  'failed launch fabricated a child or success')
        h.require(isinstance(record['error'], str) and bool(record['error']), 'missing original launch error')
        h.require((mode == 'legacy' and not lines) or (mode == 'process' and not stdout and not stderr),
                  'unexpected failed-launch output')
    else:
        h.require(isinstance(child, dict) and child['schema'] == 1 and child['purpose'] == 'non_mqb_helper',
                  'missing actual helper marker')
        h.require(type(child['pid']) is int and child['pid'] > 0 and child['mode'] == scenario, 'wrong child')
        h.require(h.same(child['payload'], PAYLOAD), 'argv changed (including empty/quoted/Unicode tokens)')
        h.require(norm(child['cwd']) == norm(plan['root']+'/fixture space') and
                  norm(child['executable']) == norm(plan['executable']), 'wrong executable/cwd in child')
        h.require(child['source_sha256'] == plan['sources']['external_noop_preflight_child.py'], 'wrong child code')
        if scenario == 'timeout':
            h.require(mode == 'process' and record['exit_code'] is None and rt is None and
                      isinstance(record['error'], str) and 'Root wait exceeded 180 seconds.' in record['error'],
                      'missing real root timeout')
            h.require(h.same(record['cleanup'], dict(root_exited=True, descendants_verified=False)),
                      'root cleanup failed or descendants falsely verified')
            h.require(q['wait_return'] is None and type(q['start_return']) is int and
                      q['native_start'] <= q['start_return'] <= q['native_end'], 'invalid timeout boundary')
            h.require((q['native_end']-q['start_return'])/q['frequency'] >= 180, 'shortened root wait')
            h.require(stdout == b'PREFLIGHT-READY\n' and stderr == b'PREFLIGHT-WAITING\n',
                      'timeout prefix streams were lost')
        else:
            code = 37 if scenario == 'nonzero' else 0
            h.require(type(record['exit_code']) is int and record['exit_code'] == code and
                      record['error'] is None and record['cleanup'] is None, 'wrong original exit/result')
            if mode == 'process':
                h.require(isinstance(rt, dict) and type(rt['pid']) is int and rt['pid'] == child['pid'],
                          'wrong observed root PID')
                for k in ('created_100ns', 'exited_100ns', 'kernel_100ns', 'user_100ns'):
                    h.require(type(rt[k]) is int and rt[k] >= 0, 'missing OS time')
                h.require(0 < rt['created_100ns'] <= rt['exited_100ns'], 'invalid root lifetime')
                for k in ('start_return', 'wait_return'):
                    h.require(type(q[k]) is int, 'missing process boundary')
                h.require(q['native_start'] <= q['start_return'] <= q['wait_return'] <= q['native_end'],
                          'invalid start/wait/drain order')
            if scenario == 'streams':
                if mode == 'process':
                    h.require(stdout == stream_bytes(b'O') and stderr == stream_bytes(b'E'), 'truncated/mixed streams')
                else:
                    h.require(Counter(lines) == Counter({'O'*1023: 256, 'E'*1023: 256}), 'incomplete merged pressure lines')
            elif mode == 'process':
                h.require(h.same(json.loads(stdout), child) and stderr == b'PREFLIGHT-STDERR\n', 'wrong echo streams')
            else:
                h.require(len(lines) == 2 and lines.count('PREFLIGHT-STDERR') == 1, 'missing merged echo output')
                encoded = next(x for x in lines if x != 'PREFLIGHT-STDERR')
                h.require(h.same(json.loads(encoded), child), 'echo differs from child marker')
    return dict(row=row, status='expected_observation', clears_hold=False, study_mqb_calls=0,
                helper_started=scenario != 'missing', expected_failure=scenario in ('missing', 'nonzero', 'timeout'))


def check_call(root, row, live_interpreter=False):
    plan = h.load(root/'plan.json'); check_identity(root, plan, live_interpreter)
    host = h.load(root/'host.json')
    h.require(host['original_plan_sha256'] == h.digest((root/'plan.json').read_bytes()), 'plan changed')
    prefix = root/'calls'/f"{row['sequence']:02d}"
    start = h.load(Path(str(prefix)+'.started.json'))
    original_root = Path(plan['root'])
    h.require(h.same(start['row'], row) and h.same(start['argv'], expected_argv(original_root, row)), 'wrong dispatch argv')
    h.require(norm(start['executable']) == norm(expected_executable(plan, row)) and
              norm(start['cwd']) == norm(plan['root']+'/fixture space'), 'wrong dispatch path')
    h.require(start['interpreter_sha256'] == plan['executable_sha256'], 'wrong dispatch interpreter')
    after = h.load(Path(str(prefix)+'.host-after.json'))
    h.require(after['location'] == start['host_location'] == host['location'] and
              after['environment_cwd'] == start['host_environment_cwd'] == host['environment_cwd'],
              'launcher did not restore host cwd')
    h.require(after['interpreter_sha256'] == plan['executable_sha256'], 'interpreter changed after call')
    marker = Path(str(prefix)+'.child.json')
    child = h.load(marker) if marker.exists() else None
    record = h.load(Path(str(prefix)+'.result.json'))
    stdout = stderr = None
    if row['mode'] == 'process':
        blobs = [Path(str(prefix)+s) for s in ('.stdout.bin', '.stderr.bin')]
        h.require(all(p.stat().st_size <= 1024*1024 for p in blobs), 'excess helper output')
        stdout, stderr = [p.read_bytes() for p in blobs]
    return validate_observation(row, record, child, stdout, stderr, record['output_lines'], plan)


def expected_files():
    result = set()
    for row in schedule():
        suffixes = ['.started.json', '.result.json', '.host-after.json', '.validated.json']
        if row['scenario'] != 'missing': suffixes += ['.child.json']
        if row['mode'] == 'process': suffixes += ['.stdout.bin', '.stderr.bin']
        result.update(f"{row['sequence']:02d}"+s for s in suffixes)
    return result


def audit(root, live_interpreter=False):
    plan = h.load(root/'plan.json'); check_identity(root, plan, live_interpreter)
    expected = expected_files(); actual = set()
    for p in (root/'calls').iterdir():
        h.require(p.is_file() and not p.is_symlink() and p.name in expected, 'unplanned/non-file call evidence')
        actual.add(p.name)
    h.require(actual == expected, 'incomplete preflight call set')
    values = []
    for row in schedule():
        v = check_call(root, row, live_interpreter)
        h.require(h.same(h.load(root/'calls'/f"{row['sequence']:02d}.validated.json"), v), 'saved judgment differs')
        values.append(v)
    h.require(h.same(h.load(root/'completion.json'), dict(status='completed', attempted=9, validated=9,
              error=None, clears_hold=False, study_mqb_calls=0)), 'stopped/incomplete preflight')
    h.require(sum(v['helper_started'] for v in values) == 7, 'helper count differs')
    return dict(schema=1, status='complete_helper_preflight_only', dispatches=9, helpers=7,
                results=values, clears_hold=False, study_mqb_calls=0, cause=None,
                limits='No MQB timing score, process-tree proof, pipe-timeout fault coverage, or study allocation.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('plan', 'check-call', 'audit'))
    p.add_argument('root', type=Path); p.add_argument('--executable', type=Path)
    p.add_argument('--live-interpreter', action='store_true'); p.add_argument('--sequence', type=int); p.add_argument('--output', required=True, type=Path)
    args = p.parse_args()
    try:
        if args.command == 'plan':
            h.require(args.executable is not None, 'interpreter required')
            result = make_plan(args.root, args.executable)
        elif args.command == 'check-call':
            h.require(args.sequence in range(1, 10), 'invalid preflight row')
            result = check_call(args.root, schedule()[args.sequence-1], args.live_interpreter)
        else: result = audit(args.root, args.live_interpreter)
        h.write_new(args.output, result)
        return 0
    except (ValueError, OSError, KeyError, TypeError) as e:
        print(f'INVALID PREFLIGHT: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
