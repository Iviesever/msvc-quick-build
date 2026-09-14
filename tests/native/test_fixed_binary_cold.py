"""Portable contracts only. Real ETW/MSVC evidence comes from the first Windows run."""
from collections import Counter
from copy import deepcopy
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import diagnose_fixed_binary_cold as d
import audit_cold_process_trace as a


def example():
    events = []
    def add(pid, created, parent, image, begin, end):
        events.append(dict(provider='process', id=1, qpc=begin, sequence=len(events)+1, pid=999,
                           data=dict(ProcessID=pid, CreateTime=created, ParentProcessID=parent, ImageName=image)))
        if end is not None:
            events.append(dict(provider='process', id=2, qpc=end, sequence=len(events)+1, pid=999,
                               data=dict(ProcessID=pid, CreateTime=created)))
    add(10, 100, 1, 'C:/python.exe', 10, 100)
    add(20, 200, 10, 'C:/baseline.exe', 20, 90)
    add(30, 300, 20, 'C:/tools/cl.exe', 25, 45)
    add(31, 301, 20, 'C:/tools/cl.exe', 35, 55)
    add(40, 400, 20, 'C:/tools/link.exe', 60, 80)
    capture = dict(child=dict(pid=10, created_filetime=100, before_launch_qpc=1, after_wait_qpc=110),
                   qpc_frequency=1000)
    return capture, events, dict(pid=20, creation_100ns=200), dict(cl='C:/tools/cl.exe', link='C:/tools/link.exe'), {}


