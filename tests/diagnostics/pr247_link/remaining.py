"""One frozen-original-binary study of PR247's three remaining scenarios.

Reuses study.py's capture/identity/manifest primitives, not its link-only sampler.
No product build, instrumented binary, old matrix rerun or automatic acceptance.
"""
from __future__ import annotations
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
import study as s

PARENT = "625e70b1a68e476fd07a7e581846b1a557b27ba3"
ALLOCATION = "pr247-remaining-scenarios-001"
SCENARIOS = ("target-scale-single-tu", "discovery-header", "modules-cold")
HELPER_BLOB = "6e30f1d7a6a9f13c0bbf71d1db41ba2f58990a7c"
MAX_CALLS = 26
PREFIX = "evidence/comparison/pair-1-baseline-calls/"
FLAGS = {"product_acceptance": False, "release_authorized": False,
         "raw_scores_replace_919": False}


def check_helper() -> None:
    import hashlib
    raw = Path(s.__file__).read_bytes().replace(b"\r\n", b"\n")
    blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    s.require(blob == HELPER_BLOB, "unreviewed shared collector")


def admission() -> dict:
    s.require(os.environ.get("GITHUB_REPOSITORY") == s.REPO, "wrong repository")
    s.require(os.environ.get("GITHUB_EVENT_NAME") == "pull_request"
              and os.environ.get("GITHUB_RUN_ATTEMPT") == "1", "wrong event/attempt")
    event = s.load(Path(os.environ["GITHUB_EVENT_PATH"]))
    pr = event["pull_request"]
    s.require(event["action"] == "synchronize" and event.get("before") == PARENT
              and pr["number"] == 248 and pr["head"]["ref"] == s.BRANCH
              and pr["base"]["sha"] == s.BASE
              and pr["head"]["repo"]["full_name"] == s.REPO, "unregistered transition")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    parent = subprocess.check_output(["git", "rev-parse", "HEAD^"], text=True).strip()
    s.require(head == pr["head"]["sha"] and parent == PARENT, "wrong harness ancestry")
    return dict(allocation=ALLOCATION, harness_head=head, parent=parent, base=s.BASE,
                candidate=s.HEAD, run=os.environ["GITHUB_RUN_ID"], attempt=1,
                image=os.environ.get("ImageVersion"), max_root_mqb_calls=MAX_CALLS,
                **FLAGS)


def fixtures(members: dict) -> dict:
    """Use retained final sources; explicitly undo the one appended mutation for preparation."""
    result = {}
    for scenario, directory, changed, tail in (
        (SCENARIOS[0], "target-scale-1", "unit_127.cpp", b"// target-scale mutation 1\r\n"),
        (SCENARIOS[1], "discovery-1", "src/common.hpp", b"// discovery invalidation 1\r\n"),
        (SCENARIOS[2], "modules-1", None, b""),
    ):
        prefix = PREFIX + "post-suite-sources/" + directory + "/"
        final = {key[len(prefix):]: value for key, value in members.items() if key.startswith(prefix)}
        expected = ({"main.cpp", *[f"unit_{i:03}.cpp" for i in range(128)]}
                    if scenario == SCENARIOS[0] else
                    {"main.cpp", "src/helper.cpp", "src/common.hpp"}
                    if scenario == SCENARIOS[1] else {"main.cpp", "bench_math.ixx"})
        s.require(set(final) == expected, "fixture source membership changed")
        before = dict(final)
        if changed:
            s.require(final[changed].endswith(tail) and final[changed].count(tail) == 1,
                      "historical mutation not unique")
            before[changed] = final[changed][:-len(tail)]
        args = json.loads(members[PREFIX + "1-" + scenario + ".started.json"])["argv"]
        s.require(args[-1] == "--timings=json" and "--run" not in args, "unexpected historical command")
        result[scenario] = dict(before=before, final=final, changed=changed, argv=args)
    return result


def schedule() -> list:
    return [[pair, side, scenario] for pair, sides in enumerate(s.PAIRS, 1)
            for side in sides for scenario in SCENARIOS]


