"""Pins/extraction/architecture controls, no compiler/benchmark/archived EXE calls."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import v9_reader_oracle as o

ROOT=Path(__file__).resolve().parents[2]
class OracleControls(unittest.TestCase):
    def test_pinned_generation_is_deterministic(self):
        self.assertEqual(o.render(ROOT),o.render(ROOT))

    def test_exact_parser_and_path_body_are_extracted(self):
        h,m=o.render(ROOT)
        source=(ROOT/next(iter(o.PINS))).read_text()
        body=o.section(source,'[[nodiscard]] bool write_quoted(',
            '[[nodiscard]] std::optional<MsvcToolchain> try_reuse_visual_studio_cache(')
        self.assertIn(body,h)
        self.assertIn('record.vc_tools_root = stable_path(detail::path_from_utf8(root));',h)
        self.assertEqual(hashlib.sha256(h.encode()).hexdigest(),m['generated_header_sha256'])
        self.assertNotIn('try_reuse_visual_studio_cache(',h)

    def test_generated_reference_keeps_actual_record_type(self):
        h,_=o.render(ROOT)
        self.assertIn('using mqb::process::EnvironmentVariable;',h)
        self.assertIn('std::vector<EnvironmentVariable> environment;',h)
        self.assertIn('stream >> std::ws;',h)
        self.assertIn('!stream.eof()',h)

    def test_modified_production_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in o.PINS:
                target=root/name;target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes((ROOT/name).read_bytes())
            path=root/next(iter(o.PINS));path.write_bytes(path.read_bytes()+b'// drift\n')
            with self.assertRaisesRegex(ValueError,'Pinned production'):o.render(root)

    def test_crlf_snapshot_matches_canonical_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in o.PINS:
                target=root/name;target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(o.canonical((ROOT/name).read_bytes()).replace(b'\n',b'\r\n'))
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

    def test_prototype_not_in_product_or_default_test_driver(self):
        for folder in ('cpp/src','cpp/include'):
            for path in (ROOT/folder).rglob('*'):
                if path.is_file():self.assertNotIn(b'v9_reader_prototype',path.read_bytes())
        self.assertEqual(87,len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))))
        layout=(ROOT/'tests/native/assert_cpp_layout.ps1').read_text()
        for name in ('v9_reader_probe.cpp','v9_reader_prototype.hpp'):self.assertIn("'"+name+"'",layout)

    def test_workflow_has_no_performance_or_capture_entry(self):
        w=(ROOT/'.github/workflows/v9-reader-correctness.yml').read_text()
        self.assertIn('pull_request:',w);self.assertIn('persist-credentials: false',w)
        self.assertIn('acquire_seed.ps1',w)
        for token in ('workflow_dispatch:','wpr.exe','-ExecuteReviewed','--timings','benchmark'):
            self.assertNotIn(token,w)
        self.assertIn('if: always()',w)

    def test_generated_metadata_never_clears_hold(self):
        _,m=o.render(ROOT)
        self.assertFalse(m['performance_verified']);self.assertFalse(m['clears_hold'])
        self.assertEqual(0,m['new_study_calls']);self.assertEqual(0,m['new_etw_sessions'])

if __name__=='__main__':unittest.main(verbosity=2)
