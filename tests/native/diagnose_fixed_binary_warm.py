"""Bounded warm-path diagnosis using the original #188 cumulative executables.

Never rebuild or replace the historical pair. This is a new factorial diagnostic,
not a repeated release gate. CPU is summed root-thread work, not wall time; root
I/O is not physical-disk or per-path evidence. No AA/observer subtraction.
API contracts: Microsoft Learn GetProcessTimes, GetProcessIoCounters,
QueryProcessCycleTime; CPython v3.12.10 Lib/subprocess.py (_handle ownership).
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes as ct
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import statistics as stats
import subprocess
import sys
import time
import zipfile

import compare_reporting as h
import compare_release_cumulative as cumulative

ARCHIVE_HASH = 'd70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530'
SOURCE_RUN = '34756138330'
BASE = '1c565ca9f364b0bcb380565c4684ea543b449e04'
BINARY_HASHES = {
    'baseline': '46a3fc9fd4826b16f5130895438a82601d6531a29000982720f88cfc94fcc95f',
    'candidate': 'deab6c9dea5342f3a83ca79e2be3f51d483b03a8f045fbfa8411a8e6ad645c8c',
}
CELLS = [('baseline', False), ('candidate', False), ('baseline', True), ('candidate', True)]
WILLIAMS = ((0, 1, 3, 2), (1, 2, 0, 3), (2, 3, 1, 0), (3, 0, 2, 1))
GROUPS = [(name, jobs) for name in ('small', 'common', 'private') for jobs in ('auto', '1')]
BLOCKS = 12


def dump(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def schedule():
    rows = []
    for block in range(BLOCKS):
        rotated = GROUPS[block % 6:] + GROUPS[:block % 6]
        for name, jobs in rotated:
            for position, cell in enumerate(WILLIAMS[block % 4]):
                side, observe = CELLS[cell]
                rows.append(dict(block=block, case=name, jobs=jobs, position=position,
                                 side=side, observe=observe,
                                 label=f'{name}-j{jobs}-b{block:02d}-p{position}-{side}-o{int(observe)}'))
    return rows


def verify_archive(path):
    h.require(h.digest(path.read_bytes()) == ARCHIVE_HASH, 'Wrong original artifact bytes')
    with zipfile.ZipFile(path) as z:
        report = json.loads(z.read('evidence/cumulative.json'))
        h.require(report['provenance']['run_id'] == SOURCE_RUN and
                  report['provenance']['run_attempt'] == '1', 'Wrong original run/attempt')
        h.require(report['head_sha'] == BASE and report['binary_sha256'] == BINARY_HASHES,
                  'Wrong original candidate/binary identities')
        binaries = {s: z.read(f'{s}/mqb.exe') for s in BINARY_HASHES}
        h.require({s: h.digest(b) for s, b in binaries.items()} == BINARY_HASHES,
                  'Original executable hash mismatch')
        # Compare LF-normalized checkout source, not CRLF checkout artefacts.
        with zipfile.ZipFile(__import__('io').BytesIO(z.read('provenance/candidate-source.zip'))) as source:
            for module in (h, cumulative):
                filename = Path(module.__file__).name
                original = source.read('tests/native/' + filename)
                h.require(h.normalized(Path(module.__file__).read_bytes()) == original,
                          f'Historical helper changed: {filename}')
        return binaries, report['provenance']


class FileTime(ct.Structure):
    _fields_ = [('low', ct.c_uint32), ('high', ct.c_uint32)]

    def ticks(self):
        return (self.high << 32) | self.low


class IoCounters(ct.Structure):
    _fields_ = [(name, ct.c_uint64) for name in (
        'read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class WindowsResources:
    """Borrow Popen's original handle; never reopen by PID or change child policy."""
    def __init__(self):
        h.require(os.name == 'nt' and platform.python_implementation() == 'CPython'
                  and sys.version_info[:3] == (3, 12, 10), 'Requires audited Windows CPython 3.12.10')
        dll = ct.WinDLL('kernel32', use_last_error=True)
        self.times = dll.GetProcessTimes
        self.times.argtypes = [ct.c_void_p] + [ct.POINTER(FileTime)] * 4
        self.io = dll.GetProcessIoCounters
        self.io.argtypes = [ct.c_void_p, ct.POINTER(IoCounters)]
        self.cycles = dll.QueryProcessCycleTime
        self.cycles.argtypes = [ct.c_void_p, ct.POINTER(ct.c_uint64)]
        for function in (self.times, self.io, self.cycles):
            function.restype = ct.c_int

    def __call__(self, process):
        handle = ct.c_void_p(int(process._handle))
        created, exited, kernel, user = (FileTime() for _ in range(4))
        io, cycles = IoCounters(), ct.c_uint64()
        errors = []
        ok_times = self.times(handle, ct.byref(created), ct.byref(exited), ct.byref(kernel), ct.byref(user))
        if not ok_times:
            errors.append(['GetProcessTimes', ct.get_last_error()])
        ok_io = self.io(handle, ct.byref(io))
        if not ok_io:
            errors.append(['GetProcessIoCounters', ct.get_last_error()])
        ok_cycles = self.cycles(handle, ct.byref(cycles))
        if not ok_cycles:
            errors.append(['QueryProcessCycleTime', ct.get_last_error()])
        return dict(pid=process.pid, exit_code=process.returncode, query_count=3,
                    creation_100ns=created.ticks() if ok_times else None,
                    exit_100ns=exited.ticks() if ok_times else None,
                    kernel_100ns=kernel.ticks() if ok_times else None,
                    user_100ns=user.ticks() if ok_times else None,
                    cycles=cycles.value if ok_cycles else None,
                    io={k: getattr(io, k) for k, _ in io._fields_} if ok_io else None,
                    errors=errors)


