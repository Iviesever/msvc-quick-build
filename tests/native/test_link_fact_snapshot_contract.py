"""Pure structural regression controls, not Windows or performance qualification.

Baseline d3fc25572fb96654a79e14074fe88c63b1f7f80d. All previous 89 native
programs and the existing public/JSON/observation boundaries retain their bytes.
Actual schema/negative tests live in the two new C++ test programs.
"""
from pathlib import Path
import generation_archive_extension as archive_extension
import hashlib
import json
import re
import unittest
import accepted_reporting_extension as reporting_extension

ROOT = Path(__file__).resolve().parents[2]
NEW_TESTS = {
    "cpp/tests/core/cache/link_fact_snapshot_tests.cpp",
    "cpp/tests/orchestration/incremental/link_fact_snapshot_projection_tests.cpp",
}
NEW_TUS = {
    "cpp/src/core/cache/LinkFactSnapshot.cpp",
    "cpp/src/orchestration/incremental/LinkFactSnapshotProjection.cpp",
}
PINS = {'cpp/include/mqb/json/Json.hpp': 'ee4d127d2a9940ada21c8f908b5e9b5403c0df486766f613be940bc326ac6099', 'cpp/src/json/Json.cpp': '79c3272c15f97782179c28ac6556c6f17a04e3dce147b735f38bf6037ba409f3', 'cpp/include/mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp': '2c92d6de2c6bf9f262d5152f6e76afbec364401092f36b2916c31466ca5e825d', 'cpp/include/mqb/orchestration/ObservedLinkCompletion.hpp': '5980a2a9bb6c73046ad6c95513aa2dcce3409aa1fa953f35f92d9e2cd6c49422', 'cpp/include/mqb/core/BuildArtifactRecord.hpp': 'c0e7d0d7342647d9fb2b4fb9d4ebecaa0fe392f1123a2bfd54497ef3c39a75e4', 'cpp/include/mqb/core/StorageFileObservation.hpp': 'eec03bd186d0dd92fa816972b8fb8629d16d8b8746e4675b28e4705f0a66630f', 'cpp/include/mqb/platform/windows/StorageInventory.hpp': '39069edcc2ae944a7794e11f5736a5d32f7645afff748d4c98dd244c164dd717', 'cpp/src/orchestration/incremental/MsvcIncrementalLinkCoordinator.cpp': 'e403c70c90ca35a954e969055eac2a487008a86008cd784bd103a0ea6019260c', 'cpp/src/orchestration/incremental/ObservedLinkCompletion.cpp': '09f8b65bba192693ce9a8e2a4b64d54af37aedf5c1171d53e4658ae5c06d04f9', 'cpp/src/platform/windows/StorageFileObservation.cpp': '5f4e0018ed3829e10f2ac68663d658e7f343c266eb60a3bf237ad783cbf5a4dc', 'VERSION': 'e51f139763c0958d77e9454ad75fc927c601dad6512842bdf94a79a8e755374c'}
MODEL_TESTS = {"cpp/tests/orchestration/incremental/artifact_generation_model_tests.cpp"}
ARCHIVE_TESTS = {'cpp/tests/orchestration/incremental/link_completion_archive_tests.cpp', 'cpp/tests/e2e/mqb_link_completion_archive_e2e_tests.cpp'}
FILE_TESTS = {'cpp/tests/e2e/mqb_snapshot_file_e2e_tests.cpp', 'cpp/tests/platform/windows/link_fact_file_transfer_tests.cpp'}
MODEL_TU = "cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp"
FILE_TU = "cpp/src/platform/windows/LinkFactSnapshotFile.cpp"

OLD_TESTS_SHA = "cb9bb48dc68b6c27a0e2154d33ac66132cf850bd5239b0701647c46c1185084f"

def read(name):
    return archive_extension.read_text(ROOT, name)

def digest(value):
    return hashlib.sha256(value.encode("utf-8")).hexdigest()

def legacy_tests(values):
    old = {n: v for n, v in values.items() if n not in NEW_TESTS and n not in FILE_TESTS and n not in ARCHIVE_TESTS and n not in MODEL_TESTS}
    old = reporting_extension.legacy_view(old, read(reporting_extension.HELPER))
    encoded = ''.join(f'{n}\0{digest(v)}\n' for n, v in sorted(old.items()))
    if len(old) != 89 or digest(encoded) != OLD_TESTS_SHA:
        raise ValueError("existing native test changed or replaced")

def pure_source(text):
    # Limited literal source check; actual runtime behavior is tested in C++.
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    forbidden = ("CreateFile", "ifstream", "ofstream", "fstream", "current_path(",
                 "exists(", "last_write_time(", "file_size(", "canonical(",
                 "lexically_normal(", "run_recorded(", "observe_storage_file(",
                 "observe_link_completion(", "system(", "popen(", "::now(")
    if any(token in body for token in forbidden):
        raise ValueError("snapshot acquired filesystem/execution authority")


def repeated_fixture_copies(text):
    """Narrow source contract for the two count-limit fixtures, not a C++ alias analyzer."""
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    for collection, sample, count in (
        ("v.warnings", "warning_sample", "warnings"),
        ("v.observation.issues", "issue_sample", "issues"),
    ):
        # const auto copies the element; const auto& would preserve the bad alias.
        owned = rf"const\s+auto\s+{sample}\s*=\s*{re.escape(collection)}\.front\(\)\s*;"
        fill = rf"{re.escape(collection)}\.assign\(LinkFactSnapshotLimits::{count},\s*{sample}\)\s*;"
        if not re.search(owned + r"\s*" + fill, body):
            raise ValueError("count-limit fixture must copy its source before assign")

