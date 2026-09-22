"""Synthetic contracts only; never launches the child or a retained executable."""
from __future__ import annotations
import ast
import copy
import json
from pathlib import Path
import tempfile
import unittest

import external_noop_preflight as p
h = p.h
HERE = Path(__file__).parent


def observed(row, plan):
    scenario, mode = row['scenario'], row['mode']
    child = dict(schema=1, purpose='non_mqb_helper', pid=42, mode=scenario,
                 cwd=plan['root']+'/fixture space', executable=plan['executable'],
                 payload=p.PAYLOAD.copy(), source_sha256=plan['sources']['external_noop_preflight_child.py'])
    clock = dict(frequency=1000, outer_start=1, native_start=2, start_return=3,
                 wait_return=4, native_end=5, outer_end=6)
    rt = dict(pid=42, created_100ns=100, exited_100ns=200, kernel_100ns=1, user_100ns=2)
    r = dict(row=row, clears_hold=False, clock=clock, root_times=rt if mode == 'process' else None,
             exit_code=37 if scenario == 'nonzero' else 0, error=None, cleanup=None,
             output_format='separate_bytes' if mode == 'process' else 'powershell_merged_lines', output_lines=None)
    stdout, stderr = (json.dumps(child)+'\n').encode(), b'PREFLIGHT-STDERR\n'
    lines = [json.dumps(child), 'PREFLIGHT-STDERR']
    if scenario == 'streams':
        stdout, stderr = p.stream_bytes(b'O'), p.stream_bytes(b'E')
        lines = ['O'*1023]*256 + ['E'*1023]*256
    elif scenario == 'missing':
        child = None; r.update(exit_code=None, error='synthetic missing target', root_times=None)
        clock.update(start_return=None, wait_return=None)
        stdout = stderr = b''; lines = []
    elif scenario == 'timeout':
        r.update(exit_code=None, error='Root wait exceeded 180 seconds.', root_times=None,
                 cleanup=dict(root_exited=True, descendants_verified=False))
        clock.update(wait_return=None, native_end=180004, outer_end=180005)
        stdout, stderr = b'PREFLIGHT-READY\n', b'PREFLIGHT-WAITING\n'
    if mode == 'legacy': r['output_lines'] = lines; stdout = stderr = None
    return [row, r, child, stdout, stderr, r['output_lines'], plan]


