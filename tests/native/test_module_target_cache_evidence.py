"""Owning same-call target evidence; portable contracts, not Windows execution."""
from pathlib import Path
import recorded_storage_projection_contract as storage_projection_extension
import accepted_target_wave_cache_extension as wave_extension
import unittest

import accepted_module_target_cache_extension as x

ROOT = Path(__file__).resolve().parents[2]
TU = 'cpp/src/orchestration/modules/MsvcModuleTargetCoordinator.cpp'
HEADER = 'cpp/include/mqb/orchestration/MsvcModuleTargetCoordinator.hpp'


def read(path):
    return wave_extension.legacy_text(path, storage_projection_extension.legacy_text(path, (ROOT / path).read_text(encoding='utf-8')))


def original_target(text):
    replacements = [
        ('    std::optional<ModuleCompileWaveCacheEvidence> cache_evidence;\n', ''),
        ('if (!record.compiles || !record.link || !record.cache_evidence)', 'if (!record.compiles || !record.link)'),
        ('        .cache_evidence = std::move(*record.cache_evidence),\n', ''),
        ('        record->cache_evidence.emplace(std::move(completed->cache_evidence));\n', ''),
    ]
    for after, before in replacements:
        if text.count(after) != 1:
            raise ValueError('missing or duplicate same-call target connection')
        text = text.replace(after, before, 1)
    if x.git_blob(text) != '0974cff5e19bab603a6a822c408cf006b1fafeb3':
        raise ValueError('original target flow changed')
    return text