def work(vector: dict, scenario: str, preparing: bool = False) -> None:
    hits, misses, cl = {SCENARIOS[0]: (128, 1, 1), SCENARIOS[1]: (0, 2, 2),
                        SCENARIOS[2]: (0, 2, 4)}[scenario]
    if preparing:
        s.require(scenario != SCENARIOS[2], "cold module preparation forbidden")
        hits, misses, cl = (0, 129, 129) if scenario == SCENARIOS[0] else (0, 2, 2)
    s.require(vector["type"] == "mqb.timings" and vector["schema_version"] == 2
              and vector["unit"] == "ms", "timing schema changed")
    s.require(vector["cache"]["compile"] == {"hits": hits, "misses": misses}
              and vector["cache"]["link"] == {"hits": 0, "misses": 1}, "unexpected cache work")
    s.require(vector["phases"]["total"] > 0, "nonpositive total time")
    c = vector["counters"]
    s.require((c["cl_processes_launched"], c["link_processes_launched"], c["lib_processes_launched"])
              == (cl, 1, 0), "unexpected process work")
    for group in (vector["phases"], vector["attribution"]["work"], vector["attribution"]["wall"]):
        s.require(all(type(v) in (int, float) and math.isfinite(v) and v >= 0 for v in group.values()),
                  "invalid timing value")


def byte_manifest(root: Path, recorded: dict) -> None:
    actual = s.manifest(root)
    s.require(set(actual) == set(recorded) and all(
        actual[k]["sha256"] == recorded[k]["sha256"] and actual[k]["bytes"] == recorded[k]["bytes"]
        for k in actual), "snapshot bytes differ")


def measure(archive: Path, out: Path) -> None:
    s.require(os.name == "nt", "Windows required")
    s.require(not out.exists(), "output already exists")
    check_helper()
    request = admission()
    out.mkdir()
    s.dump(out / "request.json", request)
    members = s.read_original(archive)
    shutil.copyfile(archive, out / "original919.zip")
    for name in ("remaining.py", "study.py"):
        shutil.copyfile(Path(__file__).with_name(name), out / name)
    config = fixtures(members)
    exe = {}
    for side in ("baseline", "candidate"):
        exe[side] = out / "inputs" / side / "mqb.exe"
        exe[side].parent.mkdir(parents=True)
        exe[side].write_bytes(members[side + "/mqb.exe"])
    s.dump(out / "identity-before.json", {k: s.sha(p.read_bytes()) for k, p in exe.items()})
    s.dump(out / "schedule.json", schedule())
    env = os.environ.copy()
    env.pop(s.TRACE_ENV, None)
    ledger = s.Calls(out / "calls", MAX_CALLS)
    fixed = {}
    for scenario in SCENARIOS:
        spec = config[scenario]
        root = out / "fixtures" / scenario
        root.mkdir(parents=True)
        for name, raw in spec["before"].items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        if spec["changed"]:
            label = "prepare-" + scenario
            ledger.run(label, [str(exe["candidate"]), *spec["argv"]], root, env, timeout=600)
            vector = s.timing(out / "calls" / label)
            work(vector, scenario, preparing=True)
            s.dump(out / "calls" / label / "timing.json", vector)
            shutil.copytree(root, out / "prepared" / scenario)
            s.dump(out / "prepared" / (scenario + ".json"), s.manifest(root))
            changed = root / spec["changed"]
            old_ns = changed.stat().st_mtime_ns
            changed.write_bytes(spec["final"][spec["changed"]])
            stamp = max(time.time_ns(), old_ns + 2_000_000_000)
            os.utime(changed, ns=(stamp, stamp))
        pristine = out / "pristine" / scenario
        shutil.copytree(root, pristine)
        fixed[scenario] = s.manifest(root)
        s.require(s.manifest(pristine) == fixed[scenario], "pristine timestamp copy differs")
    s.dump(out / "fixture-manifests.json", fixed)
    rows = []
    for pair, side, scenario in schedule():
        label = f"sample-{pair}-{side}-{scenario}"
        root = out / "fixtures" / scenario
        shutil.rmtree(root)  # Only the explicitly owned study fixture, never project/user paths.
        shutil.copytree(out / "pristine" / scenario, root)
        s.require(s.manifest(root) == fixed[scenario], "input restoration differs")
        s.dump(out / "before" / (label + ".json"), s.manifest(root))
        result = ledger.run(label, [str(exe[side]), *config[scenario]["argv"]], root, env, timeout=600)
        folder = out / "calls" / label
        vector = s.timing(folder)
        # Retain the full post-state before work assertions can reject it.
        s.dump(folder / "timing.json", vector)
        s.dump(folder / "post.json", s.manifest(root))
        shutil.copytree(root, folder / "post")
        work(vector, scenario)
        rows.append(dict(pair=pair, side=side, scenario=scenario, timing=vector, launcher=result))
    after = {k: s.sha(p.read_bytes()) for k, p in exe.items()}
    s.dump(out / "identity-after.json", after)
    s.require(after == s.BINARY_SHA, "measured executable changed")
    s.dump(out / "completed.json", dict(allocation=ALLOCATION, root_mqb_calls=ledger.used,
           preparation_calls=2, measured_calls=24, samples=rows, **FLAGS))


