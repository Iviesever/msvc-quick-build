"""Selected-field model contracts; no filesystem or benchmark execution."""
from pathlib import Path
import generation_archive_extension as archive_extension
import recorded_storage_projection_contract as storage_projection_extension
import hashlib
import json
import re
import unittest
import accepted_reporting_extension as reporting_extension
from noop_identity_slots_workflow_contract import legacy_workflow_view

ROOT = Path(__file__).resolve().parents[2]
TU = 'cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp'
PUBLIC = 'cpp/include/mqb/orchestration/ArtifactGenerationModel.hpp'
TEST = 'cpp/tests/orchestration/incremental/artifact_generation_model_tests.cpp'
PINS = {
    'cpp/include/mqb/core/CompilerOptions.hpp': 'c65d54a4003baed8fece319d2be91d0c411e28865449a9f5662d3c5cbcf248ad',
    'cpp/include/mqb/core/LinkOptions.hpp': 'da33af2272ed66fbec978c2927b0a529d7df7445f7092078c80ac699bb542209',
    'cpp/include/mqb/core/TranslationUnit.hpp': '6c8fbf80750d4a4f2c6c54ad9e0a148f62548a3c826be80b518407390eaa3209',
    'cpp/include/mqb/core/BuildArtifactRecord.hpp': 'c0e7d0d7342647d9fb2b4fb9d4ebecaa0fe392f1123a2bfd54497ef3c39a75e4',
    'cpp/src/orchestration/incremental/ArtifactStorageProjection.cpp': '03800e0bf298b30be0885d74ac7289503183f7a38f58362c2f399a2e53f08842',
    'cpp/src/core/cache/ArtifactStorageAssociation.cpp': '4d97df5226f7daa1e84cd3fc5c67e668717d92e878c7237f5ebcd0cdaa1f394b',
}

def read(name): return storage_projection_extension.legacy_text(name, archive_extension.read_text(ROOT, name))
def sha(data): return hashlib.sha256(data).hexdigest()
def body(text): return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
def pure(text):
    text = body(text)
    for forbidden in ('ifstream', 'ofstream', 'CreateFile', 'DeleteFile', 'MoveFile',
                      'create_link_fact_snapshot_file(', 'read_link_fact_snapshot_file(',
                      'archive_link_completion(', 'run_recorded(', 'run_observed(',
                      'observe_storage_file(', 'current_path(', 'last_write_time(',
                      'file_size(', 'canonical(', '::remove(', '::rename(', '::now(',
                      'system(', 'popen('):
        if forbidden in text: raise ValueError('model acquired effects: '+forbidden)

class GenerationContracts(unittest.TestCase):
    def test_all_previous_95_native_programs_remain(self):
        files = {p.relative_to(ROOT).as_posix():p.read_text(encoding='utf-8')
                 for p in archive_extension.legacy_native_paths(ROOT) if p.relative_to(ROOT).as_posix()!=TEST}
        files = reporting_extension.legacy_view(files, read(reporting_extension.HELPER))
        values = {name:sha(text.encode('utf-8')) for name,text in files.items()}
        self.assertEqual(95,len(values))
        self.assertEqual('68ee6c490957e9aa5dda54be8f5e88623779e75eff8e3a029e2b8882f2867c04',
            sha(''.join(f'{n}\0{v}\n' for n,v in sorted(values.items())).encode()))
    def test_typed_fields_and_projection_owners_unchanged(self):
        for name,pin in PINS.items(): self.assertEqual(pin,sha(read(name).encode()),name)
    def test_source_has_no_execution_or_mutation(self):
        pure(read(TU)); pure(read(PUBLIC))
    def test_effect_negative_controls(self):
        for token in ('CreateFileW(p)','std::filesystem::remove(p)','current_path()',
                      'archive_link_completion(r,p)','run_recorded(r)','std::ifstream stream;'):
            with self.assertRaises(ValueError): pure(read(TU)+'\n'+token)
    def test_original_projectors_and_codec_reused(self):
        text=body(read(TU))
        for token in ('project_storage_references(target)', 'associate_artifact_storage(projected, empty, key)',
                      'encode_link_fact_snapshot(*in.snapshot)', 'validate_link_fact_snapshot(s)'):
            self.assertIn(token,text)
        self.assertNotIn('BuildSignature::for_',text)
        self.assertNotIn('json::parse',text)
    def test_no_default_caller_or_transitive_model_include(self):
        for p in archive_extension.legacy_source_paths(ROOT):
            if p.suffix not in ('.cpp','.hpp') or p.relative_to(ROOT).as_posix()==TU: continue
            text=body(archive_extension.read_text(ROOT, p.relative_to(ROOT).as_posix()))
            self.assertNotIn('ArtifactGenerationModel',text,str(p))
            self.assertNotIn('model_artifact_generations(',text,str(p))
    def test_authority_remains_false(self):
        text=body(read(PUBLIC))
        for name in ('producer_identity_verified','current_content_verified','complete_producer_inventory','deletion_authorized'):
            self.assertIn(f'static constexpr bool {name} = false;',text)
        self.assertNotIn('reclaimable_bytes',text)
    def test_options_order_and_bounds_explicit(self):
        text=body(read(TU))
        for token in ('add(o.additional_arguments)', 'add(o.defines)', 'add(o.external_module_providers)',
                      'add(o.address_sanitizer_runtime_library)', 'add(r.association.signature.digest().high)',
                      'ArtifactGenerationLimits::records', 'ArtifactGenerationLimits::text_bytes',
                      'Issue::mixed_reuse', 'Issue::duplicate_generation', 'Issue::missing_compiler'):
            self.assertIn(token,text)
        self.assertNotIn('std::sort',text)
    def test_new_product_and_test_registered_once(self):
        declared=['src/app/main.cpp']+json.loads(read('cpp/mqb.json'))['discovery']['extra_sources']
        self.assertEqual(96,len(declared)); self.assertEqual(len(declared),len(set(declared)))
        self.assertEqual(set(declared),{p.relative_to(ROOT/'cpp').as_posix() for p in archive_extension.legacy_product_paths(ROOT)})
        self.assertEqual(96,len(list(archive_extension.legacy_native_paths(ROOT))))
        driver=read('tests/native/run_native_tests.ps1')
        self.assertIn('$allTestFiles.Count -ne 96',driver); self.assertEqual(3,driver.count('96'))
        for n in (Path(TU).name,Path(TEST).name): self.assertEqual(1,read('tests/native/assert_cpp_layout.ps1').count("'"+n+"'"))
    def test_bilingual_scope_and_no_release(self):
        en=read('docs/ARTIFACT_GENERATIONS.md');zh=read('docs/ARTIFACT_GENERATIONS_ZH.md')
        self.assertEqual(en.count('\n## '),zh.count('\n## '))
        self.assertIn('ARTIFACT_GENERATIONS_ZH.md',en);self.assertIn('ARTIFACT_GENERATIONS.md',zh)
        self.assertEqual('5.6.0',read('VERSION').strip())
        values={p.relative_to(ROOT).as_posix():sha(p.read_text(encoding='utf-8').encode()) for p in (ROOT/'.github/workflows').glob('*') if p.is_file()}
        values = legacy_workflow_view(values)
        self.assertEqual(50,len(values))
        self.assertEqual('2e928f795bff8a8148f4e3f7f40eb3ac869f7dc0f0ddf859ea1827a3fd560a3c',sha(''.join(f'{n}\0{v}\n' for n,v in sorted(values.items())).encode()))

if __name__ == '__main__': unittest.main(verbosity=2)
