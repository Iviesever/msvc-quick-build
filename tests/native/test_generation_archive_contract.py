"""Actual archive registration and exact historical-source refusal controls.

No MQB, MSVC, ETW or benchmark is executed. The new native program exercises
the product codec; these contracts preserve the older assertions and inventory.
"""
import ast
import copy
import importlib
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
TU = 'cpp/src/orchestration/incremental/ArtifactGenerationArchive.cpp'
NATIVE = 'cpp/tests/orchestration/incremental/artifact_generation_archive_tests.cpp'
PUBLIC = 'cpp/include/mqb/orchestration/ArtifactGenerationArchive.hpp'
PRIVATE = 'cpp/src/orchestration/incremental/ArtifactGenerationArchiveInternal.hpp'
HELPER = 'cpp/tests/e2e/TargetWaveCacheEvidenceChecks.hpp'
MANIFEST = 'cpp/mqb.json'
DRIVER = 'tests/native/run_native_tests.ps1'
LAYOUT = 'tests/native/assert_cpp_layout.ps1'


def read(path):
    return (ROOT/path).read_text(encoding='utf-8')


def source_map():
    return {p.relative_to(ROOT).as_posix(): p.read_text(encoding='utf-8')
            for folder in ('cpp', 'tests/native') for p in (ROOT/folder).rglob('*')
            if p.is_file() and p.suffix in ('.cpp', '.hpp', '.ps1', '.py', '.json')}


