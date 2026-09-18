"""Read-only supplement to #190; never launch MQB, a tracer or a tool.

Decode only the two exact manifest layouts retained on the original host. This
is not TDH replay, a general ETW decoder, CPU milliseconds or a wait diagnosis.
The original reports, counters, scores, exits and missing OFF values stay intact.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import tempfile
import zipfile
from collections import Counter
from pathlib import Path

import audit_cold_process_trace as lifetime

need = lifetime.need
EvidenceError = lifetime.trace.EvidenceError
ARCHIVE_SHA = '85daa4dfdb6b342644fe42be555960bc0edb2fe28b924b020a6a8a3bf927270b'
INPUT_SHA = '71ceb8b6c861f9e2b7ba36aea680b3909cf21b65380e0853bd584fe3c3419220'
SOURCE_SHA = '68d2bb75deeaeac4928d25087678cf5c503837ed83b7a074715756cb46f5f7b6'
ORIGINAL_SHA = 'd70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530'
BINARIES = {'baseline': '46a3fc9fd4826b16f5130895438a82601d6531a29000982720f88cfc94fcc95f',
            'candidate': 'deab6c9dea5342f3a83ca79e2be3f51d483b03a8f045fbfa8411a8e6ad645c8c'}
HELPERS = {'audit_cold_process_trace.py': 'c6ae47303e7c2bd8f999c9a2c569fe4ab031a4006cb59bac403828de964a25e6',
           'verify_pdb_file_trace.py': '9bdedbd13d420d5b5f66711b3bf4c3b1f2fb8fbedf4e0f6978e5293b9c058da3'}
START = (('ProcessID', 8), ('ProcessSequenceNumber', 10), ('CreateTime', 17),
         ('ParentProcessID', 8), ('ParentProcessSequenceNumber', 10), ('SessionID', 8),
         ('Flags', 8), ('ProcessTokenElevationType', 8), ('ProcessTokenIsElevated', 8),
         ('MandatoryLabel', 19), ('ImageName', 1), ('ImageChecksum', 8),
         ('TimeDateStamp', 8), ('PackageFullName', 1), ('PackageRelativeAppId', 1),
         ('SecurityMitigations', 8))
STOP = (('ProcessID', 8), ('ProcessSequenceNumber', 10), ('CreateTime', 17),
        ('ExitTime', 17), ('ExitCode', 8), ('TokenElevationType', 8), ('HandleCount', 8),
        ('CommitCharge', 10), ('CommitPeak', 10), ('CPUCycleCount', 10),
        ('ReadOperationCount', 8), ('WriteOperationCount', 8),
        ('ReadTransferKiloBytes', 8), ('WriteTransferKiloBytes', 8),
        ('HardFaultCount', 8), ('ImageName', 2))
LAYOUTS = {(1, 4): START, (2, 2): STOP}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def decode_payload(raw: str, layout: tuple) -> dict:
    """No padding guesses, implicit arrays, ANSI code-page guesses or truncation."""
    need(isinstance(raw, str) and len(raw) % 2 == 0 and
         all(c in '0123456789abcdefABCDEF' for c in raw), 'invalid raw hex')
    payload = bytes.fromhex(raw)
    offset, result = 0, {}

    def take(size: int) -> bytes:
        nonlocal offset
        need(offset + size <= len(payload), 'truncated payload')
        data = payload[offset:offset + size]
        offset += size
        return data

    for name, kind in layout:
        if kind in (8, 10, 17):
            result[name] = int.from_bytes(take(4 if kind == 8 else 8), 'little')
        elif kind in (1, 2):
            width = 2 if kind == 1 else 1
            parts = []
            while True:
                part = take(width)
                if part == bytes(width):
                    break
                parts.append(part)
            data = b''.join(parts)
            if kind == 1:
                try:
                    result[name] = data.decode('utf-16-le', errors='strict')
                except UnicodeError as error:
                    raise EvidenceError('invalid UTF-16 payload') from error
            else:
                result[name] = {'encoding': 'opaque-ansi', 'bytes': data.hex()}
        elif kind == 19:
            header = take(8)
            need(header[0] == 1 and header[1] <= 15, 'unsupported SID')
            result[name] = (header + take(4 * header[1])).hex()
        else:
            raise EvidenceError('unsupported field type')
    need(offset == len(payload), 'unconsumed payload suffix')
    return result


def decode_records(events: list[dict]) -> dict[int, dict]:
    """Schema cache is local to ONE capture, keyed by provider/id/version."""
    schemas, records = {}, {}
    for event in sorted(events, key=lambda e: e['sequence']):
        if event['provider'] != 'process' or event['id'] not in (1, 2):
            continue
        key = (event['id'], event['version'])
        need(type(key[0]) is int and type(key[1]) is int and key in LAYOUTS,
             'unsupported process event version')
        need(event.get('decode_error') is None and type(event.get('data')) is dict,
             'original decoder failed')
        flags = event.get('flags')
        need(type(flags) is int and flags & 0x60 == 0x40, 'unsupported event architecture')
        supplied = event.get('property_schema')
        expected = [{'name': n, 'flags': 0, 'in_type': t} for n, t in LAYOUTS[key]]
        if supplied is not None:
            need(type(supplied) is list and supplied == expected and
                 all(type(p['flags']) is int and type(p['in_type']) is int for p in supplied),
                 'unknown or conflicting property schema')
            schemas[key] = supplied
        need(key in schemas, 'schema absent in this capture before record')
        sequence = lifetime.integer(event['sequence'], 'sequence')
        need(sequence not in records, 'duplicate process event sequence')
        parsed = decode_payload(event['raw_payload'], LAYOUTS[key])
        for name, value in event['data'].items():
            need(name in parsed and type(value) is type(parsed[name]) and value == parsed[name],
                 'raw payload disagrees with retained TDH data: ' + name)
        records[sequence] = dict(id=event['id'], version=event['version'],
                                 qpc=lifetime.integer(event['qpc'], 'qpc'), data=parsed,
                                 raw_sha256=sha(bytes.fromhex(event['raw_payload'])))
    return records


def supplement(process: dict, records: dict, processes: list, hz: int,
               *, check_parent: bool = True) -> dict:
    """Extend an already validated generation, never a bare PID lookup."""
    process = lifetime.locate(processes, process)
    start, stop = [records.get(process[k]) for k in ('start_sequence', 'stop_sequence')]
    need(start is not None and stop is not None and start['id'] == 1 and stop['id'] == 2,
         'missing raw start/stop record')
    a, b = start['data'], stop['data']
    need(start['qpc'] == process['start'] and stop['qpc'] == process['end'], 'QPC identity mismatch')
    need(a['ProcessID'] == b['ProcessID'] == process['pid'] and
         a['CreateTime'] == b['CreateTime'] == process['created'], 'process generation mismatch')
    need(a['ProcessSequenceNumber'] == b['ProcessSequenceNumber'] > 0, 'process sequence mismatch')
    need(a['ImageName'] == process['image'] and b['ExitTime'] >= b['CreateTime'],
         'image or exit time mismatch')
    name = a['ImageName'].replace('/', '\\').rsplit('\\', 1)[-1]
    need(name.isascii() and b['ImageName']['bytes'] == name.encode('ascii').hex(),
         'stop basename is not the exact known ASCII image')
    parent_sequence = None
    if check_parent:
        parent = lifetime.parent_of(processes, process)
        parent_start = records.get(parent['start_sequence'])
        need(parent_start is not None and parent_start['id'] == 1 and
             a['ParentProcessID'] == parent['pid'] and a['ParentProcessSequenceNumber'] ==
             parent_start['data']['ProcessSequenceNumber'], 'parent sequence mismatch')
        parent_sequence = parent['start_sequence']
    need(type(hz) is int and hz > 0, 'invalid QPC frequency')
    return dict(pid=process['pid'], created=process['created'], image=process['image'],
                start_sequence=process['start_sequence'], stop_sequence=process['stop_sequence'],
                parent_start_sequence=parent_sequence, raw_start_sha256=start['raw_sha256'],
                raw_stop_sha256=stop['raw_sha256'], stop_fields=b,
                lifetime_ms=(process['end'] - process['start']) * 1000 / hz,
                cpu_ms=None, wait_ms=None, wait_cause=None)


def compare_query(record: dict, resource: dict) -> dict:
    fields = record['stop_fields']
    need(not resource['errors'] and resource['query_count'] == 3, 'invalid retained root queries')
    for field, query in (('ProcessID', 'pid'), ('CreateTime', 'creation_100ns'),
                         ('ExitTime', 'exit_100ns'), ('ExitCode', 'exit_code')):
        need(fields[field] == resource[query], 'query identity/exit mismatch: ' + field)
    # The post-exit handle query and stop-event counter snapshot are NOT identical
    # sampling points. Report differences; never tune a tolerance until they pass.
    return dict(retained_query=resource,
                query_minus_event_cycles=resource['cycles'] - fields['CPUCycleCount'],
                query_minus_event_read_ops=resource['io']['read_ops'] - fields['ReadOperationCount'],
                query_minus_event_write_ops=resource['io']['write_ops'] - fields['WriteOperationCount'],
                event_read_transfer_kilobytes=fields['ReadTransferKiloBytes'],
                event_write_transfer_kilobytes=fields['WriteTransferKiloBytes'],
                query_read_bytes=resource['io']['read_bytes'], query_write_bytes=resource['io']['write_bytes'])


class Archive:
    def __init__(self, path: Path, expected: str):
        need(file_sha(path) == expected, 'wrong original archive SHA256')
        self.path, self.expected, self.zip = path, expected, zipfile.ZipFile(path)
        names = self.zip.namelist()
        need(len(names) == len(set(names)), 'duplicate ZIP members')
        self.members = {}

    def read(self, name: str) -> bytes:
        data = self.zip.read(name)
        self.members[name] = sha(data)
        return data

    def load(self, name: str):
        return json.loads(self.read(name).decode('utf-8-sig'))

    def finish(self):
        self.zip.close()
        need(file_sha(self.path) == self.expected, 'input archive changed during audit')


def capture_from_archive(archive: Archive, label: str):
    # Copy only fixed capture filenames into an ephemeral directory. No arbitrary
    # ZIP extraction, original build-state access, tool execution or input writes.
    with tempfile.TemporaryDirectory(prefix='mqb-retained-stop-') as directory:
        root = Path(directory)
        for name in ('capture.json', 'decode.json', 'events.jsonl', 'events.etl'):
            (root / name).write_bytes(archive.read(f'study/{label}-trace/{name}'))
        return lifetime.load_trace(root)


def verify_inputs(inputs: Archive) -> dict:
    source = inputs.read('source.zip')
    need(sha(source) == SOURCE_SHA, 'wrong original source archive')
    with zipfile.ZipFile(io.BytesIO(source)) as code:
        for name, expected in HELPERS.items():
            old = code.read('tests/native/' + name).replace(b'\r\n', b'\n')
            current = (Path(__file__).parent / name).read_bytes().replace(b'\r\n', b'\n')
            need(sha(old) == sha(current) == expected, 'read-only helper changed')
    original = inputs.read('original.zip')
    need(sha(original) == ORIGINAL_SHA, 'wrong cumulative original archive')
    with zipfile.ZipFile(io.BytesIO(original)) as binaries:
        for side, expected in BINARIES.items():
            need(sha(binaries.read(side + '/mqb.exe')) == expected, 'wrong measured executable')
    identity = inputs.load('trace.identity.json')
    need(sha(inputs.read('recorder.exe')) == identity['tracer_sha256'] ==
         '705636206e05872f9783fcda72e9eabd85099ad3c7ddc0945ebc71d0967cfccb', 'wrong original recorder')
    return identity


def calibration(archive: Archive) -> list:
    capture, _, events = capture_from_archive(archive, 'process-calibration')
    records = decode_records(events)
    processes, _ = lifetime.process_table(events)
    wrapper = lifetime.locate(processes, dict(pid=capture['child']['pid'], created=capture['child']['created_filetime']))
    data = archive.load('study/process-calibration/result.json')
    need(len(data['calls']) == len(data['resources']) == 3, 'calibration count mismatch')
    answer = []
    for index, call in enumerate(data['calls']):
        need(call['exit_code'] == call['expected'] == (0, 17, 0)[index], 'calibration expected exit mismatch')
        resource = [r for r in data['resources'] if r['pid'] == call['pid']]
        need(len(resource) == 1, 'calibration query identity ambiguity')
        p = lifetime.locate(processes, dict(pid=call['pid'], created=resource[0]['creation_100ns']))
        need(lifetime.parent_of(processes, p) is wrapper and p['end'] <= wrapper['end'], 'calibration parent mismatch')
        row = supplement(p, records, processes, capture['qpc_frequency'])
        row['query_comparison'] = compare_query(row, resource[0])
        for stream in ('stdout', 'stderr'):
            raw = archive.read(f'study/process-calibration/{index}.{stream}')
            marker = ('OUT' if stream == 'stdout' else 'ERR')
            need(raw == f'CAL_{marker}_{index}\r\n'.encode(), 'calibration diagnostic mismatch')
        answer.append(row)
    return answer


def verify_plan(plan: dict, identity: dict) -> None:
    # plan.json uses an integer attempt; trace.identity.json retains the original
    # environment string. Validate each format, not a permissive coercion.
    need(plan['head'] == '49770368ce1c7be1a757df392a2251b9e94c9cf5' and
         plan['base'] == '752668650dfbf8e32a3a5d66d9d0808e10daf2ec' and
         plan['run'] == '34765281714' and type(plan['attempt']) is int and plan['attempt'] == 1,
         'original source/run mismatch')
    need(plan['binary_sha256'] == BINARIES and plan['trace_build'] == identity and
         identity['attempt'] == '1', 'original plan binary/recorder identity mismatch')


def audit_steps(archive_path: Path, input_path: Path):
    """Yield progress between captures; final report follows all immutable-input checks."""
    archive, inputs = Archive(archive_path, ARCHIVE_SHA), Archive(input_path, INPUT_SHA)
    try:
        identity = verify_inputs(inputs)
        plan, result = archive.load('study/plan.json'), archive.load('study/result.json')
        verify_plan(plan, identity)
        need(result['status'] == 'completed' and all(result[k] is False for k in
             ('historical_cause_resolved', 'historical_risks_cleared', 'release_authorized')), 'original state mismatch')
        need(len(result['rows']) == 32 and len(result['failure_controls']) == 2 and
             result['mqb_calls'] == 78 and result['observed_mqb_resources'] == 18, 'original accounting mismatch')
        need([{k: r[k] for k in ('block', 'case', 'position', 'side', 'observe', 'label')}
              for r in result['rows']] == plan['schedule'], 'original schedule mismatch')
        tools = {Path(p).stem: p for p in result['tools']}
        need(set(tools) == {'cl', 'link'}, 'wrong retained tools')
        outputs = []
        for original in result['rows'] + result['failure_controls']:
            label = original['label']
            worker = archive.load(f'study/{label}-worker/worker.json')
            need(worker == original['worker'] and worker['call'] == original['call'], 'original worker/call mismatch')
            for stream in ('stdout', 'stderr'):
                raw = archive.read(f"study/{label}-worker/mqb/{worker['call'][stream + '_file']}")
                need(sha(raw) == worker['call'][stream + '_sha256'], 'original diagnostic hash mismatch')
            failure = label.startswith('failure-')
            row = dict(label=label, side=original['side'], case=original['case'],
                       block=original.get('block'), scored=not failure, observe=original['observe'],
                       original_call=original['call'], original_trace=original['trace'], supplement=None)
            if not original['observe']:
                need(not worker['resources'] and original['trace'] is None, 'OFF unexpectedly observed')
                outputs.append(row)
                continue
            need(len(worker['resources']) == 1, 'missing MQB handle query')
            capture, _, events = capture_from_archive(archive, label)
            records = decode_records(events)
            resource = worker['resources'][0]
            checked = lifetime.audit_lifetimes(capture, events, resource, tools, plan['device_map'],
                                              units=None if failure else (129 if original['case'] == 'common' else 2))
            need(checked == original['trace'], 'original lifetime audit no longer reproduces')
            processes, _ = lifetime.process_table(events)
            extended = [dict(supplement(p, records, processes, capture['qpc_frequency']), kind=p['kind'])
                        for p in checked['tools']]
            root = supplement(checked['root'], records, processes, capture['qpc_frequency'])
            wrapper = supplement(checked['wrapper'], records, processes, capture['qpc_frequency'], check_parent=False)
            need(wrapper['stop_fields']['ExitCode'] == capture['child']['exit_code'] == 0, 'wrapper exit mismatch')
            need(root['stop_fields']['ExitCode'] == original['call']['exit_code'] == (4 if failure else 0), 'MQB exit mismatch')
            # This checks both original fault controls rather than masking them as
            # successful tools. cl exit 2 is separate from MQB's mapped exit 4.
            need([p['stop_fields']['ExitCode'] for p in extended] ==
                 ([2] if failure else [0] * len(extended)), 'unexpected original tool exit')
            need(not failure or Counter(p['kind'] for p in extended) == {'cl': 1}, 'fault control tool count mismatch')
            row['supplement'] = dict(root=root, wrapper=wrapper, tools=extended,
                                      root_query_comparison=compare_query(root, resource))
            outputs.append(row)
            print('Audited retained capture:', label, flush=True)
            del events, records, processes, checked
            yield dict(label=label, recovered_tools=len(extended))
        controls = calibration(archive)
        counts = Counter(p['kind'] for r in outputs if r['supplement'] for p in r['supplement']['tools'])
        need(counts == {'cl': 1050, 'link': 16}, 'complete original tool count mismatch')
        need(sum(r['supplement'] is not None for r in outputs) == 18, 'capture budget mismatch')
        answer = dict(status='completed', schema=1, original_run=34765281714, original_attempt=1,
                    original_archive_sha256=ARCHIVE_SHA, original_input_sha256=INPUT_SHA,
                    archive_member_sha256=archive.members, input_member_sha256=inputs.members,
                    original_summaries=result['summaries'], rows=outputs, calibration=controls,
                    recovered_tool_counts=dict(counts), new_mqb_calls=0, new_trace_captures=0,
                    cpu_time_available=False, wait_time_available=False,
                    historical_cause_resolved=False, historical_risks_cleared=False, release_authorized=False)
    finally:
        archive.finish()
        inputs.finish()
    yield dict(label='completed', report=answer)


def audit(archive_path: Path, input_path: Path) -> dict:
    answer = None
    for progress in audit_steps(archive_path, input_path):
        if 'report' in progress:
            answer = progress['report']
    need(answer is not None, 'audit did not complete')
    return answer


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New directory; existing output is refused')
    args = parser.parse_args()
    try:
        args.output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        parser.error('output already exists; retained evidence must not be overwritten')
    try:
        answer = audit(args.archive, args.input)
    except Exception as error:
        (args.output / 'failure.json').write_text(json.dumps(dict(status='failed', error=repr(error),
            release_authorized=False, historical_risks_cleared=False), indent=2), encoding='utf-8')
        raise
    (args.output / 'result.json').write_text(json.dumps(answer, indent=2, ensure_ascii=False), encoding='utf-8')
    print('Read-only supplement completed; no new MQB calls or traces; release HOLD.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
