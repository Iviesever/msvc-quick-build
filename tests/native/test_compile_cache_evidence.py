"""Compile-cache capture wiring and historical preservation; no native execution.

The real coordinator cases run in the existing 96-program Windows native graph.
These portable checks do not substitute for those MSVC executions.
"""
from pathlib import Path
import json
import re
import unittest

import accepted_compile_evidence_extension as extension

ROOT = Path(__file__).resolve().parents[2]
TU = 'cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp'
PUBLIC = 'cpp/include/mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp'
OLD_TU = 'c67f64a6efc12c5e17679b1862a58fd93dc48a55'


def read(name):
    return (ROOT / name).read_text(encoding='utf-8')


def once(text, old, new=''):
    if text.count(old) != 1:
        raise ValueError('non-unique compile capture extraction anchor')
    return text.replace(old, new, 1)


def original_coordinator(text):
    """Reverse only the opt-in wiring and check every pre-existing source byte."""
    text = once(text, '''struct MsvcIncrementalCompileCoordinator::CacheCapture {
    std::optional<CompileCacheEntry> entry;
    CompileCacheEvidenceState state{CompileCacheEvidenceState::reused};
    std::optional<CompileCacheFileError> save_error;
};

''')
    text = once(text, '''std::expected<IncrementalCompileInspection, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::inspect(const IncrementalCompileRequest& request) const {
    return inspect_impl<false>(request, nullptr);
}

template<bool Capture>
std::expected<IncrementalCompileInspection, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::inspect_impl(
    const IncrementalCompileRequest& request, CacheCapture* capture) const {''',
        '''std::expected<IncrementalCompileInspection, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::inspect(const IncrementalCompileRequest& request) const {''')
    text = once(text, '''            if constexpr (Capture) {
                // Preserve the exact value accepted by BOTH freshness checks.
                // No cache reread and no copy into the default inspection API.
                capture->entry = std::move(cached_entry);
            }
''')
    text = once(text, '''std::expected<IncrementalCompileResult, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::execute_inspected(
    const IncrementalCompileRequest& request,
    IncrementalCompileInspection inspection) const {
    return execute_inspected_impl<false>(request, std::move(inspection), nullptr);
}

template<bool Capture>
std::expected<IncrementalCompileResult, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::execute_inspected_impl(
    const IncrementalCompileRequest& request,
    IncrementalCompileInspection inspection,
    CacheCapture* capture) const {''', '''std::expected<IncrementalCompileResult, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::execute_inspected(
    const IncrementalCompileRequest& request,
    IncrementalCompileInspection inspection) const {''')
    text = once(text, '''    if constexpr (Capture) {
        // Move only AFTER sealing and the original persistence attempt. A save
        // failure is retained as typed evidence, never upgraded to durability.
        capture->state = saved_cache ? CompileCacheEvidenceState::saved
                                    : CompileCacheEvidenceState::save_failed;
        if (!saved_cache) capture->save_error = std::move(saved_cache.error());
        capture->entry = std::move(executed->cache_entry);
    }
''')
    first = 'std::expected<RecordedIncrementalCompileResult, IncrementalCompileError>\n'
    if text.count(first) != 1:
        raise ValueError('recorded entry missing or duplicated')
    index = text.index(first)
    end = text.index('} // namespace mqb::orchestration', index)
    text = text[:index] + text[end:]
    if extension.git_blob(text) != OLD_TU:
        raise ValueError('original compile decision or execution changed')
    return text


