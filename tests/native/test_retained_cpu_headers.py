"""Portable rejection tests; no Windows execution is implied by these fixtures."""
import copy
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from unittest.mock import patch

import audit_retained_cpu_headers as a


def event(kind=3, seq=10, stamp=100, pid=30, tid=40):
    layout = a.THREAD_START if kind == 3 else a.THREAD_STOP
    payload = struct.pack('<II7QI' + ('Q' if kind == 4 else ''), pid, tid,
                          *range(1, 8), 0, *([2**63+1] if kind == 4 else []))
    return dict(provider='process', id=kind, version=1, sequence=seq, qpc=stamp,
                pid=pid if kind == 4 else 999, tid=tid if kind == 4 else 888,
                flags=576, raw_payload=payload.hex(), decode_error=None,
                data=dict(ProcessID=pid, ThreadID=tid),
                property_schema=[dict(name=n, flags=0, in_type=t) for n, t in layout])


def fixture(flags=576):
    events = [event(), event(4, 11, 200)]
    for e in events: e['flags'] = flags
    process = dict(pid=30, created=1000, image='cl.exe', start=90, end=210,
                   start_sequence=1, stop_sequence=12)
    headers = {e['sequence']: dict(e, kernel_units=1, user_units=2, processor_time_raw=(2<<32)|1) for e in events}
    return process, [process], a.decode_threads(events), headers


def query(kernel=200000, user=100000):
    return dict(pid=30, creation_100ns=1000, errors=[], query_count=3,
                kernel_100ns=kernel, user_100ns=user)