class ColdTests(unittest.TestCase):
    def test_budget_and_each_position_and_predecessor(self):
        rows = d.schedule()
        self.assertEqual(len(rows), 32)
        self.assertEqual(len({r['label'] for r in rows}), 32)
        self.assertEqual(sum(r['observe'] for r in rows), 16)
        self.assertEqual(4+4+32+32+2+2+2, d.BUDGET['mqb_calls'])
        self.assertEqual(16+2+2, d.BUDGET['trace_entries'])
        for case in d.CASES:
            r = [x for x in rows if x['case'] == case]
            self.assertEqual(set(Counter((x['side'], x['observe'], x['position']) for x in r).values()), {1})
            pred = Counter()
            for b in range(4):
                cells = [(x['side'], x['observe']) for x in r if x['block'] == b]
                pred.update(zip(cells, cells[1:]))
            self.assertEqual(len(pred), 12)
            self.assertEqual(set(pred.values()), {1})

    def test_union_not_parallel_sum(self):
        self.assertEqual(a.union([(0, 5), (3, 9), (12, 15)]), [[0, 9], [12, 15]])
        self.assertEqual(a.span([(0, 5), (3, 9), (12, 15)]), 12)
        with self.assertRaises(a.trace.EvidenceError):
            a.union([(3, 2)])

    def test_complete_timeline_uses_payload_pid(self):
        r = a.audit_lifetimes(*example(), units=2)
        self.assertEqual(r['tool_counts'], {'cl': 2, 'link': 1})
        self.assertEqual(r['cl_union_ms'], 30)
        self.assertEqual(r['tools_union_ms'], 50)
        self.assertEqual(r['root_lifetime_ms'], 70)
        self.assertEqual(r['uncovered_ms'], 20)
        self.assertTrue(all(p['exit_code'] is None for p in r['tools']))
        self.assertFalse(r['all_writer_coverage_proven'])

    def test_eight_identity_mutations_rejected(self):
        mutations = [
            lambda c,e,r,t,m: r.update(creation_100ns=201),
            lambda c,e,r,t,m: e.pop(3),  # root stop
            lambda c,e,r,t,m: e.pop(5),  # tool stop
            lambda c,e,r,t,m: e[2]['data'].update(ParentProcessID=1),
            lambda c,e,r,t,m: e[4]['data'].update(ImageName='C:/other/cl.exe'),
            lambda c,e,r,t,m: e[5].update(qpc=95),
            lambda c,e,r,t,m: e.append(deepcopy(e[2])),
            lambda c,e,r,t,m: e[4]['data'].update(ParentProcessID=999)]
        for i, mutate in enumerate(mutations):
            data = example()
            mutate(*data)
            with self.subTest(i=i), self.assertRaises(a.trace.EvidenceError):
                a.audit_lifetimes(*data, units=2)

    def test_reused_parent_pid_not_root_descendant(self):
        c, e, r, t, m = example()
        e.extend([dict(provider='process', id=1, sequence=11, qpc=92,
                       data=dict(ProcessID=20, CreateTime=202, ParentProcessID=10, ImageName='other.exe')),
                  dict(provider='process', id=1, sequence=12, qpc=93,
                       data=dict(ProcessID=50, CreateTime=500, ParentProcessID=20, ImageName='C:/tools/cl.exe'))])
        result = a.audit_lifetimes(c,e,r,t,m,units=2)
        self.assertEqual(result['tool_counts']['cl'], 2)
        self.assertNotIn(50, [p['pid'] for p in result['descendants']])

    def test_overlapping_root_generation_rejected(self):
        c,e,r,t,m = example()
        e.append(dict(provider='process', id=1, sequence=11, qpc=30,
                      data=dict(ProcessID=20, CreateTime=202, ParentProcessID=10, ImageName='other.exe')))
        with self.assertRaisesRegex(a.trace.EvidenceError, 'overlapping'):
            a.audit_lifetimes(c,e,r,t,m,units=2)

    def test_selected_tool_count_not_zero_on_missing_event(self):
        c,e,r,t,m = example()
        e = [x for x in e if x['data']['ProcessID'] != 31]
        with self.assertRaisesRegex(a.trace.EvidenceError, 'count'):
            a.audit_lifetimes(c,e,r,t,m,units=2)

    def test_source_volume_mapping_and_unicode(self):
        mapping = {'\\Device\\HarddiskVolume3': 'C:'}
        self.assertEqual(a.canonical('\\Device\\HarddiskVolume3/Tools/测试/cl.exe', mapping),
                         'c:\\tools\\测试\\cl.exe')
        with self.assertRaises(a.trace.EvidenceError):
            a.canonical({'encoding':'opaque-ansi'}, mapping)

    def test_existing_health_readiness_negative_contracts(self):
        result = a.trace.self_test()
        self.assertIsInstance(result, dict)
        # self_test itself rejects any unmet positive/negative expectation.

    def test_raw_summaries_keep_unfavourable_values(self):
        rows = deepcopy(d.schedule())
        for row in rows:
            row['call'] = dict(external_ms=10+int(row['observe'])+2*(row['side']=='candidate'))
            row['trace'] = dict.fromkeys(('root_lifetime_ms','cl_union_ms','link_union_ms',
                'tools_union_ms','uncovered_ms','max_cl_lifetime_ms'), 4)
        for item in d.summaries(rows):
            self.assertEqual(item['off']['paired_deltas_ms'], [2]*4)
            self.assertEqual(item['candidate_observer_deltas_ms'], [1]*4)

    def test_off_worker_uses_unmodified_launch_no_queries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            spec = dict(worker=str(root/'worker'), binary=sys.executable, side='baseline',
                case='small', case_root=str(root), units=2, arguments=['-c','print("ORIGINAL")'],
                cold=False, observe=False, label='original', expected_exit=0)
            d.dump(root/'spec.json', spec)
            digest = d.h.digest(Path(sys.executable).read_bytes())
            with patch.dict(d.warm.BINARY_HASHES, {'baseline':digest}), patch.object(
                    d.warm, 'WindowsResources', side_effect=AssertionError('OFF query')):
                d.worker(root/'spec.json')
            result=d.load(root/'worker/worker.json')
            self.assertEqual(result['resources'], [])
            self.assertEqual(result['status'], 'completed')

    def test_unexpected_worker_exit_raw_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            spec=dict(worker=str(root/'worker'), binary=sys.executable, side='baseline', case='small',
                case_root=str(root), units=2, arguments=['-c','import sys;print("OUT");print("ERR",file=sys.stderr);sys.exit(17)'],
                cold=False, observe=False, label='failed', expected_exit=0)
            d.dump(root/'spec.json', spec)
            with patch.dict(d.warm.BINARY_HASHES, {'baseline':d.h.digest(Path(sys.executable).read_bytes())}):
                with self.assertRaisesRegex(RuntimeError, 'original MQB exit'):
                    d.worker(root/'spec.json')
            r=d.load(root/'worker/worker.json')
            self.assertEqual(r['call']['exit_code'],17)
            self.assertEqual(r['status'],'failed')
            for name in ('stdout','stderr'):
                self.assertTrue((root/'worker/mqb'/r['call'][name+'_file']).read_bytes())

    def test_no_overwrite_or_second_attempt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with self.assertRaisesRegex(RuntimeError,'existing'):
                d.run(root,root,root,root)
            with patch.dict(d.os.environ, {'GITHUB_RUN_ATTEMPT':'2'}):
                with self.assertRaisesRegex(RuntimeError,'first attempt'):
                    d.run(root,root,root,root/'study')

    def test_original_cold_report_has_four_fields(self):
        fake=type('Raw',(),{'human':lambda *args: b'[compile] main.cpp\n[compile] unit.cpp\n[link] report.exe\n'})()
        self.assertEqual(d.cumulative.counts(fake,{}), dict(compile=2,link=1,archive=0,pch=0))


if __name__ == '__main__':
    unittest.main(verbosity=2)
