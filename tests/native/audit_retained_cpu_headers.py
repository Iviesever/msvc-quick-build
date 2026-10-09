"""Audit CPU-header availability in the exact retained #190 ETLs.

No new capture or measured MQB execution. Ordinary event-header CPU units are
charged to the emitting thread, not an arbitrary payload PID. Thread CycleTime
is never converted to milliseconds. Scaled exit snapshots are not full-process
CPU accounts or scheduling/wait observations, even when retained queries agree.
"""
from __future__ import annotations

import argparse
from collections import Counter
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

import audit_retained_process_stop as retained

need = retained.need
Error = retained.EvidenceError
BUNDLE_SHA = '9fabb1745891a8ddf7dbf9f69d4382d40a7a67764b5eb73dbd49bec7c985acb6'
RESULT_SHA = '4718aea7f8eeeed36f9baf5d55009fd642cd4c53ba2680ed32b0e6089fbb1500'
THREAD_START = (('ProcessID', 8), ('ThreadID', 8), ('StackBase', 16), ('StackLimit', 16),
                ('UserStackBase', 16), ('UserStackLimit', 16), ('StartAddr', 16),
                ('Win32StartAddr', 16), ('TebBase', 16), ('SubProcessTag', 8))
THREAD_STOP = THREAD_START + (('CycleTime', 10),)
IDENTITY = ('sequence', 'provider', 'id', 'version', 'flags', 'pid', 'tid', 'qpc', 'raw_payload')


def uint(value, bits=64):
    need(type(value) is int and 0 <= value < 1 << bits, 'invalid unsigned integer')
    return value


def decode_threads(events):
    """Only exact same-capture schemas; pointer width is the retained 64-bit width."""
    cache, answer = set(), {}
    for event in sorted(events, key=lambda e: e['sequence']):
        if event['provider'] != 'process' or event['id'] not in (3, 4):
            continue
        need(type(event['id']) is int and type(event['version']) is int and event['version'] == 1,
             'unsupported thread version')
        need(uint(event['flags'], 16) & 0x60 == 0x40, 'unsupported pointer width')
        need(event.get('decode_error') is None, 'original thread decode error')
        layout = THREAD_START if event['id'] == 3 else THREAD_STOP
        schema = event.get('property_schema')
        if schema is not None:
            expected = [dict(name=n, flags=0, in_type=t) for n, t in layout]
            need(schema == expected and all(type(p['flags']) is int and type(p['in_type']) is int for p in schema),
                 'unknown thread schema')
            cache.add(event['id'])
        need(event['id'] in cache, 'missing same-capture thread schema')
        raw = event['raw_payload']
        need(type(raw) is str and all(c in '0123456789abcdefABCDEF' for c in raw), 'invalid thread hex')
        fmt = '<II7QI' + ('Q' if event['id'] == 4 else '')
        need(len(raw) == 2 * struct.calcsize(fmt), 'thread payload length mismatch')
        values = dict(zip((n for n, _ in layout), struct.unpack(fmt, bytes.fromhex(raw))))
        data = event['data']
        need(type(data) is dict and set(data) == {'ProcessID', 'ThreadID'}, 'unknown retained identity fields')
        need(all(type(v) is int and v == values[k] for k, v in data.items()), 'raw/TDH thread identity conflict')
        sequence = uint(event['sequence'])
        need(sequence not in answer, 'duplicate thread event')
        answer[sequence] = dict(event, fields=values)
    return answer