class ModuleTargetCacheEvidenceContracts(unittest.TestCase):
    def test_only_moves_are_added_to_original_target_flow(self):
        original_target(read(TU))

    def test_disconnected_copied_default_and_io_variants_rejected(self):
        t = read(TU)
        variants = [
            t.replace('record->cache_evidence.emplace(std::move(completed->cache_evidence));', ''),
            t.replace('std::move(completed->cache_evidence)', 'completed->cache_evidence'),
            t.replace('std::move(completed->cache_evidence)', 'ModuleCompileWaveCacheEvidence{}'),
            t.replace(' || !record.cache_evidence', ''),
            t.replace('return run_impl(request, nullptr);', 'Recording record; return run_impl(request, &record);'),
            t.replace('return std::move(completed->result);', 'compile_coordinator_.run(prepared->compile_request); return std::move(completed->result);', 1),
            t + '\nCompileCacheFile::load(file);',
            t + '\nBuildSignature::for_compile(unit, toolchain, options);',
            t.replace('if (!compiled)', 'if (false)', 1),
            t.replace('if (!linked)', 'if (false)', 1),
        ]
        for i, variant in enumerate(variants):
            with self.subTest(i=i), self.assertRaises(ValueError):
                original_target(variant)

    def test_public_payload_is_owning_nonoptional_and_separate(self):
        h = read(HEADER)
        payload = h.split('struct RecordedModuleTargetResult {', 1)[1].split('\n};', 1)[0]
        for field in ('IncrementalModuleTargetResult result;', 'ModuleTargetArtifactRecord record;',
                      'ModuleCompileWaveCacheEvidence cache_evidence;'):
            self.assertIn(field, payload)
        self.assertNotIn('optional', payload)
        record = h.split('struct ModuleTargetArtifactRecord {', 1)[1].split('\n};', 1)[0]
        self.assertNotIn('cache_evidence', record)
        self.assertIn('static constexpr bool deletion_authorized = false;', record)

    def test_success_only_after_full_pipeline_and_required_stage_values(self):
        t = read(TU)
        call = t.split('MsvcModuleTargetCoordinator::run_recorded(', 1)[1].split('MsvcModuleTargetCoordinator::run_impl(', 1)[0]
        self.assertLess(call.index('if (!completed) return std::unexpected'), call.index('return RecordedModuleTargetResult'))
        self.assertIn('if (!record.compiles || !record.link || !record.cache_evidence)', call)
        self.assertEqual(1, t.count('compile_coordinator_.run_recorded('))
        self.assertEqual(1, t.count('link_coordinator_.run_recorded('))

    def test_original_native_assertions_preserved_exactly(self):
        self.assertEqual(x.PRIOR, x.git_blob(x.original_program(read(x.TEST))))

    def test_old_and_new_assertion_mutations_rejected(self):
        t = read(x.TEST)
        for phrase in ('real whole-target recorded reuse adds no process or writes',
                       'actual fixed-point std chain, header and external association',
                       'original stage error yields no public successful target',
                       'target scan/compile evidence correspondence',
                       'warm target reads each scanned source twice and each HU once, with zero writes',
                       'later overwrite/failure cannot mutate earlier target cache evidence'):
            self.assertIn(phrase, t)
            with self.subTest(phrase=phrase), self.assertRaises(ValueError):
                x.original_program(t.replace(phrase, '', 1))

    def test_legacy_mapping_preserves_types_and_unrelated_files(self):
        for binary in (False, True):
            value = read(x.TEST)
            if binary:
                value = value.encode()
            before = {x.TEST: value, 'other': value}
            after = x.legacy_view(before)
            self.assertIsNot(before, after)
            self.assertEqual(before[x.TEST], value)
            self.assertEqual(after['other'], value)
            self.assertIsInstance(after[x.TEST], type(value))

    def test_counts_exclude_test_serialization_and_do_not_add_calls(self):
        t = read(x.TEST)
        wrapper = t.rsplit('run_target_cache_checked(', 1)[1]
        self.assertEqual(1, wrapper.count('target.run_recorded('))
        self.assertNotIn('target.run(', wrapper)
        self.assertNotIn('CompileCacheFile::save', wrapper)
        self.assertLess(wrapper.index('performance::Activation active'), wrapper.index('target.run_recorded('))
        self.assertLess(wrapper.index('}();'), wrapper.index('collector.snapshot()'))
        for phrase in ('reads - nodes == scans', 'writes == 0',
                       '++count <= 10', 'calls.count == 10 && runner.programs == 2'):
            self.assertIn(phrase, t)

    def test_target_counter_budget_includes_scans_without_allowing_extra_reads(self):
        t = read(x.TEST)
        helper = t.split('constexpr bool target_cache_counts_match(', 1)[1].split('\n}', 1)[0]
        self.assertIn('if (writes > nodes) return false;', helper)
        self.assertIn('if (warm) return writes == 0 && reads >= nodes && reads - nodes == scans;', helper)
        self.assertIn('return reads <= nodes || reads - nodes <= scans;', helper)
        self.assertIn('result->record.scans.size()', t)
        self.assertIn('scans==result->cache_evidence.compiles.size()', t)
        for args in ('9, 0, 4, 5, true', '5, 0, 2, 3, true', '6, 0, 3, 3, true', '1, 0, 0, 1, true'):
            self.assertIn('static_assert(target_cache_counts_match('+args+'));', t)
        for args in ('10, 0, 4, 5, false', '10, 0, 4, 5, true', '8, 0, 4, 5, true',
                     '9, 1, 4, 5, true', '0, 6, 4, 5, false', '2, 0, 0, 1, true'):
            self.assertIn('static_assert(!target_cache_counts_match('+args+'));', t)
        self.assertIn('UINT64_MAX', t)

    def test_counter_raw_journal_precedes_every_counter_refusal(self):
        wrapper = read(x.TEST).rsplit('run_target_cache_checked(', 1)[1]
        saved = wrapper.index('write(prefix.string()+".cache-counts.txt",out.str());')
        self.assertLess(wrapper.index('collector.snapshot()'), saved)
        self.assertLess(saved, wrapper.index('require(scans=='))
        self.assertLess(saved, wrapper.index('require(target_cache_counts_match'))
        self.assertIn('counter_scope=target_scan_and_compile', wrapper)
        self.assertIn('compile_cache_reads=', wrapper)
        self.assertIn('compile_cache_writes=', wrapper)

    def test_scan_and_compile_counter_sources_are_unchanged(self):
        # Source pins justify adding existing scan reads, not a blanket allowance.
        for path, expected in {
            'cpp/src/orchestration/modules/MsvcIncrementalModuleScanCoordinator.cpp': '2cbdb35f1fdb2bddf39e365e98bad24b09eec54b',
            'cpp/src/orchestration/modules/ModuleTargetPreparation.cpp': '45d509cc090c9252a8667956c5e003cf4a00cff9',
            'cpp/src/orchestration/modules/ModuleTargetScanner.cpp': '467d2e2b4042e74ff6060236dc88547fe2ba00bd',
            'cpp/src/orchestration/modules/StandardLibraryModuleProvider.cpp': 'c0401e29a13ccabc982d12c0ef6eae2370826d1c',
            'cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp': '280ff02e95a9b6ee1502823ca77025f7436394a6',
        }.items():
            self.assertEqual(expected, x.git_blob(read(path)), path)
        self.assertIn('CompileCacheFile::load(request.compile_cache_file)',
                      read('cpp/src/orchestration/modules/MsvcIncrementalModuleScanCoordinator.cpp'))

    def test_dynamic_provider_and_header_values_are_not_reconstructed(self):
        t = read(x.TEST)
        for phrase in ('scan.scan.recipe.invocation.source==cache.request.unit.source',
                       'scan.scan.compile_cache_reference==cache.request.cache_file',
                       'header identity and no fabricated scan',
                       'target keeps actual injected providers but no fabricated external cache slot'):
            self.assertIn(phrase, t)
        production = read(TU)
        for phrase in ('CompileCacheFile::', 'BuildSignature::', 'std::ifstream', 'std::ofstream'):
            self.assertNotIn(phrase, production)

    def test_owning_values_survive_later_configuration_and_stage_failures(self):
        t = read(x.TEST)
        self.assertEqual(8, t.count('check_target_cache_history('))  # six calls, declaration and definition
        for phase in ('01-cold', '02-reuse', '06-standard-cold', '07-standard-reuse'):
            self.assertIn('check_target_cache_history(evidence / "'+phase+'"', t)
        self.assertIn('retained target cache request survives configuration overwrite and later errors', t)
        self.assertIn('copy.inspection_toolchain.environment.push_back', t)
        self.assertNotIn('environment[0].value', t)

    def test_bounded_adoption_does_not_reach_cli_or_other_targets(self):
        allowed = {TU, 'cpp/src/orchestration/modules/MsvcModuleCompileCoordinator.cpp',
                   'cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp',
                   'cpp/src/orchestration/incremental/MsvcIncrementalPchCoordinator.cpp'}
        for path in (ROOT/'cpp/src').rglob('*'):
            if path.suffix not in ('.cpp', '.hpp') or path.relative_to(ROOT).as_posix() in allowed:
                continue
            text = wave_extension.legacy_text(path.relative_to(ROOT).as_posix(), storage_projection_extension.legacy_text(path.relative_to(ROOT).as_posix(), path.read_text(encoding='utf-8')))
            self.assertNotIn('CompileCacheEvidence', text, str(path))
            self.assertNotIn('ModuleCompileWaveCacheEvidence', text, str(path))
        self.assertIn('return run_impl(request, nullptr);', read(TU))
        self.assertEqual(96, len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))))
        self.assertEqual(51, len(list((ROOT/'.github/workflows').glob('*.yml'))))

    def test_docs_and_release_boundary(self):
        english = read('docs/MODULE_TARGET_CACHE_EVIDENCE.md')
        chinese = read('docs/MODULE_TARGET_CACHE_EVIDENCE_ZH.md')
        self.assertEqual(english.count('\n## '), chinese.count('\n## '))
        self.assertIn('MODULE_TARGET_CACHE_EVIDENCE_ZH.md', english)
        self.assertIn('MODULE_TARGET_CACHE_EVIDENCE.md', chinese)
        self.assertEqual('5.6.0', read('VERSION').strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)