class Tests(unittest.TestCase):
    def test_exact_widths_and_unsigned_cycles(self):
        result = a.decode_threads([event(), event(4, 11, 200)])
        self.assertEqual(result[11]['fields']['CycleTime'], 2**63+1)
        self.assertEqual(result[10]['fields']['TebBase'], 7)
        self.assertEqual((len(event()['raw_payload']), len(event(4)['raw_payload'])), (136, 152))

    def test_capture_local_schema_and_no_late_backfill(self):
        first, second = event(), event(seq=12, tid=41)
        second['property_schema'] = None
        self.assertEqual(len(a.decode_threads([first, second])), 2)
        with self.assertRaises(a.Error): a.decode_threads([second])
        first['property_schema'] = None
        with self.assertRaises(a.Error): a.decode_threads([first, event(seq=12)])

    def test_schema_versions_architecture_and_identity_rejected(self):
        variants = []
        for k,v in [('version', True), ('version', 2), ('flags', 32), ('flags', True), ('decode_error', 'bad')]:
            e=event();e[k]=v;variants.append(e)
        for change in ('order','type','struct','bool','missing','extra','data'):
            e=event()
            if change=='order': e['property_schema'].reverse()
            if change=='type': e['property_schema'][2]['in_type']=10
            if change=='struct': e['property_schema'][2]['flags']=1
            if change=='bool': e['property_schema'][2]['flags']=False
            if change=='missing': e['property_schema'].pop()
            if change=='extra': e['property_schema'].append(dict(name='extra', flags=0, in_type=8))
            if change=='data': e['data']['ProcessID']=31
            variants.append(e)
        for e in variants:
            with self.subTest(e=e), self.assertRaises(a.Error): a.decode_threads([e])

    def test_payload_lengths_bad_hex_and_duplicate_sequence(self):
        for kind in (3,4):
            full=event(kind)
            for length in range(len(full['raw_payload'])):
                e=dict(full,raw_payload=full['raw_payload'][:length])
                with self.assertRaises(a.Error): a.decode_threads([e])
            for tail in ('00',' ', 'gg'):
                with self.assertRaises(a.Error): a.decode_threads([dict(full,raw_payload=full['raw_payload']+tail)])
        with self.assertRaises(a.Error): a.decode_threads([event(),event()])

    def test_native_identity_union_and_health(self):
        e=event(4);h=dict(e,kernel_units=1,user_units=2,processor_time_raw=(2<<32)|1)
        meta=dict(open_status=0,process_trace_status=0,close_trace_status=0,errors=0,log_header_events_lost=0,
                  total_events=99,selected_events=1,etl_bytes=128,perf_frequency=1000,pointer_size=8,timer_resolution_100ns=156250)
        decode=dict(total_events=99,etl_bytes=128)
        self.assertIn(10,a.match_headers([e],[h],meta,decode,128,1000))
        for key in a.IDENTITY:
            bad=dict(h);bad[key]=None
            with self.subTest(key=key), self.assertRaises(a.Error): a.match_headers([e],[bad],meta,decode,128,1000)
        for key in meta:
            bad=dict(meta);bad[key]+=1
            if key=='timer_resolution_100ns':bad[key]=0
            with self.subTest(key=key), self.assertRaises(a.Error): a.match_headers([e],[h],bad,decode,128,1000)
        for key,value in [('kernel_units',True),('user_units',-1),('processor_time_raw',0)]:
            bad=dict(h);bad[key]=value
            with self.assertRaises(a.Error): a.match_headers([e],[bad],meta,decode,128,1000)

    def test_start_emitter_not_used_and_no_wait_claim(self):
        row=a.process_threads(*fixture(),156250)
        self.assertEqual(row['header_kernel_snapshot_sum_100ns'],156250)
        self.assertEqual(row['header_user_snapshot_sum_100ns'],312500)
        self.assertEqual(row['thread_count'],1)
        self.assertTrue(all(row[k] is None for k in ('full_process_cpu_100ns','wait_100ns','wait_cause')))

    def test_private_or_no_cpu_fields_are_not_scaled(self):
        for flag in (2,8,16,256):
            row=a.process_threads(*fixture(576|flag),156250)
            self.assertIsNone(row['header_kernel_snapshot_sum_100ns'])
            self.assertIsNotNone(row['threads'][0]['cpu_header'])
        p,ps,t,h=fixture();row=a.process_threads(p,ps,t,None,None)
        self.assertIsNone(row['threads'][0]['cpu_header'])

    def test_foreign_stop_emitter_is_rejected(self):
        for key in ('pid','tid'):
            p,ps,t,h=fixture();t[11][key]+=1
            with self.assertRaises(a.Error): a.process_threads(p,ps,t,h,156250)

    def test_missing_overlapping_and_same_timestamp_threads(self):
        for mutation in ('stop','start','overlap','tie'):
            p,ps,t,h=fixture()
            if mutation=='stop':del t[11]
            if mutation=='start':del t[10]
            if mutation=='overlap':t[9]=dict(t[10],sequence=9,qpc=110)
            if mutation=='tie':t[11]['qpc']=100
            with self.subTest(mutation=mutation), self.assertRaises(a.Error): a.process_threads(p,ps,t,h,156250)

    def test_tid_reuse_after_stop_and_pid_reuse_outside_window(self):
        p,ps,t,h=fixture();p['end']=410
        for e in (event(seq=20,stamp=300),event(4,21,400)):
            t.update(a.decode_threads([e]));h[e['sequence']]=dict(e,kernel_units=1,user_units=2)
        self.assertEqual(a.process_threads(p,ps,t,h,156250)['thread_count'],2)
        p['end']=210
        ps.append(dict(p,created=2000,start=290,end=410,start_sequence=19,stop_sequence=22))
        self.assertEqual(a.process_threads(p,ps,t,h,156250)['thread_count'],1)
        ps[-1]['start']=205
        with self.assertRaises(a.Error):a.process_threads(p,ps,t,h,156250)

    def test_query_disagreement_and_all_zero_not_called_zero_cpu(self):
        row=a.process_threads(*fixture(),156250)
        diff=a.query_comparison(row,query())
        self.assertEqual(diff['query_minus_header_kernel_100ns'],43750)
        self.assertEqual(diff['query_minus_header_user_100ns'],-212500)
        self.assertFalse(diff['exact_snapshot_match'])
        row['header_kernel_snapshot_sum_100ns']=row['header_user_snapshot_sum_100ns']=0
        self.assertTrue(a.query_comparison(row,query())['nonzero_query_with_all_zero_headers'])
        with self.assertRaises(a.Error):a.query_comparison(row,dict(query(),creation_100ns=999))

    def test_existing_output_refused(self):
        with tempfile.TemporaryDirectory() as d:
            sentinel=Path(d)/'sentinel';sentinel.write_text('keep')
            argv=['audit','--bundle','missing','--output',d,'--inventory-only']
            with patch.object(sys,'argv',argv),patch.object(a,'audit',side_effect=AssertionError('must not read')):
                with redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):a.main()
            self.assertEqual(sentinel.read_text(),'keep')

    def test_reader_timeout_diagnostics_and_existing_output(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); etl=root/'saved.etl'; etl.write_bytes(b'unchanged')
            failure=subprocess.TimeoutExpired(['reader'],120,output=b'partial-out',stderr=b'partial-err')
            with patch.object(a.subprocess,'run',side_effect=failure),self.assertRaises(a.Error):
                a.run_reader(root/'reader.exe',etl,root/'read')
            self.assertEqual((root/'read.stdout').read_bytes(),b'partial-out')
            self.assertEqual((root/'read.stderr').read_bytes(),b'partial-err')
            self.assertEqual(etl.read_bytes(),b'unchanged')
            (root/'read').mkdir()
            with patch.object(a.subprocess,'run',side_effect=AssertionError('must not start')),self.assertRaises(a.Error):
                a.run_reader(root/'reader.exe',etl,root/'read')

    def test_native_source_does_not_capture_or_launch(self):
        source=(Path(__file__).parents[2]/'cpp/tests/platform/windows/retained_cpu_header_probe.cpp').read_text()
        for call in ('::StartTrace','::EnableTrace','::CreateProcess','::OpenProcess','::AdjustTokenPrivileges','::ControlTrace'):
            self.assertNotIn(call,source)
        self.assertIn('FILE_SHARE_READ',source)


if __name__=='__main__':
    unittest.main(verbosity=2)
