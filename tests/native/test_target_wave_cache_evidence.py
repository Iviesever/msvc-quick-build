"""Opt-in ordinary target capture: actual new code and strict inverse controls.

These are portable source contracts, not Windows execution or performance tests.
The existing native test TU exercises the new deterministic cases separately.
"""
import copy
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import accepted_target_wave_cache_extension as ext

ROOT = Path(__file__).resolve().parents[2]
WAVE = 'cpp/src/orchestration/incremental/TargetCompileWave.hpp'
COMPILE = 'cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp'
CH = 'cpp/include/mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp'
TARGET = 'cpp/src/orchestration/incremental/MsvcIncrementalTargetCoordinator.cpp'
STATIC = 'cpp/src/orchestration/incremental/MsvcIncrementalStaticTargetCoordinator.cpp'
CASES = 'cpp/tests/orchestration/incremental/TargetWaveCacheEvidenceCases.hpp'
HELPER = 'cpp/tests/e2e/TargetWaveCacheEvidenceChecks.hpp'
LAYOUT = 'tests/native/assert_cpp_layout.ps1'
REGISTRATION = ",\n        'TargetWaveCacheEvidenceCases.hpp'"


def read(path):
    return (ROOT/path).read_text(encoding='utf-8')


def between(text, first, last):
    return text.split(first, 1)[1].split(last, 1)[0]


def check_incremental_layout(layout, actual):
    """Check this literal leaf declaration, not a general PowerShell evaluator."""
    root = "Assert-LeafLayout -Root (Join-Path $testsRoot 'orchestration') -LeafFiles ([ordered]@{"
    if layout.count(root) != 1:
        raise ValueError('ambiguous orchestration layout')
    leaf = layout.split(root, 1)[1].split("    'modules' = @(", 1)[0]
    match = re.fullmatch(r"\s*'incremental' = @\((.*?)\)\s*", leaf, re.S)
    if match is None:
        raise ValueError('unexpected incremental layout expression')
    names = re.findall(r"'([^']+)'", match[1])
    remainder = re.sub(r"'[^']+'", '', match[1])
    if re.sub(r'[\s,]', '', remainder) or len(names) != len(set(names)):
        raise ValueError('nonliteral or duplicate layout registration')
    if len(actual) != len(set(actual)) or set(names) != set(actual):
        raise ValueError('incremental leaf membership mismatch')


