"""Deterministic isolation/extraction contracts; not ABI or performance evidence.

Reference main: 433c127566619009579f3f09df3e5627279b24da.
Original HOLD candidate: bd12a59e9894068384d1a9b56b052ce5fec86098.
No original executable, native API, benchmark or capture is run here.
"""
from pathlib import Path
import generation_archive_extension as archive_extension
import hashlib
import json
import posixpath
import re
import unittest
import accepted_reporting_extension as reporting_extension

ROOT = Path(__file__).resolve().parents[2]
SCAN = 'cpp/src/platform/windows/StorageInventory.cpp'
DOMAIN = 'cpp/src/platform/windows/WindowsWriteDomain.cpp'
PRIM = 'cpp/src/platform/windows/StorageReadPrimitives.hpp'
PRED = 'cpp/src/platform/windows/PhysicalPath.hpp'
ADAPT = 'cpp/src/orchestration/incremental/ObservedLinkCompletion.cpp'
E2E = 'cpp/tests/e2e/mqb_file_observation_e2e_tests.cpp'
CORE = 'cpp/include/mqb/core/StorageFileObservation.hpp'
PLAT = 'cpp/src/platform/windows/StorageFileObservation.cpp'
PINS = {'cpp/src/platform/windows/StorageInventory.cpp': 'de49412b7e444420cf4bf5eed5b6679f1df9b126e7a88d03a581d96cebcf706e', 'cpp/src/platform/windows/WindowsWriteDomain.cpp': '44a33cb5d7376d153e88267502df8ffa92a4a85facd0ccc388ac5fed58c0356e', 'cpp/include/mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp': '2c92d6de2c6bf9f262d5152f6e76afbec364401092f36b2916c31466ca5e825d', 'cpp/include/mqb/platform/windows/StorageInventory.hpp': '39069edcc2ae944a7794e11f5736a5d32f7645afff748d4c98dd244c164dd717', 'cpp/src/orchestration/incremental/MsvcIncrementalLinkCoordinator.cpp': 'e403c70c90ca35a954e969055eac2a487008a86008cd784bd103a0ea6019260c', 'cpp/mqb.json': 'bae86741d8e5dc224d1eb11a76921402072d4696f9b5da5036f423a93f6dc878', 'tests/native/run_native_tests.ps1': '4a770ce3c33ad4ffcdb3f19c8e1c2037d65631ed46a44a2c9d4c77be0541defa', 'tests/native/assert_cpp_layout.ps1': '392a1a938d26cdf0f4320587519fee9be05f52978217774a8b23f7528f18ba68'}
LEGACY_TESTS_SHA = '9c5a2143783687924e0449e01536a686d234242c295afd5b00f3c201bf78316d'
OLD_OBSERVER_SHA = '1ff5775264219b16a85e44aacb46ab256a6c6272851614c409262b333caedd50'
OLD_MODEL_SHA = 'eec03bd186d0dd92fa816972b8fb8629d16d8b8746e4675b28e4705f0a66630f'
FILE_TU = 'cpp/src/platform/windows/LinkFactSnapshotFile.cpp'
ARCHIVE_TESTS = {'cpp/tests/orchestration/incremental/link_completion_archive_tests.cpp', 'cpp/tests/e2e/mqb_link_completion_archive_e2e_tests.cpp'}
MODEL_TESTS = {"cpp/tests/orchestration/incremental/artifact_generation_model_tests.cpp"}
MODEL_TU = "cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp"
ARCHIVE_TU = 'cpp/src/orchestration/incremental/LinkCompletionArchive.cpp'
FILE_TESTS = {'cpp/tests/e2e/mqb_snapshot_file_e2e_tests.cpp', 'cpp/tests/platform/windows/link_fact_file_transfer_tests.cpp'}
SNAPSHOT_TESTS = {
    'cpp/tests/core/cache/link_fact_snapshot_tests.cpp',
    'cpp/tests/orchestration/incremental/link_fact_snapshot_projection_tests.cpp',
}
SNAPSHOT_TUS = {
    'cpp/src/core/cache/LinkFactSnapshot.cpp',
    'cpp/src/orchestration/incremental/LinkFactSnapshotProjection.cpp',
}
OLD_NATIVE_BODY_SHA = '277cd33fe295ba4b473f01dee8aeff7a783653ddd57e4d7c19dc724621bf8d27'

def need(ok, message):
    if not ok:
        raise ValueError(message)

def digest(value):
    if isinstance(value, str):
        value = value.encode()
    return hashlib.sha256(value).hexdigest()

