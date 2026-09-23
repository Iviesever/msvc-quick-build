"""Portable admission/clock tests. Native API validation is a separate Windows job."""
import copy, tempfile, unittest
from pathlib import Path
import prepare_noop_windows_review as p

def record():
    call=dict(clock=dict(frequency=10000000,outer_start=200000,native_start=200001,
                        native_end=209999,outer_end=210000),exit_code=0,error=None)
    return dict(calls=[None,call])

class Admission(unittest.TestCase):
    def test_good_bounds(self): self.assertEqual(p.bounds(record()),(100000,310000))
    def test_wrong_frequency(self):
        r=record();r['calls'][1]['clock']['frequency']=1
        with self.assertRaises(ValueError):p.bounds(r)
    def test_bool_clock(self):
        r=record();r['calls'][1]['clock']['outer_start']=True
        with self.assertRaises(ValueError):p.bounds(r)
    def test_clock_reversal(self):
        r=record();r['calls'][1]['clock']['native_end']=210001
        with self.assertRaises(ValueError):p.bounds(r)
    def test_failed_call(self):
        r=record();r['calls'][1]['exit_code']=1
        with self.assertRaises(ValueError):p.bounds(r)
    def test_missing_measured_call(self):
        with self.assertRaises(ValueError):p.bounds(dict(calls=[None]))
    def test_wrong_archive_refused_before_output(self):
        with tempfile.TemporaryDirectory() as d:
            base=Path(d);a=base/'wrong.zip';a.write_bytes(b'not the pinned artifact')
            with self.assertRaises(ValueError):p.prepare(a,base/'work',base/'out')
            self.assertFalse((base/'work').exists());self.assertFalse((base/'out').exists())
    def test_marker_export_is_independent_of_measured_interval(self):
        s=Path(__file__).with_name('read_noop_windows_etl.cs').read_text()
        self.assertIn('if(!Select(r.Time,lo,hi,r.Provider,r.Opcode))return;',s)
        self.assertIn('ce1dbfb4-137e-4da6-87b0-3f59aa102cbc',s)
        for name in ('"TTID"','"Flag"','"ExitStatus"'): self.assertIn(name,s)
    def test_reader_is_file_only(self):
        s=Path(__file__).with_name('read_noop_windows_etl.cs').read_text()
        self.assertIn('Mode=0x10001000',s)
        for symbol in ('StartTrace(', 'ControlTrace(', 'Process.Start(', 'wpr.exe', 'mqb.exe'):
            self.assertNotIn(symbol,s)
        self.assertIn('TdhGetEventInformation',s);self.assertIn('payload_sha256',s)

if __name__=='__main__':unittest.main(verbosity=2)