@contextmanager
def observation(enabled, query, records):
    """OFF uses the original Popen class; ON's post-exit overhead is INCLUDED."""
    original = subprocess.Popen
    if not enabled:
        yield
        return

    class ObservedPopen(original):
        def __exit__(self, *exc):
            try:
                return super().__exit__(*exc)
            finally:
                start = time.perf_counter_ns()
                try:
                    row = query(self)
                except Exception as error:
                    # Do not replace the original launch/timeout/child exception.
                    row = {'errors': [repr(error)]}
                row['query_envelope_ms'] = (time.perf_counter_ns() - start) / 1e6
                records.append(row)
    subprocess.Popen = ObservedPopen
    try:
        yield
    finally:
        subprocess.Popen = original


def valid_resource(row, exit_code):
    h.require(not row['errors'] and row['query_count'] == 3, 'Resource query failed')
    h.require(row['exit_code'] == exit_code and row['pid'] > 0, 'Root exit/identity mismatch')
    h.require(row['exit_100ns'] >= row['creation_100ns'] > 0, 'Root lifetime not valid')
    h.require(row['cycles'] > 0 and row['kernel_100ns'] >= 0 and row['user_100ns'] >= 0,
              'Invalid root resource accounting')
    h.require(all(v >= 0 for v in row['io'].values()), 'Invalid root IO accounting')


def calibrate(output, query):
    programs = [('idle', 'import time; time.sleep(.06)', 0),
                ('busy', 'import time\nt=time.process_time()\nwhile time.process_time()-t < .20: pass', 0),
                ('io', 'from pathlib import Path\np=Path("calibration.bin")\n'
                       'b=b"x"*(1024*1024)\np.write_bytes(b)\nassert p.read_bytes()==b\np.unlink()', 0),
                ('exit17', 'import sys; print("CAL_OUT"); print("CAL_ERR",file=sys.stderr); sys.exit(17)', 17)]
    rows = []
    for name, code, expected in programs:
        observed = []
        try:
            with observation(True, query, observed):
                result = subprocess.run([sys.executable, '-c', code], cwd=output,
                                        capture_output=True, timeout=10, check=False)
        except Exception as error:
            (output / f'calibration-{name}.stdout').write_bytes(getattr(error, 'stdout', None) or b'')
            (output / f'calibration-{name}.stderr').write_bytes(getattr(error, 'stderr', None) or b'')
            rows.append(dict(name=name, code=code, error=repr(error), resources=observed))
            dump(output / 'calibration.json', rows)
            raise
        (output / f'calibration-{name}.stdout').write_bytes(result.stdout)
        (output / f'calibration-{name}.stderr').write_bytes(result.stderr)
        rows.append(dict(name=name, code=code, exit_code=result.returncode, resources=observed))
        dump(output / 'calibration.json', rows)
        h.require(result.returncode == expected and len(observed) == 1, 'Calibration process mismatch')
        valid_resource(observed[0], expected)
    h.require(rows[1]['resources'][0]['cycles'] > rows[0]['resources'][0]['cycles'], 'CPU control failed')
    io = rows[2]['resources'][0]['io']
    h.require(io['write_bytes'] >= 1024 * 1024 and io['read_bytes'] >= 1024 * 1024, 'IO control failed')
    h.require(b'CAL_OUT' in (output / 'calibration-exit17.stdout').read_bytes() and
              b'CAL_ERR' in (output / 'calibration-exit17.stderr').read_bytes(), 'Exit17 diagnostics lost')


