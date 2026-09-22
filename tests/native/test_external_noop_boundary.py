"""Synthetic contracts only. No MQB, MSVC, process timing or ETW is executed."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import external_noop_boundary as h


def files():
    return [dict(path=n, size=len(b), mtime_ticks=100, sha256=h.digest(b)) for n, b in h.SOURCES.items()]


def case(mode='legacy', phase='noop'):
    row = next(r for r in h.schedule() if r['mode'] == mode and r['phase'] == phase)
    before = files(); after = files()+[dict(path='.mqb/bin/timing_bench.exe', size=1, mtime_ticks=110, sha256='a'*64)]
    if phase == 'noop':
        before = copy.deepcopy(after)
    root = None if mode == 'legacy' else dict(pid=42, created_100ns=100, exited_100ns=200,
                                             kernel_100ns=0, user_100ns=1000)
    record = dict(row=row, argv=h.ARGV.copy(), executable_sha256=h.BINARIES[row['side']],
                  error=None, exit_code=0, dispatch_attempted=True, clears_hold=False, cleanup=None,
                  output_format='powershell_merged_lines' if mode == 'legacy' else 'separate_bytes',
                  root_times=root, clock=dict(frequency=1000, outer_start=1, native_start=2,
                                             start_return=3, wait_return=4, native_end=5, outer_end=6))
    lines = ['[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe'] if phase == 'noop' else [
        '[compile] main.cpp', '[compile] helper.cpp', '[link] timing_bench.exe']
    return row, record, before, after, lines


class Contracts(unittest.TestCase):
    def test_fixed_budget_and_independent_fixtures(self):
        rows = h.schedule()
        self.assertEqual(list(range(1, 41)), [r['sequence'] for r in rows])
        self.assertEqual(20, len({r['fixture'] for r in rows}))
        self.assertEqual(32, sum(r['control'] == 'AB' for r in rows))
        self.assertEqual(8, sum(r['control'] == 'BB' for r in rows))
        for i in range(0, 40, 2):
            self.assertEqual(rows[i]['fixture'], rows[i+1]['fixture'])
            self.assertEqual(['prime', 'noop'], [rows[i]['phase'], rows[i+1]['phase']])
        self.assertEqual(20, sum(r['mode'] == 'legacy' for r in rows))

    def test_order_and_bb_are_not_a_cost_subtraction(self):
        rows = h.schedule()
        for mode in ('legacy', 'process'):
            for n in range(1, 5):
                sides = [r['side'] for r in rows if r['control'] == 'AB' and r['pair'] == n and
                         r['mode'] == mode and r['phase'] == 'noop']
                self.assertEqual(['baseline', 'candidate'] if n % 2 else ['candidate', 'baseline'], sides)
        self.assertTrue(all(r['side'] == 'candidate' for r in rows if r['control'] == 'BB'))

    def test_four_mode_phase_contracts(self):
        for mode in ('legacy', 'process'):
            for phase in ('prime', 'noop'):
                r = h.validate_call(*case(mode, phase))
                self.assertEqual(5, r['outer_ms'])
                self.assertEqual(3, r['native_envelope_ms'])

    def test_root_cpu_is_not_bounded_by_wall(self):
        r = h.validate_call(*case('process'))
        self.assertGreater(r['root_cpu_ms'], r['root_lifetime_ms'])

    def test_changed_schedule_or_numeric_types_refused(self):
        args = list(case()); args[1] = copy.deepcopy(args[1]); args[1]['row']['sequence'] = True
        with self.assertRaises(ValueError): h.validate_call(*args)
        self.assertFalse(h.same(1, True))
        self.assertFalse(h.same(1, 1.0))

    def test_missing_reversed_and_noninteger_qpc_refused(self):
        for key, value in [('frequency', 0), ('native_start', None), ('outer_end', 0), ('native_end', 5.0)]:
            args = list(case()); args[1] = copy.deepcopy(args[1]); args[1]['clock'][key] = value
            with self.assertRaises(ValueError): h.validate_call(*args)

    def test_unknown_legacy_root_is_not_zero(self):
        args = list(case()); args[1]['root_times'] = dict(pid=0)
        with self.assertRaises(ValueError): h.validate_call(*args)
        args = list(case('process')); args[1]['root_times'] = None
        with self.assertRaises(ValueError): h.validate_call(*args)

    def test_process_api_failure_or_order_refused(self):
        for key, value in [('pid', 0), ('created_100ns', 201), ('kernel_100ns', None)]:
            args = list(case('process')); args[1]['root_times'][key] = value
            with self.assertRaises(ValueError): h.validate_call(*args)
        args = list(case('process')); args[1]['clock']['wait_return'] = 2
        with self.assertRaises(ValueError): h.validate_call(*args)

    def test_failed_dispatch_or_exit_never_success(self):
        for key, value in [('error', 'first failure'), ('exit_code', 2), ('exit_code', False),
                           ('dispatch_attempted', False), ('clears_hold', True), ('executable_sha256', '0'*64)]:
            args = list(case()); args[1][key] = value
            with self.assertRaises(ValueError): h.validate_call(*args)

    def test_noop_recompile_or_timing_refused(self):
        for text in ('[compile] main.cpp', '[link] timing_bench.exe', '{"type":"mqb.timings"}'):
            args = list(case()); args[-1].append(text)
            with self.assertRaises(ValueError): h.validate_call(*args)
        args = list(case()); args[-1].pop()
        with self.assertRaises(ValueError): h.validate_call(*args)

    def test_retained_call02_compact_progress(self):
        # Exact text from 701-boundary-001/run35725316959/call02; the surrounding
        # case's clock, PID and file manifests are SYNTHETIC, not new study data.
        lines = [
            '[up-to-date] 2 translation units',
            '[up-to-date] timing_bench.exe',
            'output: D:/a/msvc-quick-build/msvc-quick-build/execution-out/evidence/'
            'study/fixtures/AB-1-legacy-0-baseline/.mqb/bin/timing_bench.exe',
        ]
        for mode in ('legacy', 'process'):
            args = list(case(mode)); args[-1] = lines.copy()
            self.assertEqual(5, h.validate_call(*args)['outer_ms'])

    def test_compact_progress_checks_meaning_not_line_count(self):
        invalid = [
            ['[up-to-date] 1 translation unit', '[up-to-date] timing_bench.exe'],
            ['[up-to-date] 3 translation units', '[up-to-date] timing_bench.exe'],
            ['[up-to-date] 2 translation units', '[up-to-date] other.exe'],
            ['[up-to-date] main.cpp', '[up-to-date] helper.cpp'],
            ['[up-to-date] main.cpp', '[up-to-date] helper.cpp',
             '[up-to-date] timing_bench.exe'],  # --verbose is not in fixed ARGV.
            ['[up-to-date] 2 translation units', '[up-to-date] 2 translation units'],
            ['[up-to-date] timing_bench.exe', '[up-to-date] 2 translation units'],
            ['[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe',
             '[up-to-date] main.cpp'],
            ['[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe extra'],
            ['[up-to-date] 2 translation units'],
            [],
        ]
        for mode in ('legacy', 'process'):
            for lines in invalid:
                with self.subTest(mode=mode, lines=lines):
                    args = list(case(mode)); args[-1] = lines
                    with self.assertRaises(ValueError): h.validate_call(*args)

    def test_same_path_replacement_or_mtime_change_refused(self):
        for key, value in [('sha256', 'b'*64), ('size', 2), ('mtime_ticks', 120)]:
            args = list(case()); args[3][-1][key] = value
            with self.assertRaises(ValueError): h.validate_call(*args)

    def test_source_change_missing_output_and_dirty_prime_refused(self):
        args = list(case()); args[3][0]['sha256'] = 'a'*64
        with self.assertRaises(ValueError): h.validate_call(*args)
        args = list(case()); args[3].pop()
        with self.assertRaises(ValueError): h.validate_call(*args)
        args = list(case(phase='prime')); args[2].append(copy.deepcopy(args[3][-1]))
        with self.assertRaises(ValueError): h.validate_call(*args)

    def test_bad_paths_values_and_duplicates_refused(self):
        for value in ('../escape', '/absolute', 'C:/drive', 'dir\\file', 'a//b'):
            f = files(); f.append(dict(path=value, size=1, mtime_ticks=1, sha256='a'*64))
            with self.assertRaises(ValueError): h.manifest(f)
        f = files(); f.append(copy.deepcopy(f[0]))
        with self.assertRaises(ValueError): h.manifest(f)
        f = files(); f[0]['size'] = True
        with self.assertRaises(ValueError): h.manifest(f)

    def test_json_no_overwrite_duplicate_and_nonfinite(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'old.json'; h.write_new(p, {'a': 1}); old = p.read_bytes()
            with self.assertRaises(FileExistsError): h.write_new(p, {'a': 2})
            self.assertEqual(old, p.read_bytes())
            for n, data in enumerate(['{"a":1,"a":2}', '{"a":NaN}']):
                f = Path(d)/f'{n}.json'; f.write_text(data)
                with self.assertRaises(ValueError): h.load(f)

    def test_bad_artifact_does_not_create_root(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'bad.zip'; p.write_bytes(b'not the pinned archive'); out = Path(d)/'evidence'
            with self.assertRaises(ValueError): h.prepare(p, out)
            self.assertFalse(out.exists())

    def test_full_synthetic_audit_and_missing_final_record(self):
        # Explicitly synthetic identities; never use this report as a measured sample.
        archive = b'synthetic archive placeholder'
        binaries = {'baseline': b'synthetic A', 'candidate': b'synthetic B'}
        hashes = {n: h.digest(b) for n, b in binaries.items()}
        with tempfile.TemporaryDirectory() as d, patch.object(h, 'ARCHIVE', h.digest(archive)), patch.object(h, 'BINARIES', hashes):
            root = Path(d)
            for n in ('inputs', 'calls', 'collector-source'):
                (root/n).mkdir()
            (root/'inputs/original-701.zip').write_bytes(archive)
            for n, b in binaries.items():
                (root/'inputs'/n).mkdir(); (root/'inputs'/n/'mqb.exe').write_bytes(b)
            collector = {}
            for n in h.COLLECTORS:
                b = Path(h.__file__).with_name(n).read_bytes()
                (root/'collector-source'/n).write_bytes(b); collector[n] = h.digest(b)
            plan = dict(schema=1, source_run='35681224762', archive_sha256=h.ARCHIVE,
                        binaries=hashes, revisions=h.REVISIONS, argv=h.ARGV,
                        max_calls=40, rows=h.schedule(), clears_hold=False, root=str(root),
                        source_hashes={n: h.digest(b) for n, b in h.SOURCES.items()}, collector_hashes=collector)
            h.write_new(root/'plan.json', plan)
            h.write_new(root/'host.json', {'original_plan_sha256': h.digest((root/'plan.json').read_bytes())})
            h.write_new(root/'completion.json', dict(status='completed', attempted=40, validated=40, error=None, clears_hold=False))
            for row in h.schedule():
                _, record, before, after, lines = case(row['mode'], row['phase'])
                record['row'] = row; record['executable_sha256'] = hashes[row['side']]
                record['output_lines'] = lines if row['mode'] == 'legacy' else None
                prefix = root/'calls'/f"{row['sequence']:02d}"
                h.write_new(Path(str(prefix)+'.started.json'), dict(row=row, argv=h.ARGV,
                    executable=str(root/'inputs'/row['side']/'mqb.exe'),
                    cwd=str(root/'fixtures'/row['fixture']), executable_sha256=hashes[row['side']]))
                for suffix, value in [('.result.json', record), ('.before.json', before), ('.after.json', after)]:
                    h.write_new(Path(str(prefix)+suffix), value)
                if row['mode'] == 'process':
                    Path(str(prefix)+'.stdout.bin').write_bytes(('\n'.join(lines)+'\n').encode())
                    Path(str(prefix)+'.stderr.bin').write_bytes(b'')
                h.write_new(Path(str(prefix)+'.validated.json'), h.audit_call(root, row))
            value = h.audit(root)
            self.assertEqual(40, len(value['samples'])); self.assertEqual(8, len(value['pairs']))
            self.assertFalse(value['clears_hold']); self.assertIsNone(value['cause'])
            # Synthetic mutations are isolated; no historical or CI evidence is edited.
            extra = root/'calls/41.started.json'
            h.write_new(extra, {'sequence': 41, 'synthetic': True})
            with self.assertRaises(ValueError): h.audit(root)
            extra.unlink()
            verdict = root/'calls/01.validated.json'; original = verdict.read_bytes()
            altered = h.load(verdict); altered['outer_ms'] += 1
            verdict.write_text(json.dumps(altered))
            with self.assertRaises(ValueError): h.audit(root)
            verdict.write_bytes(original)
            completion_path = root/'completion.json'; original = completion_path.read_bytes()
            for field, value in [('attempted', 40.0), ('validated', 40.0), ('clears_hold', 0)]:
                altered = json.loads(original); altered[field] = value
                completion_path.write_text(json.dumps(altered))
                with self.assertRaises(ValueError): h.audit(root)
            completion_path.write_bytes(original)
            (root/'calls/40.result.json').unlink()
            with self.assertRaises(OSError): h.audit(root)
            self.assertEqual(39, len(list((root/'calls').glob('*.result.json'))))

    def test_success_cannot_carry_cleanup_evidence(self):
        for mode in ('legacy', 'process'):
            for cleanup in ({'root_exited': False, 'descendants_verified': False},
                            {'root_exited': True, 'descendants_verified': False},
                            {}, False, 'cleanup failed'):
                args = list(case(mode)); args[1]['cleanup'] = cleanup
                with self.assertRaises(ValueError): h.validate_call(*args)
            args = list(case(mode)); del args[1]['cleanup']
            with self.assertRaises(KeyError): h.validate_call(*args)

    def test_exact_journal_inventory_accepts_only_complete_plan(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); calls = root/'calls'; calls.mkdir()
            rows = [h.schedule()[0], next(r for r in h.schedule() if r['mode'] == 'process')]
            expected = set()
            for row in rows:
                suffixes = ['.started.json', '.before.json', '.result.json', '.after.json', '.validated.json']
                if row['mode'] == 'process': suffixes += ['.stdout.bin', '.stderr.bin']
                for suffix in suffixes:
                    name = f"{row['sequence']:02d}" + suffix
                    (calls/name).write_bytes(b'synthetic inventory placeholder')
                    expected.add(name)
            h.validate_call_inventory(root, rows)
            for name in sorted(expected):
                p = calls/name; data = p.read_bytes(); p.unlink()
                with self.assertRaises(FileNotFoundError): h.validate_call_inventory(root, rows)
                self.assertFalse(p.exists())  # Refusal must not fill evidence slots.
                p.write_bytes(data)

    def test_inventory_rejects_dangling_markers_and_wrong_mode_streams(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); calls = root/'calls'; calls.mkdir()
            rows = [h.schedule()[0]]
            for suffix in ('.started.json', '.before.json', '.result.json', '.after.json', '.validated.json'):
                (calls/('01'+suffix)).write_bytes(b'synthetic')
            for name in ('41.started.json', '41.before.json', '41.result.json', '41.after.json',
                         '41.validated.json', '01.stdout.bin', '01.stderr.bin', '01.result.json.tmp',
                         'unexpected.txt'):
                p = calls/name; p.write_bytes(b'preserve unexpected evidence')
                with self.assertRaises(ValueError): h.validate_call_inventory(root, rows)
                self.assertEqual(b'preserve unexpected evidence', p.read_bytes())
                p.unlink()
            p = calls/'nested'; p.mkdir()
            with self.assertRaises(ValueError): h.validate_call_inventory(root, rows)
            self.assertTrue(p.is_dir())

    def test_cli_incomplete_study_fails_without_filling_slots(self):
        with tempfile.TemporaryDirectory() as d:
            p = subprocess.run([sys.executable, h.__file__, 'audit', d, '--output', str(Path(d)/'audit.json')],
                               capture_output=True, text=True, timeout=10)
            self.assertEqual(2, p.returncode)
            self.assertFalse((Path(d)/'calls').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
