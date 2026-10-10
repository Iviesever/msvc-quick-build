"""Pure consumer contracts and exact inverse controls; not native execution."""
import copy
from pathlib import Path
import generation_archive_extension as archive_extension
import re
import unittest
from unittest.mock import patch
import recorded_storage_projection_contract as ext

ROOT = Path(__file__).resolve().parents[2]
TU = 'cpp/src/orchestration/incremental/ArtifactStorageProjection.cpp'
HEADER = 'cpp/include/mqb/orchestration/ArtifactStorageProjection.hpp'
TEST = 'cpp/tests/e2e/mqb_storage_association_e2e_tests.cpp'


def read(path):
    return archive_extension.read_text(ROOT, path)


def body(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)


def pure(text):
    text = body(text)
    for token in ('CompileCacheFile::load', 'CompileCacheFile::save', 'BuildSignature::for_',
                  'current_path(', 'canonical(', 'ifstream', 'ofstream', 'CreateFile',
                  'DeleteFile', 'directory_iterator', 'getenv(', 'run_recorded(',
                  '::execute(', '::remove(', '.environment'):
        if token in text:
            raise ValueError('projection acquired effects or reconstructed authority: '+token)


class RecordedStorageProjectionContracts(unittest.TestCase):
    def test_complete_new_files_restore_exact_previous_bytes(self):
        self.assertEqual(ext.SPEC['base'], '77b7b7af383c1ebfcc17e971d70b97b0906cdee4')
        self.assertEqual(len(ext.SPEC['files']), 5)
        for path, pin in ext.SPEC['files'].items():
            actual = read(path)
            old = ext.legacy_text(path, actual)
            self.assertEqual(ext.digest(old), pin['prior'], path)
            self.assertNotEqual(actual, old)

    def test_missing_old_new_or_duplicate_assertions_are_not_ignored(self):
        for path in ext.SPEC['files']:
            actual = read(path)
            for damaged in (actual+'\n// unexpected\n', actual[:-20], ext.legacy_text(path, actual)):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    ext.legacy_text(path, damaged)

    def test_wrong_inverse_anchor_or_prior_is_rejected(self):
        for mode in ('current', 'prior', 'cuts'):
            spec = copy.deepcopy(ext.SPEC)
            if mode == 'cuts':
                spec['files'][TU]['cuts'][0][0] = 'ABSENT MARKER'
            else:
                spec['files'][TU][mode] = '0'*64
            with patch.object(ext, 'SPEC', spec), self.assertRaises(ValueError):
                ext.legacy_text(TU, read(TU))

    def test_inverse_preserves_bytes_input_and_does_not_mutate_mapping(self):
        values = {TEST: read(TEST).encode(), 'untouched': b'original'}
        before = dict(values)
        result = ext.legacy_view(values)
        self.assertEqual(values, before)
        self.assertEqual(result['untouched'], b'original')
        self.assertIsInstance(result[TEST], bytes)
        self.assertEqual(ext.digest(result[TEST].decode()), ext.SPEC['files'][TEST]['prior'])

    def test_projector_has_no_io_or_environment_export(self):
        pure(read(TU)); pure(read(HEADER))

    def test_effect_negative_controls(self):
        for token in ('CompileCacheFile::load(p)', 'BuildSignature::for_compile(u,t,o)',
                      'current_path()', 'std::ifstream f;', 'e.inspection_toolchain.environment'):
            with self.subTest(token=token), self.assertRaises(ValueError):
                pure(read(TU)+'\n'+token)

    def test_default_cli_and_generation_consumer_unchanged(self):
        paths = list((ROOT/'cpp/src/app').rglob('*.cpp')) + [ROOT/'cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp']
        for path in paths:
            text = body(archive_extension.legacy_text(path.relative_to(ROOT).as_posix(), path.read_text(encoding='utf-8')))
            self.assertNotIn('RecordedTargetStorageReferences', text, str(path))
            self.assertNotIn('RecordedStorageProjectionLimits', text, str(path))
        self.assertIn('project_storage_references(target)', read('cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp'))

    def test_authority_and_toolchain_difference_are_explicit(self):
        text = body(read(HEADER))
        for name in ('producer_identity_verified','current_content_verified','complete_producer_inventory','deletion_authorized'):
            self.assertIn('static constexpr bool '+name+' = false;', text)
        self.assertIn('ToolchainIdentity inspection_toolchain;', text)
        self.assertIn('ToolchainIdentity cache_toolchain;', text)
        self.assertIn('captured_signature', text)
        self.assertNotIn('MsvcToolchain inspection_toolchain;', text)
        self.assertIn('!identical_toolchain(e.inspection_toolchain.identity, cache.toolchain)', read(TU))

    def test_exact_correspondence_and_failure_checks_are_present(self):
        text = body(read(TU))
        for token in ('source_count', 'source_mismatch', 'options_mismatch', 'cache_mismatch',
                      'outcome_mismatch', 'terminal_mismatch', 'path_conflict', 'invalid_path',
                      'save_warnings == 1', 'recorded.result.any_compiled', 'paths.finish();',
                      'object_keys[i - additional.size()]'):
            self.assertIn(token, text)
        self.assertNotIn('std::sort', text)

    def test_limits_precede_bulk_allocations_and_no_quadratic_terminal_search(self):
        text = body(read(TU))
        self.assertLess(text.index('n <= budget.left.sources'), text.index('out.compiles.reserve(n)'))
        self.assertLess(text.index('budget.paths(objects)'), text.index('project_storage_references(terminal)'))
        for token in ('requested.items', 'requested.text_code_units', 'n <= left.items',
                      'n <= left.text_code_units', 'left.items -= n', 'left.text_code_units -= n'):
            self.assertIn(token, text)
        tail = text.split('for (std::size_t i = 0; i < declared.size(); ++i)')[1]
        self.assertNotIn('for (const auto& s : record.sources)', tail)

    def test_runtime_controls_and_existing_real_lifecycles_registered(self):
        text = read(TEST)
        for token in ('recorded_projection_contracts();', 'rejected==22', 'rich_static()',
                      'KEEP original failure', 'different-compile-context', 'sources.insert'):
            self.assertIn(token, text+read(TU))
        for name in ('mqb_artifact_record_e2e_tests.cpp','mqb_static_record_e2e_tests.cpp'):
            actual = read('cpp/tests/e2e/'+name)
            self.assertIn('project_storage_references(*result, platform::windows::path_identity_key)', actual)
            self.assertIn('target_wave_cache_checks::recorded', actual)
        self.assertEqual(len(list(archive_extension.legacy_native_paths(ROOT))), 96)
        self.assertEqual(len(list(archive_extension.legacy_product_paths(ROOT))), 96)
        self.assertEqual(len(list((ROOT/'.github/workflows').glob('*.yml'))), 51)
        self.assertEqual(read('VERSION').strip(), '5.6.0')

    def test_native_record_is_saved_before_projection_can_refuse(self):
        path = 'cpp/tests/e2e/mqb_artifact_record_e2e_tests.cpp'
        # Original #246 failure-preservation contract runs on the exact pinned
        # predecessor. The new LINK-role suite independently checks current code.
        text = ext.link_roles.legacy_text(path, read(path))
        start = text.index('auto run = [&](const char* phase)')
        end = text.index('const auto cold = run("01-cold")', start)
        call = text[start:end]
        self.assertLess(call.index('".record.txt"'), call.index('project_storage_references(*result,'))
        self.assertIn('"07-dll"', call)
        self.assertIn('contains_import(file_inputs) && contains_import(side_outputs)', call)
        self.assertIn('projected.error().issue == RecordedStorageProjectionIssue::path_conflict', call)
        self.assertIn('!projected.error().source_index', call)
        self.assertIn('"output or metadata aliases a protected input"', call)
        self.assertIn('association.file_inputs == file_inputs', call)
        self.assertIn('association.side_outputs == side_outputs', call)
        self.assertIn('if (!projected) throw', call)
        self.assertEqual(call.count('project_storage_references(*result,'), 1)
        self.assertIn('require(completed_calls == 7,', text)

    def test_dll_dual_role_controls_do_not_replace_existing_refusals(self):
        text = read(TEST)
        self.assertIn('for (const bool reused : {false, true})', text)
        self.assertIn('for (const auto& overlap : {import_library, export_file})', text)
        self.assertIn('dll_admitted == 2 && dll_refused == 4', text)
        self.assertIn('rejected==22', text)
        self.assertIn('has_role(Role::input) && has_role(Role::declared_output)', text)
        self.assertIn('!refused.error().source_index', text)

    def test_bilingual_scope_and_unqualified_costs(self):
        en=read('docs/RECORDED_STORAGE_PROJECTION.md'); zh=read('docs/RECORDED_STORAGE_PROJECTION_ZH.md')
        self.assertEqual(en.count('\n## '), zh.count('\n## '))
        for text in (en, zh):
            for token in ('save_failed','run/inspect/CLI','clean/prune','v5.7.0','100000','1000000'):
                self.assertIn(token, text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
