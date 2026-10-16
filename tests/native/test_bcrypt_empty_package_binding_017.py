"""Empty package identity regression; synthetic qualification, real temp bytes.

The dev007 archive records an empty urllib/__init__.py. This is NOT a new
allowlist, loader qualification, target preparation, or native observation.
All child/qualification seams remain the existing explicit fixture doubles.
"""
import copy
from importlib.machinery import ModuleSpec
from pathlib import Path
from types import ModuleType
import unittest
from unittest import mock

import bcrypt_admission_contract_017 as c
import bcrypt_bootstrap_plan_017 as boot
import bcrypt_bound_observation_017 as bound
from bcrypt_import_boundary_017 import describe
from bcrypt_toolchain_017 import Budget, snapshot
from test_bcrypt_bound_observation_017 import Fixture

EMPTY_SHA = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'


class EmptyPackageFixture(Fixture):
    def __init__(self):
        super().__init__()
        package = self.root/'empty_package'; package.mkdir()
        self.ref = self.put('empty_package/__init__.py', b'')
        mod = ModuleType('empty_package')
        mod.__spec__ = ModuleSpec('empty_package', object(), origin=self.ref['path'], is_package=True)
        mod.__spec__.submodule_search_locations = [str(package)]
        mod.__file__, mod.__cached__, mod.__package__ = self.ref['path'], None, 'empty_package'
        mod.__path__, mod.__loader__ = [str(package)], mod.__spec__.loader
        self.modules['empty_package'] = mod
        self.context['modules']['empty_package'] = {
            k: list(v) if type(v) is tuple else v for k, v in describe(mod).items()}
        self.files['empty_package'] = self.ref
        self.seal()


