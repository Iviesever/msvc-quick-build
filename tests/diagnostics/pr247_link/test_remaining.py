"""Portable tests; synthetic calls are not Windows/MSVC measurements."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import types
import unittest
from unittest import mock
import remaining as r
import study as s

ARCHIVE = os.environ.get("PR247_TEST_ARCHIVE")


def vector(scenario, preparing=False):
    hits, misses, cl = {r.SCENARIOS[0]: (128, 1, 1), r.SCENARIOS[1]: (0, 2, 2),
                        r.SCENARIOS[2]: (0, 2, 4)}[scenario]
    if preparing:
        hits, misses, cl = (0, 129, 129) if scenario == r.SCENARIOS[0] else (0, 2, 2)
    return dict(type="mqb.timings", schema_version=2, unit="ms", phases=dict(total=100., link=30.),
                cache=dict(compile=dict(hits=hits, misses=misses), link=dict(hits=0, misses=1)),
                counters=dict(cl_processes_launched=cl, link_processes_launched=1, lib_processes_launched=0),
                counter_breakdown={}, attribution=dict(work=dict(link_execution=29., compile_execution=40.),
                                                       wall=dict(toolchain_discovery=1.)))


class Core(unittest.TestCase):
    def test_helper_identity(self): r.check_helper()
    def test_schedule(self):
        self.assertEqual(len(r.schedule()), 24)
        for i, sides in enumerate(s.PAIRS, 1):
            self.assertEqual([x for x in r.schedule() if x[0] == i],
                             [[i, side, sc] for side in sides for sc in r.SCENARIOS])
        self.assertNotIn("link-only", str(r.schedule()))
    def test_expected_work(self):
        for sc in r.SCENARIOS: r.work(vector(sc), sc)
    def test_preparation_work(self):
        for sc in r.SCENARIOS[:2]: r.work(vector(sc, True), sc, True)
    def test_extra_compiler_rejected(self):
        for sc in r.SCENARIOS:
            v = vector(sc); v["counters"]["cl_processes_launched"] += 1
            with self.assertRaises(ValueError): r.work(v, sc)
    def test_wrong_cache_rejected(self):
        v = vector(r.SCENARIOS[0]); v["cache"]["compile"]["hits"] = 0
        with self.assertRaises(ValueError): r.work(v, r.SCENARIOS[0])
    def test_nonfinite_rejected(self):
        v = vector(r.SCENARIOS[0]); v["attribution"]["wall"]["toolchain_discovery"] = float("nan")
        with self.assertRaises(ValueError): r.work(v, r.SCENARIOS[0])
    def test_zero_total_rejected(self):
        v=vector(r.SCENARIOS[0]);v["phases"]["total"]=0
        with self.assertRaises(ValueError): r.work(v,r.SCENARIOS[0])
    def test_cold_module_prime_forbidden(self):
        with self.assertRaises(ValueError): r.work(vector(r.SCENARIOS[2]), r.SCENARIOS[2], True)

    def admit(self, **changes):
        event = dict(action="synchronize", before=r.PARENT, pull_request=dict(number=248,
                     head=dict(ref=s.BRANCH, sha="a"*40, repo=dict(full_name=s.REPO)), base=dict(sha=s.BASE)))
        event.update(changes.pop("event", {}))
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/"event.json"; s.dump(p, event)
            env = dict(GITHUB_REPOSITORY=s.REPO, GITHUB_EVENT_NAME="pull_request", GITHUB_RUN_ATTEMPT="1",
                       GITHUB_EVENT_PATH=str(p), GITHUB_RUN_ID="123")
            env.update(changes.pop("env", {}))
            with mock.patch.object(r, "os", types.SimpleNamespace(environ=env)), \
                 mock.patch.object(r.subprocess, "check_output", side_effect=["a"*40, changes.get("parent", r.PARENT)]):
                return r.admission()
    def test_exact_transition(self): self.assertEqual(self.admit()["allocation"], r.ALLOCATION)
    def test_rerun_rejected(self):
        with self.assertRaises(ValueError): self.admit(env={"GITHUB_RUN_ATTEMPT":"2"})
    def test_wrong_parent(self):
        with self.assertRaises(ValueError): self.admit(parent="b"*40)
    def test_wrong_before(self):
        with self.assertRaises(ValueError): self.admit(event={"before":"b"*40})
    def test_dispatch_rejected(self):
        with self.assertRaises(ValueError): self.admit(env={"GITHUB_EVENT_NAME":"workflow_dispatch"})
    def test_opened_rejected(self):
        with self.assertRaises(ValueError): self.admit(event={"action":"opened"})
    def test_workflow_is_bounded(self):
        text = (Path(__file__).resolve().parents[3]/".github/workflows/pr247-remaining-scenarios.yml").read_text()
        self.assertIn(r.PARENT, text)
        self.assertIn("github.run_attempt == 1", text)
        self.assertNotIn("workflow_dispatch", text)
        self.assertNotIn("continue-on-error", text)
        self.assertIn("if: always()", text)


@unittest.skipUnless(ARCHIVE, "pinned original919 required")
class OriginalInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.members = s.read_original(Path(ARCHIVE))
    def test_three_exact_fixtures(self):
        f = r.fixtures(self.members)
        self.assertEqual([len(f[k]["final"]) for k in r.SCENARIOS], [129, 3, 2])
        self.assertTrue(f[r.SCENARIOS[0]]["final"]["unit_127.cpp"].endswith(b"// target-scale mutation 1\r\n"))
    def test_changed_only_one_file(self):
        for f in r.fixtures(self.members).values():
            self.assertEqual([k for k in f["final"] if f["final"][k] != f["before"][k]],
                             [f["changed"]] if f["changed"] else [])
    def test_extra_source_rejected(self):
        m = dict(self.members); m[r.PREFIX+"post-suite-sources/modules-1/extra.cpp"] = b"bad"
        with self.assertRaises(ValueError): r.fixtures(m)
    def test_wrong_mutation_rejected(self):
        m = dict(self.members); m[r.PREFIX+"post-suite-sources/target-scale-1/unit_127.cpp"] = b"bad"
        with self.assertRaises(ValueError): r.fixtures(m)


@unittest.skipUnless(ARCHIVE, "pinned original919 required")
class Synthetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.root = Path(cls.temp.name); cls.original = cls.root/"collected"
        request = dict(allocation=r.ALLOCATION, parent=r.PARENT, base=s.BASE, candidate=s.HEAD,
                       attempt=1, run="synthetic", harness_head="a"*40, max_root_mqb_calls=26, **r.FLAGS)
        def fake_run(argv, cwd, env, **kwargs):
            sc = cwd.name; preparing = not (cwd/".mqb").exists() and sc != r.SCENARIOS[2]
            state = cwd/".mqb"; state.mkdir(exist_ok=True)
            (state/"cache").write_bytes(b"synthetic-preparation" if preparing else b"synthetic-measured")
            return subprocess.CompletedProcess(argv, 0, (json.dumps(vector(sc, preparing))+"\n").encode(), b"")
        fake_os = types.SimpleNamespace(name="nt", environ=dict(os.environ), utime=os.utime)
        with mock.patch.object(r, "os", fake_os), mock.patch.object(r, "admission", return_value=request), \
             mock.patch.object(s.subprocess, "run", side_effect=fake_run) as run:
            r.measure(Path(ARCHIVE), cls.original)
            if run.call_count != 26: raise AssertionError(run.call_count)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def setUp(self):
        self.case = Path(tempfile.mkdtemp(dir=self.root)); self.out=self.case/"out"
        shutil.copytree(self.original, self.out)
    def tearDown(self): shutil.rmtree(self.case)
    def audit(self): return r.audit(self.out, self.case/"audit.json")
    def mutate(self, name, change):
        p=self.out/name;v=s.load(p);change(v);p.write_text(json.dumps(v), encoding="utf-8")
    @property
    def first(self): return "calls/sample-1-baseline-"+r.SCENARIOS[0]+"/"
    def test_complete_read_only_audit(self):
        before=s.manifest(self.out); result=self.audit()
        self.assertEqual(before,s.manifest(self.out));self.assertEqual(len(result["pairs"]),12)
        self.assertIs(result["product_acceptance"],False)
    def test_missing_sample(self):
        self.mutate("completed.json", lambda v:v["samples"].pop())
        with self.assertRaises(ValueError): self.audit()
    def test_raw_capture(self):
        (self.out/self.first/"stdout.bin").write_bytes(b"wrong")
        with self.assertRaises(ValueError): self.audit()
    def test_changed_budget(self):
        self.mutate(self.first+"started.json", lambda v:v.update(charged=1))
        with self.assertRaises(ValueError): self.audit()
    def test_changed_argv(self):
        self.mutate(self.first+"started.json", lambda v:v["argv"].append("--run"))
        with self.assertRaises(ValueError): self.audit()
    def test_changed_cwd(self):
        self.mutate(self.first+"started.json", lambda v:v.update(cwd="elsewhere"))
        with self.assertRaises(ValueError): self.audit()
    def test_changed_binary(self):
        (self.out/"inputs/baseline/mqb.exe").write_bytes(b"wrong")
        with self.assertRaises(ValueError): self.audit()
    def test_changed_snapshot(self):
        (self.out/self.first/"post/.mqb/cache").write_bytes(b"wrong")
        with self.assertRaises(ValueError): self.audit()
    def test_changed_source(self):
        (self.out/"pristine/modules-cold/main.cpp").write_bytes(b"wrong")
        with self.assertRaises(ValueError): self.audit()
    def test_changed_restore(self):
        self.mutate("before/sample-1-baseline-"+r.SCENARIOS[0]+".json", lambda v:v.pop("main.cpp"))
        with self.assertRaises(ValueError): self.audit()
    def test_authority_not_promoted(self):
        self.mutate("completed.json", lambda v:v.update(product_acceptance=True))
        with self.assertRaises(ValueError): self.audit()
    def test_no_919_replacement(self):
        self.mutate("completed.json", lambda v:v.update(raw_scores_replace_919=True))
        with self.assertRaises(ValueError): self.audit()
    def test_extra_call_rejected(self):
        (self.out/"calls/unregistered").mkdir()
        with self.assertRaises(ValueError): self.audit()
    def test_preparation_failure_stops(self):
        target=self.case/"failed"
        request=s.load(self.out/"request.json")
        fake_os=types.SimpleNamespace(name="nt",environ=dict(os.environ),utime=os.utime)
        with mock.patch.object(r,"os",fake_os),mock.patch.object(r,"admission",return_value=request), \
             mock.patch.object(s.subprocess,"run",return_value=subprocess.CompletedProcess([],7,b"",b"original failure")) as run:
            with self.assertRaises(ValueError):r.measure(Path(ARCHIVE),target)
            self.assertEqual(run.call_count,1)
            self.assertEqual((target/"calls"/("prepare-"+r.SCENARIOS[0])/"stderr.bin").read_bytes(),b"original failure")
    def test_output_not_overwritten(self):
        with mock.patch.object(r,"os",types.SimpleNamespace(name="nt")),mock.patch.object(s.subprocess,"run") as run:
            with self.assertRaises(ValueError):r.measure(Path(ARCHIVE),self.out)
            run.assert_not_called()


if __name__ == "__main__": unittest.main()