class CompileCacheEvidenceContracts(unittest.TestCase):
    def test_preexisting_coordinator_body_preserved(self):
        self.assertEqual(OLD_TU, extension.git_blob(original_coordinator(read(TU))))

    def test_unrelated_compile_behavior_drift_refused(self):
        value = read(TU).replace('request.force_rebuild)', 'false)', 1)
        with self.assertRaises(ValueError):
            original_coordinator(value)

    def test_include_root_check_cannot_be_removed(self):
        value = read(TU).replace('cached_entry->include_search_roots,', 'std::vector<std::filesystem::path>{},', 1)
        with self.assertRaises(ValueError):
            original_coordinator(value)

    def test_preexisting_native_program_exact(self):
        self.assertEqual(extension.PRIOR, extension.git_blob(extension.original_program(read(extension.TEST))))

    def test_added_native_assertion_changes_refused(self):
        value = read(extension.TEST).replace('runner.calls == 10', 'runner.calls >= 0')
        with self.assertRaises(ValueError):
            extension.original_program(value)

    def test_old_native_assertion_changes_refused(self):
        value = read(extension.TEST).replace('unchanged warm build should reuse the cached object', 'changed old assertion')
        with self.assertRaises(ValueError):
            extension.original_program(value)

    def test_missing_or_duplicate_native_call_refused(self):
        call = extension.LINES[1]
        for value in (read(extension.TEST).replace(call, ''), read(extension.TEST).replace(call, call + call)):
            with self.subTest(value_length=len(value)), self.assertRaises(ValueError):
                extension.original_program(value)

    def test_byte_and_text_historical_views_are_owning(self):
        for binary in (False, True):
            text = read(extension.TEST)
            values = {extension.TEST: text.encode() if binary else text, 'unrelated': b'x' if binary else 'x'}
            previous = dict(values)
            result = extension.legacy_view(values)
            self.assertEqual(previous, values)
            self.assertIsNot(result, values)
            self.assertEqual(type(result[extension.TEST]), type(values[extension.TEST]))
            self.assertEqual(result['unrelated'], values['unrelated'])

    def test_default_path_is_compile_time_disabled(self):
        text = read(TU)
        self.assertIn('return inspect_impl<false>(request, nullptr);', text)
        self.assertIn('return execute_inspected_impl<false>(request, std::move(inspection), nullptr);', text)
        self.assertEqual(2, text.count('if constexpr (Capture)'))
        self.assertEqual(1, text.count('CompileCacheFile::load('))
        self.assertEqual(1, text.count('CompileCacheFile::save('))
        self.assertEqual(1, text.count('executor_.execute('))

    def test_warm_capture_follows_all_freshness_checks(self):
        text = read(TU)
        self.assertLess(text.index('current_include_search_roots);') if 'current_include_search_roots);' in text else
                        text.index('current_include_search_roots))'), text.index('capture->entry = std::move(cached_entry);'))
        self.assertIn('if (result.validation.reusable()) {\n            if constexpr (Capture)', text)

    def test_executed_capture_follows_seal_and_original_save(self):
        text = read(TU)
        seal = text.index('    seal_module_scan_evidence(\n')
        save = text.index('    auto saved_cache = CompileCacheFile::save(')
        capture = text.index('capture->entry = std::move(executed->cache_entry);')
        self.assertLess(seal, save)
        self.assertLess(save, capture)
        self.assertIn('capture->save_error = std::move(saved_cache.error());', text)

    def test_public_inspection_is_not_an_execution_ticket(self):
        text = read(PUBLIC)
        private = text.index('\nprivate:\n')
        self.assertGreater(text.index('    execute_inspected('), private)
        self.assertGreater(text.index('    inspect_impl('), private)
        result = text.split('struct IncrementalCompileResult :', 1)[1].split('\n};', 1)[0]
        self.assertNotIn('CompileCacheEntry', result)
        inspected = text.split('struct IncrementalCompileInspection {', 1)[1].split('\n};', 1)[0]
        self.assertNotIn('CompileCacheEntry', inspected)

    def test_original_errors_precede_record_construction(self):
        text = read(TU).split('MsvcIncrementalCompileCoordinator::run_recorded(', 1)[1]
        self.assertLess(text.index('if (!inspected)'), text.index('CompileCacheEvidence{'))
        self.assertLess(text.index('if (!completed)'), text.index('CompileCacheEvidence{'))
        self.assertIn('if (!capture.entry)', text)
        for token in ('CompileCacheFile::load', 'CompileCacheFile::save', 'executor_.execute',
                      'BuildSignature::for_', 'ofstream', 'ifstream'):
            self.assertNotIn(token, text)

    def test_evidence_is_not_authority_and_no_default_adoption(self):
        text = read(PUBLIC)
        for name in ('producer_identity_verified', 'current_content_verified', 'complete_producer_inventory', 'deletion_authorized'):
            self.assertIn(f'static constexpr bool {name} = false;', text)
        for path in (ROOT / 'cpp/src').rglob('*'):
            if path.suffix not in ('.cpp', '.hpp') or path.relative_to(ROOT).as_posix() == TU:
                continue
            self.assertNotIn('CompileCacheEvidence', path.read_text(encoding='utf-8'), str(path))

    def test_native_counts_and_production_manifest_unchanged(self):
        self.assertEqual(96, len(list((ROOT / 'cpp/tests').rglob('*_tests.cpp'))))
        self.assertIn('$allTestFiles.Count -ne 96', read('tests/native/run_native_tests.ps1'))
        declared = ['src/app/main.cpp'] + json.loads(read('cpp/mqb.json'))['discovery']['extra_sources']
        self.assertEqual(96, len(declared))
        self.assertEqual(len(declared), len(set(declared)))
        self.assertEqual(set(declared), {p.relative_to(ROOT / 'cpp').as_posix() for p in (ROOT / 'cpp/src').rglob('*.cpp')})
        self.assertEqual(51, len(list((ROOT / '.github/workflows').glob('*.yml'))))

    def test_bilingual_scope_and_version(self):
        en = read('docs/COMPILE_CACHE_EVIDENCE.md')
        zh = read('docs/COMPILE_CACHE_EVIDENCE_ZH.md')
        self.assertEqual(en.count('\n## '), zh.count('\n## '))
        self.assertIn('COMPILE_CACHE_EVIDENCE_ZH.md', en)
        self.assertIn('COMPILE_CACHE_EVIDENCE.md', zh)
        self.assertEqual('5.6.0', read('VERSION').strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)
