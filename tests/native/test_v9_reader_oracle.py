"""Frozen old reference, actual product-reader wiring and preserved policy controls."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest
import v9_reader_oracle as o

ROOT=Path(__file__).resolve().parents[2]
CACHE='cpp/src/msvc/toolchain/VisualStudioToolchainCache.cpp'
READER='cpp/src/msvc/toolchain/VisualStudioToolchainCacheReader.hpp'

def function(text, signature):
    # Exact known bodies here contain no brace-valued literals. Fail on duplicate
    # anchors; this is a narrow source contract, not a general C++ parser.
    if text.count(signature) != 1: raise ValueError('Non-unique function')
    start=text.index(signature); brace=text.index('{', start); depth=1; end=brace+1
    while end<len(text) and depth:
        depth += (text[end]=='{') - (text[end]=='}'); end+=1
    if depth: raise ValueError('Unterminated function')
    return text[start:end]

def preserved_cache_source(repo):
    h,_=o.render(repo); current=(repo/CACHE).read_text()
    required='auto record = v9_cache::read_prechecked(stream, size).record;'
    if current.count(required)!=1: raise ValueError('Product reader not wired exactly once')
    current=current.replace(required, 'auto record = read_record(stream);')
    include='#include "VisualStudioToolchainCacheReader.hpp"\n'
    if current.count(include)!=1: raise ValueError('Private reader include absent')
    current=current.replace(include,'')
    a=current.index('using v9_cache::CacheRecord;'); b=current.index('struct ToolPaths',a)
    if current[a:b] != 'using v9_cache::CacheRecord;\nusing v9_cache::cache_magic;\nusing v9_cache::max_cache_size;\nusing v9_cache::max_cache_entries;\nusing v9_cache::max_cache_string;\nusing v9_cache::stable_path;\nusing v9_cache::environment_name_equal;\nusing v9_cache::cacheable_environment_name;\nconstexpr auto max_cache_age = std::chrono::minutes{30};\n\n':
        raise ValueError('Moved aliases or cache age changed')
    current=current[:a]+o.section(h,'constexpr std::string_view cache_magic','[[nodiscard]] fs::path stable_path(').rstrip('\n')+'\n\n'+current[b:]
    sections=[
        ('[[nodiscard]] fs::path stable_path(', '[[nodiscard]] bool environment_name_equal(', '[[nodiscard]] bool same_path('),
        ('[[nodiscard]] bool environment_name_equal(', '[[nodiscard]] bool write_quoted(', '[[nodiscard]] const EnvironmentVariable* find_environment('),
        ('[[nodiscard]] bool read_quoted(', '[[nodiscard]] bool write_record(', '[[nodiscard]] bool write_record('),
    ]
    for first,last,dest in sections:
        current=current.replace(dest,o.section(h,first,last).rstrip('\n')+'\n\n'+dest,1)
    body=o.section(h,'[[nodiscard]] std::optional<std::size_t> read_count(', '[[nodiscard]] std::optional<CacheRecord> read_record(')
    body+=function(h,'[[nodiscard]] std::optional<CacheRecord> read_record(')+'\n\n'
    dest='[[nodiscard]] std::optional<MsvcToolchain> try_reuse_visual_studio_cache('
    current=current.replace(dest,body+dest,1)
    if hashlib.sha256(o.canonical(current.encode())).hexdigest()!=o.PINS[CACHE]:
        raise ValueError('Production admission/writer/freshness changed outside approved reading seam')
    return current

def reference_fixture(root):
    for name in (o.REFERENCE_PATH, 'cpp/include/mqb/process/Process.hpp'):
        p=root/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes((ROOT/name).read_bytes())

class OracleControls(unittest.TestCase):
    def test_pinned_generation_is_deterministic(self):
        self.assertEqual(o.render(ROOT),o.render(ROOT))

    def test_exact_old_fallback_bodies_preserved(self):
        h,_=o.render(ROOT); product=(ROOT/READER).read_text().replace('[[nodiscard]] inline ','[[nodiscard]] ')
        expected=o.section(h,'constexpr std::string_view cache_magic','[[nodiscard]] fs::path stable_path(').replace('constexpr auto max_cache_age = std::chrono::minutes{30};\n','').strip()
        actual=o.section(product,'constexpr std::string_view cache_magic','[[nodiscard]] fs::path stable_path(').strip()
        self.assertEqual(expected,actual)
        for signature in ('[[nodiscard]] fs::path stable_path(', '[[nodiscard]] bool environment_name_equal(',
                '[[nodiscard]] bool cacheable_environment_name(', '[[nodiscard]] bool read_quoted(',
                '[[nodiscard]] std::optional<std::size_t> read_count(', '[[nodiscard]] std::optional<CacheRecord> read_record('):
            with self.subTest(signature=signature):self.assertEqual(function(h,signature),function(product,signature))

    def test_generated_reference_keeps_actual_record_type(self):
        h,m=o.render(ROOT)
        self.assertIn('using mqb::process::EnvironmentVariable;',h)
        self.assertIn('std::vector<EnvironmentVariable> environment;',h)
        self.assertIn('stream >> std::ws;',h);self.assertIn('!stream.eof()',h)
        self.assertEqual(m['generated_header_sha256'],'da5b64cd2e2e4d3d6b2cdb592a2c6e71bcb98da9eef40bb98c3c2a378b7dcfed')

    def test_modified_frozen_reference_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reference_fixture(root);p=root/o.REFERENCE_PATH;p.write_bytes(p.read_bytes()+b'// drift\n')
            with self.assertRaisesRegex(ValueError,'Frozen V9'):o.render(root)

    def test_crlf_snapshot_matches_canonical_source(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reference_fixture(root)
            for name in (o.REFERENCE_PATH,'cpp/include/mqb/process/Process.hpp'):
                p=root/name;p.write_bytes(o.canonical(p.read_bytes()).replace(b'\n',b'\r\n'))
            self.assertEqual(o.render(root),o.render(ROOT))

    def test_missing_or_duplicate_anchors_refused(self):
        for text,first,last in [('abc','x','c'),('ababc','ab','c'),('abc','c','a')]:
            with self.assertRaises(ValueError):o.section(text,first,last)

    def test_cli_refuses_existing_directory(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'generated';cmd=[sys.executable,'-B',str(Path(o.__file__)), '--repo',str(ROOT),'--output',str(out)]
            self.assertEqual(0,subprocess.run(cmd,capture_output=True).returncode)
            before={p.name:p.read_bytes() for p in out.iterdir()}
            self.assertNotEqual(0,subprocess.run(cmd,capture_output=True).returncode)
            self.assertEqual(before,{p.name:p.read_bytes() for p in out.iterdir()})

    def test_product_does_not_depend_on_test_reference_or_adapter(self):
        for folder in ('cpp/src','cpp/include'):
            for path in (ROOT/folder).rglob('*'):
                if path.is_file():
                    for token in (b'v9_reader_prototype',b'v9_oracle.hpp',b'mqb_v9_oracle'):
                        self.assertNotIn(token,path.read_bytes())
        self.assertEqual(88,len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))))
        layout=(ROOT/'tests/native/assert_cpp_layout.ps1').read_text()
        for name in ('v9_reader_probe.cpp','v9_reader_prototype.hpp','VisualStudioToolchainCacheReader.hpp'):
            self.assertIn("'"+name+"'",layout)

    def test_workflow_uses_product_conversion_without_performance_or_capture(self):
        w=(ROOT/'.github/workflows/v9-reader-correctness.yml').read_text()
        self.assertIn('pull_request:',w);self.assertIn('persist-credentials: false',w)
        self.assertIn('acquire_seed.ps1',w);self.assertIn('v9_reader_probe.cpp cpp/src/msvc/toolchain/ToolchainDiscoveryPrimitives.cpp',w)
        for token in ('workflow_dispatch:','wpr.exe','-ExecuteReviewed','--timings','benchmark'): self.assertNotIn(token,w)
        self.assertIn('if: always()',w)

    def test_generated_metadata_never_clears_hold(self):
        _,m=o.render(ROOT)
        self.assertFalse(m['performance_verified']);self.assertFalse(m['clears_hold'])
        self.assertEqual(0,m['new_study_calls']);self.assertEqual(0,m['new_etw_sessions'])

    def test_reference_cannot_follow_production_edits(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reference_fixture(root);p=root/CACHE;p.parent.mkdir(parents=True);p.write_text('new parser is not an oracle')
            self.assertEqual(o.render(root),o.render(ROOT))

    def test_reference_dependency_drift_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reference_fixture(root);(root/'cpp/include/mqb/process/Process.hpp').write_text('changed')
            with self.assertRaisesRegex(ValueError,'dependency'):o.render(root)

    def test_complete_production_policy_only_changes_reading_seam(self):
        preserved_cache_source(ROOT)

    def test_policy_drift_or_disconnected_reader_is_rejected(self):
        for before,after in [('auto record = v9_cache::read_prechecked(stream, size).record;', 'auto record = read_record(stream);'),
                             ('record->ambient_path != current_ambient_path','false'),
                             ('constexpr auto max_cache_age = std::chrono::minutes{30};','constexpr auto max_cache_age = std::chrono::hours{30};')]:
            with tempfile.TemporaryDirectory() as d:
                root=Path(d);reference_fixture(root);p=root/CACHE;p.parent.mkdir(parents=True);p.write_text((ROOT/CACHE).read_text().replace(before,after))
                with self.assertRaises(ValueError):preserved_cache_source(root)

    def test_adapter_has_no_copy_of_the_parser(self):
        t=(ROOT/'cpp/tests/msvc/toolchain/v9_reader_prototype.hpp').read_text()
        self.assertIn('VisualStudioToolchainCacheReader.hpp',t)
        for call in ('product::read_prechecked','product::parse_bytes'):self.assertIn(call,t)
        for forbidden in ('class Cursor','old::read_record(', 'std::from_chars','std::ispanstream'):self.assertNotIn(forbidden,t)

    def test_native_admission_matrix_calls_real_entries(self):
        t=(ROOT/'cpp/tests/msvc/toolchain/v9_reader_adoption_tests.cpp').read_text()
        self.assertIn('verify_v9_production_admission(options, *options.cache_file, bytes);',t)
        for token in ('wire::read_prechecked','detail::reuse_visual_studio_toolchain_cache','MsvcToolchainLocator{denied}.discover',
                      'existing-untrusted-include','age-expired','age-future','newer-vc','compiler-file-stamp','cases == 28'):
            self.assertIn(token,t)

if __name__=='__main__':unittest.main(verbosity=2)
