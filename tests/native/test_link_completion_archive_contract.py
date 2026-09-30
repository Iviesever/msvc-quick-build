"""Explicit archival wiring contracts; no native IO or performance execution."""
from pathlib import Path
import hashlib
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = "cpp/include/mqb/orchestration/LinkCompletionArchive.hpp"
TU = "cpp/src/orchestration/incremental/LinkCompletionArchive.cpp"
OP = "cpp/src/orchestration/incremental/LinkCompletionArchiveOperation.hpp"
NEW_TESTS = {"cpp/tests/e2e/mqb_link_completion_archive_e2e_tests.cpp", "cpp/tests/orchestration/incremental/link_completion_archive_tests.cpp"}
PINS = {'cpp/src/orchestration/incremental/LinkFactSnapshotProjection.cpp': 'eabba24ce173ee940d169c218125cb6b0f3cd0e9346abada477d6265632fac1b', 'cpp/src/core/cache/LinkFactSnapshot.cpp': '7e8f574780532ed9a1289c728b58a7b57b53402cb69f690f07847b3595f4a458', 'cpp/src/platform/windows/LinkFactSnapshotFile.cpp': 'ba3941881bda2d57044414284d5bf82b531463d110972e4d9deb51c7e9b75419', 'cpp/src/platform/windows/LinkFactFileTransfer.hpp': '9dc4aac2f376cd3a954ef9530a0d9099ef9d8dcf5410f1c7d08c12947de51cd0', 'cpp/src/orchestration/incremental/ObservedLinkCompletion.cpp': '09f8b65bba192693ce9a8e2a4b64d54af37aedf5c1171d53e4658ae5c06d04f9', 'cpp/include/mqb/orchestration/ObservedLinkCompletion.hpp': '5980a2a9bb6c73046ad6c95513aa2dcce3409aa1fa953f35f92d9e2cd6c49422', 'cpp/include/mqb/platform/windows/LinkFactSnapshotFile.hpp': '64ce1f58a10807801c7b5af7e58e0953575d694397898c3ae08088b4ff2a92ce', 'cpp/include/mqb/core/LinkFactSnapshot.hpp': '0f6dc6524d1ce7e4bbc151dce3d2b8eba38e6a83305b7eadcd09d44c4b04f036', 'VERSION': 'e51f139763c0958d77e9454ad75fc927c601dad6512842bdf94a79a8e755374c'}
OLD_TESTS = "530a6c00f33dbc6774700ef821f6d1a59b47ab5220e7e7ef5cf248c7d5ff590a"

def read(n): return (ROOT/n).read_text(encoding="utf-8")
def sha(b): return hashlib.sha256(b).hexdigest()
def body(s): return re.sub(r"//[^\n]*|/\*.*?\*/", "", s, flags=re.S)
def no_execution(s):
    s=body(s)
    for token in ("run_recorded(", "observe_link_completion(", "observe_storage_file(", "read_link_fact_snapshot_file(", "current_path(", "exists(", "create_directories(", "remove(", "DeleteFile", "CreateFile", "while (", "for (", "catch ("):
        if token in s: raise ValueError("unexpected execution or fallback: "+token)

class ArchiveContracts(unittest.TestCase):
    def test_original_93_native_programs_unchanged(self):
        old={p.relative_to(ROOT).as_posix():p.read_text(encoding="utf-8").encode() for p in (ROOT/"cpp/tests").rglob("*_tests.cpp") if p.relative_to(ROOT).as_posix() not in NEW_TESTS}
        self.assertEqual(93,len(old))
        self.assertEqual(OLD_TESTS,sha(''.join(f'{n}\0{sha(v)}\n' for n,v in sorted(old.items())).encode()))
    def test_existing_implementations_and_authority_flags_unchanged(self):
        for n,h in PINS.items(): self.assertEqual(h,sha(read(n).encode()),n)
    def test_public_borrows_completed_only(self):
        s=body(read(PUBLIC))
        self.assertIn("const ObservedLinkResult& completed",s)
        for token in ("IncrementalLinkRequest", "ProcessRunner", "StoragePathObserver", "Create&&"):
            self.assertNotIn(token,s)
        self.assertIn("std::variant<LinkFactSnapshotError",s)
    def test_one_capture_one_create_and_no_automatic_readback(self):
        s=body(read(OP));self.assertEqual(s.count("capture_link_fact_snapshot(completed, capture_label)"),1)
        self.assertEqual(s.count("std::forward<Create>(create)(destination, *snapshot)"),1)
        self.assertLess(s.index("if (!snapshot)"),s.index("std::forward<Create>"))
        self.assertIn("platform::windows::create_link_fact_snapshot_file",read(TU))
        for n in (TU,OP):no_execution(read(n))
    def test_no_retry_or_build_negative_controls(self):
        for token in ("run_recorded(r)","observe_storage_file(p)","read_link_fact_snapshot_file(p)","while (again)","DeleteFileW(p)"):
            with self.assertRaises(ValueError):no_execution(read(OP)+"\n"+token)
    def test_no_default_callers(self):
        for p in (ROOT/"cpp/src").rglob("*.cpp"):
            if p.relative_to(ROOT).as_posix()==TU:continue
            self.assertNotIn("LinkCompletionArchive",p.read_text(encoding="utf-8"))
            self.assertNotRegex(body(p.read_text(encoding="utf-8")),r"\barchive_link_completion\s*\(")
    def test_exact_product_and_native_inventory(self):
        declared=["src/app/main.cpp"]+json.loads(read("cpp/mqb.json"))["discovery"]["extra_sources"]
        self.assertEqual(95,len(declared));self.assertEqual(len(declared),len(set(declared)))
        self.assertEqual(set(declared),{p.relative_to(ROOT/"cpp").as_posix() for p in (ROOT/"cpp/src").rglob("*.cpp")})
        self.assertEqual(95,len(list((ROOT/"cpp/tests").rglob("*_tests.cpp"))))
        self.assertIn("$allTestFiles.Count -ne 95",read("tests/native/run_native_tests.ps1"))
    def test_original_50_workflows_unchanged(self):
        values={p.relative_to(ROOT).as_posix():sha(p.read_text(encoding="utf-8").encode()) for p in (ROOT/".github/workflows").glob("*") if p.is_file()}
        self.assertEqual(50,len(values))
        self.assertEqual("2e928f795bff8a8148f4e3f7f40eb3ac869f7dc0f0ddf859ea1827a3fd560a3c",sha(''.join(f'{n}\0{v}\n' for n,v in sorted(values.items())).encode()))
    def test_e2e_retains_counts_and_real_file_operations(self):
        s=read("cpp/tests/e2e/mqb_link_completion_archive_e2e_tests.cpp")
        for token in ("linking.run_recorded(req)","archive_link_completion(completed, path, label)","read_link_fact_snapshot_file(path)","builds == 4 && observations == 3 && archives == 7", "runner.compiles == 1 && runner.links == 3", "ERROR_SHARING_VIOLATION", "original-image.bin", "original-linkcache.bin"):
            self.assertIn(token,s)
    def test_bilingual_docs(self):
        en=read("docs/LINK_COMPLETION_ARCHIVE.md");zh=read("docs/LINK_COMPLETION_ARCHIVE_ZH.md")
        self.assertEqual(en.count("\n## "),zh.count("\n## "))
        self.assertIn("LINK_COMPLETION_ARCHIVE_ZH.md",en);self.assertIn("LINK_COMPLETION_ARCHIVE.md",zh)

if __name__=="__main__":unittest.main(verbosity=2)
