"""Preregistered cold diagnosis of archived MQB binaries, not a release retest.

ETW ON reuses the unchanged file+process/thread recorder, including its extra
ancestor and root query overhead. OFF launches the same worker without ETW.
Only the unchanged Recorder's MQB interval is scored; entry/worker startup,
readiness/closing/decoding, audits and program checks are separately recorded.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import ctypes as ct
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
import zipfile

import compare_reporting as h
import compare_release_cumulative as cumulative
import diagnose_fixed_binary_warm as warm
import audit_cold_process_trace as audit

BASE = '752668650dfbf8e32a3a5d66d9d0808e10daf2ec'
BASE_TREE = '4fc13a345866c6b162f13b0b8300e5a32f80d227'
CASES = ('small', 'common')
BUDGET = dict(cold_scores=32, scored_on=16, scored_off=16, mqb_calls=78, program_checks=34,
              trace_entries=20, observed_mqb_resources=18)


def dump(path, obj):
    path.write_text(json.dumps(obj, indent=2), encoding='utf-8')


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def schedule():
    rows = []
    for block in range(4):
        for case in CASES if block % 2 == 0 else CASES[::-1]:
            for position, cell in enumerate(warm.WILLIAMS[block]):
                side, observe = warm.CELLS[cell]
                rows.append(dict(block=block, case=case, position=position, side=side, observe=observe,
                                 label=f'{case}-b{block}-p{position}-{side}-o{int(observe)}'))
    return rows


def clean_case(case):
    state = case.root / '.mqb'
    if state.exists():
        shutil.rmtree(state)  # Only before a NEW declared cold call, never in failure cleanup.
    h.require(not state.exists(), 'Cold state was not removed')


def vector(row):
    return {k: row['timing'][k] for k in ('cache', 'counters', 'counter_breakdown')}


def entry(argv, output, *, timeout=180):
    start = time.perf_counter_ns()
    try:
        p = subprocess.run([str(a) for a in argv], capture_output=True, timeout=timeout, check=False)
        result = dict(argv=[str(a) for a in argv], exit_code=p.returncode,
                      entry_ms=(time.perf_counter_ns() - start) / 1e6)
        out, err = p.stdout, p.stderr
    except subprocess.TimeoutExpired as error:
        result = dict(argv=[str(a) for a in argv], error=repr(error), exit_code=None,
                      entry_ms=(time.perf_counter_ns() - start) / 1e6)
        out, err = error.stdout or b'', error.stderr or b''
    output.with_suffix('.stdout').write_bytes(out)
    output.with_suffix('.stderr').write_bytes(err)
    dump(output.with_suffix('.json'), result)
    return result


def worker(spec_path):
    spec = load(spec_path)
    root = Path(spec['worker'])
    h.require(not root.exists(), 'Existing worker evidence')
    root.mkdir()
    resources = []
    result = dict(status='started', resources=resources)
    dump(root / 'worker.json', result)
    try:
        binary = Path(spec['binary'])
        h.require(h.digest(binary.read_bytes()) == warm.BINARY_HASHES[spec['side']], 'Worker binary mismatch')
        case = h.Fixture(spec['case'], Path(spec['case_root']), spec['arguments'], spec['units'])
        if spec['cold']:
            h.require(not (case.root / '.mqb').exists(), 'Worker cold precondition missing')
        with h.Recorder(root / 'mqb') as recorder:
            query = warm.WindowsResources() if spec['observe'] else None
            with warm.observation(spec['observe'], query, resources):
                call = recorder.run(binary, case, spec['label'], success=False)
            result['call'] = call
            dump(root / 'worker.json', result)
            h.require(len(resources) == int(spec['observe']), 'Unexpected resource coverage')
            if resources:
                warm.valid_resource(resources[0], call['exit_code'])
            h.require(call['exit_code'] == spec['expected_exit'], 'Unexpected original MQB exit')
            recorder.finalize()
            result['status'] = 'completed'
    except Exception as error:
        result.update(status='failed', error=repr(error))
        raise
    finally:
        dump(root / 'worker.json', result)


def process_calibration(output):
    """One traced worker, three actual children, original exit17 and both streams."""
    output.mkdir()
    resources, calls = [], []
    query = warm.WindowsResources()
    try:
        with warm.observation(True, query, resources), ExitStack() as stack:
            children = [stack.enter_context(subprocess.Popen(
                [sys.executable, '-c', f'import sys,time;print("CAL_OUT_{i}");'
                 f'print("CAL_ERR_{i}",file=sys.stderr);time.sleep(.05);sys.exit({code})'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)) for i, code in enumerate((0, 17, 0))]
            for i, child in enumerate(children):
                out, err = child.communicate(timeout=10)
                (output / f'{i}.stdout').write_bytes(out)
                (output / f'{i}.stderr').write_bytes(err)
                calls.append(dict(pid=child.pid, exit_code=child.returncode, expected=(0, 17, 0)[i]))
        h.require(len(resources) == 3, 'Calibration queries incomplete')
        for c in calls:
            h.require(c['exit_code'] == c['expected'], 'Calibration original exit mismatch')
            r = next(r for r in resources if r['pid'] == c['pid'])
            warm.valid_resource(r, c['exit_code'])
    finally:
        dump(output / 'result.json', dict(calls=calls, resources=resources))


def device_map():
    dll = ct.WinDLL('kernel32', use_last_error=True)
    dll.GetLogicalDrives.restype = ct.c_uint32
    dll.QueryDosDeviceW.argtypes = [ct.c_wchar_p, ct.c_wchar_p, ct.c_uint32]
    dll.QueryDosDeviceW.restype = ct.c_uint32
    mask, result = dll.GetLogicalDrives(), {}
    for i in range(26):
        if mask & (1 << i):
            drive, value = chr(65 + i) + ':', ct.create_unicode_buffer(32768)
            h.require(dll.QueryDosDeviceW(drive, value, len(value)) > 0, 'Drive mapping unavailable')
            result[value.value] = drive
    return result


def trace_health(path):
    return audit.load_trace(path)


def summaries(rows):
    result = []
    for case in CASES:
        group = {(r['block'], r['side'], r['observe']): r for r in rows if r['case'] == case}
        item = dict(case=case)
        for observe in (False, True):
            item['on' if observe else 'off'] = h.summarize([
                {s: group[b, s, observe]['call'] for s in warm.BINARY_HASHES} for b in range(4)])
        for side in warm.BINARY_HASHES:
            values = [group[b, side, True]['call']['external_ms'] -
                      group[b, side, False]['call']['external_ms'] for b in range(4)]
            item[side + '_observer_deltas_ms'] = values
        for metric in ('root_lifetime_ms', 'cl_union_ms', 'link_union_ms', 'tools_union_ms', 'uncovered_ms', 'max_cl_lifetime_ms'):
            values = [group[b, 'candidate', True]['trace'][metric] -
                      group[b, 'baseline', True]['trace'][metric] for b in range(4)]
            item[metric] = dict(paired_deltas=values, paired_median=statistics.median(values))
        result.append(item)
    return result


def run(archive, tracer, trace_identity, output):
    h.require(not output.exists(), 'Refusing existing study')
    h.require(os.environ.get('GITHUB_RUN_ATTEMPT') == '1', 'Only original first attempt')
    repo = Path(__file__).resolve().parents[2]
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    h.require(head == os.environ.get('MQB_EXPECTED_HEAD') and os.environ.get('MQB_EXPECTED_BASE') == BASE,
              'Unexpected source base/head')
    h.require(not subprocess.check_output(['git', 'diff', 'HEAD', '--'], cwd=repo), 'Dirty tracked source')
    h.require((repo / 'VERSION').read_text().strip() == '5.5.0', 'Frozen version changed')
    binaries, identity = warm.verify_archive(archive)
    warm.WindowsResources()  # Audited Windows CPython requirement before any scored work.
    trace_build = load(trace_identity)
    h.require(trace_build['head'] == head and trace_build['run'] == os.environ.get('GITHUB_RUN_ID') and
              str(trace_build['attempt']) == '1' and trace_build['tracer_sha256'] == h.digest(tracer.read_bytes()),
              'Wrong trace build provenance')
    for path, digest in trace_build['sources'].items():
        h.require(h.digest((repo / path).read_bytes()) == digest, 'Trace source changed')
    output.mkdir()
    mapping = device_map()
    bins = {s: output / f'{s}.exe' for s in binaries}
    for side, path in bins.items():
        path.write_bytes(binaries[side])
    fixtures = output / 'fixtures'
    fixtures.mkdir()
    cases = {key: h.fixture(fixtures, key, 2 if key == 'small' else 129) for key in CASES}
    inputs = {str(p.relative_to(fixtures)): h.digest(p.read_bytes()) for p in fixtures.rglob('*') if p.is_file()}
    dump(output / 'fixture-inputs.json', inputs)
    with zipfile.ZipFile(output / 'fixture-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for path in inputs:
            z.write(fixtures / path, path)
    subprocess.run(['git', 'archive', '--format=zip', f'--output={output / "harness-source.zip"}', 'HEAD'],
                   cwd=repo, check=True)
    plan = dict(base=BASE, head=head, budget=BUDGET, schedule=schedule(), original_identity=identity,
                binary_sha256=warm.BINARY_HASHES, trace_build=trace_build, device_map=mapping,
                created_utc=datetime.now(timezone.utc).isoformat(), run=os.environ.get('GITHUB_RUN_ID'), attempt=1,
                python=sys.version, python_sha256=h.digest(Path(sys.executable).read_bytes()),
                subprocess_sha256=h.digest(Path(subprocess.__file__).read_bytes()), platform=platform.platform(),
                environment={k: os.environ.get(k) for k in ('ImageOS', 'ImageVersion', 'PROCESSOR_IDENTIFIER',
                    'NUMBER_OF_PROCESSORS', 'CL', '_CL_', 'LINK', '_LINK_', 'INCLUDE', 'LIB', 'LIBPATH', 'PATH', 'TEMP')})
    dump(output / 'plan.json', plan)
    result = dict(status='started', historical_cause_resolved=False, historical_risks_cleared=False,
                  release_authorized=False, rows=[], audits=[], entries=[], program_checks=[], tools={})
    save = lambda: dump(output / 'result.json', result)
    with h.Recorder(output / 'audits') as recorder:
        calls, observed = [], []
        tools = {}
        script = Path(__file__).resolve()
        def do_entry(argv, name):
            row = entry(argv, output / name)
            result['entries'].append(dict(label=name, **row))
            save()
            return row
        def check_program(case, name):
            row = entry([case.output], output / name, timeout=30)
            result['program_checks'].append(dict(label=name, **row))
            save()
            h.require(row['exit_code'] == 0, 'Built program check failed')
        def audit_warm(side, case, name):
            state = h.state(case)
            row = recorder.run(bins[side], case, name, verbose=True, timings=True)
            result['audits'].append(row)
            save()
            cumulative.audit_warm(row, case)
            h.require(state == h.state(case), 'Warm audit changed metadata')
            paths = re.findall(r'^  (cl|link):\s+(.+)$', recorder.human(row, 'stdout').decode('utf-8'), re.M)
            h.require(len(paths) == 2, 'Selected tool identity missing')
            for kind, path in paths:
                h.require(kind not in tools or tools[kind] == path, 'Selected tool changed')
                tools[kind] = path
                result['tools'][path] = h.digest(Path(path).read_bytes())
            prior = next((r for r in result['audits'] if r is not row and r['cwd'] == str(case.root)), None)
            h.require(prior is None or vector(prior) == vector(row), 'Full warm vector differs')
            save()
        def measured(slot, expected_exit=0, cold=True):
            case = cases[slot['case']]
            name = slot['label']
            workdir, tracedir = output / (name + '-worker'), output / (name + '-trace')
            spec = dict(**slot, worker=str(workdir), binary=str(bins[slot['side']]), case_root=str(case.root),
                        units=case.units, arguments=case.arguments, cold=cold, expected_exit=expected_exit)
            specpath = output / (name + '-spec.json')
            dump(specpath, spec)
            command = [sys.executable, script, '--worker', specpath]
            row = do_entry([tracer, tracedir, *command] if slot['observe'] else command, name + '-entry')
            data = load(workdir / 'worker.json') if (workdir / 'worker.json').exists() else None
            saved = dict(**slot, worker=data, entry=row)
            result['rows'].append(saved) if cold else result.setdefault('failure_controls', []).append(saved)
            save()
            # ETL/capture/decode and original diagnostics already exist, even on child failure.
            h.require(row['exit_code'] == 0 and data is not None, 'Entry failed; retain original evidence')
            if data.get('call'):
                calls.append(data['call'])
                saved['call'] = data['call']
            if slot['observe']:
                c, _, events = trace_health(tracedir)
                h.require(c['child']['exit_code'] == 0, 'Original worker failed; trace still retained')
                h.require(len(data['resources']) == 1, 'Missing original MQB handle identity')
                observed.append(data['resources'][0])
                saved['trace'] = audit.audit_lifetimes(c, events, data['resources'][0], tools, mapping,
                                                     units=case.units if cold else None)
                h.require(audit.canonical(saved['trace']['root']['image'], mapping) ==
                          audit.canonical(str(bins[slot['side']]), mapping), 'Observed root image differs')
            else:
                h.require(data['resources'] == [] and not tracedir.exists(), 'OFF observation unexpectedly active')
                saved['trace'] = None
            save()
            h.require(data['status'] == 'completed', 'Worker failed; original exit retained')
            raw = h.Recorder.__new__(h.Recorder)
            raw.root = workdir / 'mqb'
            if cold:
                h.require(cumulative.counts(raw, data['call']) == dict(compile=case.units, link=1, archive=0, pch=0),
                          'Cold report scope differs')
            else:
                h.require(data['call']['exit_code'] == 4 and b'cold_trace_visible_failure' in
                          raw.human(data['call'], 'stdout') + raw.human(data['call'], 'stderr'), 'Failure diagnostics lost')
            return saved
        save()
        try:
            negative = output / 'withheld-trace'
            sentinel = output / 'FORBIDDEN_SENTINEL'
            row = do_entry([tracer, '--test-withhold-file-provider', negative, tracer,
                            '--test-child-sentinel', sentinel], 'withheld')
            h.require(row['exit_code'] != 0, 'Withheld recorder unexpectedly succeeded')
            ev = [json.loads(s) for s in (negative / 'events.jsonl').read_text().splitlines()]
            result['withheld'] = audit.trace.audit_no_readiness(load(negative / 'capture.json'),
                load(negative / 'decode.json'), ev, sentinel_exists=sentinel.exists())
            positive, control = output / 'process-calibration-trace', output / 'process-calibration'
            row = do_entry([tracer, positive, sys.executable, script, '--process-calibration', control], 'process-control')
            h.require(row['exit_code'] == 0, 'Process calibration trace failed')
            capture, _, events = trace_health(positive)
            h.require(capture['child']['exit_code'] == 0, 'Process calibration worker failed')
            table, _ = audit.process_table(events)
            wrapper = audit.locate(table, dict(pid=capture['child']['pid'], created=capture['child']['created_filetime']))
            calibration = load(control / 'result.json')
            h.require(len(calibration['resources']) == len(calibration['calls']) == 3, 'Missing three real children')
            for i, call in enumerate(calibration['calls']):
                r = next(r for r in calibration['resources'] if r['pid'] == call['pid'])
                p = audit.locate(table, dict(pid=r['pid'], created=r['creation_100ns']))
                h.require(audit.parent_of(table, p) is wrapper and wrapper['start'] <= p['start'] <= p['end'] <= wrapper['end'],
                          'Calibration child generation/interval mismatch')
                h.require(call['exit_code'] == call['expected'] and f'CAL_OUT_{i}'.encode() in
                          (control / f'{i}.stdout').read_bytes() and f'CAL_ERR_{i}'.encode() in
                          (control / f'{i}.stderr').read_bytes(), 'Calibration exit/diagnostics differ')
            result['process_calibration'] = calibration
            save()
            for case in cases.values():
                for side in bins:
                    recorder.run(bins[side], case, f'prime-{case.name}-{side}')
                    audit_warm(side, case, f'pre-{case.name}-{side}')
            initial_tools = dict(result['tools'])
            dump(output / 'tools-before.json', initial_tools)
            for slot in plan['schedule']:
                case = cases[slot['case']]
                clean_case(case)
                measured(slot)
                audit_warm(slot['side'], case, slot['label'] + '-post')
                check_program(case, slot['label'] + '-program')
            case = cases['small']
            unit = case.root / 'unit_000.cpp'
            original = unit.read_bytes()
            for side in bins:
                cumulative.write_later(unit, b'#error cold_trace_visible_failure\n')
                dump(output / (side + '-failed-input.json'), dict(path=str(unit), bytes_hex=unit.read_bytes().hex()))
                measured(dict(case='small', side=side, observe=True, label='failure-' + side), expected_exit=4, cold=False)
                cumulative.write_later(unit, original)
                recorder.run(bins[side], case, 'caller-repair-' + side)
                audit_warm(side, case, 'caller-repaired-' + side)
                check_program(case, 'caller-repaired-program-' + side)
            h.require(len(recorder.calls) + len(calls) == 78 and len(result['rows']) == 32 and len(observed) == 18
                      and len(result['entries']) == 36 and len(result['program_checks']) == 34, 'Fixed budget incomplete')
            h.require(result['tools'] == initial_tools and all(h.digest(Path(p).read_bytes()) == v for p, v in initial_tools.items()),
                      'Selected tools changed')
            h.require({s: h.digest(p.read_bytes()) for s, p in bins.items()} == warm.BINARY_HASHES, 'Measured binaries changed')
            h.require(h.digest(tracer.read_bytes()) == trace_build['tracer_sha256'], 'Recorder binary changed')
            h.require({p: h.digest((fixtures / p).read_bytes()) for p in inputs} == inputs, 'Fixture source changed')
            recorder.finalize()
            result.update(status='completed', summaries=summaries(result['rows']), mqb_calls=78,
                          observed_mqb_resources=len(observed))
            save()
        except Exception as error:
            result.update(status='failed', error=repr(error), completed_parent_recorder_calls=len(recorder.calls),
                          loaded_worker_calls=len(calls))
            save()
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--process-calibration', type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--tracer', type=Path)
    parser.add_argument('--trace-identity', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.worker:
        worker(args.worker)
    elif args.process_calibration:
        process_calibration(args.process_calibration)
    else:
        h.require(all((args.archive, args.tracer, args.trace_identity, args.output)), 'Missing study paths')
        run(*(p.resolve() for p in (args.archive, args.tracer, args.trace_identity, args.output)))