def vector(row):
    t = row['timing']
    return {key: t[key] for key in ('cache', 'counters', 'counter_breakdown')}


def describe(values):
    return dict(values=values, median=stats.median(values), minimum=min(values), maximum=max(values))


def summaries(rows):
    result = []
    for name, jobs in GROUPS:
        group = [r for r in rows if r['case'] == name and r['jobs'] == jobs]
        by = {(r['block'], r['side'], r['observe']): r for r in group}
        item = dict(case=name, jobs=jobs)
        for mode in (False, True):
            pairs = [dict(baseline=by[b, 'baseline', mode]['call'],
                          candidate=by[b, 'candidate', mode]['call']) for b in range(BLOCKS)]
            item['on' if mode else 'off'] = h.summarize(pairs)
        for side in BINARY_HASHES:
            item[side + '_observer_delta_ms'] = describe([
                by[b, side, True]['call']['external_ms'] - by[b, side, False]['call']['external_ms']
                for b in range(BLOCKS)])
        for metric in ('cycles', 'kernel_100ns', 'user_100ns'):
            item[metric + '_delta'] = describe([
                by[b, 'candidate', True]['resource'][metric] - by[b, 'baseline', True]['resource'][metric]
                for b in range(BLOCKS)])
        for metric, _ in IoCounters._fields_:
            item[metric + '_delta'] = describe([
                by[b, 'candidate', True]['resource']['io'][metric] - by[b, 'baseline', True]['resource']['io'][metric]
                for b in range(BLOCKS)])
        result.append(item)
    return result


