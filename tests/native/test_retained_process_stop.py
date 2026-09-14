"""Portable adversarial tests for the fixed, read-only #190 supplement."""
from __future__ import annotations

import ast
import copy
import io
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

import audit_retained_process_stop as audit


def event(event_id, version, sequence, qpc, payload, data, layout):
    return dict(provider='process', id=event_id, version=version, sequence=sequence,
                flags=576, pid=999999, tid=888888, qpc=qpc, raw_payload=payload.hex(),
                decode_error=None, data=data,
                property_schema=[dict(name=n, flags=0, in_type=t) for n, t in layout])


def start(pid=30, psn=300, created=103, parent=20, parent_psn=200,
          sequence=3, qpc=30, image='C:\\tools\\cl.exe'):
    # Independent struct layout rather than calling the production encoder.
    prefix = struct.pack('<IQQIQIIII', pid, psn, created, parent, parent_psn, 0, 0, 1, 1)
    sid = bytes.fromhex('010100000000001000400000')
    strings = image.encode('utf-16-le') + b'\0\0' + struct.pack('<II', 123, 456)
    strings += 'package\0relative\0'.encode('utf-16-le') + struct.pack('<I', 0)
    return event(1, 4, sequence, qpc, prefix + sid + strings,
                 dict(ProcessID=pid, CreateTime=created, ParentProcessID=parent, ImageName=image), audit.START)


def stop(pid=30, psn=300, created=103, exit_time=203, exit_code=2,
         sequence=4, qpc=40, name=b'cl.exe', cycles=2**63 + 123):
    payload = struct.pack('<IQQQIIIQQQIIIII', pid, psn, created, exit_time, exit_code,
                          1, 60, 4096, 8192, cycles, 1, 58, 0, 1, 28) + name + b'\0'
    return event(2, 2, sequence, qpc, payload,
                 dict(ProcessID=pid, CreateTime=created,
                      ImageName=dict(encoding='opaque-ansi', bytes=name.hex())), audit.STOP)


def tree():
    return [start(10, 100, 101, 999, 9990, 1, 10, 'C:\\python.exe'),
            start(20, 200, 102, 10, 100, 2, 20, 'C:\\baseline.exe'), start(), stop(),
            stop(20, 200, 102, 204, 4, 5, 50, b'baseline.exe'),
            stop(10, 100, 101, 205, 0, 6, 60, b'python.exe')]


def selected(events=None):
    events = tree() if events is None else events
    processes, _ = audit.lifetime.process_table(events)
    process = audit.lifetime.locate(processes, dict(pid=30, created=103))
    return process, audit.decode_records(events), processes


