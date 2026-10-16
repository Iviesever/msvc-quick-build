"""Exact-entry inspection contracts; no real child or qualification generation."""
import ast
from contextlib import contextmanager
import copy
import hashlib
from importlib.machinery import ModuleSpec
from pathlib import Path
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

import bcrypt_bootstrap_plan_017 as p
import bcrypt_fixed_bootstrap_017 as owner
import bcrypt_observation_controller_017 as controller


@contextmanager
def fixture(role='owner'):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp).resolve()
        entry = root / (p.BOOTSTRAP_NAME if role == 'owner' else p.CONTROLLER_NAME)
        entry.write_bytes(b'# Synthetic entry bytes, not executed\n')
        python = root / 'python'; python.write_bytes(b'Synthetic interpreter, not executed')
        package = root / 'pkg'; package.mkdir()
        init = package / '__init__.py'; init.write_bytes(b'')
        main = ModuleType('__main__'); main.__file__ = str(entry)
        main.__spec__ = None; main.__package__ = None
        mod = ModuleType('pkg'); mod.__file__ = str(init); mod.__package__ = 'pkg'
        mod.__path__ = [str(package)]
        mod.__spec__ = ModuleSpec('pkg', None, origin=str(init), is_package=True)
        mod.__spec__.submodule_search_locations = [str(package)]
        mod.__cached__ = str(package / '__pycache__/__init__.cpython-312.pyc')
        runtime = SimpleNamespace(platform='linux', flags=SimpleNamespace(
            isolated=1, no_site=1, optimize=0), dont_write_bytecode=True,
            modules={'__main__': main, 'pkg': mod}, executable=str(python), version='synthetic')
        with patch.object(p, 'sys', runtime), patch.object(p, '_single_native_thread') as threads:
            yield root, entry, runtime, threads