class GenerationArchiveContracts(unittest.TestCase):
    def extension(self):
        self.assertTrue((ROOT/'tests/native/generation_archive_extension.py').is_file(),
                        'the exact archive historical-source adapter is required')
        return importlib.import_module('generation_archive_extension')

    def test_actual_product_and_native_inventory_is_exactly_97(self):
        declared = ['cpp/src/app/main.cpp'] + ['cpp/'+p for p in
            json.loads(read(MANIFEST))['discovery']['extra_sources']]
        actual = {p.relative_to(ROOT).as_posix() for p in (ROOT/'cpp/src').rglob('*.cpp')}
        native = {p.relative_to(ROOT).as_posix() for p in (ROOT/'cpp/tests').rglob('*_tests.cpp')}
        self.assertEqual(97, len(declared), 'the archive product TU must be registered')
        self.assertEqual(97, len(set(declared)))
        self.assertEqual(set(declared), actual)
        self.assertEqual(97, len(native))
        self.assertIn(TU, actual)
        self.assertIn(NATIVE, native)
        self.assertIn('$allTestFiles.Count -ne 97', read(DRIVER))
        self.assertEqual(3, read(DRIVER).count('97'))

    def test_archive_product_test_and_private_header_have_unique_layout_entries(self):
        layout = read(LAYOUT)
        for name in (TU, NATIVE, PRIVATE):
            self.assertEqual(1, layout.count("'"+Path(name).name+"'"), name)

    def test_exact_current_sources_reconstruct_the_accepted_baseline(self):
        x = self.extension()
        self.assertEqual('2762749b27fc89156b724d19e89cce84e388ef52', x.SPEC['base'])
        current = source_map()
        before = dict(current)
        old = x.historical_tree(current)
        self.assertEqual(before, current)
        self.assertEqual(96, len([p for p in old if p.startswith('cpp/src/') and p.endswith('.cpp')]))
        self.assertEqual(96, len([p for p in old if p.startswith('cpp/tests/') and p.endswith('_tests.cpp')]))
        for path, rule in x.SPEC['modified'].items():
            self.assertEqual(rule['prior'], x.digest(x.legacy_text(path, current[path])))
        for path in x.SPEC['added']:
            self.assertNotIn(path, old)

    def test_modified_old_new_truncated_and_predecessor_bytes_are_not_current(self):
        x = self.extension()
        for path in x.SPEC['modified']:
            actual = read(path)
            prior = x.legacy_text(path, actual)
            for damaged in (actual+'\n// unregistered change\n', actual[:-1], prior):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    x.legacy_text(path, damaged)

    def test_wrong_inverse_digest_or_splice_is_refused(self):
        x = self.extension()
        path = MANIFEST
        for change in ('prior', 'offset', 'after'):
            altered = copy.deepcopy(x.SPEC)
            rule = altered['modified'][path]
            if change == 'prior':
                rule['prior'] = '0'*64
            elif change == 'offset':
                rule['edits'][0]['offset'] += 1
            else:
                rule['edits'][0]['after'] += 'BROKEN'
            with self.subTest(change=change), patch.object(x, 'SPEC', altered), self.assertRaises(ValueError):
                x.legacy_text(path, read(path))

    def test_new_files_cannot_be_missing_changed_or_replaced_with_unknown_files(self):
        x = self.extension()
        original = source_map()
        for path in x.SPEC['added']:
            for missing in (False, True):
                bad = dict(original)
                if missing:
                    del bad[path]
                else:
                    bad[path] += '\n// unexpected\n'
                with self.subTest(path=path, missing=missing), self.assertRaises(ValueError):
                    x.historical_tree(bad)
        for path in ('cpp/src/orchestration/incremental/unregistered.cpp',
                     'cpp/src/orchestration/incremental/unregistered.hpp',
                     'cpp/tests/orchestration/incremental/unregistered_tests.cpp'):
            bad = dict(original)
            bad[path] = 'int main() {}\n'
            with self.subTest(path=path), self.assertRaises(ValueError):
                x.historical_tree(bad)

    def test_registration_and_driver_policy_cannot_be_weakened(self):
        x = self.extension()
        original = source_map()
        replacements = (
            (MANIFEST, '      "src/orchestration/incremental/ArtifactGenerationArchive.cpp",\n', ''),
            (LAYOUT, "        'ArtifactGenerationArchive.cpp',\n", ''),
            (LAYOUT, 'Compare-Object', 'Write-Output'),
            (DRIVER, '$allTestFiles.Count -ne 97', '$allTestFiles.Count -ne 96'),
            (DRIVER, '/WHOLEARCHIVE', '/IGNORED'),
        )
        for path, before, after in replacements:
            self.assertIn(before, original[path])
            bad = dict(original)
            bad[path] = bad[path].replace(before, after, 1)
            with self.subTest(path=path, before=before), self.assertRaises(ValueError):
                x.historical_tree(bad)

    def test_historical_python_test_methods_and_complete_bytes_survive(self):
        x = self.extension()
        self.assertGreaterEqual(len(x.SPEC['python_contracts']), 15)
        for path in x.SPEC['python_contracts']:
            current = read(path)
            prior = x.legacy_text(path, current)
            methods = lambda text: {n.name for n in ast.walk(ast.parse(text))
                                    if isinstance(n, ast.FunctionDef) and n.name.startswith('test_')}
            self.assertLessEqual(methods(prior), methods(current), path)
            self.assertEqual(x.SPEC['modified'][path]['prior'], x.digest(prior))

    def test_legacy_path_helpers_keep_precise_baseline_membership(self):
        x = self.extension()
        native = list(x.legacy_native_paths(ROOT))
        product = list(x.legacy_product_paths(ROOT))
        self.assertEqual(set(x.SPEC['native_tests']), {p.relative_to(ROOT).as_posix() for p in native})
        self.assertEqual(set(x.SPEC['product_tus']), {p.relative_to(ROOT).as_posix() for p in product})
        self.assertEqual(96, len(native))
        self.assertEqual(96, len(product))

    def test_existing_success_helper_runs_a_complete_in_memory_round_trip(self):
        text = read(HELPER)
        for token in ('project_artifact_generation_archive(', 'encode_artifact_generation_archive(',
                      'decode_artifact_generation_archive(', 'model_archived_artifact_generations(',
                      'require_same_generation_model(', 'generation_archive_round_trip(inputs, *value, root, key);',
                      'generation-archive phase=', 'producer_identity_verified=false'):
            self.assertIn(token, text)
        block = text.split('// BEGIN generation archive round-trip checks\n', 1)[1].split(
            '// END generation archive round-trip checks\n', 1)[0]
        for token in ('run_recorded(', 'run_observed(', 'observe_storage_file(', 'CompileCacheFile::save',
                      'CompileCacheFile::load', 'std::ifstream', 'std::ofstream', 'write('):
            self.assertNotIn(token, block)

    def test_original_e2e_call_sites_and_failed_evidence_budget_keep_their_bytes(self):
        x = self.extension()
        for path in ('cpp/tests/e2e/mqb_artifact_record_e2e_tests.cpp',
                     'cpp/tests/e2e/mqb_static_record_e2e_tests.cpp'):
            self.assertNotIn(path, x.SPEC['modified'])
            self.assertEqual(x.SPEC['unchanged_e2e'][path], x.digest(read(path)))

    def test_actual_new_api_has_no_default_cli_caller_or_authority(self):
        public = read(PUBLIC)
        for name in ('producer_identity_verified', 'current_content_verified',
                     'complete_producer_inventory', 'deletion_authorized'):
            self.assertIn('static constexpr bool '+name+' = false;', public)
        allowed = {TU, PRIVATE, 'cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp',
                   'cpp/src/orchestration/incremental/ArtifactStorageProjection.cpp'}
        for p in (ROOT/'cpp/src').rglob('*'):
            if p.suffix in ('.cpp', '.hpp') and p.relative_to(ROOT).as_posix() not in allowed:
                self.assertNotIn('ArtifactGenerationArchive', p.read_text(encoding='utf-8'))
                self.assertNotRegex(p.read_text(encoding='utf-8'),
                    r'\b(?:project|encode|decode)_artifact_generation_archive\s*\(')
        self.assertEqual('5.6.0', read('VERSION').strip())
        self.assertEqual(51, len(list((ROOT/'.github/workflows').glob('*.yml'))))


if __name__ == '__main__':
    unittest.main(verbosity=2)
