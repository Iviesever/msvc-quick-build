#!/usr/bin/env python3
"""One preregistered private129 jobs contrast; never replaces historical scores.

#164 comment5710129636. No product edits, MQB rebuilds, ETW or retries.
Actual original A/B bytes are mandatory. Only two primes may execute tools.
GetProcessTimes/cycles/IO refer to the root process, not its child compilers.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import statistics
import subprocess
import sys
import tempfile
import time
import traceback
import zipfile

BASE = '55f57a84ad938da10d0e28b4578cd1aef6d7f903'
BRANCH = 'codex/private129-jobs-contrast-once-20260917'
ARCHIVE_HASH = 'd70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530'
EXE_HASHES = {
    'baseline': '46a3fc9fd4826b16f5130895438a82601d6531a29000982720f88cfc94fcc95f',
    'candidate': 'deab6c9dea5342f3a83ca79e2be3f51d483b03a8f045fbfa8411a8e6ad645c8c'}
ALLOWED = {'.github/workflows/private129-jobs-contrast.yml',
           'tests/native/private129_jobs_contrast.py'}
MAX_CALLS = 58


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')


def allowed_environment(env):
    return (env.get('GITHUB_REPOSITORY') == 'Iviesever/msvc-quick-build'
            and env.get('GITHUB_REF') == 'refs/heads/' + BRANCH
            and env.get('GITHUB_RUN_NUMBER') == '1' and env.get('GITHUB_RUN_ATTEMPT') == '1'
            and env.get('GITHUB_ACTIONS') == 'true'
            and env.get('RUNNER_ENVIRONMENT') == 'github-hosted')


def plan():
    result = []
    for side in EXE_HASHES:
        result.append(dict(phase='prime', side=side, jobs='auto', block=None))
    for jobs in ('auto', '1'):
        for side in EXE_HASHES:
            result.append(dict(phase='pre', side=side, jobs=jobs, block=None))
    for block in range(1, 13):
        for jobs in (('auto', '1') if block % 2 else ('1', 'auto')):
            for side in (('baseline', 'candidate') if block % 2 else ('candidate', 'baseline')):
                result.append(dict(phase='score', side=side, jobs=jobs, block=block))
    for jobs in ('auto', '1'):
        for side in EXE_HASHES:
            result.append(dict(phase='post', side=side, jobs=jobs, block=None))
    return result


def safe_members(z):
    seen = set()
    for m in z.infolist():
        p = PurePosixPath(m.filename)
        require(not p.is_absolute() and '..' not in p.parts and '\\' not in m.filename
                and ':' not in m.filename and m.filename not in seen, 'unsafe/duplicate ZIP member')
        require((m.external_attr >> 16) & 0o170000 != 0o120000, 'ZIP symlink refused')
        seen.add(m.filename)
    return z.infolist()


def extract_pinned(archive, target):
    raw = archive.read_bytes()
    require(digest(raw) == ARCHIVE_HASH, 'not the original cumulative archive')
    target.mkdir()
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        members = safe_members(z)
        require(z.testzip() is None, 'original CRC failure')
        for m in members:
            if not m.is_dir():
                destination = target / m.filename
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(z.read(m))
    for side, expected in EXE_HASHES.items():
        require(digest((target / side / 'mqb.exe').read_bytes()) == expected, 'measuring EXE mismatch')
    # Import only the unchanged reporting module from the pinned original archive.
    with zipfile.ZipFile(target / 'provenance/harness-source.zip') as z:
        safe_members(z)
        module_bytes = z.read('tests/native/compare_reporting.py')
    module_path = target / 'original_compare_reporting.py'
    module_path.write_bytes(module_bytes)
    spec = importlib.util.spec_from_file_location('private129_original_reporting', module_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module, digest(module_bytes)


def fixture(h, root):
    root.mkdir()
    case = h.fixture(root, 'private129', 129)
    for source in sorted(case.root.glob('*.cpp')):
        header = source.with_suffix('.hpp')
        header.write_bytes((case.root / 'common.hpp').read_bytes())
        source.write_text(source.read_text(encoding='utf-8').replace('common.hpp', header.name), encoding='utf-8')
    require(len(list(case.root.glob('*.cpp'))) == 129, 'private fixture TU count')
    require(len(list(case.root.glob('*.hpp'))) == 130, 'private fixture header count')
    return case


def inventory(root):
    return [dict(path=p.relative_to(root).as_posix(), size=p.stat().st_size,
                 mtime_ns=p.stat().st_mtime_ns, sha256=digest(p.read_bytes()))
            for p in sorted(root.rglob('*')) if p.is_file()]


class FileTime(C.Structure):
    _fields_ = [('low', C.c_uint32), ('high', C.c_uint32)]


class IOCounts(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('read_operations', 'write_operations', 'other_operations',
                                              'read_bytes', 'write_bytes', 'other_bytes')]


def ft(value):
    return (int(value.high) << 32) | int(value.low)


class NativeMetrics:
    def __init__(self):
        require(os.name == 'nt' and C.sizeof(C.c_void_p) == 8, '64-bit Windows required')
        require(C.sizeof(FileTime) == 8 and C.sizeof(IOCounts) == 48, 'Win32 ABI sizes')
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.k.GetProcessTimes.argtypes = [C.c_void_p] + [C.POINTER(FileTime)] * 4
        self.k.GetProcessTimes.restype = C.c_int
        self.k.QueryProcessCycleTime.argtypes = [C.c_void_p, C.POINTER(C.c_uint64)]
        self.k.QueryProcessCycleTime.restype = C.c_int
        self.k.GetProcessIoCounters.argtypes = [C.c_void_p, C.POINTER(IOCounts)]
        self.k.GetProcessIoCounters.restype = C.c_int

    def query(self, handle):
        creation, exit_time, kernel, user = (FileTime() for _ in range(4))
        cycles, counts = C.c_uint64(), IOCounts()
        result = {'handle_source': 'retained Popen._handle; no PID reopen', 'errors': {}}
        calls = [
            ('times', self.k.GetProcessTimes, [C.byref(v) for v in (creation, exit_time, kernel, user)]),
            ('cycles', self.k.QueryProcessCycleTime, [C.byref(cycles)]),
            ('io', self.k.GetProcessIoCounters, [C.byref(counts)])]
        for key, function, args in calls:
            C.set_last_error(0)
            result['errors'][key] = None if function(C.c_void_p(int(handle)), *args) else C.get_last_error()
        result['creation_100ns'] = ft(creation) if result['errors']['times'] is None else None
        result['exit_100ns'] = ft(exit_time) if result['errors']['times'] is None else None
        result['kernel_100ns'] = ft(kernel) if result['errors']['times'] is None else None
        result['user_100ns'] = ft(user) if result['errors']['times'] is None else None
        result['cpu_ms'] = (ft(kernel) + ft(user)) / 10000 if result['errors']['times'] is None else None
        result['cycles'] = cycles.value if result['errors']['cycles'] is None else None
        result['io'] = {name: getattr(counts, name) for name, _ in IOCounts._fields_} if result['errors']['io'] is None else None
        return result


def command(exe, case, item):
    return [str(exe), case.arguments[0], *(['--verbose'] if item['phase'] == 'prime' else []),
            *case.arguments[1:], '-j', item['jobs'],
            *(['--timings=json'] if item['phase'] in ('pre', 'post') else [])]


def verify_no_tools(h, row, case, output, errors):
    human_out, out_records = h.split_timings(output)
    human_err, err_records = h.split_timings(errors)
    records = out_records + err_records
    instrumented = row['phase'] in ('pre', 'post')
    require(len(records) == int(instrumented), 'unexpected timing record count')
    row['timing'] = records[0] if records else None
    if row['phase'] == 'prime':
        return
    human = h.normalized(human_out + human_err)
    require(b'[up-to-date] 129 translation units\n' in human, 'missing full warm default summary')
    require(all(marker not in human for marker in (b'[compile]', b'[link]', b'[archive]', b'[scan]', b'[pch]')),
            'unexpected tool activity in a diagnostic warm call')
    if instrumented:
        h.assert_warm(row, case, candidate=row['side'] == 'candidate')


class Recorder:
    def __init__(self, root, h, case, native):
        self.root, self.h, self.case, self.native = root, h, case, native
        self.rows = []
        (root / 'raw').mkdir()

    def checkpoint(self):
        write_json(self.root / 'calls.json', self.rows)
        write_json(self.root / 'budget.json', {'maximum': MAX_CALLS, 'attempted': len(self.rows),
                    'remaining_not_run': MAX_CALLS - len(self.rows), 'adaptive_retry': False})

    def run(self, item, exe):
        require(len(self.rows) < MAX_CALLS, 'spent experiment')
        index = len(self.rows)
        require(item == plan()[index], 'out-of-order or duplicate call refused')
        stem = f'raw/{index:03d}-{item["phase"]}-{item["jobs"]}-{item["side"]}'
        row = {**item, 'index': index, 'label': stem, 'argv': command(exe, self.case, item),
               'cwd': str(self.case.root), 'launch_attempted': True, 'exit_code': None, 'timing': None,
               'stdout_file': stem + '.stdout', 'stderr_file': stem + '.stderr'}
        self.rows.append(row)
        self.checkpoint()  # Consume authorization before any process creation, even a failed launch.
        output, errors, p = b'', b'', None
        started = time.perf_counter_ns()
        try:
            p = subprocess.Popen(row['argv'], cwd=self.case.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            row['pid'] = p.pid
            output, errors = p.communicate(timeout=120)
            row['external_ms'] = (time.perf_counter_ns() - started) / 1e6
            row['exit_code'] = p.returncode
            # Keep the original handle alive until all root-process counters are queried.
            row['native'] = self.native.query(p._handle)
        except subprocess.TimeoutExpired as e:
            row['timeout'] = True
            output, errors = e.stdout or b'', e.stderr or b''
            p.kill()
            try:
                output, errors = p.communicate(timeout=10)
                row['exit_code'] = p.returncode
            except subprocess.TimeoutExpired as second:
                output, errors = second.stdout or output, second.stderr or errors
                row['root_or_pipe_cleanup_unproven'] = True
            raise
        except Exception as e:
            row['exception'] = repr(e)
            raise
        finally:
            (self.root / row['stdout_file']).write_bytes(output)
            (self.root / row['stderr_file']).write_bytes(errors)
            row['stdout_sha256'], row['stderr_sha256'] = digest(output), digest(errors)
            self.checkpoint()
        require(row['exit_code'] == 0, 'native call failed; retain diagnostics and stop')
        require(all(v is None for v in row['native']['errors'].values()), 'process accounting unavailable')
        require(row['native']['exit_100ns'] >= row['native']['creation_100ns'] > 0, 'invalid process lifetime')
        verify_no_tools(self.h, row, self.case, output, errors)
        self.checkpoint()
        return row


def summaries(h, rows):
    result, paired = {}, {}
    for jobs in ('auto', '1'):
        pairs = []
        for block in range(1, 13):
            pair = {row['side']: row for row in rows if row['phase'] == 'score'
                    and row['jobs'] == jobs and row['block'] == block}
            require(set(pair) == set(EXE_HASHES), 'incomplete pairs cannot be summarized as complete')
            pairs.append({'block': block, **pair})
        paired[jobs] = pairs
        summary = h.summarize(pairs)
        for metric in ('cpu_ms', 'cycles'):
            deltas = [pair['candidate']['native'][metric] - pair['baseline']['native'][metric] for pair in pairs]
            summary[metric] = {'paired_deltas': deltas, 'paired_median_delta': statistics.median(deltas),
                              'baseline_median': statistics.median(p['baseline']['native'][metric] for p in pairs),
                              'candidate_median': statistics.median(p['candidate']['native'][metric] for p in pairs)}
        result[jobs] = summary
    interactions = [(a['candidate']['external_ms'] - a['baseline']['external_ms'])
                    - (b['candidate']['external_ms'] - b['baseline']['external_ms'])
                    for a, b in zip(paired['auto'], paired['1'])]
    return {'by_jobs': result, 'auto_minus_j1_block_deltas_ms': interactions,
            'auto_minus_j1_median_ms': statistics.median(interactions),
            'no_new_acceptance_threshold': True, 'historical_flag_cleared': False,
            'causal_attribution_proven': False, 'wait_time_or_cause': None}


def actual_tools(rows, output):
    files = {}
    for row in rows:
        if row['phase'] != 'prime':
            continue
        for line in (output / row['stdout_file']).read_text(encoding='utf-8', errors='replace').splitlines():
            for marker in ('cl:', 'link:'):
                if line.strip().startswith(marker):
                    p = Path(line.strip()[len(marker):].strip())
                    if p.is_file():
                        for name in ('cl.exe', 'link.exe', 'c1xx.dll', 'c2.dll', 'mspdbsrv.exe', 'mspdbcore.dll'):
                            tool = p.parent / name
                            files[str(tool)] = {'sha256': digest(tool.read_bytes()), 'bytes': tool.stat().st_size} if tool.is_file() else None
    require(any(Path(p).name.lower() == 'cl.exe' and value for p, value in files.items()), 'prime did not preserve actual compiler identity')
    return files


def self_test():
    items = plan()
    require(len(items) == MAX_CALLS and sum(i['phase'] == 'score' for i in items) == 48, 'budget')
    require(sum(i['phase'] in ('pre', 'post') for i in items) == 8, 'audit budget')
    for jobs in ('auto', '1'):
        for block in range(1, 13):
            selected = [i for i in items if i['phase'] == 'score' and i['block'] == block and i['jobs'] == jobs]
            require([i['side'] for i in selected] == (['baseline','candidate'] if block % 2 else ['candidate','baseline']), 'pairing')
    valid = dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build', GITHUB_REF='refs/heads/' + BRANCH,
                 GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1', GITHUB_ACTIONS='true', RUNNER_ENVIRONMENT='github-hosted')
    require(allowed_environment(valid), 'valid allocation')
    for key in valid:
        require(not allowed_environment({**valid, key: ''}), 'missing allocation accepted')
    for run in ('2', '0', '01', ''):
        require(not allowed_environment({**valid, 'GITHUB_RUN_NUMBER': run}), 'new run accepted')
        require(not allowed_environment({**valid, 'GITHUB_RUN_ATTEMPT': run}), 'retry accepted')
    require(ft(FileTime(0xffffffff, 0xffffffff)) == 2**64-1 and C.sizeof(IOCounts) == 48, 'unsigned ABI')
    for bad in ('../evil', '/evil', 'c:/evil', 'a\\evil'):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as z:
            z.writestr(bad, b'')
        try:
            with zipfile.ZipFile(io.BytesIO(stream.getvalue())) as z:
                safe_members(z)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('unsafe archive accepted')
    return {'passed': True, 'plan_calls': 58, 'native_execution': False,
            'checks': ['budget', '24 pair orders', 'allocation/missing/new-run/retry', 'unsigned/ABI', 'unsafe ZIP paths']}


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.self_test:
        print(json.dumps(self_test(), indent=2))
        return
    require(os.name == 'nt' and allowed_environment(os.environ), 'not the unique authorized hosted run')
    require(args.input is not None and args.output is not None, 'missing paths')
    output = args.output.resolve()
    require(not output.exists(), 'never overwrite or resume this experiment')
    output.mkdir(parents=True)
    try:
        write_json(output / 'self-tests.json', self_test())
        head = git('rev-parse', 'HEAD')
        require(head == os.environ['GITHUB_SHA'] and git('rev-parse', 'HEAD^') == BASE, 'exact source parent mismatch')
        require(set(git('diff', '--name-only', BASE, head).splitlines()) == ALLOWED, 'unexpected source changes')
        require(git('status', '--porcelain', '--untracked-files=no') == '', 'tracked source dirty')
        require(Path('VERSION').read_text().strip() == '5.5.0', 'version changed')
        subprocess.run(['git', 'archive', '-o', str(output / 'controller-source.zip'), head], check=True)
        write_json(output / 'identity.json', {'head': head, 'base': BASE, 'tree': git('rev-parse','HEAD^{tree}'),
                    'version': '5.5.0', 'run_id': os.environ.get('GITHUB_RUN_ID'), 'run_number': 1, 'attempt': 1,
                    'os': platform.platform(), 'python': sys.version, 'cpu_count': os.cpu_count(),
                    'image': os.environ.get('ImageOS'), 'image_version': os.environ.get('ImageVersion'),
                    'preregistration': 5710129636, 'no_product_change': True})
        write_json(output / 'environment.json', {k: os.environ.get(k) for k in
                   ('PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_','PROCESSOR_IDENTIFIER','NUMBER_OF_PROCESSORS')})
        h, helper_hash = extract_pinned(args.input.resolve(), output / 'original')
        h.self_test()
        write_json(output / 'pinned-inputs.json', {'archive_sha256': ARCHIVE_HASH, 'binaries': EXE_HASHES,
                                                  'unchanged_reporting_sha256': helper_hash})
        native = NativeMetrics()
        case = fixture(h, output / 'fixture-parent')
        write_json(output / 'fixture-inputs.json', inventory(case.root))
        write_json(output / 'plan.json', plan())
        rec = Recorder(output, h, case, native)
        for item in plan():
            rec.run(item, output / 'original' / item['side'] / 'mqb.exe')
            if len(rec.rows) == 2:
                write_json(output / 'actual-tools.json', actual_tools(rec.rows, output))
                write_json(output / 'sealed-before.json', inventory(case.root))
        for phase in ('pre','post'):
            for jobs in ('auto','1'):
                pair = [r for r in rec.rows if r['phase'] == phase and r['jobs'] == jobs]
                require(h.semantic_counters(pair[0]) == h.semantic_counters(pair[1]), 'audit A/B counters differ')
        after = inventory(case.root)
        write_json(output / 'sealed-after.json', after)
        require(after == json.loads((output / 'sealed-before.json').read_text()), 'warm fixture changed')
        for side, expected in EXE_HASHES.items():
            require(digest((output / 'original' / side / 'mqb.exe').read_bytes()) == expected, 'measuring EXE mutated')
        require(digest(args.input.read_bytes()) == ARCHIVE_HASH, 'original archive mutated')
        write_json(output / 'summary.json', summaries(h, rec.rows))
        write_json(output / 'completion.json', {'completed': True, 'mqb_calls': len(rec.rows),
                    'prime_calls': 2, 'audit_calls': 8, 'off_calls': 48, 'new_mqb_builds': 0,
                    'new_etw_captures': 0, 'historical_flag_cleared': False, 'release_authorized': False})
    except Exception:
        (output / 'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
    finally:
        write_json(output / 'file-hashes.json', inventory(output))


if __name__ == '__main__':
    main()
