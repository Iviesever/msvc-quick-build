"""Read-only cold-build ETW lifetime attribution; no CPU/IO or causality claim.

Reuse the established recorder health/readiness contract. Interval complements
are merely time not covered by observed tools, never 'MQB overhead'. Missing
child exits and ambiguous process generations are rejected, not counted as zero.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path

import verify_pdb_file_trace as trace

need = trace.need
integer = trace.integer


def union(intervals):
    merged = []
    for start, end in sorted(intervals):
        need(type(start) is int and type(end) is int and 0 <= start <= end, 'invalid interval')
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def span(intervals):
    return sum(b - a for a, b in union(intervals))


def canonical(path, mapping):
    need(isinstance(path, str) and bool(path), 'missing Unicode process image')
    path = path.replace('/', '\\').casefold()
    for device, drive in sorted(mapping.items(), key=lambda x: -len(x[0])):
        if path.startswith(device.casefold() + '\\'):
            path = drive.casefold() + path[len(device):]
            break
    for prefix in ('\\??\\', '\\\\?\\'):
        if path.startswith(prefix):
            path = path[len(prefix):]
    return path


def process_table(events):
    """Use payload PID, not the emitting event-header PID. Keep unmatched stops."""
    processes, unmatched = [], []
    for e in sorted(events, key=lambda x: (x['qpc'], x['sequence'])):
        if e['provider'] != 'process' or e['id'] not in (1, 2):
            continue
        data = e['data']
        pid = integer(data.get('ProcessID'), 'payload ProcessID')
        if e['id'] == 1:
            processes.append(dict(pid=pid, created=integer(data.get('CreateTime'), 'CreateTime'),
                                  parent_pid=integer(data.get('ParentProcessID'), 'ParentProcessID'),
                                  image=data.get('ImageName'), start=e['qpc'], end=None,
                                  start_sequence=e['sequence'], stop_sequence=None))
        else:
            candidates = [p for p in processes if p['pid'] == pid and p['end'] is None]
            if data.get('CreateTime') is not None:
                candidates = [p for p in candidates if p['created'] == data['CreateTime']]
            if len(candidates) == 1:
                candidates[0].update(end=e['qpc'], stop_sequence=e['sequence'])
            else:
                unmatched.append(e['sequence'])
    return processes, unmatched


def locate(processes, identity):
    matches = [p for p in processes if p['pid'] == identity['pid'] and p['created'] == identity['created']]
    need(len(matches) == 1, 'missing/duplicate process generation')
    p = matches[0]
    need(p['created'] > 0 and p['end'] is not None and p['start'] <= p['end'], 'missing process stop')
    collisions = [q for q in processes if q is not p and q['pid'] == p['pid'] and
                  q['start'] <= p['end'] and (q['end'] is None or p['start'] <= q['end'])]
    need(not collisions, 'overlapping process generations')
    return p


def parent_of(processes, process):
    matches = [p for p in processes if p['pid'] == process['parent_pid'] and
               p['start'] <= process['start'] and (p['end'] is None or process['start'] <= p['end'])]
    need(len(matches) == 1, 'missing/ambiguous event-time parent generation')
    return matches[0]


def boundary_calibrations(capture, events):
    """Verify both real success/conflict controls without the old A/B PDB fixture."""
    for name in ('before', 'after'):
        marker = capture.get(name)
        need(isinstance(marker, dict) and marker.get('win32_error') == 32 and
             marker.get('expected_ntstatus') == 0xc0000043, 'missing boundary calibration')
        need(isinstance(marker.get('file_id'), str) and len(marker['file_id']) == 32,
             'missing held calibration file identity')
        path = trace.canonical(marker['path'], capture)
        pending, pairs = {}, []
        for event in events:
            if event['provider'] != 'file':
                continue
            d = event['data']
            irp = integer(d.get('Irp'), 'Irp')
            if event['id'] == 12:
                if irp in pending:
                    need(trace.canonical(pending[irp]['data']['FileName'], capture) != path,
                         'ambiguous calibration IRP')
                pending[irp] = event
            elif event['id'] == 24:
                start = pending.pop(irp, None)
                if start and trace.canonical(start['data']['FileName'], capture) == path:
                    pairs.append((start, event))
        need(len(pairs) == 2 and sorted(e['data']['Status'] for _, e in pairs) == [0, 0xc0000043],
             'calibration success/failure pair missing')
        for a, b in pairs:
            low, high = ((marker['start_qpc'], marker['denied_before_qpc']) if b['data']['Status'] == 0
                         else (marker['denied_before_qpc'], marker['denied_after_qpc']))
            need(low <= a['qpc'] <= b['qpc'] <= high and a['pid'] == marker['owner']['pid'],
                 'calibration outside actual operation/owner')
    child = capture['child']
    need(capture['before']['denied_after_qpc'] < child['before_launch_qpc'] <= child['after_wait_qpc']
         < capture['after']['start_qpc'], 'calibrations do not bracket child')


def load_trace(directory):
    capture, decode = [trace.load(directory / name) for name in ('capture.json', 'decode.json')]
    events = [__import__('json').loads(line) for line in (directory / 'events.jsonl').read_text(encoding='utf-8').splitlines()]
    need((directory / 'events.etl').stat().st_size == decode['etl_bytes'], 'ETL byte count mismatch')
    events = trace.checked_events(capture, decode, events)
    trace.validate_readiness(capture, events)
    boundary_calibrations(capture, events)
    return capture, decode, events


def audit_lifetimes(capture, events, resource, tool_paths, mapping, *, units=None):
    processes, unmatched = process_table(events)
    wrapper = locate(processes, dict(pid=capture['child']['pid'], created=capture['child']['created_filetime']))
    need(capture['child']['before_launch_qpc'] <= wrapper['start'] <= wrapper['end'] <=
         capture['child']['after_wait_qpc'], 'wrapper outside native launch/wait interval')
    root = locate(processes, dict(pid=resource['pid'], created=resource['creation_100ns']))
    need(parent_of(processes, root) is wrapper and wrapper['start'] <= root['start'] <= root['end'] <= wrapper['end'],
         'MQB not within exact wrapper generation')
    descendants, frontier = [], [root]
    while frontier:
        parent = frontier.pop()
        for p in processes:
            if p in descendants or p is root or p['parent_pid'] != parent['pid']:
                continue
            # A child of a later reused PID is not descended from this parent.
            if parent['start'] <= p['start'] and (parent['end'] is None or p['start'] <= parent['end']):
                need(parent_of(processes, p) is parent, 'ambiguous descendant parent')
                descendants.append(p)
                frontier.append(p)
    tools = []
    expected = {kind: canonical(path, mapping) for kind, path in tool_paths.items()}
    for p in descendants:
        image = canonical(p['image'], mapping)
        name = image.rsplit('\\', 1)[-1]
        if name not in ('cl.exe', 'link.exe'):
            continue
        kind = name[:-4]
        need(image == expected[kind], 'observed tool image differs from selected tool')
        locate(processes, p)
        need(root['start'] <= p['start'] <= p['end'] <= root['end'], 'tool extends outside MQB lifetime')
        tools.append(dict(p, kind=kind, exit_code=None))  # Original decoder does not expose it.
    counts = Counter(p['kind'] for p in tools)
    if units is not None:
        need(counts['cl'] == units and counts['link'] == 1, 'incomplete/unexpected cold tool count')
    hz = capture['qpc_frequency']
    intervals = [[p['start'], p['end']] for p in tools]
    merged = union(intervals)
    complement, cursor = [], root['start']
    for a, b in merged:
        if a > cursor:
            complement.append([cursor, a])
        cursor = b
    if cursor < root['end']:
        complement.append([cursor, root['end']])
    return dict(root=root, wrapper=wrapper, tools=tools, descendants=descendants,
                unrelated_unmatched_stops=unmatched, tool_counts=dict(counts),
                tool_union_qpc=merged, uncovered_qpc=complement,
                root_lifetime_ms=(root['end'] - root['start']) * 1000 / hz,
                cl_union_ms=span([(p['start'], p['end']) for p in tools if p['kind'] == 'cl']) * 1000 / hz,
                link_union_ms=span([(p['start'], p['end']) for p in tools if p['kind'] == 'link']) * 1000 / hz,
                tools_union_ms=span(intervals) * 1000 / hz, uncovered_ms=span(complement) * 1000 / hz,
                max_cl_lifetime_ms=max(((p['end'] - p['start']) * 1000 / hz for p in tools if p['kind'] == 'cl'), default=None),
                historical_cause_resolved=False, all_writer_coverage_proven=False)