def synthetic(root):
    (root/'source').mkdir(); (root/'calls').mkdir()
    for n in p.SOURCE_NAMES: (root/'source'/n).write_bytes((HERE/n).read_bytes())
    exe = root/'python.exe'; exe.write_bytes(b'explicit non-executable synthetic interpreter')
    plan = p.make_plan(root, exe)
    h.write_new(root/'plan.json', plan)
    host = dict(location='SYNTHETIC host cwd', environment_cwd='SYNTHETIC environment cwd')
    h.write_new(root/'host.json', dict(host, original_plan_sha256=h.digest((root/'plan.json').read_bytes())))
    for row in p.schedule():
        prefix = root/'calls'/f"{row['sequence']:02d}"
        h.write_new(Path(str(prefix)+'.started.json'), dict(row=row, argv=p.expected_argv(root, row),
            executable=p.expected_executable(plan, row), cwd=plan['root']+'/fixture space',
            interpreter_sha256=plan['executable_sha256'], host_location=host['location'],
            host_environment_cwd=host['environment_cwd']))
        args = observed(row, plan)
        h.write_new(Path(str(prefix)+'.result.json'), args[1])
        h.write_new(Path(str(prefix)+'.host-after.json'), dict(host, interpreter_sha256=plan['executable_sha256']))
        if args[2] is not None: h.write_new(Path(str(prefix)+'.child.json'), args[2])
        if row['mode'] == 'process':
            Path(str(prefix)+'.stdout.bin').write_bytes(args[3])
            Path(str(prefix)+'.stderr.bin').write_bytes(args[4])
        h.write_new(Path(str(prefix)+'.validated.json'), p.check_call(root, row))
    h.write_new(root/'completion.json', dict(status='completed', attempted=9, validated=9,
                                            error=None, clears_hold=False, study_mqb_calls=0))
    return plan


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.plan = dict(root='C:/synthetic', executable='C:/synthetic/python.exe',
                         sources={'external_noop_preflight_child.py': 'c'*64})

    def test_fixed_plan_and_separate_budget(self):
        rows = p.schedule()
        self.assertEqual(list(range(1, 10)), [r['sequence'] for r in rows])
        self.assertEqual(7, sum(r['scenario'] != 'missing' for r in rows))
        self.assertEqual({'sequence': 9, 'mode': 'process', 'scenario': 'timeout'}, rows[-1])
        self.assertEqual(53, len(p.expected_files()))

    def test_all_nine_expected_outcomes(self):
        for row in p.schedule():
            v = p.validate_observation(*observed(row, self.plan))
            self.assertFalse(v['clears_hold']); self.assertEqual(0, v['study_mqb_calls'])

    def test_argument_changes_refused(self):
        for row in p.schedule()[:2]:
            for bad in ([], ['space', 'argument'], p.PAYLOAD[:-1], p.PAYLOAD[::-1]):
                args = observed(row, self.plan); args[2]['payload'] = bad
                # Keep the duplicated echo consistent, isolating the argv oracle.
                if row['mode'] == 'process': args[3] = (json.dumps(args[2])+'\n').encode()
                else: args[5][0] = json.dumps(args[2])
                with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_cwd_pid_and_helper_identity(self):
        for field, value in [('cwd', 'wrong'), ('executable', 'other'), ('source_sha256', 'bad'), ('pid', 0)]:
            args = observed(p.schedule()[1], self.plan); args[2][field] = value
            with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_process_pipe_truncation_and_crossing(self):
        for which in (3, 4):
            args = observed(p.schedule()[3], self.plan); args[which] = args[which][1:]
            with self.assertRaises(ValueError): p.validate_observation(*args)
        args = observed(p.schedule()[3], self.plan); args[3], args[4] = args[4], args[3]
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_legacy_lines_not_fabricated_bytes(self):
        args = observed(p.schedule()[2], self.plan); args[5].pop()
        with self.assertRaises(ValueError): p.validate_observation(*args)
        args = observed(p.schedule()[0], self.plan); args[1]['root_times'] = {'pid': 1}
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_nonzero_is_not_zero_or_exception(self):
        for row in p.schedule()[4:6]:
            args = observed(row, self.plan); args[1]['exit_code'] = 0
            with self.assertRaises(ValueError): p.validate_observation(*args)
            args = observed(row, self.plan); args[1]['error'] = 'swallowed failure'
            with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_missing_launch_has_no_child_or_exit(self):
        for row in p.schedule()[6:8]:
            for key, value in [('exit_code', 0), ('error', None), ('root_times', {'pid': 42})]:
                args = observed(row, self.plan); args[1][key] = value
                with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_timeout_cannot_be_shortened(self):
        args = observed(p.schedule()[-1], self.plan)
        args[1]['clock'].update(native_end=179999, outer_end=180000)
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_timeout_cleanup_is_limited_and_required(self):
        for value in (None, {}, {'root_exited': False, 'descendants_verified': False},
                      {'root_exited': True, 'descendants_verified': True}):
            args = observed(p.schedule()[-1], self.plan); args[1]['cleanup'] = value
            with self.assertRaises(ValueError): p.validate_observation(*args)
        args = observed(p.schedule()[-1], self.plan); args[3] = b''
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_clock_domains_and_boundaries(self):
        for key, value in [('frequency', 0), ('native_end', 0), ('start_return', 99), ('wait_return', None)]:
            args = observed(p.schedule()[1], self.plan); args[1]['clock'][key] = value
            with self.assertRaises(ValueError): p.validate_observation(*args)
        args = observed(p.schedule()[1], self.plan); args[1]['root_times']['exited_100ns'] = 0
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_success_has_no_cleanup(self):
        args = observed(p.schedule()[1], self.plan); args[1]['cleanup'] = {}
        with self.assertRaises(ValueError): p.validate_observation(*args)

    def test_complete_synthetic_journal(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic(root)
            v = p.audit(root)
            self.assertEqual((9, 7, False), (v['dispatches'], v['helpers'], v['clears_hold']))
            self.assertIsNone(v['cause'])

    def test_incomplete_extra_and_wrong_saved_verdict(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic(root)
            extra = root/'calls/10.started.json'; extra.write_text('SYNTHETIC extra dispatch')
            with self.assertRaises(ValueError): p.audit(root)
            self.assertTrue(extra.exists()); extra.unlink()
            path = root/'calls/01.validated.json'; value = h.load(path); value['clears_hold'] = True
            path.write_text(json.dumps(value))
            with self.assertRaises(ValueError): p.audit(root)
            path.unlink()
            with self.assertRaises(ValueError): p.audit(root)
            self.assertFalse(path.exists())

    def test_completion_types_and_stopped_prefix(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic(root); path = root/'completion.json'; value = h.load(path)
            for key, bad in [('attempted', 9.0), ('validated', 8), ('status', 'stopped'), ('study_mqb_calls', False)]:
                other = dict(value); other[key] = bad; path.write_text(json.dumps(other))
                with self.assertRaises(ValueError): p.audit(root)

    def test_plan_source_and_interpreter_replacement(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); plan = synthetic(root)
            for key, value in [('max_dispatches', 10), ('payload', []), ('study_mqb_calls', 1)]:
                other = copy.deepcopy(plan); other[key] = value
                with self.assertRaises(ValueError): p.validate_plan(other)
            (root/'python.exe').write_bytes(b'replaced synthetic interpreter')
            with self.assertRaises(ValueError): p.audit(root, live_interpreter=True)

    def test_host_location_restore_is_not_inferred(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); synthetic(root)
            path = root/'calls/01.host-after.json'; value = h.load(path); value['location'] = 'wrong'
            path.write_text(json.dumps(value))
            with self.assertRaises(ValueError): p.audit(root)

    def test_launcher_is_original_and_study_not_imported(self):
        data = (HERE/'collect_external_noop_boundary.ps1').read_bytes().replace(b'\r\n', b'\n')
        self.assertEqual(p.COLLECTOR_SHA256, h.digest(data))
        self.assertIn(b'WaitForExit(180000)', data); self.assertIn(b'WaitForExit(5000)', data)
        runner = (HERE/'preflight_external_noop_boundary.ps1').read_text(encoding='utf-8-sig')
        allowed = runner.split('$allowed = @(', 1)[1].split(')', 1)[0]
        self.assertNotIn('Invoke-RegisteredStudy', allowed)
        self.assertIn('Invoke-LegacyBoundary $launchExe $fixture $argv $prefix', runner)
        self.assertIn('Invoke-ProcessBoundary $launchExe $fixture $argv $prefix', runner)
        self.assertLess(runner.index('Write-NewJson "$prefix.result.json"'), runner.index('check-call $root'))
        self.assertIn('if ($LASTEXITCODE -ne 0) { throw', runner)

    def test_workflow_is_bounded_and_retains_failures(self):
        repo = HERE.parent.parent
        text = (repo/'.github/workflows/external-noop-preflight.yml').read_text()
        self.assertIn('timeout-minutes: 10', text)
        self.assertIn('needs: contracts', text)
        self.assertIn('if: always()', text)
        self.assertIn('path: preflight-out/', text)
        active = '\n'.join(x for x in text.splitlines() if not x.lstrip().startswith('#'))
        for forbidden in ('continue-on-error', 'workflow_dispatch', 'ExecuteRegisteredStudy', 'download-artifact', 'acquire_seed'):
            self.assertNotIn(forbidden, active)

    def test_windows_paths_replay_without_losing_argv_separators(self):
        row = p.schedule()[0]
        argv = p.expected_argv(Path('D:/synthetic root'), row)
        self.assertEqual('D:\\synthetic root\\source\\external_noop_preflight_child.py', argv[2])
        self.assertEqual(p.PAYLOAD, argv[5:])

    def test_child_does_not_launch_descendants(self):
        tree = ast.parse((HERE/'external_noop_preflight_child.py').read_text())
        imports = {x.name for n in ast.walk(tree) if isinstance(n, ast.Import) for x in n.names}
        self.assertFalse({'subprocess', 'multiprocessing', 'ctypes'} & imports)
        self.assertIn('time.sleep(240)', (HERE/'external_noop_preflight_child.py').read_text())
        self.assertEqual(262144, len(p.stream_bytes(b'O')))


if __name__ == '__main__':
    unittest.main(verbosity=2)