class TargetWaveCacheContracts(unittest.TestCase):
    def test_exact_inverse_covers_all_old_product_and_native_changes(self):
        self.assertEqual(len(ext.SPEC['files']), 10)
        self.assertEqual(ext.SPEC['base'], '0d001342409db347287161873d1af9de3160c52a')
        ext.verify_helpers()
        for path, pin in ext.SPEC['files'].items():
            with self.subTest(path=path):
                actual = read(path)
                self.assertEqual(ext.git_blob(actual), pin['current'])
                old = ext.legacy_text(path, actual)
                self.assertEqual(ext.git_blob(old), pin['prior'])
                self.assertNotEqual(old, actual)
                self.assertEqual(ext.legacy_view({path: actual.encode()})[path], old.encode())

    def test_whole_file_pin_rejects_even_changes_outside_new_code(self):
        for path in ext.SPEC['files']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                ext.legacy_text(path, read(path)+'\n// unexpected change\n')

    def test_inverse_rejects_changed_substitution_and_wrong_prior(self):
        original = copy.deepcopy(ext.SPEC)
        for mode in ('anchor', 'prior'):
            bad = copy.deepcopy(original)
            if mode == 'anchor':
                bad['files'][WAVE]['replacements'][0][1] = 'ABSENT IN CURRENT SOURCE'
            else:
                bad['files'][WAVE]['prior'] = '0'*40
            with self.subTest(mode=mode), patch.object(ext, 'SPEC', bad), self.assertRaises(ValueError):
                ext.legacy_text(WAVE, read(WAVE))

    def test_new_seams_are_private_and_inspection_result_does_not_grow(self):
        header = read(CH)
        private = header.split('private:', 1)[1]
        public = header.split('private:', 1)[0]
        for name in ('inspect_for_target_record', 'execute_inspected_for_target_record'):
            self.assertIn(name, private)
            self.assertNotIn(name, public)
        self.assertIn('friend class detail::TargetCompileWave;', private)
        self.assertNotIn('CompileCacheEvidence', between(header,
            'struct IncrementalCompileInspection', 'struct IncrementalCompileResult'))
        self.assertNotIn('PendingCompile', header)

    def test_initial_inspection_context_is_owned_for_both_hits_and_misses(self):
        source = read(COMPILE)
        inspect = between(source, '::inspect_for_target_record(',
            '::execute_inspected_for_target_record(')
        self.assertLess(inspect.index('auto captured_toolchain = toolchain_;'),
                        inspect.index('inspect_impl<true>(request, &capture)'))
        self.assertEqual(inspect.count('inspect_impl<true>('), 1)
        self.assertIn('accepted.reset();', inspect)
        self.assertIn('miss_context.reset();', inspect)
        self.assertIn('miss_context.emplace(std::move(captured_toolchain));', inspect)
        self.assertIn('.cache_entry = std::move(*capture.entry)', inspect)
        execute = between(source, '::execute_inspected_for_target_record(', '::run_recorded(')
        self.assertIn('auto captured_toolchain = std::move(inspection_context);', execute)
        self.assertNotIn('= toolchain_', execute)
        self.assertNotIn('inspect_impl', execute)
        self.assertEqual(execute.count('execute_inspected_impl<true>('), 1)
        self.assertIn('.save_error = std::move(capture.save_error)', execute)
        for forbidden in ('CompileCacheFile::load', 'CompileCacheFile::save', 'BuildSignature', 'executor_.execute'):
            self.assertNotIn(forbidden, inspect+execute)

    def test_default_and_admission_paths_do_not_capture(self):
        source = read(WAVE)
        self.assertIn('bool CaptureCache = false', source)
        self.assertIn('static_assert(!WithAdmissionStop || !CaptureCache', source)
        self.assertIn('attempts[index].emplace(coordinator.run(compile_request));', source)
        self.assertIn('return coordinator.inspect(compile_request);', source)
        self.assertIn('coordinator.execute_inspected(', source)
        # New temporary context slots allocate only inside the capture specialization.
        self.assertIn('if constexpr (CaptureCache) captured_contexts.resize(request.sources.size());', source)
        for owner in (TARGET, STATIC):
            self.assertIn('TargetCompileWave::run', read(owner))
        self.assertNotIn('run_recorded', between(read(TARGET), '::run_with_compile_admission_stop(', '::run_recorded('))

    def test_direct_and_split_paths_never_add_inspection_or_cache_io(self):
        source = read(WAVE)
        self.assertEqual(source.count('coordinator.run_recorded('), 1)
        self.assertEqual(source.count('coordinator.inspect_for_target_record('), 1)
        self.assertEqual(source.count('coordinator.execute_inspected_for_target_record('), 1)
        split = source.split('std::vector<std::unique_ptr<PendingCompile>>', 1)[1]
        self.assertNotIn('coordinator.run_recorded(', split)
        self.assertIn('std::move(*captured_contexts[source_index])', split)
        for token in ('CompileCacheFile::load', 'CompileCacheFile::save', 'build_signature', 'serialize'):
            self.assertNotIn(token, source)

    def test_independent_slots_source_mapping_and_new_wave_reset(self):
        source = read(WAVE)
        self.assertLess(source.index('captured->clear();'), source.index('const auto dispatch'))
        self.assertIn('captured->resize(request.sources.size());', source)
        self.assertIn('(*captured)[index]', source)
        self.assertIn('(*captured)[source_index]', source)
        self.assertIn('const std::size_t source_index = misses[miss_index];', source)
        self.assertNotIn('captured->push_back', source)
        self.assertIn('if (misses.empty()) return summary;', source)

    def test_post_miss_barrier_and_retry_precede_final_collection(self):
        for path in (TARGET, STATIC):
            text = read(path)
            with self.subTest(path=path):
                barrier = text.index('!shared_evidence->revalidate_shared()')
                retry = text.index('run_wave(true, nullptr)', barrier)
                collect = text.index('cache_evidence->compiles.push_back', retry)
                self.assertLess(barrier, retry)
                self.assertLess(retry, collect)
                self.assertIn('for (std::size_t index = 0; index < request.sources.size(); ++index)', text)
                self.assertIn('std::move(*captured_compiles[index])', text)
                self.assertIn('IncrementalCompileErrorCode::cache_evidence_unavailable', text)
                self.assertIn('TargetCompileCacheEvidence cache_evidence;', text)
                self.assertIn('std::move(cache_evidence)', text)

    def test_original_tls_safety_remains_exact(self):
        current = read(WAVE)
        old = ext.legacy_text(WAVE, current)
        for token in ('ScopedFilesystemEvidenceActivation active{evidence_table};',
                      'ScopedFilesystemEvidenceActivation suspended{nullptr};',
                      'active_filesystem_evidence_table != nullptr',
                      'if (inspected->stop_requested)'):
            self.assertEqual(current.count(token), old.count(token), token)

    def test_known_semantic_corruptions_rejected_without_relaxing_old_contracts(self):
        mutations = {
            WAVE: [('captured->clear();', '(void)captured;'),
                   ('(*captured)[source_index]', '(*captured)[miss_index]'),
                   ('std::move(*captured_contexts[source_index])', 'msvc::MsvcToolchain{}'),
                   ('if (misses.empty()) return summary;', 'if (false) return summary;')],
            COMPILE: [('auto captured_toolchain = std::move(inspection_context);', 'auto captured_toolchain = toolchain_;')],
            TARGET: [('!shared_evidence->revalidate_shared()', 'false'),
                     ('std::move(*captured_compiles[index])', 'CompileCacheEvidence{}')],
            STATIC: [('run_wave(true, nullptr)', 'run_wave(false, shared_evidence)')],
        }
        for path, replacements in mutations.items():
            for before, after in replacements:
                with self.subTest(path=path, mutation=before):
                    actual=read(path)
                    self.assertIn(before, actual)
                    with self.assertRaises(ValueError): ext.legacy_text(path, actual.replace(before,after,1))

    def test_deterministic_native_cases_are_bounded_and_cover_semantics(self):
        text = read(CASES)
        phases = re.findall(r'invoke\([^;]*?,"(\d\d-[^"]+)"\)', text)
        self.assertEqual(len(phases), 15)
        self.assertEqual([p[:2] for p in phases], [f'{i:02}' for i in range(1,16)])
        for token in ('++calls<=15', 'b_finished', 'fail_sources', 'before+5',
                      'target-wave-cache', 'std::chrono::seconds{2}',
                      '"08-conservative-rebuild",4,5', '"09-save-failure",0,4',
                      'CompileCacheEvidenceState::save_failed', 'check::history',
                      'active_filesystem_evidence_table==nullptr'):
            self.assertIn(token, text)

    def test_existing_native_calls_and_production_inventory_preserved(self):
        for path in ('cpp/tests/e2e/mqb_artifact_record_e2e_tests.cpp',
                     'cpp/tests/e2e/mqb_static_record_e2e_tests.cpp'):
            actual = read(path)
            self.assertIn('target_wave_cache_checks::recorded', actual)
            self.assertEqual(ext.git_blob(ext.legacy_text(path,actual)), ext.SPEC['files'][path]['prior'])
        self.assertEqual(len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))),96)
        self.assertEqual(len(list((ROOT/'cpp/src').rglob('*.cpp'))),96)
        self.assertEqual(len(list((ROOT/'.github/workflows').glob('*.yml'))),51)
        self.assertEqual(read('VERSION').strip(),'5.6.0')

    def test_mock_serialization_is_after_count_capture_and_never_identity_authority(self):
        text = read(HELPER)
        self.assertIn('mqb::performance::Activation active{collector};', text)
        self.assertIn('compile-counts.txt', text)
        self.assertIn('CompileCacheFile::save', text)
        self.assertIn('!CompileCacheEvidence::deletion_authorized', text)
        measured = between(text, 'auto result=[&]', '\n    const auto i=')
        self.assertNotIn('CompileCacheFile::save', measured)
        self.assertLess(text.index('write(prefix.string()+".compile-counts.txt"'), text.index('save(prefix,*result,request)'))
        self.assertIn('save_error', text)
        self.assertIn('cache_entry', text)
        self.assertNotIn('CompileCacheFile::load', text)
        self.assertNotIn('environment=', text)

    def test_registered_incremental_leaf_matches_actual_source(self):
        leaf = ROOT/'cpp/tests/orchestration/incremental'
        actual = [p.name for p in leaf.iterdir() if p.is_file()]
        check_incremental_layout(read(LAYOUT), actual)
        self.assertEqual(read(LAYOUT).count(REGISTRATION), 1)

    def test_missing_extra_duplicate_and_wrong_leaf_registration_rejected(self):
        layout = read(LAYOUT)
        actual = [p.name for p in (ROOT/'cpp/tests/orchestration/incremental').iterdir() if p.is_file()]
        helper = Path(CASES).name
        for changed_layout, changed_files in (
            (layout.replace(REGISTRATION, '', 1), actual),
            (layout.replace(REGISTRATION, REGISTRATION*2, 1), actual),
            (layout.replace(helper, 'Unregistered.hpp', 1), actual),
            (layout, [name for name in actual if name != helper]),
            (layout, actual+['Unregistered.hpp']),
        ):
            with self.subTest(files=changed_files), self.assertRaises(ValueError):
                check_incremental_layout(changed_layout, changed_files)

    def test_legacy_layout_guard_rejects_unrelated_changes(self):
        import test_observed_link_isolation as isolation
        original = isolation.read_tree(ROOT)
        for changed in (original[LAYOUT].replace(REGISTRATION, '', 1),
                        original[LAYOUT]+REGISTRATION,
                        original[LAYOUT].replace('Compare-Object', 'Write-Output', 1),
                        original[LAYOUT]+"\n# unrelated layout change\n"):
            values = dict(original)
            values[LAYOUT] = changed
            with self.subTest(change=changed[-100:]), self.assertRaises(ValueError):
                isolation.audit(values)
        self.assertEqual(isolation.audit(original)['native_programs'], 96)

    @unittest.skipUnless(shutil.which('pwsh'), 'PowerShell unavailable: literal/legacy contracts still run')
    def test_real_powershell_layout_accepts_source_and_rejects_missing_extra_files(self):
        # Catch the actual exception before ConciseView adds ANSI/wrapping. The
        # production gate is unchanged; require its full raw message and exit 1.
        with tempfile.TemporaryDirectory() as temp:
            wrapper = Path(temp)/'capture-layout-error.ps1'
            wrapper.write_text("""param([string]$LayoutScript, [string]$CppRoot)
$ErrorActionPreference = 'Stop'
try { & $LayoutScript -CppRoot $CppRoot }
catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
""", encoding='utf-8')
            def invoke(root):
                return subprocess.run([shutil.which('pwsh'), '-NoProfile', '-NonInteractive',
                    '-File', str(wrapper), '-LayoutScript', str(ROOT/LAYOUT), '-CppRoot', str(root)],
                    capture_output=True, text=True, timeout=30)
            result = invoke(ROOT/'cpp')
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('C++ responsibility layout contract passed.', result.stdout)
            self.assertEqual(result.stderr, '')
            root = Path(temp)/'cpp'
            # The actual gate reads directory membership, not source contents.
            for path in (ROOT/'cpp').rglob('*'):
                target = root/path.relative_to(ROOT/'cpp')
                if path.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                elif path.is_file():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.touch()
            helper = root/Path(CASES).relative_to('cpp')
            helper.unlink()
            result = invoke(root)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, '')
            self.assertEqual(result.stderr,
                f"Responsibility layout drift under '{helper.parent}':\n"
                '  missing: TargetWaveCacheEvidenceCases.hpp\n')
            helper.touch()
            (helper.parent/'Unregistered.hpp').touch()
            result = invoke(root)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, '')
            self.assertEqual(result.stderr,
                f"Responsibility layout drift under '{helper.parent}':\n"
                '  unexpected: Unregistered.hpp\n')

    def test_bilingual_contract_documents_keep_validation_and_cost_boundaries(self):
        for path in ('docs/TARGET_WAVE_CACHE_EVIDENCE.md','docs/TARGET_WAVE_CACHE_EVIDENCE_ZH.md'):
            text=read(path)
            for token in ('run_recorded', 'TargetCompileWave', 'CompileCacheEvidence',
                          'run/inspect/CLI', '15', '96', 'v5.7.0'):
                self.assertIn(token,text)
            self.assertIn('Native',text)
            self.assertIn('Release',text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