def run(archive, output, harness_sha):
    h.require(not output.exists(), 'Refusing existing evidence directory')
    h.require(os.environ.get('GITHUB_RUN_ATTEMPT') == '1', 'Only the first attempt is admissible')
    repo = Path(__file__).resolve().parents[2]
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    h.require(actual == harness_sha and os.environ.get('MQB_EXPECTED_BASE') == BASE, 'Wrong PR base/head')
    h.require(not subprocess.check_output(['git', 'diff', 'HEAD', '--'], cwd=repo), 'Dirty harness source')
    h.require((repo / 'VERSION').read_text().strip() == '5.5.0', 'Frozen VERSION changed')
    original_bytes, original_identity = verify_archive(archive)
    resources = WindowsResources()  # Bind before any timed call.
    output.mkdir(parents=True)
    bins = {}
    for side, data in original_bytes.items():
        bins[side] = output / f'{side}.exe'
        bins[side].write_bytes(data)
    subprocess.run(['git', 'archive', '--format=zip', f'--output={output / "harness-source.zip"}', 'HEAD'],
                   cwd=repo, check=True)
    fixtures = output / 'fixtures'
    fixtures.mkdir()
    all_cases = cumulative.build_fixtures(fixtures)
    cases = {name: all_cases[name] for name in ('small', 'common', 'private')}
    source_files = sorted(p for p in fixtures.rglob('*') if p.is_file())
    dump(output / 'fixture-inputs.json', {str(p.relative_to(fixtures)): h.digest(p.read_bytes()) for p in source_files})
    with zipfile.ZipFile(output / 'fixture-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for path in source_files:
            z.write(path, str(path.relative_to(fixtures)))
    plan = dict(schema_version=1, original_identity=original_identity, original_archive_sha256=ARCHIVE_HASH,
                binary_sha256=BINARY_HASHES, harness_sha=actual,
                harness_tree=subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=repo, text=True).strip(),
                run_id=os.environ.get('GITHUB_RUN_ID'), run_attempt='1',
                created_utc=datetime.now(timezone.utc).isoformat(), python=sys.version,
                python_sha256=h.digest(Path(sys.executable).read_bytes()),
                subprocess_sha256=h.digest(Path(subprocess.__file__).read_bytes()),
                platform=platform.platform(), processor=platform.processor(), cpu_count=os.cpu_count(),
                environment={k: os.environ.get(k) for k in ('ImageOS', 'ImageVersion', 'CL', '_CL_', 'LINK',
                    '_LINK_', 'INCLUDE', 'LIB', 'LIBPATH', 'PATH', 'VCToolsInstallDir', 'WindowsSDKVersion',
                    'PROCESSOR_IDENTIFIER', 'NUMBER_OF_PROCESSORS', 'TEMP')},
                measurement='Original Recorder elapsed; ON adds post-exit root queries within elapsed; no subtraction',
                budget=dict(diagnostic_calls=288, resource_records=144, primes=6, audits=24, mqb_calls=318,
                            python_calibrations=4), schedule=schedule())
    dump(output / 'plan.json', plan)
    (output / 'subprocess-source.py').write_bytes(Path(subprocess.__file__).read_bytes())
    recorder = h.Recorder(output / 'mqb')
    result = dict(status='running', release_authorized=False, historical_cause_resolved=False,
                  historical_risks_cleared=False, rows=[], audits=[], tools={})
    save = lambda: dump(output / 'result.json', result)
    save()
    try:
        calibrate(output, resources)
        for name, case in cases.items():
            for side in bins:
                recorder.run(bins[side], case, f'prime-{name}-{side}')
        before = {name: h.state(case) for name, case in cases.items()}
        dump(output / 'state-before.json', before)
        for phase in ('pre', 'post'):
            if phase == 'post':
                for slot in plan['schedule']:
                    observed = []
                    try:
                        with observation(slot['observe'], resources, observed):
                            call = recorder.run(bins[slot['side']], cases[slot['case']], slot['label'], jobs=slot['jobs'])
                    except Exception:
                        dump(output / 'failed-observation.json', dict(slot=slot, resources=observed))
                        raise
                    row = dict(**slot, call=call, resource=observed[0] if len(observed) == 1 else None)
                    result['rows'].append(row)
                    save()
                    h.require(len(observed) == int(slot['observe']), 'OFF queried or ON coverage missing')
                    if observed:
                        valid_resource(observed[0], call['exit_code'])
                    human = recorder.human(call, 'stdout')
                    h.require(all(v == 0 for v in cumulative.counts(recorder, call).values()), 'Warm call rebuilt')
                    h.require(f"[up-to-date] {cases[slot['case']].units} translation units".encode() in human,
                              'Warm default report missing')
                    h.require(call['human_stderr_sha256'] == h.digest(b''), 'Unexpected warm stderr')
            for name, jobs in GROUPS:
                pair = {}
                for side in bins:
                    row = recorder.run(bins[side], cases[name], f'{phase}-{name}-j{jobs}-{side}',
                                       jobs=jobs, timings=True, verbose=True)
                    cumulative.audit_warm(row, cases[name])
                    pair[side] = row
                    result['audits'].append(dict(phase=phase, case=name, jobs=jobs, side=side, call=row))
                    # The verbose original CLI identifies the actual selected tool paths.
                    paths = re.findall(r'^  (?:cl|link):\s+(.+)$', recorder.human(row, 'stdout').decode('utf-8'), re.M)
                    h.require(len(paths) == 2, 'Selected compiler/linker identity unavailable')
                    for path in paths:
                        if path not in result['tools']:
                            result['tools'][path] = h.digest(Path(path).read_bytes())
                    save()
                h.require(vector(pair['baseline']) == vector(pair['candidate']), 'Cross-version warm vectors differ')
                if phase == 'post':
                    for side in bins:
                        pre = next(a['call'] for a in result['audits'] if a['phase'] == 'pre'
                                   and a['case'] == name and a['jobs'] == jobs and a['side'] == side)
                        h.require(vector(pre) == vector(pair[side]), 'Pre/post warm vectors differ')
        after = {name: h.state(case) for name, case in cases.items()}
        dump(output / 'state-after.json', after)
        h.require(before == after, 'Warm sequence changed build-state metadata')
        h.require(len(recorder.calls) == 318 and len(result['rows']) == 288
                  and sum(r['resource'] is not None for r in result['rows']) == 144, 'Incomplete fixed budget')
        h.require({s: h.digest(p.read_bytes()) for s, p in bins.items()} == BINARY_HASHES, 'Binaries changed')
        h.require(all(h.digest(Path(p).read_bytes()) == digest for p, digest in result['tools'].items()),
                  'Selected tool changed during study')
        result.update(status='completed', summaries=summaries(result['rows']), mqb_calls=len(recorder.calls))
        save()
        print('Fixed diagnosis complete: 288 calls / 144 root resources / 318 MQB calls. Historical HOLD unchanged.')
    except Exception as error:
        result.update(status='failed', error=repr(error), mqb_calls=len(recorder.calls))
        save()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--harness-sha', required=True)
    args = parser.parse_args()
    run(args.archive.resolve(), args.output.resolve(), args.harness_sha)
