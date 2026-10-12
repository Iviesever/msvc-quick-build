"""Pure bootstrap boundary checks; no original input or child is used."""
import builtins
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

PATH = Path(__file__).with_name('run_bcrypt_disassembly_016.py')


def load_subject(case):
    case.assertTrue(PATH.is_file(), '016 must verify complete source sets before loading')
    spec = importlib.util.spec_from_file_location('bootstrap_test_subject_016', PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.subject = load_subject(self)

    def test_phase_source_sets_are_distinct_and_exclude_old_entrypoints(self):
        selection = self.subject._source_names('selection')
        actual = self.subject._source_names('actual')
        self.assertEqual((len(selection), len(actual)), (6, 7))
        self.assertIn('prepare_bcrypt_selection_016.py', selection)
        self.assertNotIn('bcrypt_disassembly_owner_016.py', selection)
        self.assertIn('bcrypt_disassembly_owner_016.py', actual)
        self.assertNotIn('prepare_bcrypt_selection_016.py', actual)
        for names in (selection, actual):
            self.assertNotIn('bcrypt_disassembly_owner_014.py', names)
            self.assertNotIn('run_bcrypt_disassembly_014.py', names)
            self.assertEqual(names[-1], 'run_bcrypt_disassembly_016.py')

    def test_unknown_phase_is_refused(self):
        with self.assertRaises(ValueError):
            self.subject._source_names('014')

    def test_last_bad_source_prevents_every_module_execution(self):
        names = self.subject._source_names('selection')
        with tempfile.TemporaryDirectory() as directory:
            records = {}
            for name in names:
                p = Path(directory, name)
                raw = b'import builtins\nbuiltins.bootstrap_016_executed = True\n'
                p.write_bytes(raw)
                records[name] = {'path': str(p), 'bytes': len(raw),
                                 'sha256': hashlib.sha256(raw).hexdigest()}
            records[names[-1]]['sha256'] = '0' * 64
            if hasattr(builtins, 'bootstrap_016_executed'):
                del builtins.bootstrap_016_executed
            with patch.object(self.subject, '__file__', str(Path(directory, names[-1]))):
                with self.assertRaises(ValueError):
                    self.subject._load_sources({'phase': 'selection', 'sources': records})
            self.assertFalse(hasattr(builtins, 'bootstrap_016_executed'))

    def test_complete_load_uses_verified_bytes_without_source_reopen(self):
        names = self.subject._source_names('selection')
        with tempfile.TemporaryDirectory() as directory:
            records = {}
            for name in names:
                raw = ('VALUE = ' + repr(name) + '\n').encode()
                p = Path(directory, name)
                p.write_bytes(raw)
                records[name] = {'path': str(p), 'bytes': len(raw),
                                 'sha256': hashlib.sha256(raw).hexdigest()}
            saved = {name[:-3]: sys.modules.get(name[:-3]) for name in names[:-1]}
            try:
                with patch.object(self.subject, '__file__', str(Path(directory, names[-1]))):
                    with patch.object(self.subject, '_read', wraps=self.subject._read) as read:
                        model, phase = self.subject._load_sources(
                            {'phase': 'selection', 'sources': records})
                self.assertEqual(read.call_count, 6)
                self.assertEqual(model.VALUE, 'bcrypt_disassembly_model_016.py')
                self.assertEqual(phase.VALUE, 'prepare_bcrypt_selection_016.py')
            finally:
                for name, old in saved.items():
                    if old is None:
                        sys.modules.pop(name, None)
                    else:
                        sys.modules[name] = old

    def test_source_set_cannot_include_unlisted_member(self):
        with self.assertRaises(ValueError):
            self.subject._load_sources({'phase': 'selection', 'sources': {'extra.py': {}}})

    def test_manifest_duplicate_float_excess_depth_and_size_are_refused(self):
        bad = (b'{"x":1,"x":2}', b'{"x":1.2}', b'{"x":NaN}',
               b'[' * 17 + b'0' + b']' * 17, b' ' * 32769,
               b'{"x":18446744073709551616}')
        for raw in bad:
            with self.subTest(raw=raw[:40]):
                with self.assertRaises(ValueError):
                    self.subject._decode(raw)

    def test_read_rejects_symlink_and_mismatched_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory, 'source.py')
            p.write_bytes(b'x=1\n')
            link = Path(directory, 'alias.py')
            link.symlink_to(p)
            with self.assertRaises(ValueError):
                self.subject._read(str(link), 32768)
            with self.assertRaises(ValueError):
                self.subject._read(str(p), 32768, '0' * 64)

    def test_read_keeps_original_nanosecond_precision(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory, 'source.py')
            raw = b'x=1\n'
            p.write_bytes(raw)
            self.assertEqual(self.subject._read(str(p), len(raw),
                             hashlib.sha256(raw).hexdigest()), raw)

    def test_invalid_interface_is_bounded_refusal(self):
        for argv in ([], ['a'], ['a', 'x'], ['a', '0' * 64, 'extra']):
            code, diagnostic = self.subject.main(argv)
            self.assertEqual(code, 2)
            self.assertEqual(diagnostic, b'BOOTSTRAP_REFUSED\n')

    def test_failed_diagnostic_write_exits_without_traceback(self):
        self.assertTrue(hasattr(self.subject, '_finish'), 'bounded diagnostic write must be guarded')
        with patch.object(self.subject.os, 'write', side_effect=OSError('closed stderr')), \
             patch.object(self.subject.os, '_exit', side_effect=SystemExit) as terminate:
            with self.assertRaises(SystemExit):
                self.subject._finish(1, b'PREPARATION_REFUSED\n')
        terminate.assert_called_once_with(2)


if __name__ == '__main__':
    unittest.main()