class SnapshotContracts(unittest.TestCase):
    def test_repeated_fixture_sources_are_owned(self):
        repeated_fixture_copies(read("cpp/tests/core/cache/link_fact_snapshot_tests.cpp"))

    def test_repeated_fixture_sources_reject_self_alias(self):
        text = read("cpp/tests/core/cache/link_fact_snapshot_tests.cpp")
        for collection, sample, count in (
            ("v.warnings", "warning_sample", "warnings"),
            ("v.observation.issues", "issue_sample", "issues"),
        ):
            for old, new in (
                (f"const auto {sample}", f"const auto& {sample}"),
                (f"{count}, {sample});", f"{count}, {collection}[0]);"),
            ):
                self.assertIn(old, text)
                with self.assertRaises(ValueError):
                    repeated_fixture_copies(text.replace(old, new, 1))

    def test_existing_boundaries_unchanged(self):
        for name, pin in PINS.items():
            self.assertEqual(digest(read(name)), pin, name)

    def test_all_previous_native_programs_preserved(self):
        values = {p.relative_to(ROOT).as_posix(): p.read_text(encoding="utf-8")
                  for p in archive_extension.legacy_native_paths(ROOT)}
        self.assertEqual(len(values), 96)
        self.assertTrue(NEW_TESTS <= values.keys())
        legacy_tests(values)

    def test_legacy_test_mutation_refused(self):
        values = {p.relative_to(ROOT).as_posix(): p.read_text(encoding="utf-8")
                  for p in archive_extension.legacy_native_paths(ROOT)}
        name = "cpp/tests/e2e/mqb_file_observation_e2e_tests.cpp"
        values[name] += "// changed"
        with self.assertRaises(ValueError):
            legacy_tests(values)

    def test_exact_production_inventory(self):
        manifest = json.loads(read("cpp/mqb.json"))
        declared = ["cpp/src/app/main.cpp"] + ["cpp/"+n for n in manifest["discovery"]["extra_sources"]]
        actual = {p.relative_to(ROOT).as_posix() for p in archive_extension.legacy_product_paths(ROOT)}
        self.assertEqual(len(declared), 96)
        self.assertEqual(len(declared), len(set(declared)))
        self.assertEqual(set(declared), actual)
        self.assertTrue(NEW_TUS <= actual)

    def test_exact_driver_inventory(self):
        driver = read("tests/native/run_native_tests.ps1")
        self.assertIn("$allTestFiles.Count -ne 96", driver)
        self.assertEqual(driver.count("96"), 3)

    def test_layout_registers_new_files(self):
        layout = read("tests/native/assert_cpp_layout.ps1")
        for name in NEW_TUS | NEW_TESTS:
            self.assertEqual(layout.count("'"+Path(name).name+"'"), 1)

    def test_new_code_has_no_execution_or_io(self):
        for name in NEW_TUS:
            pure_source(read(name))

    def test_io_adoption_negative_control(self):
        for token in ("std::filesystem::current_path()", "observe_storage_file(p)",
                      "std::ifstream input", "std::chrono::system_clock::now()"):
            with self.assertRaises(ValueError):
                pure_source(read("cpp/src/orchestration/incremental/LinkFactSnapshotProjection.cpp") + "\n" + token)

    def test_core_dependency_direction(self):
        for name in ("cpp/include/mqb/core/LinkFactSnapshot.hpp", "cpp/src/core/cache/LinkFactSnapshot.cpp"):
            for include in re.findall(r'#include "([^"]+)"', read(name)):
                self.assertFalse(include.startswith(("mqb/msvc/", "mqb/orchestration/", "mqb/platform/", "mqb/app/")))

    def test_unique_existing_json_grammar(self):
        codec = read("cpp/src/core/cache/LinkFactSnapshot.cpp")
        self.assertEqual(codec.count("json::parse(text)"), 1)
        self.assertLess(codec.index("preflight(text);"), codec.index("json::parse(text)"))
        self.assertIn("std::from_chars", codec)
        self.assertNotRegex(codec, r"\b(?:stod|strtod|atof)\s*\(")

    def test_default_production_has_no_snapshot_adoption(self):
        for p in archive_extension.legacy_product_paths(ROOT):
            if p.relative_to(ROOT).as_posix() in NEW_TUS | {FILE_TU, MODEL_TU}:
                continue
            text = archive_extension.read_text(ROOT, p.relative_to(ROOT).as_posix())
            self.assertNotRegex(text, r"(?:capture|encode|decode)_link_fact_snapshot\s*\(", str(p))
            self.assertNotIn('LinkFactSnapshot.hpp', text, str(p))
            self.assertNotIn('LinkFactSnapshotProjection.hpp', text, str(p))

    def test_bilingual_codec_documentation(self):
        en = read("docs/LINK_FACT_SNAPSHOT.md")
        zh = read("docs/LINK_FACT_SNAPSHOT_ZH.md")
        self.assertIn("LINK_FACT_SNAPSHOT_ZH.md", en)
        self.assertIn("LINK_FACT_SNAPSHOT.md", zh)
        self.assertEqual(len(re.findall(r"^## ", en, re.M)), len(re.findall(r"^## ", zh, re.M)))
        checker = read("tests/docs/verify_bilingual_docs.ps1")
        self.assertIn("'docs/LINK_FACT_SNAPSHOT.md'", checker)
        self.assertIn("'docs/LINK_FACT_SNAPSHOT_ZH.md'", checker)

if __name__ == "__main__":
    unittest.main(verbosity=2)
