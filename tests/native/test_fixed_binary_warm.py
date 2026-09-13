"""Portable tests of the diagnostic harness, not Windows/MSVC product evidence."""
from collections import Counter
from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import diagnose_fixed_binary_warm as d


class FixedWarmTests(unittest.TestCase):
    def test_exact_budget_position_and_predecessor_balance(self):
        rows = d.schedule()
        self.assertEqual(len(rows), 288)
        self.assertEqual(len({r['label'] for r in rows}), 288)
        self.assertEqual(sum(r['observe'] for r in rows), 144)
        for name, jobs in d.GROUPS:
            group = [r for r in rows if (r['case'], r['jobs']) == (name, jobs)]
            self.assertEqual(Counter((r['side'], r['observe']) for r in group), Counter(dict.fromkeys(d.CELLS, 12)))
            positions = Counter((r['position'], r['side'], r['observe']) for r in group)
            self.assertEqual(set(positions.values()), {3})
            predecessors = Counter()
            for b in range(12):
                cells = [(r['side'], r['observe']) for r in group if r['block'] == b]
                predecessors.update(zip(cells, cells[1:]))
            self.assertEqual(len(predecessors), 12)
            self.assertEqual(set(predecessors.values()), {3})

    def test_observer_off_uses_original_without_query(self):
        original, rows = subprocess.Popen, []
        with d.observation(False, lambda p: self.fail('OFF query'), rows):
            self.assertIs(subprocess.Popen, original)
            r = subprocess.run([sys.executable, '-c', 'print("OFF")'], capture_output=True, check=True)
        self.assertEqual(r.stdout.strip(), b'OFF')
        self.assertEqual(rows, [])
        self.assertIs(subprocess.Popen, original)

    def test_original_recorder_exit17_and_both_raw_streams_survive(self):
        original, rows = subprocess.Popen, []
        def query(p):
            self.assertEqual(p.returncode, 17)
            self.assertTrue(p.stdout.closed and p.stderr.closed)
            return {'pid': p.pid, 'exit_code': p.returncode}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            recorder = d.h.Recorder(root / 'records')
            case = d.h.Fixture('control', root, ['-c', 'import sys;print("OUT");print("ERR",file=sys.stderr);sys.exit(17)'], 1)
            with self.assertRaisesRegex(RuntimeError, 'exit 17'):
                with d.observation(True, query, rows):
                    recorder.run(Path(sys.executable), case, 'failure')
            self.assertEqual(recorder.calls[0]['exit_code'], 17)
            self.assertIn(b'OUT', recorder.human(recorder.calls[0], 'stdout'))
            self.assertIn(b'ERR', recorder.human(recorder.calls[0], 'stderr'))
            self.assertTrue((root / 'records/calls.json').is_file())
        self.assertEqual(len(rows), 1)
        self.assertIs(subprocess.Popen, original)

    def test_query_exception_does_not_mask_child_error(self):
        original, rows = subprocess.Popen, []
        def broken(p):
            raise ValueError('injected query failure')
        with d.observation(True, broken, rows):
            result = subprocess.run([sys.executable, '-c', 'raise SystemExit(17)'], capture_output=True)
        self.assertEqual(result.returncode, 17)
        self.assertIn('injected query failure', rows[0]['errors'][0])
        self.assertIs(subprocess.Popen, original)

    def test_launch_failure_restores_patch(self):
        original, rows = subprocess.Popen, []
        with self.assertRaises(FileNotFoundError):
            with d.observation(True, lambda p: self.fail('unlaunched query'), rows):
                subprocess.run(['mqb-no-such-calibration-executable-6dc7'])
        self.assertIs(subprocess.Popen, original)
        self.assertEqual(rows, [])

    def test_timeout_preserves_original_error_and_restores_patch(self):
        original, rows = subprocess.Popen, []
        with self.assertRaises(subprocess.TimeoutExpired):
            with d.observation(True, lambda p: {'exit_code': p.returncode}, rows):
                subprocess.run([sys.executable, '-c', 'import time;time.sleep(3)'], capture_output=True, timeout=.05)
        self.assertIs(subprocess.Popen, original)
        self.assertEqual(len(rows), 1)
        self.assertIsNotNone(rows[0]['exit_code'])

    def test_resource_positive_and_six_rejecting_mutations(self):
        valid = dict(errors=[], query_count=3, pid=1, exit_code=17, creation_100ns=1,
                     exit_100ns=2, kernel_100ns=0, user_100ns=0, cycles=1, io={'read_ops': 0})
        d.valid_resource(valid, 17)
        for key, value in [('errors', [['API', 5]]), ('query_count', 2), ('pid', 0),
                           ('exit_code', 0), ('exit_100ns', 0), ('cycles', None)]:
            bad = {**valid, key: value}
            with self.subTest(key=key), self.assertRaises((RuntimeError, TypeError)):
                d.valid_resource(bad, 17)

    def test_first_attempt_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(RuntimeError, 'existing'):
                d.run(root / 'missing.zip', root, 'head')
            with patch.dict(d.os.environ, {'GITHUB_RUN_ATTEMPT': '2'}):
                with self.assertRaisesRegex(RuntimeError, 'first attempt'):
                    d.run(root / 'missing.zip', root / 'new', 'head')
            self.assertFalse((root / 'new').exists())

    def test_wrong_archive_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'wrong.zip'
            path.write_bytes(b'not original evidence')
            with self.assertRaisesRegex(RuntimeError, 'Wrong original artifact'):
                d.verify_archive(path)

    def test_summary_keeps_every_adverse_and_observer_sample(self):
        rows = deepcopy(d.schedule())
        for r in rows:
            r['call'] = {'external_ms': 10 + (2 if r['side'] == 'candidate' else 0) + int(r['observe'])}
            r['resource'] = {'cycles': 10, 'kernel_100ns': 0, 'user_100ns': 10,
                             'io': {key: 0 for key, _ in d.IoCounters._fields_}}
        for row in d.summaries(rows):
            self.assertEqual(row['off']['paired_deltas_ms'], [2] * 12)
            self.assertEqual(row['on']['paired_deltas_ms'], [2] * 12)
            self.assertEqual(row['baseline_observer_delta_ms']['values'], [1] * 12)
            self.assertEqual(row['candidate_observer_delta_ms']['values'], [1] * 12)


if __name__ == '__main__':
    unittest.main(verbosity=2)
