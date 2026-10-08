"""Rich-generation source/lineage contracts. No native build or benchmark."""
import copy
import hashlib
from pathlib import Path
import unittest
from unittest.mock import patch
import recorded_generation_extension as ext
import test_artifact_generation_model as old

ROOT = Path(__file__).resolve().parents[2]
HEADER = "cpp/include/mqb/orchestration/ArtifactGenerationModel.hpp"
SOURCE = "cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp"
NATIVE = "cpp/tests/orchestration/incremental/artifact_generation_model_tests.cpp"
OLD_NATIVE = "d5a617248f26f2c6ed37951ac9a93a981ddbda61822847adaa5027ce8c34a29c"

def read(path):
    return (ROOT/path).read_text(encoding="utf-8")

class RecordedGenerationContracts(unittest.TestCase):
    def test_all_78_old_model_assertions_remain(self):
        text = read(NATIVE)
        first = "// BEGIN recorded generation model contracts\n"
        last = "// END recorded generation model contracts\n"
        self.assertEqual(1,text.count(first)); self.assertEqual(1,text.count(last))
        start = text.index(first)-1  # new block's leading newline
        end = text.index(last)+len(last)+1
        text = text[:start]+text[end:]
        text = text.replace('#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"\n','',1)
        text = text.replace('recorded_generation_contracts::run();','',1)
        self.assertEqual(OLD_NATIVE,hashlib.sha256(text.encode()).hexdigest())

    def test_exact_e2e_inverse_preserves_all_predecessor_bytes(self):
        self.assertEqual("33c57a10fc77cbe28da5487710e94ca8e5a83c55",ext.SPEC["base"])
        self.assertEqual(3,len(ext.SPEC["files"]))
        for path,rule in ext.SPEC["files"].items():
            self.assertEqual(rule["prior"],ext.digest(ext.legacy_text(path,read(path))))

    def test_changed_new_or_old_e2e_bytes_refused(self):
        for path in ext.SPEC["files"]:
            for damaged in (read(path)+"\n",read(path).replace("generation(", "ignored_generation(",1)):
                with self.subTest(path=path),self.assertRaises(ValueError):
                    ext.legacy_text(path,damaged)

    def test_missing_generation_checks_not_admitted_as_current(self):
        for path in ext.SPEC["files"]:
            prior=ext.legacy_text(path,read(path))
            with self.assertRaises(ValueError):
                ext.legacy_text(path,prior)

    def test_bad_inverse_prior_is_refused(self):
        altered=copy.deepcopy(ext.SPEC)
        path=next(iter(altered["files"]))
        altered["files"][path]["prior"]="0"*64
        with patch.object(ext,"SPEC",altered),self.assertRaises(ValueError):
            ext.legacy_text(path,read(path))

    def test_new_entry_is_pure_and_does_not_hash_recipes(self):
        old.pure(read(SOURCE));old.pure(read(HEADER))
        for forbidden in ("BuildSignature::for_","json::parse","environment.begin","CompileCacheFile::load","CompileCacheFile::save"):
            self.assertNotIn(forbidden,read(SOURCE))

    def test_borrowed_input_and_owned_context_are_explicit(self):
        text=read(HEADER)
        for token in ("reference_wrapper<const RecordedTargetResult>","reference_wrapper<const RecordedStaticTargetResult>",
                      "std::vector<std::vector<CompileStorageContext>> compiles;","projection_error","record_index"):
            self.assertIn(token,text)

    def test_strict_projection_and_lineage_are_shared(self):
        text=read(SOURCE)
        self.assertEqual(2,text.count("finish_model(std::move("))
        self.assertEqual(1,text.count("associate_artifact_storage(projected, empty, key)"))
        self.assertIn("return project_storage_references(value, key,",text)
        self.assertIn("projection.error().message, i, projection.error()",text)

    def test_budget_precedes_owned_projection_for_whole_batch(self):
        text=read(SOURCE).split("model_recorded_artifact_generations(",1)[1]
        self.assertLess(text.index("Fields count{preflight, {}, true}"),text.index("ArtifactGenerationModel model;"))
        self.assertLess(text.index("shape.check(count, value)"),text.index("project_storage_references(value, key,"))
        self.assertIn("ArtifactGenerationLimits::stages - 1",text)

    def test_actual_options_identities_and_signatures_participate(self):
        text=read(SOURCE).split("void recorded_recipe(",1)[1].split("void snapshot_budget",1)[0]
        for token in ("e.request.options","e.request.working_directory","e.inspection_toolchain.identity",
                      "c.toolchain","c.signature.digest().high","c.signature.digest().low","c.dependencies","c.include_search_roots"):
            self.assertIn(token,text)
        for token in ("force_rebuild","e.state","warnings","environment"):
            self.assertNotIn(token,text)

    def test_default_product_paths_and_counts_unchanged(self):
        for path in (ROOT/"cpp/src").rglob("*.cpp"):
            if path.relative_to(ROOT).as_posix()==SOURCE: continue
            self.assertNotIn("model_recorded_artifact_generations(",path.read_text())
        self.assertEqual(96,len(list((ROOT/"cpp/src").rglob("*.cpp"))))
        self.assertEqual(96,len(list((ROOT/"cpp/tests").rglob("*_tests.cpp"))))
        self.assertEqual("5.6.0",read("VERSION").strip())

    def test_scope_docs_and_false_authority(self):
        en=read("docs/ARTIFACT_GENERATIONS.md");zh=read("docs/ARTIFACT_GENERATIONS_ZH.md")
        self.assertEqual(en.count("\n## "),zh.count("\n## "))
        for name in ("producer_identity_verified","current_content_verified","complete_producer_inventory","deletion_authorized"):
            self.assertEqual(2,read(HEADER).count(f"static constexpr bool {name} = false;"))
        self.assertIn("not a total",en)
        self.assertIn("model_recorded_artifact_generations",en+zh)

if __name__ == "__main__":
    unittest.main(verbosity=2)