def match_headers(events, headers, meta, decode, etl_bytes, hz):
    for k in ('open_status', 'process_trace_status', 'close_trace_status', 'errors', 'log_header_events_lost'):
        need(uint(meta[k]) == 0, 'native header reader failed: ' + k)
    expected = [e for e in events if e['provider'] == 'process' and e['id'] in (1, 2, 3, 4)]
    need(uint(meta['total_events']) == decode['total_events'] and
         uint(meta['selected_events']) == len(headers) == len(expected), 'event coverage mismatch')
    need(uint(meta['etl_bytes']) == etl_bytes == decode['etl_bytes'], 'ETL size mismatch')
    need(uint(meta['perf_frequency']) == hz and uint(meta['pointer_size']) == 8, 'log clock/pointer mismatch')
    need(uint(meta['timer_resolution_100ns'], 32) > 0, 'CPU timer resolution missing')
    answer = {}
    for old, header in zip(expected, headers):
        need(all(type(header.get(k)) is type(old[k]) and header[k] == old[k] for k in IDENTITY),
             'native header does not match original event')
        k, u = uint(header['kernel_units'], 32), uint(header['user_units'], 32)
        need(uint(header['processor_time_raw']) == (u << 32) | k, 'CPU union conflict')
        need(old['sequence'] not in answer, 'duplicate native record')
        answer[old['sequence']] = header
    return answer


def process_threads(process, processes, threads, headers, timer):
    process = retained.lifetime.locate(processes, process)
    events = sorted((e for e in threads.values() if e['fields']['ProcessID'] == process['pid'] and
                     process['start'] <= e['qpc'] <= process['end']), key=lambda e: (e['qpc'], e['sequence']))
    active, rows = {}, []
    for event in events:
        tid = event['fields']['ThreadID']
        if event['id'] == 3:
            need(tid not in active, 'overlapping thread generation')
            active[tid] = event
            continue
        start = active.pop(tid, None)
        need(start is not None and start['qpc'] < event['qpc'], 'missing/ambiguous thread start')
        need(event['pid'] == process['pid'] and event['tid'] == tid, 'thread stop emitted by different thread')
        header = headers.get(event['sequence']) if headers is not None else None
        usable = header is not None and not (header['flags'] & (0x2 | 0x8 | 0x10 | 0x100))
        row = dict(tid=tid, start_sequence=start['sequence'], stop_sequence=event['sequence'],
                   start_qpc=start['qpc'], stop_qpc=event['qpc'], cycle_time=event['fields']['CycleTime'],
                   cpu_header=header, ordinary_cpu_fields=usable,
                   header_kernel_snapshot_100ns=header['kernel_units'] * timer if usable else None,
                   header_user_snapshot_100ns=header['user_units'] * timer if usable else None)
        rows.append(row)
    need(rows and not active, 'incomplete selected process thread coverage')
    ordinary = all(r['ordinary_cpu_fields'] for r in rows)
    return dict(pid=process['pid'], created=process['created'], image=process['image'],
                start_sequence=process['start_sequence'], stop_sequence=process['stop_sequence'],
                thread_count=len(rows), threads=rows, cycle_time_sum=sum(r['cycle_time'] for r in rows),
                header_kernel_snapshot_sum_100ns=sum(r['header_kernel_snapshot_100ns'] for r in rows) if ordinary else None,
                header_user_snapshot_sum_100ns=sum(r['header_user_snapshot_100ns'] for r in rows) if ordinary else None,
                full_process_cpu_100ns=None, wait_100ns=None, wait_cause=None)


def query_comparison(row, query):
    need(row['pid'] == query['pid'] and row['created'] == query['creation_100ns'], 'query generation mismatch')
    need(not query['errors'] and query['query_count'] == 3, 'retained query incomplete')
    kernel, user = (uint(query[k]) for k in ('kernel_100ns', 'user_100ns'))
    sk, su = row['header_kernel_snapshot_sum_100ns'], row['header_user_snapshot_sum_100ns']
    return dict(retained_query=query, query_minus_header_kernel_100ns=None if sk is None else kernel-sk,
                query_minus_header_user_100ns=None if su is None else user-su,
                exact_snapshot_match=sk == kernel and su == user,
                nonzero_query_with_all_zero_headers=(kernel+user > 0 and sk == su == 0))