def read_tree(repo):
    return {str(p.relative_to(repo)).replace('\\', '/'): p.read_text(encoding='utf-8')
            for folder in ('cpp', 'tests/native') for p in (repo / folder).rglob('*')
            if p.is_file() and p.suffix in ('.hpp', '.cpp', '.ps1', '.json')}

def section(value, first, last):
    need(value.count(first) == 1 and value.count(last) == 1, 'non-unique extraction anchors')
    return value[value.index(first):value.index(last)]

def without_comments(value):
    # Limited literal-include model; no preprocessor/macro/AST claims.
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', value, flags=re.S))

def audit(files):
    files = archive_extension.historical_tree(files)
    # Reverse the three known blocks and one include, then check the WHOLE
    # original file, so unrelated scanner/alias/share changes are rejected.
    primitives = files[PRIM]
    handle = section(primitives, 'struct Handle {', 'inline std::wstring native_path(')
    paths = section(primitives, 'inline std::wstring native_path(', 'inline std::string physical_id(')
    paths = paths.replace('inline std::wstring native_path(', 'std::wstring native_path(')
    paths = paths.replace('inline Handle pin(', 'Handle pin(')
    identity = section(primitives, 'inline std::string physical_id(', '\n} // namespace mqb::platform::windows::detail')
    identity = identity.replace('inline std::string physical_id(', 'std::string physical_id(')
    scanner = files[SCAN].replace('#include "StorageReadPrimitives.hpp"\n', '')
    scanner = scanner.replace('using detail::Handle;\n', handle)
    scanner = scanner.replace('using detail::native_path;\nusing detail::pin;\n', paths)
    scanner = scanner.replace('using detail::physical_id;\n', identity)
    need(digest(scanner) == PINS[SCAN], 'scanner extraction changed original body')

    predicate = section(files[PRED], '// This initial inventory boundary', '} // namespace mqb::platform::windows::detail')
    predicate = predicate.replace('inline bool physical_path_supported(', 'bool inventory_path_supported(')
    domain = files[DOMAIN].replace('#include "PhysicalPath.hpp"\n', '')
    domain = domain.replace('void observe_inventory_leaf(', predicate + 'void observe_inventory_leaf(', 1)
    domain = domain.replace('if (!detail::physical_path_supported(', 'if (!inventory_path_supported(')
    need(digest(domain) == PINS[DOMAIN], 'write-domain extraction changed original body')
    for name, pin in PINS.items():
        if name.endswith('MsvcIncrementalLinkCoordinator.cpp') or name.endswith('.hpp'):
            need(digest(files[name]) == pin, 'existing public facade/coordinator changed')

    # Preserve all 88 existing native programs, including V9 adoption.
    tests = {n: v for n, v in files.items() if n.startswith('cpp/tests/') and n.endswith('_tests.cpp')}
    need(len(tests) == 96 and E2E in tests and (SNAPSHOT_TESTS | FILE_TESTS | ARCHIVE_TESTS | MODEL_TESTS) <= tests.keys(),
         'native inventory must be the exact 88+1+2+2+2+1 union')
    old = {n: v for n, v in tests.items() if n != E2E and n not in SNAPSHOT_TESTS and n not in FILE_TESTS and n not in ARCHIVE_TESTS and n not in MODEL_TESTS}
    old = reporting_extension.legacy_view(old, files[reporting_extension.HELPER])
    encoded = ''.join(f'{n}\0{digest(v)}\n' for n, v in sorted(old.items()))
    need(digest(encoded) == LEGACY_TESTS_SHA, 'existing native test changed or replaced')
    driver = files['tests/native/run_native_tests.ps1']
    need(driver.count('96') == 3 and digest(driver.replace('96', '88')) ==
         PINS['tests/native/run_native_tests.ps1'], 'native driver policy changed')
    layout = files['tests/native/assert_cpp_layout.ps1']
    # Remove only the exact registered test helper, then keep the entire legacy
    # layout fingerprint check below. Unknown or duplicated additions still fail.
    wave_registration = ",\n        'TargetWaveCacheEvidenceCases.hpp'"
    need(layout.count(wave_registration) == 1, 'target-wave layout registration missing or duplicated')
    layout = layout.replace(wave_registration, '', 1)
    reporting_registration = ", 'storage_report_format_cases.hpp'"
    need(layout.count(reporting_registration) == 1, 'reporting layout registration missing')
    layout = layout.replace(reporting_registration, '')
    for addition in ('ArtifactGenerationModel.cpp', 'artifact_generation_model_tests.cpp', 'LinkCompletionArchive.cpp', 'LinkCompletionArchiveOperation.hpp', 'link_completion_archive_tests.cpp', 'LinkFactSnapshot.cpp', 'LinkFactSnapshotProjection.cpp',
                     'link_fact_snapshot_tests.cpp', 'link_fact_snapshot_projection_tests.cpp'):
        line = f"\n        '{addition}',"
        need(layout.count(line) == 1, 'snapshot layout registration missing')
        layout = layout.replace(line, '')
    need(layout.count("\n        'ObservedLinkCompletion.cpp',") == 1, 'layout registration missing')
    need(digest(layout.replace("\n        'ObservedLinkCompletion.cpp',", '')) ==
         PINS['tests/native/assert_cpp_layout.ps1'], 'layout changed beyond registration')
    manifest = files['cpp/mqb.json']
    for name in ('src/orchestration/incremental/ArtifactGenerationModel.cpp', 'src/orchestration/incremental/LinkCompletionArchive.cpp', 'src/orchestration/incremental/ObservedLinkCompletion.cpp',
                 'src/platform/windows/StorageFileObservation.cpp',
                 'src/core/cache/LinkFactSnapshot.cpp',
                 'src/orchestration/incremental/LinkFactSnapshotProjection.cpp',
                 'src/platform/windows/LinkFactSnapshotFile.cpp'):
        line = '      "' + name + '",\n'
        need(manifest.count(line) == 1, 'new product TU not registered exactly once')
        manifest = manifest.replace(line, '')
    need(digest(manifest) == PINS['cpp/mqb.json'], 'manifest/policy changed beyond seven registered additions')
    declared = ['cpp/src/app/main.cpp'] + ['cpp/' + n for n in json.loads(files['cpp/mqb.json'])['discovery']['extra_sources']]
    actual = sorted(n for n in files if n.startswith('cpp/src/') and n.endswith('.cpp'))
    need(sorted(declared) == actual and len(set(declared)) == len(declared), 'production manifest mismatch')

    need(digest(files[CORE]) == OLD_MODEL_SHA, 'observation value/authority contract changed')
    observer = section(files[PLAT], 'StorageFileObservation observe_storage_file(', '} // namespace mqb::platform::windows')
    need(digest(observer) == OLD_OBSERVER_SHA, 'native observer body changed')
    native = section(files[E2E], 'void native_contracts(', '\n#endif\n} // namespace')
    native = native.replace('observe_link_completion(linking.run_recorded(r), observer)',
                            'linking.run_observed(r, observer)')
    need(digest(native) == OLD_NATIVE_BODY_SHA, 'existing Windows E2E behavior/budget changed')
    adapter = without_comments(files[ADAPT])
    for forbidden in ('run_recorded(', 'run_observed(', 'inspect(', 'ProcessRunner',
                      'IncrementalLinkRequest', 'ifstream', 'fstream', 'CreateFile',
                      'load_cache', 'system(', 'popen('):
        need(forbidden not in adapter, 'adapter acquired an execution/IO dependency')
    need('if (!completed) return std::unexpected(std::move(completed.error()));' in adapter,
         'failed result not forwarded before observation')
    need('ObservedLinkResult out{std::move(*completed), {}, false};' in adapter,
         'successful result is not moved unchanged')
    need(adapter.count('observer(path)') == 1, 'adapter must have one callback dispatch site')

    # Follow repository-local literal includes. All ORIGINAL production TUs must
    # remain unable to reach the new public observation type via this graph.
    graph = {}
    for name, value in files.items():
        if name.endswith(('.cpp', '.hpp')):
            edges = []
            for include in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', without_comments(value), re.M):
                for candidate in (posixpath.normpath(posixpath.join(posixpath.dirname(name), include)),
                                  'cpp/include/' + include):
                    if candidate in files:
                        edges.append(candidate)
                        break
            graph[name] = edges
    reachable = []
    for tu in actual:
        if tu in (ADAPT, PLAT, FILE_TU, ARCHIVE_TU, MODEL_TU) or tu in SNAPSHOT_TUS:
            continue
        seen = set()
        todo = [tu]
        while todo:
            name = todo.pop()
            if name in seen:
                continue
            seen.add(name)
            todo.extend(graph.get(name, ()))
        if CORE in seen:
            reachable.append(tu)
        need(not re.search(r'\b(?:observe_link_completion|observe_storage_file|run_observed)\s*\(',
                           without_comments(files[tu])), 'existing production caller adopted observation')
    need(not reachable, 'observation type leaks into an original production TU')
    return dict(native_programs=96, original_native_programs=88, production_tus=len(actual),
                source_view='exact_pre_archive', current_native_programs=97, current_production_tus=97,
                original_tus_reaching_observation=reachable, legacy_extractions_exact=True,
                new_benchmarks=0, performance_verified=False, clears_hold=False)


