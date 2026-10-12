"""Pure bootstrap boundary tests; no child tools or image execution."""
import importlib
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

bootstrap = importlib.import_module('run_bcrypt_disassembly_014')


class BootstrapTests(unittest.TestCase):
    def test_missing_arguments_have_fixed_diagnostic(self):
        code, diagnostic = bootstrap.main([])
        self.assertEqual(code, 2)
        self.assertEqual(diagnostic, b'BOOTSTRAP_REFUSED\n')

    def test_invalid_digest_does_not_read_manifest(self):
        self.assertEqual(bootstrap.main(['/does/not/exist', 'bad']),
                         (2, b'BOOTSTRAP_REFUSED\n'))

    def test_decoder_refuses_deep_input_before_json_loads(self):
        with mock.patch.object(bootstrap.json, 'loads', side_effect=AssertionError('too early')):
            with self.assertRaises(ValueError):
                bootstrap._decode(b'[' * 17 + b'0' + b']' * 17)

    def test_decoder_rejects_duplicate_and_nonfinite_numbers(self):
        for raw in (b'{"x":1,"x":2}', b'NaN', b'1.0', b'1e100', b'1' * 21,
                    b'"\xff"', b'{]'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                bootstrap._decode(raw)

    def test_bounded_regular_read_and_identity(self):
        with tempfile.TemporaryDirectory(prefix='bcrypt014-bootstrap-') as td:
            path = Path(td).resolve() / 'input'
            path.write_bytes(b'abc')
            self.assertEqual(bootstrap._read(str(path), 3, hashlib.sha256(b'abc').hexdigest()), b'abc')
            for cap, digest in ((2, None), (3, '0' * 64)):
                with self.assertRaises(ValueError):
                    bootstrap._read(str(path), cap, digest)
            link = path.with_name('link')
            link.symlink_to(path)
            with self.assertRaises(ValueError):
                bootstrap._read(str(link), 3)
            fifo = path.with_name('fifo')
            os.mkfifo(fifo)
            with self.assertRaises(ValueError):
                bootstrap._read(str(fifo), 3)

    def test_last_source_hash_mismatch_prevents_any_module_execution(self):
        with tempfile.TemporaryDirectory(prefix='bcrypt014-bootstrap-') as td:
            root = Path(td).resolve()
            records = {}
            for name in bootstrap.SOURCE_NAMES:
                path = root / name
                raw = b'raise AssertionError("must not execute")\n'
                path.write_bytes(raw)
                records[name] = {'path': str(path), 'bytes': len(raw),
                                 'sha256': hashlib.sha256(raw).hexdigest()}
            records[bootstrap.SOURCE_NAMES[-1]]['sha256'] = '0' * 64
            with mock.patch.object(bootstrap, '__file__', str(root / bootstrap.SOURCE_NAMES[-1])), \
                 mock.patch('builtins.compile', side_effect=AssertionError('compiled before full verification')):
                with self.assertRaises(ValueError):
                    bootstrap._load_sources({'sources': records})

    def test_all_source_bytes_verified_then_compile_exact_bytes_in_order(self):
        with tempfile.TemporaryDirectory(prefix='bcrypt014-bootstrap-') as td:
            root = Path(td).resolve()
            records, expected, seen = {}, {}, []
            names = [name[:-3] for name in bootstrap.SOURCE_NAMES[:-1]]
            saved = {name: bootstrap.sys.modules.get(name) for name in names}
            for number, name in enumerate(bootstrap.SOURCE_NAMES):
                path = root / name
                raw = f'VALUE = {number}\n'.encode()
                path.write_bytes(raw)
                expected[str(path)] = raw
                records[name] = {'path': str(path), 'bytes': len(raw),
                                 'sha256': hashlib.sha256(raw).hexdigest()}
            original = compile
            def check_compile(raw, path, mode):
                seen.append(path)
                self.assertEqual(raw, expected[path])
                # Later files are changed after verification; loading must use retained bytes.
                for other in root.iterdir():
                    other.write_bytes(b'raise AssertionError("reopened after verification")\n')
                return original(raw, path, mode)
            try:
                with mock.patch.object(bootstrap, '__file__', str(root / bootstrap.SOURCE_NAMES[-1])), \
                     mock.patch('builtins.compile', side_effect=check_compile):
                    model, owner = bootstrap._load_sources({'sources': records})
                self.assertEqual(model.VALUE, 0)
                self.assertEqual(owner.VALUE, 3)
                self.assertEqual(seen, [str(root / name) for name in bootstrap.SOURCE_NAMES[:-1]])
            finally:
                for name, value in saved.items():
                    if value is None:
                        bootstrap.sys.modules.pop(name, None)
                    else:
                        bootstrap.sys.modules[name] = value


if __name__ == '__main__':
    unittest.main()