class EmptyPackageBindingTests(unittest.TestCase):
    def setUp(self):
        self.f = EmptyPackageFixture(); self.addCleanup(self.f.close)

    def validate(self):
        self.f.seal()
        with self.f.provenance():
            return c.validate_request(self.f.raw, self.f.sha)

    def test_exact_empty_package_can_be_bound_without_removal(self):
        try: request = self.validate()
        except c.Refused as error: self.fail('Registered empty package refused: '+str(error))
        self.assertEqual(request['context']['files']['empty_package'], self.f.ref)
        self.assertIn('empty_package', request['context']['modules'])
        self.assertEqual(self.f.ref['sha256'], EMPTY_SHA)

    def test_retained_python312_package_record_can_join_the_schema(self):
        # Minimal unmodified projection from dev007 artifact12030386446, NOT a
        # current-host hash or approval. The rest of this request is synthetic.
        path = '/usr/lib/python3.12/urllib/__init__.py'
        cached = '/usr/lib/python3.12/urllib/__pycache__/__init__.cpython-312.pyc'
        del self.f.context['modules']['empty_package']; del self.f.files['empty_package']
        self.f.context['modules']['urllib'] = dict(file=path, origin=path, cached=cached,
            package='urllib', spec_name='urllib', search=['/usr/lib/python3.12/urllib'],
            path=['/usr/lib/python3.12/urllib'])
        self.f.files['urllib_source'] = dict(path=path, bytes=0, sha256=EMPTY_SHA)
        self.f.files['urllib_cache'] = dict(path=cached, bytes=135,
            sha256='26bd006e2ede1f41ad38e11ccf872f3dcb6176df7690fef59cc835337212e6a6')
        try: result = self.validate()
        except c.Refused as error: self.fail('Recorded package cannot be represented: '+str(error))
        self.assertEqual(result['context']['files']['urllib_source']['bytes'], 0)
        self.assertEqual(result['context']['files']['urllib_cache']['bytes'], 135)

    def test_default_identity_still_requires_positive_content(self):
        with self.assertRaises(c.Refused): c.identity(self.f.ref)
        self.assertEqual(c.identity(dict(self.f.ref, bytes=1)), dict(self.f.ref, bytes=1))

    def test_empty_hash_is_exact_not_arbitrary_64_hex(self):
        for digest in ('f'*64, EMPTY_SHA.upper(), '', None):
            with self.subTest(digest=digest):
                self.f.ref['sha256'] = digest
                with self.assertRaises(c.Refused): self.validate()

    def test_zero_is_integer_not_false_and_negative_size_stays_invalid(self):
        for size in (False, True, -1, None, '0'):
            with self.subTest(size=size):
                self.f.ref['bytes'] = size
                with self.assertRaises(c.Refused): self.validate()

    def test_missing_or_extra_identity_fields_refused(self):
        original = dict(self.f.ref)
        for field in ('path', 'bytes', 'sha256', 'extra'):
            self.f.ref.clear(); self.f.ref.update(original)
            if field == 'extra': self.f.ref[field] = True
            else: del self.f.ref[field]
            with self.subTest(field=field), self.assertRaises(c.Refused): self.validate()

    def test_unbound_empty_file_refused(self):
        del self.f.context['modules']['empty_package']
        with self.assertRaises(c.Refused): self.validate()

    def test_package_name_spec_and_locations_must_agree(self):
        row = self.f.context['modules']['empty_package']; original = copy.deepcopy(row)
        changes = [('spec_name', 'other'), ('package', ''), ('origin', 'frozen'),
                   ('origin', None), ('search', None), ('path', None),
                   ('search', [str(self.f.root)]), ('path', [str(self.f.root)]),
                   ('search', [str(self.f.root/'empty_package'), str(self.f.root)])]
        for field, value in changes:
            row.clear(); row.update(copy.deepcopy(original)); row[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(c.Refused): self.validate()

    def test_empty_non_package_py_and_shared_library_refused(self):
        original = copy.deepcopy(self.f.context['modules']['empty_package'])
        for name in ('empty.py', 'empty.so', '__init__.pyc'):
            self.f.ref['path'] = str(self.f.root/'empty_package'/name)
            row = self.f.context['modules']['empty_package']; row.update(copy.deepcopy(original))
            row['file'] = row['origin'] = self.f.ref['path']
            with self.subTest(name=name), self.assertRaises(c.Refused): self.validate()

    def test_empty_package_cannot_also_be_a_program(self):
        original = dict(self.f.context['programs'])
        for role in original:
            self.f.context['programs'].update(original)
            self.f.context['programs'][role] = self.f.ref['path']
            with self.subTest(role=role), self.assertRaises(c.Refused): self.validate()

    def test_fixed_project_source_cannot_be_empty(self):
        for name in c.SOURCES:
            original = dict(self.f.files[name]); self.f.files[name].update(bytes=0, sha256=EMPTY_SHA)
            with self.subTest(name=name), self.assertRaises(c.Refused): self.validate()
            self.f.files[name].clear(); self.f.files[name].update(original)

    def test_main_cannot_claim_the_package_exception(self):
        row = self.f.context['modules'].pop('empty_package')
        row.update(spec_name='__main__', package='__main__')
        self.f.context['modules']['__main__'] = row
        with self.assertRaises(c.Refused): self.validate()

    def test_main_cannot_share_an_approved_empty_package_source(self):
        self.f.context['modules']['__main__'] = dict(self.f.context['modules']['empty_package'])
        self.f.context['modules']['__main__'].update(spec_name=None, origin=None,
                                                   package=None, search=None, path=None)
        with self.assertRaises(c.Refused): self.validate()

    def test_empty_receipts_and_metadata_stay_refused(self):
        for group, key in [('receipts', 'qualification'), ('receipts', 'primary'), ('receipts', 'independent'),
                           ('metadata', 'index')]:
            with self.subTest(group=group, key=key):
                self.f.seal(); request = copy.deepcopy(self.f.request)
                refs = request[group] if group == 'receipts' else request['context'][group]
                refs[key].update(bytes=0, sha256=EMPTY_SHA)
                raw = c.encode(request)
                with self.f.provenance(), self.assertRaises(c.Refused):
                    c.validate_request(raw, c.fingerprint(raw)['sha256'])

    def test_missing_external_qualification_is_not_generated(self):
        with self.assertRaises(c.Refused):
            bound.BoundObservation(self.f.raw, self.f.sha,
                                   metadata=self.f.metadata, receipts=self.f.receipts)

    def test_zero_content_still_consumes_file_pass_and_eof(self):
        budget = Budget(limit=0)
        observed, data = snapshot(self.f.ref['path'], budget, keep=True)
        self.assertEqual({k: observed[k] for k in self.f.ref}, self.f.ref)
        self.assertEqual((data, budget.content_bytes, budget.file_passes, budget.eof_requests), (b'', 0, 1, 1))

    def test_bound_owner_hashes_empty_package_in_both_passes(self):
        try: result = self.f.complete()
        except c.Refused as error: self.fail('Empty package prevented binding: '+str(error))
        self.assertEqual(result['status'], 'owner_complete_unreviewed', result)
        expected = len(self.f.files) + len(self.f.metadata) + len(self.f.receipts)
        for phase in ('before', 'after'):
            self.assertEqual(result['verification'][phase]['files'], expected)
            self.assertEqual(result['verification'][phase]['eof_requests'], expected)
        self.assertFalse(result['observation_accepted'])

    def test_missing_file_before_run_is_not_treated_as_empty(self):
        try: gate = self.f.bound()
        except c.Refused as error: self.fail('Empty package prevented binding: '+str(error))
        Path(self.f.ref['path']).unlink()
        with self.f.execution(): result = c.decode(gate.run(self.f.backend(), b'\x90'*102+b'\xc3', b'\x90'*102+b'\xc3'))
        self.assertEqual(result['status'], 'refused'); self.f.popen.assert_not_called()

    def test_empty_file_growing_between_passes_refuses(self):
        try: result = self.f.complete(lambda: Path(self.f.ref['path']).write_bytes(b'changed'))
        except c.Refused as error: self.fail('Empty package prevented binding: '+str(error))
        self.assertEqual(result['status'], 'refused')
        self.assertEqual(result['verification']['failure'], 'IDENTITY_PASS_REFUSED')
        self.assertIsNone(result['instructions'])

    def test_runtime_pass_keeps_empty_file_no_payload_and_one_eof(self):
        # Exercise the real controller byte pass without manufacturing a plan/authority.
        inputs = object.__new__(boot.BootstrapInputs)
        inputs.request = {'context': self.f.context}
        with mock.patch.dict(boot.os.environ, self.f.context['environment'], clear=True):
            result = inputs.verify_launch_files()
        self.assertEqual(result['files'], len(self.f.files))
        self.assertEqual(result['eof_requests'], len(self.f.files))
        self.assertEqual(result['bytes'], sum(ref['bytes'] for ref in self.f.files.values()))

    def test_package_alias_requires_pinned_target_and_matching_locations(self):
        actual = self.f.ref['path']; alias = str(self.f.root/'alias'/'__init__.py')
        row = self.f.context['modules']['empty_package']
        row.update(file=alias, origin=alias, search=[str(self.f.root/'alias')], path=[str(self.f.root/'alias')])
        self.f.context['aliases'] = [dict(path=alias, target=actual)]
        try: self.validate()
        except c.Refused as error: self.fail('Pinned empty package alias refused: '+str(error))
        self.f.context['aliases'][0]['target'] = str(self.f.root/'unregistered')
        with self.assertRaises(c.Refused): self.validate()

    def test_empty_cached_bytecode_is_not_empty_source(self):
        self.f.files['empty_cache'] = self.f.put('empty_package/empty.pyc', b'')
        self.f.context['modules']['empty_package']['cached'] = self.f.files['empty_cache']['path']
        with self.assertRaises(c.Refused): self.validate()


if __name__ == '__main__': unittest.main(verbosity=2)