class Isolation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = read_tree(ROOT)

    def test_complete_actual_source(self):
        result = audit(self.files)
        self.assertEqual(96, result['production_tus'])
        self.assertFalse(result['clears_hold'])

    def test_public_facade_drift(self):
        files = dict(self.files)
        name = 'cpp/include/mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp'
        files[name] += '\n#include "mqb/core/StorageFileObservation.hpp"\n'
        with self.assertRaisesRegex(ValueError, 'facade'):
            audit(files)

    def test_share_mode_cannot_be_relaxed(self):
        files = dict(self.files)
        files[PRIM] = files[PRIM].replace('FILE_SHARE_READ,', 'FILE_SHARE_READ | FILE_SHARE_WRITE,')
        with self.assertRaisesRegex(ValueError, 'scanner'):
            audit(files)

    def test_unrelated_scanner_change_refused(self):
        files = dict(self.files)
        files[SCAN] = files[SCAN].replace('1000000u', '2000000u')
        with self.assertRaisesRegex(ValueError, 'scanner'):
            audit(files)

    def test_strict_path_rule_cannot_be_relaxed(self):
        files = dict(self.files)
        files[PRED] = files[PRED].replace('name == L".."', 'false')
        with self.assertRaisesRegex(ValueError, 'write-domain'):
            audit(files)

    def test_original_v9_program_cannot_be_removed(self):
        files = dict(self.files)
        del files['cpp/tests/msvc/toolchain/v9_reader_adoption_tests.cpp']
        with self.assertRaisesRegex(ValueError, 'inventory'):
            audit(files)

    def test_original_program_cannot_be_edited(self):
        files = dict(self.files)
        files['cpp/tests/msvc/toolchain/v9_reader_adoption_tests.cpp'] += '\n// drift\n'
        with self.assertRaisesRegex(ValueError, 'native test'):
            audit(files)

    def test_driver_options_cannot_change(self):
        files = dict(self.files)
        files['tests/native/run_native_tests.ps1'] += '\n# changed driver\n'
        with self.assertRaisesRegex(ValueError, 'driver policy'):
            audit(files)

    def test_missing_production_tu_refused(self):
        files = dict(self.files)
        files['cpp/mqb.json'] = files['cpp/mqb.json'].replace('      "src/platform/windows/StorageFileObservation.cpp",\n', '')
        with self.assertRaisesRegex(ValueError, 'registered'):
            audit(files)

    def test_native_observer_semantics_pinned(self):
        files = dict(self.files)
        files[PLAT] = files[PLAT].replace('State::missing_leaf', 'State::unavailable')
        with self.assertRaisesRegex(ValueError, 'observer body'):
            audit(files)

    def test_e2e_failure_budget_not_reduced(self):
        files = dict(self.files)
        files[E2E] = files[E2E].replace('link_calls == 8', 'link_calls == 7')
        with self.assertRaisesRegex(ValueError, 'E2E'):
            audit(files)

    def test_adapter_cannot_rebuild(self):
        files = dict(self.files)
        files[ADAPT] += '\nvoid wrong() { run_recorded(request); }\n'
        with self.assertRaisesRegex(ValueError, 'execution/IO'):
            audit(files)

    def test_adapter_cannot_drop_failed_input(self):
        files = dict(self.files)
        files[ADAPT] = files[ADAPT].replace('std::unexpected(std::move(completed.error()))', 'ObservedLinkResult{}')
        with self.assertRaisesRegex(ValueError, 'failed result'):
            audit(files)

    def test_existing_producer_cannot_adopt(self):
        files = dict(self.files)
        files['cpp/src/app/Application.cpp'] += '\nvoid wrong() { observe_link_completion(done, callback); }\n'
        with self.assertRaisesRegex(ValueError, 'production caller'):
            audit(files)

    def test_transitive_type_coupling_refused(self):
        files = dict(self.files)
        files['cpp/src/app/Application.cpp'] += '\n#include "mqb/orchestration/ObservedLinkCompletion.hpp"\n'
        with self.assertRaisesRegex(ValueError, 'leaks'):
            audit(files)

    def test_model_does_not_grant_authority(self):
        files = dict(self.files)
        files[CORE] = files[CORE].replace('deletion_authorized = false', 'deletion_authorized = true')
        with self.assertRaisesRegex(ValueError, 'authority'):
            audit(files)

    def test_crlf_has_same_text_contract(self):
        # read_text performs universal newline conversion, as it does for the
        # actual Windows checkout; binary artifacts are outside this checker.
        files = {n: v.replace('\n', '\r\n').replace('\r\n', '\n') for n, v in self.files.items()}
        self.assertEqual(audit(files), audit(self.files))


if __name__ == '__main__':
    unittest.main(verbosity=2)
