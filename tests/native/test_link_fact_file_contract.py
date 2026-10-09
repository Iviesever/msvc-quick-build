"""Registered file-layer boundaries. Static checks, not native IO/performance proof."""
from pathlib import Path
import generation_archive_extension as archive_extension
import hashlib
import json
import re
import unittest
from noop_identity_slots_workflow_contract import legacy_workflow_view
import accepted_reporting_extension as reporting_extension

ROOT = Path(__file__).resolve().parents[2]
PINS = {'VERSION': 'e51f139763c0958d77e9454ad75fc927c601dad6512842bdf94a79a8e755374c', 'cpp/include/mqb/core/LinkFactSnapshot.hpp': '0f6dc6524d1ce7e4bbc151dce3d2b8eba38e6a83305b7eadcd09d44c4b04f036', 'cpp/src/core/cache/LinkFactSnapshot.cpp': '7e8f574780532ed9a1289c728b58a7b57b53402cb69f690f07847b3595f4a458', 'cpp/src/orchestration/incremental/LinkFactSnapshotProjection.cpp': 'eabba24ce173ee940d169c218125cb6b0f3cd0e9346abada477d6265632fac1b', 'cpp/src/orchestration/incremental/ObservedLinkCompletion.cpp': '09f8b65bba192693ce9a8e2a4b64d54af37aedf5c1171d53e4658ae5c06d04f9', 'cpp/src/platform/windows/PhysicalPath.hpp': '0251fd534f01432d67dce6b8f8b4c8ce8e050be3daeab61803082eb5a67fbf02', 'cpp/src/platform/windows/StorageFileObservation.cpp': '5f4e0018ed3829e10f2ac68663d658e7f343c266eb60a3bf237ad783cbf5a4dc', 'cpp/src/platform/windows/StorageReadPrimitives.hpp': 'a0e5ae3edad3a0be67449318ee36c77d7d0b0b6f0c5d841ec82eadb32815f627'}
WORKFLOW_SHA = '2e928f795bff8a8148f4e3f7f40eb3ac869f7dc0f0ddf859ea1827a3fd560a3c'
OLD_NATIVE_SHA = 'e4ae1deb69aec53786ca81e2a4acbdc935687770ec1d0dfa554abe60c78c207c'
MODEL_TESTS = {"cpp/tests/orchestration/incremental/artifact_generation_model_tests.cpp"}
ARCHIVE_TESTS = {'cpp/tests/orchestration/incremental/link_completion_archive_tests.cpp', 'cpp/tests/e2e/mqb_link_completion_archive_e2e_tests.cpp'}
NEW_TESTS = {'cpp/tests/platform/windows/link_fact_file_transfer_tests.cpp', 'cpp/tests/e2e/mqb_snapshot_file_e2e_tests.cpp'}
TU = 'cpp/src/platform/windows/LinkFactSnapshotFile.cpp'
TRANSFER = 'cpp/src/platform/windows/LinkFactFileTransfer.hpp'

def sha(data): return hashlib.sha256(data).hexdigest()
def read(name): return archive_extension.read_text(ROOT, name)
def canonical(name): return read(name).encode('utf-8')
def old_tests(values):
    old = {n:v for n,v in values.items() if n not in NEW_TESTS and n not in ARCHIVE_TESTS and n not in MODEL_TESTS}
    old = reporting_extension.legacy_view(old, read(reporting_extension.HELPER))
    text = ''.join(f'{n}\0{sha(v)}\n' for n,v in sorted(old.items()))
    if len(old) != 91 or sha(text.encode()) != OLD_NATIVE_SHA:
        raise ValueError('old native test changed')
def no_destructive_policy(text):
    # Literal source boundary in addition to actual native no-clobber cases.
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    for token in ('CREATE_ALWAYS','OPEN_ALWAYS','TRUNCATE_EXISTING','DeleteFile','MoveFile',
                  'ReplaceFile','SetEndOfFile','FILE_FLAG_DELETE_ON_CLOSE','create_directories',
                  'remove(', 'exists(', 'observe_storage_file(', 'load_cache('):
        if token in body: raise ValueError('destructive/default capability: '+token)