def audit(out: Path, report: Path) -> dict:
    """Read-only replay. Recorded nanosecond timestamps are not re-inferred from extracted ZIPs."""
    check_helper()
    r, done = s.load(out / "request.json"), s.load(out / "completed.json")
    s.require(r["allocation"] == done["allocation"] == ALLOCATION and r["parent"] == PARENT
              and r["base"] == s.BASE and r["candidate"] == s.HEAD and r["attempt"] == 1
              and r["max_root_mqb_calls"] == MAX_CALLS, "request mismatch")
    s.require(all(r[k] is False and done[k] is False for k in FLAGS), "authority changed")
    s.require((done["root_mqb_calls"], done["preparation_calls"], done["measured_calls"]) == (26, 2, 24),
              "incomplete budget")
    config = fixtures(s.read_original(out / "original919.zip"))
    identities = {side: s.sha((out / "inputs" / side / "mqb.exe").read_bytes()) for side in s.BINARY_SHA}
    s.require(identities == s.BINARY_SHA == s.load(out / "identity-before.json")
              == s.load(out / "identity-after.json"), "binary identity drift")
    s.require(s.load(out / "schedule.json") == schedule() and len(done["samples"]) == 24, "grid changed")
    fixed = s.load(out / "fixture-manifests.json")
    s.require(set(fixed) == set(SCENARIOS), "fixture set changed")
    for scenario, spec in config.items():
        root = out / "pristine" / scenario
        byte_manifest(root, fixed[scenario])
        for name, raw in spec["final"].items():
            s.require((root / name).read_bytes() == raw, "fixture source drift")
        if spec["changed"]:
            prepared = s.load(out / "prepared" / (scenario + ".json"))
            byte_manifest(out / "prepared" / scenario, prepared)
            s.require(set(prepared) == set(fixed[scenario]), "mutation changed membership")
            differences = [k for k in prepared if prepared[k] != fixed[scenario][k]]
            s.require(differences == [spec["changed"]], "mutation affected other inputs")
            for name, raw in spec["before"].items():
                s.require((out / "prepared" / scenario / name).read_bytes() == raw, "preparation input drift")
        else:
            s.require(set(fixed[scenario]) == set(spec["final"]), "module fixture is not cold")
    tasks = [("prepare-" + sc, 0, "candidate", sc) for sc in SCENARIOS[:2]] + [
        (f"sample-{pair}-{side}-{sc}", pair, side, sc) for pair, side, sc in schedule()]
    s.require({p.name for p in (out / "calls").iterdir()} == {t[0] for t in tasks}, "extra/missing calls")
    rows = []
    previous = 0
    for index, (label, pair, side, scenario) in enumerate(tasks, 1):
        folder = out / "calls" / label
        start, result = s.load(folder / "started.json"), s.load(folder / "result.json")
        s.require((start["weight"], start["charged"], start["ceiling"], result["exit_code"])
                  == (1, index, MAX_CALLS, 0), "call budget/result changed")
        s.require(type(result["external_ns"]) is int and result["external_ns"] > 0
                  and start["utc_ns"] >= previous, "call clock invalid")
        previous = start["utc_ns"]
        normalized = lambda text: text.replace("\\", "/")
        expected_cwd = normalized(tasks_cwd(out, scenario))
        s.require(normalized(start["cwd"]) == expected_cwd, "working directory changed")
        expected_exe = expected_cwd.rsplit("/fixtures/", 1)[0] + "/inputs/" + side + "/mqb.exe"
        s.require(normalized(start["argv"][0]) == expected_exe
                  and start["argv"][1:] == config[scenario]["argv"], "command changed")
        s.require(all(s.sha((folder / (key + ".bin")).read_bytes()) == result[key + "_sha256"]
                      for key in ("stdout", "stderr")), "raw capture drift")
        vector = s.timing(folder)
        s.require(vector == s.load(folder / "timing.json"), "timing differs from raw")
        work(vector, scenario, preparing=(pair == 0))
        if pair:
            s.require(s.load(out / "before" / (label + ".json")) == fixed[scenario], "restore manifest drift")
            byte_manifest(folder / "post", s.load(folder / "post.json"))
            row = dict(pair=pair, side=side, scenario=scenario, timing=vector, launcher=result)
            s.require(row == done["samples"][len(rows)], "sample summary drift")
            rows.append(row)
    pairs, summaries = [], []
    for scenario in SCENARIOS:
        deltas, percentages = [], []
        for pair in range(1, 5):
            a, b = [next(x for x in rows if (x["pair"], x["side"], x["scenario"]) == (pair, side, scenario))
                    for side in ("baseline", "candidate")]
            va, vb = a["timing"], b["timing"]
            delta = vb["phases"]["total"] - va["phases"]["total"]
            pct = 100 * delta / va["phases"]["total"]
            deltas.append(delta); percentages.append(pct)
            pairs.append(dict(scenario=scenario, pair=pair, delta_ms=delta, delta_pct=pct,
                 counters_equal=va["counters"] == vb["counters"],
                 counter_breakdown_equal=va.get("counter_breakdown") == vb.get("counter_breakdown"),
                 phase_delta={k: vb["phases"][k] - v for k, v in va["phases"].items()},
                 work_delta={k: vb["attribution"]["work"][k] - v for k, v in va["attribution"]["work"].items()},
                 wall_delta={k: vb["attribution"]["wall"][k] - v for k, v in va["attribution"]["wall"].items()}))
        summaries.append(dict(scenario=scenario, median_delta_ms=statistics.median(deltas),
                              median_delta_pct=statistics.median(percentages), slower_pairs=sum(x > 0 for x in deltas)))
    value = dict(raw_integrity=True, pairs=pairs, summaries=summaries, **FLAGS,
                 limits=["new runner/launcher and restored B-built preparation, not original919 sequence",
                         "modules cold means project-state cold, not OS or machine cold",
                         "work intervals can overlap; no historical causal attribution or automatic HOLD removal"])
    s.dump(report, value)
    return value


def tasks_cwd(out: Path, scenario: str) -> str:
    # Bind all calls to the same recorded absolute study root, not the auditor's extraction path.
    first = s.load(out / "calls" / ("prepare-" + SCENARIOS[0]) / "started.json")["cwd"].replace("\\", "/")
    suffix = "/fixtures/" + SCENARIOS[0]
    s.require(first.endswith(suffix), "invalid recorded fixture root")
    return first[:-len(suffix)] + "/fixtures/" + scenario


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=["measure", "audit"])
    p.add_argument("--archive", type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--report", type=Path)
    args = p.parse_args()
    out = args.out.resolve()
    if args.phase == "measure" and out.exists():
        print("Refusing existing output directory; no files modified.", file=sys.stderr)
        return 1
    try:
        if args.phase == "measure":
            s.require(args.archive is not None, "archive required")
            measure(args.archive.resolve(), out)
        else:
            audit(out, args.report or out / "audit.json")
        return 0
    except BaseException as exc:
        if args.phase == "measure":
            s.dump(out / "failure.json", dict(error=repr(exc), complete=False, no_retry=True, **FLAGS))
        print(repr(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