class RetainedStopTests(unittest.TestCase):
    def test_exact_original_plan_formats(self):
        identity = dict(attempt='1')
        plan = dict(head='49770368ce1c7be1a757df392a2251b9e94c9cf5',
                    base='752668650dfbf8e32a3a5d66d9d0808e10daf2ec', run='34765281714',
                    attempt=1, binary_sha256=audit.BINARIES, trace_build=identity)
        audit.verify_plan(plan, identity)
        for value in ('1', 2, True, 1.0, None):
            mutated = dict(plan, attempt=value)
            with self.subTest(value=value), self.assertRaises(audit.EvidenceError):
                audit.verify_plan(mutated, identity)
        for key in ('head', 'base', 'run'):
            with self.subTest(key=key), self.assertRaises(audit.EvidenceError):
                audit.verify_plan(dict(plan, **{key: 'wrong'}), identity)

    def test_exact_unsigned_fields(self):
        parsed = audit.decode_records([stop()])[4]['data']
        self.assertEqual(parsed['CPUCycleCount'], 2**63 + 123)
        self.assertEqual(parsed['ExitCode'], 2)
        self.assertEqual(parsed['ReadOperationCount'], 1)
        self.assertEqual(parsed['WriteOperationCount'], 58)
        self.assertEqual(parsed['HardFaultCount'], 28)
        self.assertIs(type(parsed['CPUCycleCount']), int)

    def test_unicode_start_with_sid_and_strings(self):
        image = 'C:\\目录😀\\cl.exe'
        result = audit.decode_records([start(image=image)])[3]['data']
        self.assertEqual(result['ImageName'], image)
        self.assertEqual(result['MandatoryLabel'], '010100000000001000400000')
        self.assertEqual((result['PackageFullName'], result['PackageRelativeAppId']), ('package', 'relative'))
        self.assertEqual(result['ImageChecksum'], 123)

    def test_opaque_ansi_and_unsigned_exit(self):
        result = audit.decode_records([stop(name=b'\xff.exe', exit_code=0xffffffff)])[4]['data']
        self.assertEqual(result['ImageName'], {'encoding': 'opaque-ansi', 'bytes': 'ff2e657865'})
        self.assertEqual(result['ExitCode'], 0xffffffff)

    def test_schema_cache_is_capture_local(self):
        first, second = stop(), stop(pid=40, sequence=5)
        second['property_schema'] = None
        self.assertEqual(len(audit.decode_records([first, second])), 2)
        with self.assertRaisesRegex(audit.EvidenceError, 'schema absent'):
            audit.decode_records([second])

    def test_late_schema_is_not_backfilled(self):
        first, second = stop(), stop(pid=40, sequence=5)
        first['property_schema'] = None
        with self.assertRaisesRegex(audit.EvidenceError, 'schema absent'):
            audit.decode_records([first, second])

    def test_schema_mutations_refused(self):
        for change in ('order', 'width', 'flags', 'missing', 'extra', 'bool', 'float'):
            with self.subTest(change=change):
                e = stop()
                if change == 'order': e['property_schema'][0:2] = e['property_schema'][1::-1]
                if change == 'width': e['property_schema'][0]['in_type'] = 10
                if change == 'flags': e['property_schema'][0]['flags'] = 1
                if change == 'missing': e['property_schema'].pop()
                if change == 'extra': e['property_schema'].append(dict(name='extra', flags=0, in_type=8))
                if change == 'bool': e['property_schema'][0]['flags'] = False
                if change == 'float': e['property_schema'][0]['in_type'] = 8.0
                with self.assertRaises(audit.EvidenceError): audit.decode_records([e])

    def test_conflicting_later_schema_refused(self):
        first, second = stop(), stop(pid=40, sequence=5)
        second['property_schema'][0]['name'] = 'OtherID'
        with self.assertRaises(audit.EvidenceError): audit.decode_records([first, second])

    def test_unsupported_version_architecture_and_original_error(self):
        for key, value in [('version', 1), ('version', True), ('flags', 32), ('flags', 96), ('decode_error', 'TDH failed')]:
            e = stop(); e[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(audit.EvidenceError):
                audit.decode_records([e])

    def test_all_truncated_prefixes_and_suffix_refused(self):
        for e in (start(), stop()):
            payload = bytes.fromhex(e['raw_payload'])
            for length in range(len(payload)):
                with self.subTest(id=e['id'], length=length), self.assertRaises(audit.EvidenceError):
                    audit.decode_payload(payload[:length].hex(), audit.LAYOUTS[(e['id'], e['version'])])
            with self.assertRaisesRegex(audit.EvidenceError, 'suffix'):
                audit.decode_payload(e['raw_payload'] + '00', audit.LAYOUTS[(e['id'], e['version'])])

    def test_invalid_hex_and_utf16_and_sid(self):
        for raw in ('0', '0x00', '00 ff', 'gg'):
            with self.assertRaises(audit.EvidenceError): audit.decode_payload(raw, audit.STOP)
        payload = bytearray.fromhex(start()['raw_payload'])
        payload[48] = 2
        with self.assertRaisesRegex(audit.EvidenceError, 'SID'): audit.decode_payload(payload.hex(), audit.START)
        payload = bytearray.fromhex(start()['raw_payload']); payload[60:62] = b'\x00\xd8'
        with self.assertRaisesRegex(audit.EvidenceError, 'UTF-16'): audit.decode_payload(payload.hex(), audit.START)

    def test_raw_identity_must_match_original_tdh(self):
        for key, value in [('ProcessID', 99), ('CreateTime', 999), ('ProcessID', True), ('UnknownField', 0)]:
            e = stop(); e['data'][key] = value
            with self.subTest(key=key), self.assertRaises(audit.EvidenceError): audit.decode_records([e])

    def test_header_pid_is_not_payload_pid(self):
        p, records, processes = selected()
        answer = audit.supplement(p, records, processes, 1000)
        self.assertEqual(answer['pid'], 30)
        self.assertEqual(answer['parent_start_sequence'], 2)
        self.assertEqual(answer['stop_fields']['ExitCode'], 2)
        self.assertEqual(answer['lifetime_ms'], 10)
        self.assertTrue(all(answer[k] is None for k in ('cpu_ms', 'wait_ms', 'wait_cause')))

    def test_parent_and_process_sequence_mismatches_refused(self):
        for sequence, field in [(4, 'ProcessSequenceNumber'), (3, 'ParentProcessSequenceNumber')]:
            p, records, processes = selected(); records[sequence]['data'][field] += 1
            with self.subTest(field=field), self.assertRaises(audit.EvidenceError):
                audit.supplement(p, records, processes, 1000)

    def test_missing_stop_and_changed_qpc_refused(self):
        p, records, processes = selected(); del records[4]
        with self.assertRaises(audit.EvidenceError): audit.supplement(p, records, processes, 1000)
        p, records, processes = selected(); records[4]['qpc'] += 1
        with self.assertRaises(audit.EvidenceError): audit.supplement(p, records, processes, 1000)

    def test_duplicate_or_overlapping_generations_refused(self):
        p, records, processes = selected()
        duplicate = dict(p, created=104, start=35, end=45, start_sequence=7, stop_sequence=8)
        with self.assertRaisesRegex(audit.EvidenceError, 'overlapping'):
            audit.supplement(p, records, processes + [duplicate], 1000)
        with self.assertRaises(audit.EvidenceError): audit.decode_records([stop(), stop()])

    def test_pid_reuse_after_lifetime_does_not_change_identity(self):
        p, records, processes = selected()
        later = dict(p, created=104, start=100, end=110, start_sequence=7, stop_sequence=8)
        self.assertEqual(audit.supplement(p, records, processes + [later], 1000)['created'], 103)

    def test_wrong_image_and_exit_before_creation_refused(self):
        for field, value in [('ImageName', dict(encoding='opaque-ansi', bytes=b'other.exe'.hex())), ('ExitTime', 1)]:
            p, records, processes = selected(); records[4]['data'][field] = value
            with self.subTest(field=field), self.assertRaises(audit.EvidenceError):
                audit.supplement(p, records, processes, 1000)

    def test_queries_preserve_counter_disagreement(self):
        p, records, processes = selected(); row = audit.supplement(p, records, processes, 1000)
        query = dict(pid=30, creation_100ns=103, exit_100ns=203, exit_code=2, query_count=3,
                     errors=[], cycles=2**63 + 999, io=dict(read_ops=3, write_ops=60, read_bytes=1, write_bytes=1500))
        result = audit.compare_query(row, query)
        self.assertEqual(result['query_minus_event_cycles'], 876)
        self.assertEqual(result['query_minus_event_read_ops'], 2)
        self.assertEqual(result['event_read_transfer_kilobytes'], 0)
        self.assertEqual(result['query_read_bytes'], 1)  # zero KB is not zero bytes
        for field in ('pid', 'creation_100ns', 'exit_100ns', 'exit_code'):
            mutated = copy.deepcopy(query); mutated[field] += 1
            with self.subTest(field=field), self.assertRaises(audit.EvidenceError): audit.compare_query(row, mutated)

    def test_archive_read_is_immutable_and_hash_guarded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input.zip'
            with zipfile.ZipFile(path, 'w') as z: z.writestr('value.json', '{"n":17}')
            before = path.read_bytes(); expected = audit.sha(before)
            archive = audit.Archive(path, expected)
            self.assertEqual(archive.load('value.json'), {'n': 17})
            archive.finish(); self.assertEqual(path.read_bytes(), before)
            with self.assertRaisesRegex(audit.EvidenceError, 'SHA256'): audit.Archive(path, '0' * 64)
            archive = audit.Archive(path, expected)
            # Close before deliberately mutating the fixture, for Windows portability.
            archive.zip.close(); path.write_bytes(before + b'external mutation')
            with self.assertRaisesRegex(audit.EvidenceError, 'changed'): archive.finish()

    def test_existing_output_refused_before_reading_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            sentinel = Path(directory) / 'sentinel'; sentinel.write_text('keep')
            argv = ['audit', '--archive', 'missing', '--input', 'missing', '--output', directory]
            with patch.object(sys, 'argv', argv), patch.object(audit, 'audit', side_effect=AssertionError('must not read')):
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as exit_info: audit.main()
            self.assertEqual(exit_info.exception.code, 2)
            self.assertEqual(sentinel.read_text(), 'keep')
            self.assertEqual([p.name for p in Path(directory).iterdir()], ['sentinel'])

    def test_failure_evidence_is_written(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'out'
            argv = ['audit', '--archive', 'missing', '--input', 'missing', '--output', str(output)]
            with patch.object(sys, 'argv', argv), patch.object(audit, 'audit', side_effect=audit.EvidenceError('original failure')):
                with self.assertRaises(audit.EvidenceError): audit.main()
            result = json.loads((output / 'failure.json').read_text())
            self.assertIn('original failure', result['error'])
            self.assertFalse(result['release_authorized'])
            self.assertFalse((output / 'result.json').exists())

    def test_replay_has_no_process_or_windows_api_import(self):
        for module in (audit, audit.lifetime, audit.lifetime.trace):
            syntax = ast.parse(Path(module.__file__).read_text())
            modules = {alias.name.split('.')[0] for n in ast.walk(syntax) if isinstance(n, ast.Import) for alias in n.names}
            modules |= {n.module.split('.')[0] for n in ast.walk(syntax) if isinstance(n, ast.ImportFrom) and n.module}
            self.assertFalse(modules & {'subprocess', 'ctypes', 'multiprocessing', 'winreg', 'socket'})


if __name__ == '__main__':
    unittest.main(verbosity=2)
