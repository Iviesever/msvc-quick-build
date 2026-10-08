"""Portable fail-closed contracts; these tests never run MQB or MSVC."""
import copy
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import zipfile
import study as s


class Core(unittest.TestCase):
    def trace(self):
        return dict(schema=1, frequency=10000000, pid=7, child_pid=8,
                    ticks={k: (i+1)*100 for i, k in enumerate(s.POINTS)},
                    process_times_ok=True, creation_100ns=1000, exit_100ns=2000,
                    kernel_100ns=50, user_100ns=60, stdout_bytes=3, stderr_bytes=0,
                    product_acceptance=False, release_authorized=False)

    def test_boundary_units(self):
        t=s.validate_trace(self.trace(), b"abc", b"")
        self.assertEqual(t["launch_ms"], .01)
        self.assertEqual(t["child_lifetime_ms"], .1)
        self.assertIsNone(t["filter_only_ms"])
        self.assertTrue(t["intervals_overlap"])

    def test_missing_boundary(self):
        t=self.trace(); del t["ticks"]["wait_end"]
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_zero_boundary(self):
        t=self.trace(); t["ticks"]["wait_begin"]=0
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_reversed_clock(self):
        t=self.trace(); t["ticks"]["create_end"]=10
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_bad_frequency(self):
        t=self.trace(); t["frequency"]=0
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_bad_process_times(self):
        t=self.trace(); t["process_times_ok"]=False
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_missing_buffer(self):
        with self.assertRaises(ValueError): s.validate_trace(self.trace(),b"a",b"")

    def test_authority_not_promoted(self):
        t=self.trace(); t["release_authorized"]=True
        with self.assertRaises(ValueError): s.validate_trace(t,b"abc",b"")

    def test_duplicate_anchor(self):
        with self.assertRaises(ValueError): s.replace_once("x x","x","y")

    def test_missing_anchor(self):
        with self.assertRaises(ValueError): s.replace_once("x","z","y")

    def test_write_once(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"r.json";s.dump(p,{"a":1})
            with self.assertRaises(FileExistsError):s.dump(p,{"a":2})
            self.assertEqual(s.load(p),{"a":1})

    def test_schedule(self):
        self.assertEqual(s.PAIRS,(("baseline","candidate"),("candidate","baseline"),
                                 ("baseline","candidate"),("candidate","baseline")))

    def test_budget_stops_before_launch(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(s.subprocess,"run") as run:
            c=s.Calls(Path(d)/"calls",1);c.used=1
            with self.assertRaises(ValueError): c.run("x",["mqb"],Path(d),{})
            run.assert_not_called()
            self.assertEqual(list(c.root.iterdir()),[])

    def test_nonzero_preserved(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(s.subprocess,"run",return_value=
                    subprocess.CompletedProcess(["mqb"],3,b"stdout",b"stderr")):
            c=s.Calls(Path(d)/"calls",1)
            with self.assertRaises(ValueError):c.run("x",["mqb"],Path(d),{})
            self.assertEqual(s.load(c.root/"x/result.json")["exit_code"],3)
            self.assertEqual((c.root/"x/stderr.bin").read_bytes(),b"stderr")

    def test_timeout_retains_and_stops(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(s.subprocess,"run",side_effect=
                    subprocess.TimeoutExpired(["mqb"],1,output=b"partial",stderr=b"error")) as run:
            c=s.Calls(Path(d)/"calls",1)
            with self.assertRaises(subprocess.TimeoutExpired):c.run("x",["mqb"],Path(d),{})
            self.assertEqual(run.call_count,1)
            self.assertFalse(s.load(c.root/"x/failed.json")["quiescence_proven"])
            self.assertEqual((c.root/"x/stdout.bin").read_bytes(),b"partial")

    def test_bad_source_pin(self):
        with self.assertRaises(ValueError):s.instrument(s.RUNNER,b"not the source")

    def test_zip_member_rejections(self):
        for name in ("../bad","/bad","C:/bad","a\\b","./bad","a//b"):
            with self.subTest(name=name):
                data=io.BytesIO()
                with zipfile.ZipFile(data,"w") as z:z.writestr(name,b"x")
                with zipfile.ZipFile(data) as z:
                    with self.assertRaises(ValueError):s.archive_members(z)

    def test_zip_case_alias(self):
        data=io.BytesIO()
        with zipfile.ZipFile(data,"w") as z:
            z.writestr("A",b"1");z.writestr("a",b"2")
        with zipfile.ZipFile(data) as z:
            with self.assertRaises(ValueError):s.archive_members(z)

    def test_zip_symlink(self):
        data=io.BytesIO();info=zipfile.ZipInfo("sym");info.external_attr=(0o120777<<16)
        with zipfile.ZipFile(data,"w") as z:z.writestr(info,b"target")
        with zipfile.ZipFile(data) as z:
            with self.assertRaises(ValueError):s.archive_members(z)

    def test_archive_sha_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"x.zip";p.write_bytes(b"wrong")
            with self.assertRaises(ValueError):s.read_original(p)

    def test_fixture_bytes_and_time_restore(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"a";p.mkdir();(p/"x").write_bytes(b"cache")
            s.shutil.copytree(p,Path(d)/"b")
            self.assertEqual(s.manifest(p),s.manifest(Path(d)/"b"))
            (Path(d)/"b/x").write_bytes(b"drift")
            self.assertNotEqual(s.manifest(p),s.manifest(Path(d)/"b"))

    def test_existing_output_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"old";p.mkdir();(p/"keep").write_bytes(b"original")
            with mock.patch.object(s.sys,"argv",["study.py","prepare","--out",str(p),"--offline"]):
                self.assertEqual(s.main(),1)
            self.assertEqual([x.name for x in p.iterdir()],["keep"])

    def test_admission_rejects_wrong_event_and_attempt(self):
        for env in ({"GITHUB_REPOSITORY":"other/repo"},
                    {"GITHUB_REPOSITORY":s.REPO,"GITHUB_EVENT_NAME":"workflow_dispatch"},
                    {"GITHUB_REPOSITORY":s.REPO,"GITHUB_EVENT_NAME":"pull_request","GITHUB_RUN_ATTEMPT":"2"}):
            with mock.patch.dict(os.environ,env,clear=True):
                with self.assertRaises(ValueError):s.admission()

    def test_timing_not_missing_or_duplicated(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/"stderr.bin").write_bytes(b"")
            row=b'{"phases":{},"counters":{}}\n'
            (p/"stdout.bin").write_bytes(row)
            self.assertEqual(s.timing(p),{"phases":{},"counters":{}})
            (p/"stdout.bin").write_bytes(row+row)
            with self.assertRaises(ValueError):s.timing(p)

    def test_mark_preserves_native_error(self):
        text=Path(s.__file__).with_name("LinkDiag.hpp").read_text()
        self.assertIn("const DWORD saved_error = ::GetLastError();",text)
        self.assertIn("::SetLastError(saved_error);",text)
        self.assertIn("CREATE_NEW",text)


@unittest.skipUnless(os.environ.get("PR247_TEST_ARCHIVE"),"pinned archive not supplied")
class OriginalInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files=s.read_original(Path(os.environ["PR247_TEST_ARCHIVE"]))

    def test_exact_both_instrumentation_patches(self):
        for side in ("baseline","candidate"):
            with zipfile.ZipFile(io.BytesIO(self.files["provenance/"+side+"-source.zip"])) as z:
                for name in (s.RUNNER,s.COORDINATOR):
                    raw=z.read(name);patched=s.instrument(name,raw)
                    self.assertNotEqual(raw,patched)
                    self.assertIn(b'pr247_link_diag.hpp',patched)
                    with self.assertRaises(ValueError):s.instrument(name,patched)

    def test_no_product_linker_patch(self):
        self.assertNotIn("cpp/src/msvc/linker/MsvcLinker.cpp",s.SOURCE_PINS)
        for side in ("baseline","candidate"):
            self.assertEqual(s.sha(self.files[side+"/mqb.exe"]),s.BINARY_SHA[side])

    def test_original_fixture_corpus(self):
        prefix="evidence/comparison/pair-1-baseline-calls/post-suite-sources/ordinary-1/"
        for name in ("main.cpp","helper.cpp","common.hpp"):
            self.assertGreater(len(self.files[prefix+name]),0)

    def test_observer_after_execution_and_parsing(self):
        with zipfile.ZipFile(io.BytesIO(self.files["provenance/candidate-source.zip"])) as z:
            text=s.instrument(s.COORDINATOR,z.read(s.COORDINATOR)).decode()
        self.assertLess(text.index("linker_.link(invocation)"),text.index("pr247_diag::mark(mqb::pr247_diag::call_end)"))
        self.assertLess(text.index("observed_library_paths(linked->stdout_text)"),text.index("pr247_diag::flush("))



@unittest.skipUnless(os.environ.get("PR247_TEST_ARCHIVE"), "pinned archive not supplied")
class SyntheticEndToEnd(unittest.TestCase):
    """Fake subprocesses exercise evidence plumbing; no Windows code is executed."""
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root=Path(self.temp.name)
        self.inputs=root/"input";self.output=root/"output"
        s.prepare(Path(os.environ["PR247_TEST_ARCHIVE"]), self.inputs, False)
        self.request={"harness_head":"a"*40,"run":"12345","allocation":"pr247-link-boundaries-001"}
        identities={}
        for side in ("baseline","candidate"):
            p=self.inputs/"instrumented"/side/"mqb.exe";p.parent.mkdir(parents=True)
            p.write_bytes(b"synthetic non-executable "+side.encode())
            identities[side]={"binary_sha256":s.sha(p.read_bytes())}
        s.dump(self.inputs/"build-completed.json",dict(request=self.request,inputs=identities,root_mqb_budget_charged=4))
        def fake_run(argv,cwd,env,**unused):
            cold="/MAP" not in argv
            state=cwd/".mqb";state.mkdir(exist_ok=True)
            (state/"cache").write_bytes(b"cold" if cold else b"mapped")
            vector=dict(type="mqb.timings",schema_version=2,phases=dict(total=20.,link=10.),
                        cache=dict(compile=dict(hits=0 if cold else 2, misses=2 if cold else 0),
                                   link=dict(hits=0,misses=1)),
                        counters=dict(cl_processes_launched=2 if cold else 0,
                                      link_processes_launched=1,lib_processes_launched=0),
                        attribution=dict(work=dict(link_execution=9.)))
            if s.TRACE_ENV in env:
                p=Path(env[s.TRACE_ENV]);t=Core().trace()
                s.dump(p,t);Path(str(p)+".stdout").write_bytes(b"abc");Path(str(p)+".stderr").write_bytes(b"")
            return subprocess.CompletedProcess(argv,0,(json.dumps(vector)+"\n").encode(),b"")
        import types
        with mock.patch.object(s,"os",types.SimpleNamespace(name="nt",environ=dict(os.environ))), \
             mock.patch.object(s,"admission",return_value=self.request), \
             mock.patch.object(s.subprocess,"run",side_effect=fake_run) as calls:
            s.measure(self.inputs,self.output)
            self.assertEqual(calls.call_count,17)

    def mutate(self,relative,change):
        p=self.output/relative;v=s.load(p);change(v);p.write_text(json.dumps(v),encoding="utf-8")

    def test_complete_and_read_only_replay(self):
        before=s.manifest(self.output)
        s.audit(self.inputs,self.output,Path(self.temp.name)/"audit.json")
        self.assertEqual(before,s.manifest(self.output))
        result=s.load(Path(self.temp.name)/"audit.json")
        self.assertEqual(len(result["pairs"]),8)
        self.assertIs(result["product_acceptance"],False)

    def test_deleted_sample_rejected(self):
        self.mutate("completed.json",lambda v:v["samples"].pop())
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_raw_output_mutation_rejected(self):
        (self.output/"calls/original-1-baseline/stdout.bin").write_bytes(b"changed")
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_extra_compile_rejected(self):
        self.mutate("calls/original-1-baseline/timing.json",lambda v:v["counters"].update(cl_processes_launched=1))
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_timestamp_order_rejected(self):
        self.mutate("calls/instrumented-1-baseline/boundaries.json",lambda v:v["ticks"].update(wait_end=1))
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_hold_not_automatically_released(self):
        self.mutate("completed.json",lambda v:v.update(product_acceptance=True))
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_restore_mismatch_rejected(self):
        self.mutate("fixture-before/original-1-baseline.json",lambda v:v.update(extra={}))
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_process_budget_charge_rejected(self):
        self.mutate("calls/original-1-baseline/started.json",lambda v:v.update(charged=1))
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_binary_identity_rejected(self):
        (self.inputs/"instrumented/baseline/mqb.exe").write_bytes(b"tampered")
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)

    def test_post_cache_mutation_rejected(self):
        (self.output/"calls/original-1-baseline/post-fixture/.mqb/cache").write_bytes(b"tampered")
        with self.assertRaises(ValueError):s.audit(self.inputs,self.output)


if __name__=="__main__":unittest.main()