class EntryInventoryTests(unittest.TestCase):
    def test_exact_main_and_empty_package_are_recorded_not_authorized(self):
        for role in ('owner', 'controller'):
            with self.subTest(role=role), fixture(role) as (root, entry, runtime, threads):
                raw = p.record_entry_preload(role, str(entry)); r = p.decode(raw)
                self.assertEqual(r['modules']['__main__']['file'], str(entry))
                self.assertIsNone(r['modules']['__main__']['spec_name'])
                self.assertEqual(r['status'], 'ENTRY_PRELOAD_RECORDED_NOT_QUALIFIED')
                self.assertEqual(r['phase'], 'before_request_read')
                self.assertEqual(r['role'], role)
                self.assertEqual(r['reads']['passes'], 6)
                self.assertEqual(r['reads']['eof_requests'], 6)
                self.assertEqual(r['reads']['content_bytes'], 2*sum(x['bytes'] for x in r['files'].values()))
                empty = r['files'][str(root/'pkg/__init__.py')]
                self.assertEqual(empty['bytes'], 0)
                self.assertEqual(empty['sha256'], hashlib.sha256(b'').hexdigest())
                self.assertEqual(threads.call_count, 2)
                for key in ('execution_admitted', 'source_independently_reviewed', 'runtime_loader_verified',
                            'writer_domain_qualified', 'expected_modules_generated', 'full_execution_closure_verified'):
                    self.assertIs(r[key], False)
                for key in ('request_reads', 'target_reads', 'decoder_processes', 'formal_observations'):
                    self.assertEqual(r[key], 0)

    def test_module_cache_and_objects_are_never_replaced_or_filtered(self):
        with fixture() as (_, entry, runtime, _):
            mapping = runtime.modules; objects = dict(mapping)
            p.record_entry_preload('owner', str(entry))
            self.assertIs(runtime.modules, mapping)
            self.assertEqual(runtime.modules, objects)
            self.assertTrue(all(runtime.modules[k] is v for k, v in objects.items()))

    def test_refuses_collector_or_imported_entry_as_main(self):
        with fixture() as (_, entry, runtime, _):
            runtime.modules['__main__'].__file__ = '/synthetic/collector.py'
            with self.assertRaisesRegex(p.Refused, 'ENTRY_MAIN'):
                p.record_entry_preload('owner', str(entry))
        with fixture() as (_, entry, runtime, _):
            runtime.modules['__main__'].__spec__ = ModuleSpec('__main__', None, origin=str(entry))
            with self.assertRaisesRegex(p.Refused, 'ENTRY_MAIN'):
                p.record_entry_preload('owner', str(entry))

    def test_bad_role_path_and_isolation_never_read_files(self):
        for mutation in ('role', 'source', 'isolated', 'no_site', 'optimize', 'bytecode', 'platform'):
            with self.subTest(mutation=mutation), fixture() as (_, entry, runtime, _):
                role = 'owner'; source = str(entry)
                if mutation == 'role': role = 'other'
                elif mutation == 'source': source = '/wrong/name.py'
                elif mutation in ('isolated', 'no_site'): setattr(runtime.flags, mutation, 0)
                elif mutation == 'optimize': runtime.flags.optimize = 1
                elif mutation == 'bytecode': runtime.dont_write_bytecode = False
                else: runtime.platform = 'win32'
                with patch.object(p, 'snapshot') as read, self.assertRaises(p.Refused):
                    p.record_entry_preload(role, source)
                read.assert_not_called()

    def test_foreign_source_link_refused(self):
        with fixture() as (root, entry, _, _):
            alias = root/'elsewhere'; alias.mkdir()
            linked = alias/p.BOOTSTRAP_NAME; linked.symlink_to(entry)
            with self.assertRaisesRegex(p.Refused, 'ENTRY_SOURCE'):
                p.record_entry_preload('owner', str(linked))

    def test_incompatible_module_is_refused_not_hidden(self):
        for value in (None, False, object(), type('Namespace', (), {})):
            with fixture() as (_, entry, runtime, _):
                runtime.modules['typing.io'] = value
                with self.assertRaisesRegex(p.Refused, 'MODULE_OBJECT'):
                    p.record_entry_preload('owner', str(entry))
                self.assertIs(runtime.modules['typing.io'], value)

    def test_module_count_bound_precedes_copy_and_reads(self):
        with fixture() as (_, entry, runtime, _):
            runtime.modules.update({f'm{i}': None for i in range(256)})
            with patch.object(p, 'snapshot') as read, self.assertRaisesRegex(p.Refused, 'ENTRY_MODULE_COUNT'):
                p.record_entry_preload('owner', str(entry))
            read.assert_not_called()

    def test_module_drift_after_file_read_is_refused(self):
        for mutation in ('add', 'replace', 'metadata', 'mapping'):
            with self.subTest(mutation=mutation), fixture() as (_, entry, runtime, _):
                original = p.snapshot
                def changed(*args, **kwargs):
                    result = original(*args, **kwargs)
                    if mutation == 'add': runtime.modules['new'] = ModuleType('new')
                    elif mutation == 'replace': runtime.modules['pkg'] = ModuleType('pkg')
                    elif mutation == 'metadata': runtime.modules['pkg'].__package__ = 'changed'
                    else: runtime.modules = dict(runtime.modules)
                    return result
                with patch.object(p, 'snapshot', side_effect=changed), self.assertRaises(p.Refused):
                    p.record_entry_preload('owner', str(entry))

    def test_file_growth_between_passes_is_refused(self):
        with fixture() as (root, entry, _, _):
            original = p.snapshot; count = 0
            def changed(*args, **kwargs):
                nonlocal count
                count += 1
                if count == 4: (root/'pkg/__init__.py').write_bytes(b'new')
                return original(*args, **kwargs)
            with patch.object(p, 'snapshot', side_effect=changed), self.assertRaisesRegex(p.Refused, 'ENTRY_FILE_DRIFT'):
                p.record_entry_preload('owner', str(entry))

    def test_missing_file_is_not_zero_byte_evidence(self):
        with fixture() as (root, entry, _, _):
            (root/'pkg/__init__.py').unlink()
            with self.assertRaises(OSError): p.record_entry_preload('owner', str(entry))

    def test_module_path_outside_fixed_roots_is_rejected(self):
        with fixture() as (_, entry, runtime, _):
            runtime.modules['pkg'].__file__ = '/etc/private'
            with patch.object(p, 'snapshot') as read, self.assertRaisesRegex(p.Refused, 'ENTRY_FILE_ROOT'):
                p.record_entry_preload('owner', str(entry))
            read.assert_not_called()

    def test_absent_cached_file_appearing_is_refused(self):
        with fixture() as (_, entry, runtime, _):
            path = Path(runtime.modules['pkg'].__cached__)
            original = p.snapshot
            def appeared(*args, **kwargs):
                result = original(*args, **kwargs)
                path.parent.mkdir(exist_ok=True); path.write_bytes(b'changed')
                return result
            with patch.object(p, 'snapshot', side_effect=appeared), self.assertRaisesRegex(p.Refused, 'ENTRY_CACHE_DRIFT'):
                p.record_entry_preload('owner', str(entry))

    def test_second_pass_shares_first_deadline_and_counts_empty_file(self):
        with fixture() as (_, entry, _, _):
            original = p.snapshot; deadlines = []
            def record(path, budget):
                deadlines.append(budget.deadline)
                return original(path, budget)
            with patch.object(p, 'snapshot', side_effect=record): p.record_entry_preload('owner', str(entry))
            self.assertEqual(len(deadlines), 6)
            self.assertEqual(len(set(deadlines)), 1)

    def test_thread_precondition_failure_performs_no_snapshot(self):
        with fixture() as (_, entry, _, threads):
            threads.side_effect = p.Refused('PREPARATION')
            with patch.object(p, 'snapshot') as read, self.assertRaises(p.Refused):
                p.record_entry_preload('owner', str(entry))
            read.assert_not_called()

    def test_inspection_dispatch_never_constructs_inputs_or_observation(self):
        for mod, role in ((owner, 'owner'), (controller, 'controller')):
            with self.subTest(role=role), patch.object(mod, 'record_entry_preload', return_value=b'evidence\n') as inspect, \
                 patch.object(mod, 'BootstrapInputs') as inputs, patch.object(mod.os, 'write', return_value=9):
                self.assertEqual(mod.main([mod.__file__, '--inspect-preload']), 0)
                inputs.assert_not_called(); inspect.assert_called_once_with(role, mod.__file__)

    def test_inspection_error_and_partial_stdout_are_not_success(self):
        for mod in (owner, controller):
            with patch.object(mod, 'record_entry_preload', side_effect=p.Refused('SYNTHETIC')), \
                 patch.object(mod.os, 'write'):
                self.assertEqual(mod.main([mod.__file__, '--inspect-preload']), 1)
            with patch.object(mod, 'record_entry_preload', return_value=b'abc'), patch.object(mod.os, 'write', return_value=1):
                self.assertEqual(mod.main([mod.__file__, '--inspect-preload']), 1)

    def test_extra_arguments_cannot_turn_inspection_into_observation(self):
        for mod in (owner, controller):
            for suffix in (['--other'], ['--inspect-preload', 'fake-plan'], ['--inspect-preload', 'x', 'y']):
                with patch.object(mod, 'BootstrapInputs') as inputs, patch.object(mod, 'record_entry_preload') as inspect, \
                     patch.object(mod.os, 'write'):
                    self.assertEqual(mod.main([mod.__file__, *suffix]), 1)
                    inputs.assert_not_called(); inspect.assert_not_called()

    def test_owner_preload_returns_existing_exact_dependencies(self):
        from bcrypt_disassembly_io_014 import OutputDirectory
        from bcrypt_disassembly_listing_014 import parse_listing
        self.assertEqual(owner.preload_owner(), (OutputDirectory, parse_listing))

    def test_inspection_has_no_dynamic_import_or_execution_path(self):
        tree = ast.parse(Path(p.__file__).read_text())
        func = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'record_entry_preload')
        self.assertFalse(any(isinstance(n, (ast.Import, ast.ImportFrom)) for n in ast.walk(func)))
        text = ast.unparse(func)
        for forbidden in ('Popen(', 'BootstrapInputs(', 'BoundObservation(', 'ImportBoundary(', '__import__(', 'eval(', 'exec('):
            self.assertNotIn(forbidden, text)
        self.assertIn("'before_request_read'", text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
