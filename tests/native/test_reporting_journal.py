"""Recorder correctness only. Python children/mocks; no MQB or MSVC execution."""
from __future__ import annotations
import ast
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

import compare_reporting as h


class JournalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.case = h.Fixture('python', self.root, ['-c', 'print("OUT")'], 1)

    def recorder(self, name='records'):
        return h.Recorder(self.root / name)

    def fake(self, recorder, *, code=0, out=b'OUT\r\n', err=b'', **kwargs):
        response = subprocess.CompletedProcess([], code, out, err)
        with patch.object(h.subprocess, 'run', return_value=response), patch.object(h.time, 'perf_counter_ns', side_effect=[1000000, 2000000]):
            return recorder.run(Path('mqb.exe'), self.case, f'call-{len(recorder.calls)}', **kwargs)

    def sample(self, recorder, **fields):
        row = dict(label='row', external_ms=1.0, exit_code=0, text='中文\n\\"', nested={'x': [None, -1, True]})
        row.update(fields)
        recorder.calls.append(row)
        recorder.checkpoint()
        return row

    def load(self, recorder):
        return json.loads((recorder.root / 'calls.json').read_text(encoding='utf-8'))

    def test_legacy_row_and_final_bytes(self):
        with self.recorder() as r:
            row = self.fake(r)
            expected = dict(label='call-0', argv=['mqb.exe', '-c', 'print("OUT")', '-j', 'auto'], cwd=str(self.root),
                external_ms=1.0, exit_code=0, sink='captured-pipes', verbose=False, timings_enabled=False,
                stdout_file='raw/0000-call-0.stdout', stderr_file='raw/0000-call-0.stderr',
                stdout_sha256=h.digest(b'OUT\r\n'), stderr_sha256=h.digest(b''),
                human_stdout_sha256=h.digest(b'OUT\n'), human_stderr_sha256=h.digest(b''), timing=None)
            self.assertEqual(row, expected)
            self.assertFalse((r.root / 'calls.json').exists())
            self.assertEqual(h.read_call_journal(r.root/'calls.jsonl', expected_count=1), [row])
        golden = self.root / 'golden.json'
        golden.write_text(json.dumps([expected], indent=2), encoding='utf-8')
        self.assertEqual((r.root/'calls.json').read_bytes(), golden.read_bytes())

    def test_empty_finalization_and_idempotence(self):
        with self.recorder() as r:
            pass
        before = (r.root/'calls.json').stat().st_mtime_ns
        r.finalize()
        self.assertEqual(self.load(r), [])
        self.assertEqual((r.root/'calls.json').stat().st_mtime_ns, before)

    def test_expected_nonzero_row_not_removed(self):
        with self.recorder() as r:
            row = self.fake(r, code=17, err=b'ERR', success=False)
        self.assertEqual(self.load(r), [row])
        self.assertEqual(row['exit_code'], 17)

    def test_unexpected_nonzero_materializes_without_context(self):
        r = self.recorder()
        with self.assertRaisesRegex(RuntimeError, 'exit 17'):
            self.fake(r, code=17, err=b'ERR')
        self.assertEqual(self.load(r)[0]['exit_code'], 17)
        failure = json.loads((r.root/'failed-attempt.json').read_text())
        self.assertEqual(failure['stderr_hex'], b'ERR'.hex())
        self.assertEqual(failure['exit_code'], 17)
        with patch.object(h.subprocess, 'run') as spawn, self.assertRaisesRegex(RuntimeError, 'no further'):
            r.run(Path('mqb.exe'), self.case, 'must-not-run')
        spawn.assert_not_called()

    def test_contract_failure_after_run_materializes(self):
        with self.assertRaisesRegex(ValueError, 'caller contract'):
            with self.recorder() as r:
                row = self.fake(r)
                raise ValueError('caller contract')
        self.assertEqual(self.load(r), [row])

    def test_keyboard_interrupt_materializes(self):
        with self.assertRaises(KeyboardInterrupt):
            with self.recorder() as r:
                self.sample(r)
                raise KeyboardInterrupt()
        self.assertEqual(len(self.load(r)), 1)

    def test_timeout_partial_bytes_and_cause_survive(self):
        r = self.recorder()
        timeout = subprocess.TimeoutExpired(['mqb.exe'],120,output=b'partial-out',stderr=b'partial-err')
        with patch.object(h.subprocess, 'run', side_effect=timeout), self.assertRaisesRegex(RuntimeError, 'timed out') as e:
            r.run(Path('mqb.exe'), self.case, 'timeout')
        self.assertIs(e.exception.__cause__, timeout)
        self.assertEqual((r.root/'raw/timeout-timeout.stdout').read_bytes(), b'partial-out')
        self.assertEqual((r.root/'raw/timeout-timeout.stderr').read_bytes(), b'partial-err')
        self.assertEqual(self.load(r), [])  # Timeout is a failed attempt, never an invented complete sample.
        self.assertEqual(json.loads((r.root/'failed-attempt.json').read_text())['timeout_seconds'], 120)

    def test_launch_failure_is_not_a_successful_call(self):
        r = self.recorder()
        original = OSError('launch failed')
        with patch.object(h.subprocess, 'run', side_effect=original), self.assertRaises(OSError) as e:
            r.run(Path('absent'), self.case, 'launch')
        self.assertIs(e.exception, original)
        self.assertEqual(self.load(r), [])
        self.assertEqual(json.loads((r.root/'failed-attempt.json').read_text())['error'], 'launch failed')

    def test_bad_timing_json_retains_raw_and_failed_attempt(self):
        r = self.recorder()
        out = b'{"type":"mqb.timings",broken}\n'
        with self.assertRaises(json.JSONDecodeError):
            self.fake(r, out=out, timings=True)
        self.assertEqual((r.root/'raw/0000-call-0.stdout').read_bytes(), out)
        self.assertEqual(json.loads((r.root/'failed-attempt.json').read_text())['stdout_hex'], out.hex())
        self.assertEqual(self.load(r), [])

    def test_timing_count_failure_keeps_completed_row(self):
        r = self.recorder()
        with self.assertRaisesRegex(RuntimeError, 'timing record count'):
            self.fake(r, timings=True)
        self.assertEqual(len(self.load(r)), 1)
        self.assertIsNone(self.load(r)[0]['timing'])

    def test_inherited_unavailable_fields_stay_null(self):
        with self.recorder() as r:
            row = self.fake(r, out=None, err=None, inherited=True)
        for key in ('stdout_file', 'stderr_file', 'stdout_sha256', 'stderr_sha256', 'human_stdout_sha256','human_stderr_sha256','timing'):
            self.assertIsNone(row[key])
        self.assertEqual(row['sink'], 'inherited-ci-handles')

    def test_raw_write_failure_still_attempts_other_stream(self):
        for failed in ('.stdout', '.stderr'):
            with self.subTest(failed=failed):
                r = self.recorder('records'+failed)
                original = Path.write_bytes
                def write(path, data):
                    if path.name.endswith(failed):
                        raise OSError('raw disk failure')
                    return original(path, data)
                with patch.object(Path, 'write_bytes', write), self.assertRaisesRegex(OSError, 'raw disk failure'):
                    self.fake(r, out=b'OUT', err=b'ERR')
                other = '.stderr' if failed == '.stdout' else '.stdout'
                self.assertEqual((r.root/('raw/0000-call-0'+other)).read_bytes(), b'ERR' if other=='.stderr' else b'OUT')
                fail = json.loads((r.root/'failed-attempt.json').read_text())
                self.assertEqual((fail['stdout_hex'],fail['stderr_hex']), ('4f5554','455252'))

    def test_raw_short_write_is_not_success(self):
        r = self.recorder()
        original = Path.write_bytes
        def short(path, data):
            if path.suffix == '.stdout':
                return original(path, data[:1])
            return original(path, data)
        with patch.object(Path, 'write_bytes', short), self.assertRaisesRegex(RuntimeError,'short raw'):
            self.fake(r)
        self.assertTrue((r.root/'failed-attempt.json').exists())

    def test_journal_write_failure_stops_without_false_final_json(self):
        r = self.recorder()
        original = Path.open
        def broken(path, mode='r', *args, **kwargs):
            if path.name=='calls.jsonl' and mode=='ab':
                raise OSError('journal unavailable')
            return original(path,mode,*args,**kwargs)
        with patch.object(Path, 'open', broken), self.assertRaisesRegex(OSError,'journal unavailable'):
            self.fake(r)
        self.assertFalse((r.root/'calls.json').exists())
        self.assertTrue((r.root/'raw/0000-call-0.stdout').exists())
        with self.assertRaisesRegex(OSError, 'journal unavailable'):
            r.finalize()

    def test_journal_short_write_preserves_torn_tail_and_stops(self):
        r = self.recorder()
        original = Path.open
        class Short:
            def __init__(self, stream): self.stream=stream
            def __enter__(self): return self
            def write(self,data): return self.stream.write(data[:7])
            def __exit__(self,*args): return self.stream.__exit__(*args)
        def short(path, mode='r', *args, **kwargs):
            stream=original(path,mode,*args,**kwargs)
            return Short(stream) if path.name=='calls.jsonl' and mode=='ab' else stream
        with patch.object(Path, 'open', short), self.assertRaisesRegex(RuntimeError,'short journal'):
            self.fake(r)
        self.assertEqual((r.root/'calls.jsonl').stat().st_size,7)
        self.assertFalse((r.root/'calls.json').exists())

    def test_torn_duplicate_missing_invalid_journal_rejected_unchanged(self):
        valid = b'{"sequence":0,"call":{"x":1}}\n'
        cases = [valid[:-1], valid+b'{', valid+valid, valid.replace(b':0',b':1',1),
                 valid.replace(b':0',b':true',1), valid.replace(b'"x":1',b'"x":NaN'),
                 valid.replace(b'"x":1',b'"x":1,"x":2'), b'\n', b'{"sequence":0,"call":[]}\n',
                 valid.replace(b'"x":1',b'"x":Infinity'), b'{"sequence":0,"call":{"x":"\xff"}}\n']
        for i,data in enumerate(cases):
            with self.subTest(i=i):
                path=self.root/f'bad-{i}.jsonl';path.write_bytes(data)
                with self.assertRaises((RuntimeError,ValueError,UnicodeError)):
                    h.read_call_journal(path,expected_count=1)
                self.assertEqual(path.read_bytes(),data)
        path=self.root/'whole-tail-lost.jsonl';path.write_bytes(valid)
        with self.assertRaisesRegex(RuntimeError,'count differs'):
            h.read_call_journal(path,expected_count=2)

    def test_wrong_count_argument_refused(self):
        path=self.root/'empty';path.write_bytes(b'')
        for count in (None,True,-1,0.0,'0'):
            with self.subTest(count=count),self.assertRaises(RuntimeError):
                h.read_call_journal(path,expected_count=count)

    def test_mutated_row_does_not_rewrite_history(self):
        r=self.recorder();row=self.sample(r);before=(r.root/'calls.jsonl').read_bytes()
        row['nested']['x'].append('changed')
        with self.assertRaisesRegex(RuntimeError,'mutated'):
            r.finalize()
        self.assertEqual((r.root/'calls.jsonl').read_bytes(),before)
        self.assertFalse((r.root/'calls.json').exists())

    def test_unjournaled_record_refused(self):
        r=self.recorder();r.calls.append({'label':'missing'})
        with self.assertRaisesRegex(RuntimeError,'unjournaled'):
            r.finalize()

    def test_duplicate_checkpoint_no_extra_line(self):
        r=self.recorder();self.sample(r);before=(r.root/'calls.jsonl').read_bytes()
        r.checkpoint();self.assertEqual((r.root/'calls.jsonl').read_bytes(),before)
        r.finalize()

    def test_multiple_uncheckpointed_calls_refused(self):
        r=self.recorder();r.calls.extend([{'x':1},{'x':2}])
        with self.assertRaisesRegex(RuntimeError,'one new call'):
            r.checkpoint()

    def test_nonfinite_row_poisoned_before_append(self):
        for i,value in enumerate((math.nan,math.inf,-math.inf)):
            r=self.recorder(str(i));r.calls.append({'x':value})
            with self.assertRaises(ValueError):r.checkpoint()
            with self.assertRaises(ValueError):r.finalize()
            self.assertFalse((r.root/'calls.json').exists())

    def test_existing_evidence_refused_without_overwrite(self):
        for i,name in enumerate(('calls.json','calls.jsonl','calls.json.partial','failed-attempt.json','raw')):
            root=self.root/str(i);root.mkdir();(root/name).write_bytes(b'original')
            with self.assertRaises((RuntimeError,OSError)):h.Recorder(root)
            self.assertEqual((root/name).read_bytes(),b'original')

    def test_existing_target_during_finalize_preserved(self):
        r=self.recorder();self.sample(r);(r.root/'calls.json').write_bytes(b'foreign')
        with self.assertRaisesRegex(RuntimeError,'overwrite'):r.finalize()
        self.assertEqual((r.root/'calls.json').read_bytes(),b'foreign')

    def test_materialization_replace_failure_preserves_temp_and_original_error(self):
        original = ValueError('caller failed first')
        with patch.object(h.os,'replace',side_effect=OSError('replace denied')):
            with self.assertRaises(ValueError) as e:
                with self.recorder() as r:
                    self.sample(r)
                    raise original
        self.assertIs(e.exception,original)
        self.assertIn('replace denied',str(e.exception.__cause__))
        self.assertTrue((r.root/'calls.json.partial').exists())
        self.assertTrue((r.root/'calls.jsonl').exists())
        self.assertFalse((r.root/'calls.json').exists())

    def test_materialization_serialization_failure_no_false_complete(self):
        r=self.recorder();self.sample(r)
        original=json.dumps
        def bad(value,*args,**kwargs):
            if kwargs.get('indent')==2:raise OSError('serialize final failed')
            return original(value,*args,**kwargs)
        with patch.object(h.json,'dumps',bad),self.assertRaisesRegex(OSError,'serialize final'):r.finalize()
        self.assertFalse((r.root/'calls.json').exists())
        self.assertTrue((r.root/'calls.json.partial').exists())

    def test_materialization_short_write_refused(self):
        r=self.recorder();self.sample(r);original=Path.open
        class Short:
            def __init__(self,stream): self.stream=stream
            def __enter__(self): return self
            def write(self,text): return self.stream.write(text[:4])
            def __exit__(self,*args): return self.stream.__exit__(*args)
        def short(path,mode='r',*args,**kwargs):
            stream=original(path,mode,*args,**kwargs)
            return Short(stream) if path.name=='calls.json.partial' and mode=='x' else stream
        with patch.object(Path,'open',short),self.assertRaisesRegex(RuntimeError,'short calls'):r.finalize()
        self.assertEqual((r.root/'calls.json.partial').stat().st_size,4)
        self.assertFalse((r.root/'calls.json').exists())

    def test_nonzero_and_finalize_failure_retains_both(self):
        r=self.recorder()
        with patch.object(h.os,'replace',side_effect=OSError('replace failed')), self.assertRaisesRegex(RuntimeError,'exit 17') as e:
            self.fake(r,code=17)
        self.assertIn('replace failed',str(e.exception.__cause__))
        self.assertEqual(h.read_call_journal(r.root/'calls.jsonl',expected_count=1)[0]['exit_code'],17)

    def test_failed_sidecar_does_not_mask_original(self):
        r=self.recorder();original=Path.open
        def broken(path,mode='r',*args,**kwargs):
            if path.name=='failed-attempt.json':raise OSError('sidecar denied')
            return original(path,mode,*args,**kwargs)
        with patch.object(Path,'open',broken),self.assertRaisesRegex(RuntimeError,'exit 17') as e:
            self.fake(r,code=17)
        self.assertIn('sidecar denied',str(e.exception.__cause__))
        self.assertEqual(self.load(r)[0]['exit_code'],17)

    def test_linear_journal_only_new_row_serialized(self):
        r=self.recorder();old_bytes=0;deltas=[]
        original=json.dumps
        def no_history(value,*args,**kwargs):
            self.assertIsInstance(value,dict)  # No json.dumps(self.calls) during measurement.
            self.assertEqual(set(value),{'sequence','call'})
            return original(value,*args,**kwargs)
        with patch.object(h.json,'dumps',no_history):
            for i in range(100):
                self.sample(r,label='fixed')
                n=(r.root/'calls.jsonl').stat().st_size;deltas.append(n-old_bytes);old_bytes=n
        self.assertLessEqual(max(deltas)-min(deltas),1)
        r.finalize();self.assertEqual(len(self.load(r)),100)

    def test_six_creation_sites_have_lifecycle_and_early_finalize(self):
        found=0
        for name in ('compare_reporting.py','compare_archive.py','compare_release_cumulative.py','diagnose_fixed_binary_warm.py','diagnose_fixed_binary_cold.py'):
            tree=ast.parse((Path(h.__file__).parent/name).read_text())
            for node in ast.walk(tree):
                if not isinstance(node,ast.With):continue
                for item in node.items:
                    f=item.context_expr
                    if isinstance(f,ast.Call) and (getattr(f.func,'id',None)=='Recorder' or getattr(f.func,'attr',None)=='Recorder'):
                        found+=1
                        self.assertEqual(item.optional_vars.id,'recorder')
                        self.assertTrue(any(isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='finalize' for b in node.body for n in ast.walk(b)))
        self.assertEqual(found,6)

    def test_real_python_child_success_and_exit17(self):
        with self.recorder() as r:
            case=h.Fixture('child',self.root,['-c','import sys;print("OUT");print("ERR",file=sys.stderr);sys.exit(17)'],1)
            row=r.run(Path(sys.executable),case,'real',success=False)
        self.assertEqual(row['exit_code'],17)
        self.assertIn(b'OUT',r.human(row,'stdout'));self.assertIn(b'ERR',r.human(row,'stderr'))

    def test_real_python_timeout_keeps_available_output(self):
        r=self.recorder();actual=subprocess.run
        def bounded(*args,**kwargs):
            kwargs['timeout']=.15
            return actual(*args,**kwargs)
        case=h.Fixture('child',self.root,['-c','import time;print("BEFORE_TIMEOUT",flush=True);time.sleep(5)'],1)
        with patch.object(h.subprocess,'run',bounded),self.assertRaisesRegex(RuntimeError,'timed out') as caught:
            r.run(Path(sys.executable),case,'real-timeout')
        # Startup itself may exceed the test deadline. Compare the actual
        # captured bytes, not an assumption that the child reached print().
        cause=caught.exception.__cause__
        self.assertIsInstance(cause,subprocess.TimeoutExpired)
        self.assertEqual((r.root/'raw/timeout-real-timeout.stdout').read_bytes(),cause.stdout or b'')
        self.assertEqual((r.root/'raw/timeout-real-timeout.stderr').read_bytes(),cause.stderr or b'')
        self.assertEqual(self.load(r),[])


    def test_partial_temp_preexisting_is_not_destroyed(self):
        r=self.recorder();self.sample(r);temp=r.root/'calls.json.partial'
        temp.write_bytes(b'preserve earlier partial')
        with self.assertRaises(FileExistsError):r.finalize()
        self.assertEqual(temp.read_bytes(),b'preserve earlier partial')
        self.assertFalse((r.root/'calls.json').exists())

    def test_poisoned_journal_blocks_launch_even_before_finalize(self):
        r=self.recorder();r.calls.append({'x':math.nan})
        with self.assertRaises(ValueError):r.checkpoint()
        with patch.object(h.subprocess,'run') as spawn,self.assertRaisesRegex(RuntimeError,'no further'):
            r.run(Path('mqb.exe'),self.case,'not-run')
        spawn.assert_not_called()

    def test_failure_finalization_precedes_success_marker(self):
        marker=self.root/'passed.txt'
        with patch.object(h.os,'replace',side_effect=OSError('denied')),self.assertRaises(OSError):
            with self.recorder() as r:
                self.sample(r)
                r.finalize()
                marker.write_text('passed')
        self.assertFalse(marker.exists())

    def test_timeout_save_failure_keeps_timeout_and_both_bytes(self):
        r=self.recorder();timeout=subprocess.TimeoutExpired(['mqb'],120,output=b'O',stderr=b'E')
        with patch.object(h.subprocess,'run',side_effect=timeout),patch.object(r,'save_failure',side_effect=OSError('save failed')):
            with self.assertRaisesRegex(RuntimeError,'timed out') as caught:
                r.run(Path('mqb'),self.case,'timeout')
        self.assertIs(caught.exception.__cause__,timeout)
        failure=json.loads((r.root/'failed-attempt.json').read_text())
        self.assertEqual((failure['stdout_hex'],failure['stderr_hex']),('4f','45'))
        self.assertIn('TimeoutExpired',failure['traceback'])
        self.assertIn('save failed',failure['traceback'])

    def test_utf16_journal_refused_not_guessed(self):
        path=self.root/'utf16.jsonl'
        path.write_bytes('{"sequence":0,"call":{}}'.encode('utf-16')+b'\n')
        with self.assertRaises((UnicodeError,ValueError)):
            h.read_call_journal(path,expected_count=1)

    def test_jsonl_prefix_is_unchanged_after_every_append(self):
        r=self.recorder();before=b''
        for i in range(32):
            self.sample(r,label=f'call-{i}')
            after=(r.root/'calls.jsonl').read_bytes()
            self.assertEqual(after[:len(before)],before)
            self.assertEqual(after[len(before):].count(b'\n'),1)
            before=after
        r.finalize()

    @unittest.skipUnless(os.environ.get('MQB_JOURNAL_ORIGINAL_ZIP'), 'original replay ZIP not supplied')
    def test_original_1446_rows_legacy_bytes_and_exact_linear_volume(self):
        path=Path(os.environ['MQB_JOURNAL_ORIGINAL_ZIP'])
        self.assertEqual(h.digest(path.read_bytes()),'d70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530')
        with zipfile.ZipFile(path) as z:
            self.assertIsNone(z.testzip())
            raw=z.read('evidence/calls.json')
        rows=json.loads(raw)
        self.assertEqual(len(rows),1446)
        with self.recorder() as r:
            logical_bytes=0
            for i,row in enumerate(rows):
                r.calls.append(row)
                r.checkpoint()
                logical_bytes+=len((json.dumps({'sequence':i,'call':row},separators=(',',':'),allow_nan=False)+'\n').encode())
                self.assertFalse((r.root/'calls.json').exists())
            self.assertEqual((r.root/'calls.jsonl').stat().st_size,logical_bytes)
        result=(r.root/'calls.json').read_bytes()
        self.assertEqual(result.replace(b'\r\n',b'\n'),raw.replace(b'\r\n',b'\n'))
        if os.name=='nt':self.assertEqual(result,raw)
        self.assertEqual(self.load(r),rows)
        self.assertLess(logical_bytes+len(result),12_000_000)

if __name__=='__main__':
    unittest.main()