def run_reader(reader, etl, directory):
    need(not directory.exists(), 'reader output already exists')
    before = retained.file_sha(etl)
    try:
        call = subprocess.run([str(reader.resolve()), str(etl), str(directory)], capture_output=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        directory.parent.joinpath(directory.name + '.stdout').write_bytes(error.stdout or b'')
        directory.parent.joinpath(directory.name + '.stderr').write_bytes(error.stderr or b'')
        raise Error('offline reader timed out; partial diagnostics retained') from error
    finally:
        need(retained.file_sha(etl) == before, 'ETL changed during reader execution')
    directory.parent.joinpath(directory.name + '.stdout').write_bytes(call.stdout)
    directory.parent.joinpath(directory.name + '.stderr').write_bytes(call.stderr)
    need(call.returncode == 0, 'offline reader failed; logs retained')
    meta = json.loads((directory / 'reader.json').read_text(encoding='utf-8'))
    headers = [json.loads(s) for s in (directory / 'headers.jsonl').read_text(encoding='utf-8').splitlines()]
    return meta, headers


def audit_steps(bundle_path, output, reader=None, debug_reader=None):
    bundle = retained.Archive(bundle_path, BUNDLE_SHA)
    try:
        previous_bytes = bundle.read('supplement/result.json')
        need(retained.sha(previous_bytes) == RESULT_SHA, 'wrong completed ProcessStop supplement')
        previous = json.loads(previous_bytes)
        with zipfile.ZipFile(io.BytesIO(bundle.read('source.zip'))) as source:
            for name in ('audit_retained_process_stop.py', 'audit_cold_process_trace.py', 'verify_pdb_file_trace.py'):
                need(source.read('tests/native/' + name).replace(b'\r\n', b'\n') ==
                     Path(__file__).with_name(name).read_bytes().replace(b'\r\n', b'\n'), 'changed original helper')
        with tempfile.TemporaryDirectory(prefix='mqb-retained-cpu-') as temporary:
            root = Path(temporary)
            for name in ('original-cold.zip', 'original-input.zip'):
                (root / name).write_bytes(bundle.read(name))
            cold = retained.Archive(root / 'original-cold.zip', retained.ARCHIVE_SHA)
            inputs = retained.Archive(root / 'original-input.zip', retained.INPUT_SHA)
            try:
                identity = retained.verify_inputs(inputs)
                plan = cold.load('study/plan.json'); retained.verify_plan(plan, identity)
                original = cold.load('study/result.json')
                rows = [r for r in previous['rows'] if r['supplement']]
                labels = ['process-calibration', 'withheld'] + [r['label'] for r in rows]
                need(len(labels) == len(set(labels)) == 20 and
                     {f'study/{label}-trace/events.etl' for label in labels} ==
                     {n for n in cold.zip.namelist() if n.endswith('/events.etl')}, 'capture inventory mismatch')
                answers, comparisons, inventories = [], [], []
                for label in labels:
                    trace_dir = root / 'current-trace'; trace_dir.mkdir(exist_ok=True)
                    for name in ('capture.json', 'decode.json', 'events.jsonl', 'events.etl'):
                        (trace_dir / name).write_bytes(cold.read(f'study/{label}-trace/{name}'))
                    capture, decode = [json.loads((trace_dir/n).read_text(encoding='utf-8-sig')) for n in ('capture.json', 'decode.json')]
                    if label == 'withheld':
                        need(capture['child'] is None and capture['readiness']['accepted'] is False,
                             'withheld control unexpectedly admitted a child')
                        events = [json.loads(s) for s in (trace_dir/'events.jsonl').read_text().splitlines()]
                    else:
                        capture, decode, events = retained.lifetime.load_trace(trace_dir)
                    threads = decode_threads(events)
                    meta, headers = (None, None) if reader is None else run_reader(reader, trace_dir/'events.etl', output/label)
                    matched = None if headers is None else match_headers(events, headers, meta, decode,
                                    (trace_dir/'events.etl').stat().st_size, capture['qpc_frequency'])
                    inventories.append(dict(label=label, thread_events=len(threads), reader=meta,
                                            etl_sha256=retained.file_sha(trace_dir/'events.etl')))
                    if label == 'withheld':
                        yield dict(label=label, negative_control=True)
                        continue  # Never treat negative-readiness CPU bytes as a positive sample.
                    processes, _ = retained.lifetime.process_table(events)
                    timer = meta['timer_resolution_100ns'] if meta else None
                    if label == 'process-calibration':
                        selected = [(retained.lifetime.locate(processes, dict(pid=c['pid'], created=c['created'])),
                                     c['query_comparison']['retained_query'], None) for c in previous['calibration']]
                    else:
                        old = next(r for r in rows if r['label'] == label)
                        resource = old['supplement']['root_query_comparison']['retained_query']
                        tools = {Path(p).stem: p for p in original['tools']}
                        checked = retained.lifetime.audit_lifetimes(capture, events, resource, tools, plan['device_map'],
                                    units=None if not old['scored'] else (129 if old['case'] == 'common' else 2))
                        need(checked == old['original_trace'], 'old lifetime result no longer reproduces')
                        selected = [(checked['root'], resource, old['supplement']['root']['stop_fields']['CPUCycleCount'])]
                        selected += [(p, None, s['stop_fields']['CPUCycleCount']) for p, s in zip(checked['tools'], old['supplement']['tools'])]
                    records = []
                    for process, query, cycles in selected:
                        row = process_threads(process, processes, threads, matched, timer)
                        row['process_stop_cycles'] = cycles
                        row['process_minus_thread_cycles'] = None if cycles is None else cycles-row['cycle_time_sum']
                        row['kind'] = process.get('kind', 'calibration' if label == 'process-calibration' else 'mqb')
                        if query is not None:
                            row['query_comparison'] = query_comparison(row, query)
                            comparisons.append(dict(label=label, pid=row['pid'], **row['query_comparison']))
                        records.append(row)
                    answers.append(dict(label=label, records=records))
                    if label == 'process-calibration' and debug_reader:
                        debug_meta, debug_headers = run_reader(debug_reader, trace_dir/'events.etl', output/'debug-calibration')
                        need(meta == debug_meta and headers == debug_headers, 'Debug/Release original calibration read differs')
                    print(f'Audited original threads: {label}; processes={len(records)}', flush=True)
                    yield dict(label=label, selected_processes=len(records))
                counts = Counter(p['kind'] for a in answers for p in a['records'])
                need(counts == {'cl': 1050, 'link': 16, 'mqb': 18, 'calibration': 3}, 'selected generation count mismatch')
                need(len(comparisons) == 21, 'query comparison coverage mismatch')
                answer = dict(status='completed', mode='native-header-audit' if reader else 'inventory-only',
                    original_bundle_sha256=BUNDLE_SHA, original_supplement_sha256=RESULT_SHA,
                    input_member_sha256=cold.members, inventories=inventories, rows=answers,
                    process_counts=dict(counts), query_comparisons=comparisons,
                    header_snapshot_matches_all_21_queries=all(c['exact_snapshot_match'] for c in comparisons) if reader else None,
                    nonzero_queries_with_zero_headers=sum(c['nonzero_query_with_all_zero_headers'] for c in comparisons) if reader else None,
                    full_process_cpu_accounting_validated=False, scheduling_observed=False,
                    new_diagnostic_mqb_calls=0, new_trace_captures=0,
                    historical_cause_resolved=False, historical_risks_cleared=False, release_authorized=False)
            finally:
                cold.finish(); inputs.finish()
    finally:
        bundle.finish()
    yield dict(label='completed', report=answer)


def audit(bundle_path, output, reader=None, debug_reader=None):
    result = None
    for progress in audit_steps(bundle_path, output, reader, debug_reader):
        if 'report' in progress:
            result = progress['report']
    need(result is not None, 'audit incomplete')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inventory-only', action='store_true')
    parser.add_argument('--reader', type=Path)
    parser.add_argument('--debug-reader', type=Path)
    args = parser.parse_args()
    if args.inventory_only == (args.reader is not None) or (args.debug_reader and not args.reader):
        parser.error('choose either --inventory-only or --reader; debug requires reader')
    try:
        args.output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        parser.error('output already exists; no overwrite')
    try:
        result = audit(args.bundle, args.output, args.reader, args.debug_reader)
        (args.output/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    except Exception as error:
        (args.output/'failure.json').write_text(json.dumps(dict(status='failed', error=repr(error),
             release_authorized=False), indent=2), encoding='utf-8')
        raise
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
