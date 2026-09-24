"""Portable negative controls only; never run the original executables."""
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipInfo
import prepare_np001_native_review as p

class Controls(unittest.TestCase):
    def test_fixed_reader_is_unchanged(self):
        src=Path(__file__).with_name('read_noop_windows_etl.cs').read_bytes()
        self.assertEqual(p.READER_SHA, p.policy.e.sha(p.policy.e.canonical(src)))

    def test_fixed_archive_identity(self):
        self.assertEqual(p.RUN,'35994887542'); self.assertEqual(p.SIZE,19935212)
        self.assertEqual(p.COMMIT,'163d0a727549a00c526d4dad5ad9bd6a1e34efd4')

    def test_wrong_archive_creates_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); path=root/'wrong.zip'; path.write_bytes(b'wrong')
            with self.assertRaises(ValueError): p.prepare(path,root/'work',root/'out')
            self.assertFalse((root/'work').exists()); self.assertFalse((root/'out').exists())

    def test_normal_members(self):
        self.assertEqual({'evidence/traces/1-P-baseline/trace.etl'},
            p.safe_members([ZipInfo('evidence/traces/1-P-baseline/trace.etl')]))

    def test_path_escape_or_directory(self):
        for name in ('../escape','/root','a/../x','C:/drive','a\\b','directory/'):
            with self.subTest(name=name),self.assertRaises(ValueError):p.safe_members([ZipInfo(name)])

    def test_normalized_or_nul_truncated_name(self):
        i=ZipInfo('a/b'); i.orig_filename='a\\b'
        with self.assertRaises(ValueError):p.safe_members([i])
        with self.assertRaises(ValueError):p.safe_members([ZipInfo('a\x00tail')])

    def test_duplicate(self):
        with self.assertRaises(ValueError):p.safe_members([ZipInfo('a'),ZipInfo('a')])

    def test_symlink(self):
        i=ZipInfo('link');i.external_attr=0o120777 << 16
        with self.assertRaises(ValueError):p.safe_members([i])

    def test_no_capture_trigger_or_command(self):
        wf=Path(__file__).resolve().parents[2]/'.github/workflows/np001-native-review.yml'
        text=wf.read_text()
        self.assertIn('pull_request:',text);self.assertIn('persist-credentials: false',text)
        for value in ('workflow_dispatch:', '  push:', 'wpr.exe', 'Start-Process', ' -ExecuteReviewedPolicy'):
            self.assertNotIn(value,text)
        self.assertIn('10804949205',text);self.assertIn(p.RUN,text)

if __name__=='__main__':unittest.main(verbosity=2)
