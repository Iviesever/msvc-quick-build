"""Annotation-only dependency regression; no subprocesses or qualified allowlist."""
import ast
import builtins
from collections.abc import Mapping
import hashlib
import inspect
from pathlib import Path
import types
import unittest

import bcrypt_preflight_017 as preflight
from bcrypt_import_boundary_017 import ImportBoundary, Refused, describe
from check_bcrypt_runtime_evidence_017 import observed_modules
from test_bcrypt_import_boundary_017 import fixture

SOURCE = Path(preflight.__file__).read_text(encoding='utf-8')
BASE_RUNTIME_AST = '4adf3d25fc1d185b630bbedb63d328528802b48ac90251702a2e41ed19ed3c68'


def annotation_free(source):
    """Keep all value expressions, constants and guards, ignoring annotation AST."""
    class Strip(ast.NodeTransformer):
        def visit_ImportFrom(self, node):
            return None if node.module in ('typing', 'collections.abc') else node
        def visit_arg(self, node):
            node.annotation = None
            return node
        def visit_FunctionDef(self, node):
            self.generic_visit(node)
            node.returns = None
            return node
        def visit_AnnAssign(self, node):
            return (ast.Assign(targets=[node.target], value=self.visit(node.value))
                    if node.value is not None else None)
    # ast.dump omits empty fields differently in Python 3.12 and 3.13.
    def stable(node):
        if isinstance(node, ast.AST):
            return (type(node).__name__, tuple((key, stable(value))
                    for key, value in ast.iter_fields(node)
                    if value is not None and value != []))
        if isinstance(node, list):
            return tuple(stable(value) for value in node)
        return node
    return repr(stable(Strip().visit(ast.parse(source))))


class AnnotationPreloadTests(unittest.TestCase):
    def test_preflight_never_requests_typing_even_if_already_cached(self):
        requested = []
        def imports(name, *args, **kwargs):
            requested.append(name)
            if name == 'typing' or name.startswith('typing.'):
                raise AssertionError('Annotation-only typing import requested')
            return builtins.__import__(name, *args, **kwargs)
        namespace = {'__builtins__': dict(vars(builtins), __import__=imports)}
        exec(compile(SOURCE, '<preflight import contract>', 'exec'), namespace)
        self.assertEqual(namespace['PINS'], preflight.PINS)
        self.assertIn('collections.abc', requested)
        self.assertNotIn('typing', requested)

    def test_all_non_annotation_logic_matches_retained_baseline(self):
        actual = hashlib.sha256(annotation_free(SOURCE).encode()).hexdigest()
        self.assertEqual(BASE_RUNTIME_AST, actual)

    def test_only_standard_mapping_is_imported_for_annotations(self):
        imports = [n for n in ast.walk(ast.parse(SOURCE)) if isinstance(n, ast.ImportFrom)]
        self.assertNotIn('typing', [n.module for n in imports])
        abc = [n for n in imports if n.module == 'collections.abc']
        self.assertEqual(1, len(abc))
        self.assertEqual(['Mapping'], [a.name for a in abc[0].names])
        self.assertIs(preflight.Mapping, Mapping)

    def test_postponed_function_annotations_resolve_without_typing(self):
        for value in vars(preflight).values():
            if inspect.isfunction(value) and value.__module__ == preflight.__name__:
                with self.subTest(function=value.__name__):
                    self.assertTrue(all(type(x) is str for x in value.__annotations__.values()))
                    inspect.get_annotations(value, eval_str=True)
        self.assertIs(inspect.get_annotations(preflight.same, eval_str=True)['actual'], object)

    def test_collection_records_incompatible_names_instead_of_hiding_them(self):
        for name in ('typing.io', 'typing.re'):
            with self.subTest(name=name):
                unexpected = type('SyntheticNamespace', (), {})
                rows, refs, blockers = observed_modules({name: unexpected})
                self.assertEqual({}, rows)
                self.assertEqual([{'module': name, 'reason': 'MODULE_OBJECT'}], blockers)
                self.assertIs(refs[name][0], unexpected)

    def test_guard_still_refuses_an_incompatible_cached_namespace(self):
        for name in ('typing.io', 'typing.re'):
            with self.subTest(name=name):
                runtime, expected = fixture()
                runtime.modules[name] = type('SyntheticNamespace', (), {})
                guard = ImportBoundary(expected, runtime=runtime)
                with self.assertRaises(Refused):
                    guard.activate()
                self.assertIn(name, runtime.modules)
                self.assertNotIsInstance(runtime.modules[name], types.ModuleType)

    def test_regular_module_contract_remains_strict(self):
        for unexpected in (None, False, object(), type('Namespace', (), {})):
            with self.subTest(type=type(unexpected).__name__), self.assertRaises(Refused):
                describe(unexpected)

    def test_typed_equality_still_rejects_booleans_as_numbers(self):
        for actual, expected in ((True, 1), ({'x': True}, {'x': 1}), ([False], [0])):
            with self.subTest(actual=actual), self.assertRaises(preflight.Refused):
                preflight.same(actual, expected, 'typed')

    def test_invalid_json_remains_refused(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":1e999}', b'[]'):
            with self.subTest(raw=raw), self.assertRaises(preflight.Refused):
                preflight.strict_json(raw)

    def test_preflight_still_rejects_unpinned_inputs_before_parsing(self):
        with self.assertRaises(preflight.Refused):
            preflight.preflight({})
        with self.assertRaises(preflight.Refused):
            preflight.preflight({role: b'{}' for role in preflight.PINS})


if __name__ == '__main__':
    unittest.main(verbosity=2)