class FileContracts(unittest.TestCase):
    def test_previous_91_native_programs_exact(self):
        values={p.relative_to(ROOT).as_posix():p.read_text(encoding='utf-8').encode() for p in archive_extension.legacy_native_paths(ROOT)}
        self.assertEqual(len(values),96); old_tests(values)
        values['cpp/tests/core/cache/link_fact_snapshot_tests.cpp'] += b'// changed'
        with self.assertRaises(ValueError): old_tests(values)
    def test_existing_codec_and_private_primitives_unchanged(self):
        for n,h in PINS.items(): self.assertEqual(sha(canonical(n)),h,n)
    def test_all_existing_workflows_identical(self):
        actual={p.relative_to(ROOT).as_posix():sha(p.read_text(encoding='utf-8').encode()) for p in (ROOT/'.github/workflows').glob('*') if p.is_file()}
        actual=legacy_workflow_view(actual)
        self.assertEqual(sha(''.join(f'{k}\0{v}\n' for k,v in sorted(actual.items())).encode()),WORKFLOW_SHA)
    def test_exact_product_manifest(self):
        actual={p.relative_to(ROOT/'cpp').as_posix() for p in archive_extension.legacy_product_paths(ROOT)}
        declared=['src/app/main.cpp']+json.loads(read('cpp/mqb.json'))['discovery']['extra_sources']
        self.assertEqual(len(declared),96); self.assertEqual(len(set(declared)),96); self.assertEqual(set(declared),actual)
    def test_create_new_no_destructive_fallback(self):
        text=read(TU); no_destructive_policy(text)
        self.assertIn('nullptr, CREATE_NEW,',text)
        self.assertIn('GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0,',text)
        self.assertLess(text.index('encode_link_fact_snapshot(value)'),text.index('Transport io{Handle{::CreateFileW'))
        for token in ('CREATE_ALWAYS','DeleteFileW(path)','MoveFileW(a,b)','std::filesystem::exists(p)'):
            with self.assertRaises(ValueError): no_destructive_policy(text+'\n'+token)
    def test_real_native_transport_uses_the_tested_loops(self):
        text=read(TU)
        self.assertIn('ff::write_document(io, *text)',text); self.assertIn('ff::read_document(io)',text)
        tests=read('cpp/tests/platform/windows/link_fact_file_transfer_tests.cpp')
        self.assertIn('f::write_document(io, text)',tests); self.assertIn('f::read_document(in)',tests)
        self.assertIn('std::exchange(handle.value, INVALID_HANDLE_VALUE)',text)
    def test_path_checks_before_normalization(self):
        text=read(TU)
        self.assertLess(text.index('utf16_path(path.native())'),text.index('path.lexically_normal()'))
        self.assertIn('detail::physical_path_supported(path, WriteExtent::file)',text)
        self.assertIn('detail::pin(current)',text); self.assertIn('FILE_ATTRIBUTE_REPARSE_POINT',text)
        self.assertIn('VOLUME_NAME_GUID',text)
    def test_read_reuses_the_original_codec_and_size_limit(self):
        text=read(TU); self.assertEqual(text.count('decode_link_fact_snapshot(*text)'),1)
        body=read(TRANSFER)
        self.assertLess(body.index('LinkFactSnapshotLimits::document_bytes',body.index('read_document')),body.index('std::string text('))
        self.assertIn('if (*tail) return fail(Code::changed',body)
    def test_no_default_adoption(self):
        for p in archive_extension.legacy_product_paths(ROOT):
            if p.relative_to(ROOT).as_posix()==TU: continue
            self.assertNotRegex(p.read_text(encoding='utf-8'),r'\b(?:create|read)_link_fact_snapshot_file\s*\(')
    def test_documents_have_matching_sections(self):
        en=read('docs/LINK_FACT_FILE.md'); zh=read('docs/LINK_FACT_FILE_ZH.md')
        self.assertEqual(en.count('\n## '),zh.count('\n## '))
        self.assertIn('LINK_FACT_FILE_ZH.md',en); self.assertIn('LINK_FACT_FILE.md',zh)

if __name__=='__main__': unittest.main(verbosity=2)
