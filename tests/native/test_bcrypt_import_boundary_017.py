"""Synthetic module/cache/finder controls; no host inventory, loader or decoder."""
import copy
import importlib
from importlib.machinery import ModuleSpec
import sys
from types import ModuleType, SimpleNamespace
import unittest

import bcrypt_import_boundary_017 as b


def module(name='fixture', origin='/synthetic/fixture.py'):
    value = ModuleType(name)
    value.__spec__ = ModuleSpec(name, object(), origin=origin)
    value.__loader__ = value.__spec__.loader
    value.__file__ = origin
    value.__cached__ = '/synthetic/fixture.pyc'
    value.__package__ = ''
    return value


def fixture():
    value = module()
    runtime = SimpleNamespace(modules={'fixture': value}, meta_path=[object()])
    expected = {'fixture': b.describe(value)}
    return runtime, expected


class ImportBoundaryTests(unittest.TestCase):
    def test_complete_synthetic_lifecycle_never_qualifies_loading(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime)
        original = tuple(runtime.meta_path)
        guard.activate(); guard.checkpoint(); result = guard.close()
        self.assertEqual(result['status'], 'closed_unchanged')
        self.assertEqual(tuple(runtime.meta_path), original)
        for name in ('execution_admitted', 'source_independently_reviewed', 'runtime_loader_verified',
                     'python_module_bytes_verified', 'python_stdlib_and_dynamic_imports_qualified'):
            self.assertIs(result[name], False)

    def test_no_side_effect_at_construction(self):
        runtime, expected = fixture(); original = runtime.meta_path
        guard = b.ImportBoundary(expected, runtime=runtime)
        self.assertIs(runtime.meta_path, original); self.assertEqual(guard.phase, 'new')

    def test_expected_metadata_is_owned_not_mutable_by_caller(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime)
        expected['fixture']['file'] = '/changed'
        guard.activate(); self.assertEqual(guard.close()['status'], 'closed_unchanged')

    def test_name_and_module_count_limits(self):
        _, expected = fixture(); row = expected['fixture']
        for value in ({}, {'x'+str(i): row for i in range(b.MODULE_CAP+1)},
                      {'../unsafe': row}, {'': row}, {'x..y': row}, {'x'*257: row}, {True: row}):
            with self.subTest(case=list(value)[:1]), self.assertRaises(b.Refused):
                b.ImportBoundary(value)

    def test_exact_count_boundary(self):
        row = b.describe(module())
        self.assertEqual(len(b.manifest({'m'+str(i): row for i in range(b.MODULE_CAP)})), b.MODULE_CAP)

    def test_metadata_fields_types_and_paths(self):
        _, expected = fixture()
        for key, value in [('file', 'relative'), ('file', True), ('cached', '/bad\npath'),
                           ('origin', 'some-loader'), ('origin', '/'+('x'*b.TEXT_CAP)),
                           ('path', ['/ok']*(b.PATH_CAP+1)), ('path', ['relative']),
                           ('path', ('/ok', False)), ('spec_name', object())]:
            with self.subTest(key=key, value_type=type(value).__name__):
                changed = copy.deepcopy(expected); changed['fixture'][key] = value
                with self.assertRaises(b.Refused): b.ImportBoundary(changed)
        for alteration in ('extra', 'missing'):
            changed = copy.deepcopy(expected)
            if alteration == 'extra': changed['fixture']['authority'] = True
            else: del changed['fixture']['origin']
            with self.assertRaises(b.Refused): b.ImportBoundary(changed)

    def test_aggregate_text_cap(self):
        row = b.describe(module()); row['path'] = ['/'+('x'*4095)] * b.PATH_CAP
        with self.assertRaisesRegex(b.Refused, 'TOTAL_TEXT'):
            b.manifest({'a': row, 'b': row})

    def test_namespace_custom_spec_and_initializing_refused(self):
        for mode in ('namespace', 'spec', 'initializing', 'none', 'object'):
            with self.subTest(mode=mode):
                value = module()
                if mode == 'namespace': value.__spec__.origin = None
                elif mode == 'spec': value.__spec__ = SimpleNamespace(origin='/x')
                elif mode == 'initializing': value.__spec__._initializing = True
                elif mode == 'none': value.__spec__ = None
                else: value = SimpleNamespace()
                with self.assertRaises(b.Refused): b.describe(value)

    def test_main_without_spec_requires_explicit_source(self):
        value = ModuleType('__main__'); value.__spec__ = None
        with self.assertRaises(b.Refused): b.describe(value)
        value.__file__ = '/synthetic/owner.py'
        self.assertIsNone(b.describe(value)['spec_name'])

    def test_builtins_frozen_and_explicit_cache_aliases(self):
        for origin in ('built-in', 'frozen'):
            value = module(origin=origin); del value.__file__; del value.__cached__
            runtime = SimpleNamespace(modules={'one': value, 'two': value}, meta_path=[])
            expected = {name: b.describe(value) for name in runtime.modules}
            guard = b.ImportBoundary(expected, runtime=runtime).activate()
            self.assertEqual(guard.close()['status'], 'closed_unchanged')

    def test_missing_or_extra_preloaded_modules_rejected(self):
        for mode in ('missing', 'extra', 'none'):
            runtime, expected = fixture()
            if mode == 'missing': runtime.modules.clear()
            elif mode == 'extra': runtime.modules['late'] = module('late')
            else: runtime.modules['fixture'] = None
            guard = b.ImportBoundary(expected, runtime=runtime)
            with self.assertRaises(b.Refused): guard.activate()
            self.assertEqual(guard.close()['status'], 'refused')

    def test_module_set_and_mapping_drift(self):
        for mode in ('extra', 'missing', 'map', 'value'):
            runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
            if mode == 'extra': runtime.modules['late'] = module('late')
            elif mode == 'missing': del runtime.modules['fixture']
            elif mode == 'map': runtime.modules = dict(runtime.modules)
            else: runtime.modules['fixture'] = module()
            with self.assertRaises(b.Refused): guard.checkpoint()
            self.assertEqual(guard.close()['status'], 'refused')

    def test_spec_and_loader_identity_drift(self):
        for mode in ('spec', 'spec-loader', 'module-loader'):
            runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
            value = runtime.modules['fixture']
            if mode == 'spec': value.__spec__ = copy.copy(value.__spec__)
            elif mode == 'spec-loader': value.__spec__.loader = object()
            else: value.__loader__ = object()
            with self.assertRaisesRegex(b.Refused, 'IDENTITY_CHANGED'): guard.checkpoint()
            self.assertEqual(guard.close()['status'], 'refused')

    def test_package_path_change(self):
        runtime, _ = fixture(); value = runtime.modules['fixture']
        value.__path__ = ['/synthetic/pkg']; value.__spec__.submodule_search_locations = ['/synthetic/pkg']
        guard = b.ImportBoundary({'fixture': b.describe(value)}, runtime=runtime).activate()
        value.__path__.append('/new')
        with self.assertRaises(b.Refused): guard.checkpoint()
        guard.close()

    def test_source_metadata_drift(self):
        for attr in ('__file__', '__cached__', '__package__'):
            runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
            setattr(runtime.modules['fixture'], attr, '/changed')
            with self.assertRaises(b.Refused): guard.checkpoint()
            guard.close()

    def test_finder_identity_order_and_replacement_checked(self):
        for mode in ('replace-list', 'remove', 'prepend', 'replace-finder'):
            runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
            if mode == 'replace-list': runtime.meta_path = list(runtime.meta_path)
            elif mode == 'remove': runtime.meta_path.remove(guard)
            elif mode == 'prepend': runtime.meta_path.insert(0, object())
            else: runtime.meta_path[1] = object()
            with self.assertRaisesRegex(b.Refused, 'FINDER_DRIFT'): guard.checkpoint()
            others = [f for f in runtime.meta_path if f is not guard]
            self.assertEqual(guard.close()['status'], 'refused')
            self.assertEqual(runtime.meta_path, others)

    def test_finder_count_limit_before_installation(self):
        runtime, expected = fixture(); runtime.meta_path = [object()] * (b.FINDER_CAP+1)
        guard = b.ImportBoundary(expected, runtime=runtime)
        with self.assertRaises(b.Refused): guard.activate()
        self.assertNotIn(guard, runtime.meta_path)

    def test_unknown_or_known_resolution_never_delegates(self):
        for name in ('new_module', 'fixture'):
            runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
            with self.assertRaisesRegex(b.Refused, 'LATE_IMPORT_OR_RELOAD'):
                guard.find_spec(name, None, runtime.modules.get(name))
            self.assertEqual(guard.close()['status'], 'refused')

    def test_caught_import_refusal_cannot_be_cleared_by_unchanged_cache(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        try: guard.find_spec('late')
        except b.Refused: pass
        with self.assertRaises(b.Refused): guard.checkpoint()
        self.assertEqual(guard.close()['reason'], 'LATE_IMPORT_OR_RELOAD')

    def test_caught_cache_refusal_remains_after_repair(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        runtime.modules['extra'] = module('extra')
        with self.assertRaises(b.Refused): guard.checkpoint()
        del runtime.modules['extra']
        self.assertEqual(guard.close()['status'], 'refused')

    def test_one_use_and_close_before_activation(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime)
        self.assertEqual(guard.close()['status'], 'refused')
        with self.assertRaises(b.Refused): guard.activate()
        with self.assertRaises(b.Refused): guard.close()
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        with self.assertRaises(b.Refused): guard.activate()
        self.assertEqual(guard.close()['status'], 'refused')

    def test_unobserved_cache_change_is_not_claimed_detected(self):
        # Honest boundary control: this is not a continuous or hostile-code monitor.
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        runtime.modules['temporary'] = module('temporary'); del runtime.modules['temporary']
        self.assertEqual(guard.close()['status'], 'closed_unchanged')

    def test_oversized_cleanup_is_explicitly_unconfirmed(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        runtime.meta_path.extend(object() for _ in range(b.FINDER_CAP+1))
        result = guard.close()
        self.assertEqual(result['status'], 'refused')
        self.assertIs(result['finder_detached'], False)
        self.assertIn(guard, runtime.meta_path)  # Caller must terminate isolated owner, not continue.

    def test_cleanup_never_restores_over_other_finders(self):
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        replacement = object(); runtime.meta_path = [replacement, guard]
        result = guard.close()
        self.assertEqual(runtime.meta_path, [replacement])
        self.assertIs(result['finder_detached'], True)
        self.assertEqual(result['status'], 'refused')

    def test_real_protocol_restores_test_interpreter(self):
        original = sys.meta_path
        self.real_import_control()
        self.assertIs(sys.meta_path, original)
        self.assertNotIn('_mqb_017_only_synthetic_module', sys.modules)

    def real_import_control(self, cached=False, reload=False):
        name = '_mqb_017_only_synthetic_module'
        self.assertNotIn(name, sys.modules)
        runtime, expected = fixture(); guard = b.ImportBoundary(expected, runtime=runtime).activate()
        original = sys.meta_path
        sys.meta_path = [guard, *original]
        try:
            if cached or reload:
                value = module(name); sys.modules[name] = value
            if reload:
                with self.assertRaises(b.Refused): importlib.reload(value)
            elif cached:
                self.assertIs(importlib.import_module(name), value)
                self.assertIsNone(guard.reason)  # Real cache bypass: checkpoints are necessary.
            else:
                with self.assertRaises(b.Refused): importlib.import_module(name)
        finally:
            sys.modules.pop(name, None); sys.meta_path = original
            result = guard.close()
        return result

    def test_actual_import_protocol_blocks_unknown_without_loading(self):
        self.assertEqual(self.real_import_control()['status'], 'refused')

    def test_actual_reload_is_refused(self):
        self.assertEqual(self.real_import_control(reload=True)['status'], 'refused')

    def test_actual_cached_import_bypasses_finder_as_documented(self):
        self.assertEqual(self.real_import_control(cached=True)['status'], 'closed_unchanged')


if __name__ == '__main__': unittest.main(verbosity=2)
