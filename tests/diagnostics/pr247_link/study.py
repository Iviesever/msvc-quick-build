"""One registered link-only diagnosis. Never runs the old benchmark or changes #247.

Two distinct inputs: immutable 919 executables, and separately identified diagnostic
copies. Windows process/pipe boundaries exist ONLY in the latter. No automatic
performance pass, integration, source fix, retry, or observer-cost subtraction.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import math
import subprocess
import sys
import time
import zipfile

REPO = "Iviesever/msvc-quick-build"
BRANCH = "diag/pr247-link-boundaries-001"
BASE = "399e06fa43d2052ac0d452e1320438c015c65145"
HEAD = "f0ef131321dc0720c83ce55766edde4b751495ad"
ARTIFACT_ID = 11539029999
ARCHIVE_SHA = "1641d0d9b8af917fc1ea883f76b54056d7ae233af1754d2d6ce81e8aee106ce6"
ARCHIVE_BYTES = 11056074
BINARY_SHA = {
    "baseline": "38f1693634f85ef8e8dc0d31c3438e0e0718dc80ec2e74cb83cb116cd804191b",
    "candidate": "6d52d638f89336217ce8dd1ae277088bdda591445fc7aecb6f7edcee7f4d5546",
}
SOURCE_SHA = {
    "baseline": "988cc41a514dc91f34314ee3cfa63da880d7ef7417fd28d7445460e1ecd95d36",
    "candidate": "5830102f7802585ef3e97990e62711bf2e4553b254c5979ba9b655903bee3163",
}
RUNNER = "cpp/src/platform/windows/WindowsProcessRunner.cpp"
COORDINATOR = "cpp/src/orchestration/incremental/MsvcIncrementalLinkCoordinator.cpp"
SOURCE_PINS = {
    RUNNER: "bdf8c49c163fdfa2141144bdc47761647175e6b79063f36c884566589fa8a931",
    COORDINATOR: "d83a2234147237024c30a2fea7f41181a63ed6c0486843ac19c45ee555d202eb",
    "tests/native/build_mqb.ps1": "48ae9d8a1b3e6fe0306f8d494a44aff9a05a9fe146f2b6b23004b77e0abcbb73",
}
POINTS = ("call_begin", "runner_begin", "create_begin", "create_end", "wait_begin", "wait_end",
          "join_begin", "join_end", "runner_return", "call_end", "observe_begin", "observe_end")
PAIRS = (("baseline", "candidate"), ("candidate", "baseline"),
         ("baseline", "candidate"), ("candidate", "baseline"))
ARGS = ["main.cpp", "helper.cpp", "--output", "bench"]
TRACE_ENV = "MQB_PR247_LINK_TRACE"


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def archive_members(z: zipfile.ZipFile) -> dict[str, bytes]:
    names, output, total = set(), {}, 0
    for info in z.infolist():
        name = info.filename
        p = PurePosixPath(name)
        require(not p.is_absolute() and ".." not in p.parts and "\\" not in name
                and ":" not in name and name and p.as_posix() == name.rstrip("/")
                and name.casefold() not in names,
                "unsafe/duplicate archive member: " + name)
        names.add(name.casefold())
        mode = info.external_attr >> 16
        require(not stat.S_ISLNK(mode), "symlink in source/archive")
        total += info.file_size
        require(total <= 256 * 1024 * 1024, "archive expansion limit")
        if not info.is_dir():
            output[name] = z.read(info)  # checks each CRC
    return output


def read_original(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    require(len(data) == ARCHIVE_BYTES and sha(data) == ARCHIVE_SHA, "wrong 919 original ZIP")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        members = archive_members(z)
    identity = json.loads(members["provenance/identity-before.json"])
    require(identity["run_id"] == "37754575256" and identity["attempt"] == "1", "wrong original run")
    for side, commit in (("baseline", BASE), ("candidate", HEAD)):
        require(sha(members[side + "/mqb.exe"]) == BINARY_SHA[side], "binary identity mismatch")
        require(sha(members["provenance/" + side + "-source.zip"]) == SOURCE_SHA[side], "source ZIP mismatch")
        require(identity["inputs"][side]["sha"] == commit, "source commit mismatch")
    return members


def replace_once(text: str, old: str, new: str) -> str:
    require(text.count(old) == 1, "patch anchor count != 1: " + old[:75])
    return text.replace(old, new, 1)


def instrument(path: str, raw: bytes) -> bytes:
    require(sha(raw) == SOURCE_PINS[path], "unapproved diagnostic source: " + path)
    text = raw.decode("utf-8").replace("\r\n", "\n")
    text = '#include "../../../../pr247_link_diag.hpp"\n' + text
    ns = "mqb::pr247_diag::"
    if path == RUNNER:
        edits = [
            ("WindowsProcessRunner::run(const process::ProcessSpec& spec) {",
             "WindowsProcessRunner::run(const process::ProcessSpec& spec) {\n    " + ns + "mark(" + ns + "runner_begin);"),
            ("    const BOOL created = ::CreateProcessW(", "    " + ns + "mark(" + ns + "create_begin);\n    const BOOL created = ::CreateProcessW("),
            ("        &process_info);\n    const auto launch_duration", "        &process_info);\n    " + ns + "mark(" + ns + "create_end);\n    const auto launch_duration"),
            ("        DWORD wait_result = WAIT_FAILED;", "        " + ns + "mark(" + ns + "wait_begin);\n        DWORD wait_result = WAIT_FAILED;"),
            ("        if (wait_result == WAIT_FAILED) {", "        " + ns + "mark(" + ns + "wait_end);\n        if (wait_result == WAIT_FAILED) {"),
            ("        join_readers();\n        result.termination", "        " + ns + "mark(" + ns + "join_begin);\n        join_readers();\n        " + ns + "mark(" + ns + "join_end);\n        " + ns + "retain_process_times(process_handle.get());\n        result.termination"),
            ("        result.exit_code = static_cast<int>(exit_code);\n        return result;",
             "        result.exit_code = static_cast<int>(exit_code);\n        " + ns + "mark(" + ns + "runner_return);\n        return result;"),
        ]
    else:
        edits = [
            ("    auto linked = linker_.link(invocation);", "    " + ns + "begin();\n    auto linked = linker_.link(invocation);\n    " + ns + "mark(" + ns + "call_end);"),
            ("    const auto observed_libraries =\n        msvc::MsvcLinker::observed_library_paths(linked->stdout_text);",
             "    " + ns + "mark(" + ns + "observe_begin);\n    const auto observed_libraries =\n        msvc::MsvcLinker::observed_library_paths(linked->stdout_text);\n    " + ns + "mark(" + ns + "observe_end);\n    " + ns + "flush(linked->stdout_text, linked->stderr_text);"),
        ]
    for old, new in edits:
        text = replace_once(text, old, new)
    return text.replace("\n", "\r\n").encode("utf-8")


def manifest(root: Path) -> dict:
    result = {}
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), "symlink in owned fixture")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = {
                "sha256": sha(path.read_bytes()), "bytes": path.stat().st_size,
                "mtime_ns": path.stat().st_mtime_ns,
            }
    return result


def admission() -> dict:
    require(os.environ.get("GITHUB_REPOSITORY") == REPO, "wrong repository")
    require(os.environ.get("GITHUB_EVENT_NAME") == "pull_request", "wrong event")
    require(os.environ.get("GITHUB_RUN_ATTEMPT") == "1", "diagnosis cannot be retried")
    event = load(Path(os.environ["GITHUB_EVENT_PATH"]))
    pr = event["pull_request"]
    require(event["action"] == "opened" and pr["head"]["ref"] == BRANCH,
            "not the registered first opened event")
    require(pr["base"]["sha"] == BASE and pr["head"]["repo"]["full_name"] == REPO,
            "unapproved base or fork")
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    require(actual == pr["head"]["sha"], "harness checkout mismatch")
    return {"allocation": "pr247-link-boundaries-001", "harness_head": actual,
            "base": BASE, "candidate": HEAD, "run": os.environ["GITHUB_RUN_ID"],
            "attempt": 1, "event": "opened", "image": os.environ.get("ImageVersion"),
            "max_root_mqb_calls": 21, "product_acceptance": False, "release_authorized": False}


class Calls:
    def __init__(self, root: Path, ceiling: int):
        self.root, self.ceiling, self.used = root, ceiling, 0
        root.mkdir()

    def run(self, label: str, argv: list[str], cwd: Path, env: dict, weight: int = 1,
            timeout: int = 180) -> dict:
        require(self.used + weight <= self.ceiling, "root MQB call budget exhausted")
        self.used += weight
        folder = self.root / label
        folder.mkdir()
        dump(folder / "started.json", {"argv": argv, "cwd": str(cwd), "weight": weight,
             "charged": self.used, "ceiling": self.ceiling, "utc_ns": time.time_ns()})
        start = time.perf_counter_ns()
        try:
            result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, timeout=timeout)
            data = {"exit_code": result.returncode, "external_ns": time.perf_counter_ns() - start,
                    "stdout_sha256": sha(result.stdout), "stderr_sha256": sha(result.stderr)}
            (folder / "stdout.bin").write_bytes(result.stdout)
            (folder / "stderr.bin").write_bytes(result.stderr)
        except BaseException as exc:
            for name in ("stdout", "stderr"):
                (folder / (name + ".bin")).write_bytes(getattr(exc, name, None) or b"")
            dump(folder / "failed.json", {"error": repr(exc), "external_ns": time.perf_counter_ns() - start,
                 "quiescence_proven": False, "stop_no_more_calls": True})
            raise
        dump(folder / "result.json", data)
        require(data["exit_code"] == 0, "nonzero invocation; original output retained: " + label)
        return data


def prepare(archive: Path, out: Path, build: bool) -> None:
    out.mkdir()
    dump(out / "request.json", admission() if build else {"offline_prepare_only": True})
    members = read_original(archive)
    shutil.copyfile(archive, out / "original919.zip")
    header = Path(__file__).with_name("LinkDiag.hpp").read_bytes()
    patch_manifest = {}
    for side in ("baseline", "candidate"):
        binary = out / "original" / side / "mqb.exe"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(members[side + "/mqb.exe"])
        with zipfile.ZipFile(io.BytesIO(members["provenance/" + side + "-source.zip"])) as z:
            files = archive_members(z)
        root = out / "sources" / side
        root.mkdir(parents=True)
        require(sha(files["tests/native/build_mqb.ps1"]) == SOURCE_PINS["tests/native/build_mqb.ps1"], "build helper changed")
        changes = {}
        for name, raw in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if name in (RUNNER, COORDINATOR):
                modified = instrument(name, raw)
                changes[name] = {"original_sha256": sha(raw), "instrumented_sha256": sha(modified)}
                raw = modified
            path.write_bytes(raw)
        (root / "pr247_link_diag.hpp").write_bytes(header)
        changes["pr247_link_diag.hpp"] = {"sha256": sha(header)}
        patch_manifest[side] = changes
        with zipfile.ZipFile(out / (side + "-diagnostic-source.zip"), "x", zipfile.ZIP_DEFLATED) as z:
            for p in sorted(root.rglob("*")):
                if p.is_file(): z.write(p, p.relative_to(root).as_posix())
    fixture = out / "fixture-sources"
    fixture.mkdir()
    for name in ("main.cpp", "helper.cpp", "common.hpp"):
        prefix = "evidence/comparison/pair-1-baseline-calls/post-suite-sources/ordinary-1/"
        (fixture / name).write_bytes(members[prefix + name])
    dump(out / "patch-manifest.json", patch_manifest)
    if not build: return
    env = os.environ.copy()
    env.pop(TRACE_ENV, None)
    ledger = Calls(out / "build-calls", 4)
    identities = {}
    for side in ("baseline", "candidate"):
        source = out / "sources" / side
        target = out / "instrumented" / side / "mqb.exe"
        target.parent.mkdir(parents=True)
        command = ["pwsh", "-NoProfile", "-File", str(source / "tests/native/build_mqb.ps1"),
                   "-BuilderMqbPath", str(out / "original/candidate/mqb.exe"), "-RepoRoot", str(source),
                   "-Version", "5.6.0", "-Configuration", "Release", "-Clean", "-OutputPath", str(target)]
        ledger.run("build-" + side, command, source, env, weight=2, timeout=1200)
        identities[side] = {"binary_sha256": sha(target.read_bytes()),
                            "diagnostic_source_zip_sha256": sha((out / (side + "-diagnostic-source.zip")).read_bytes())}
    # Upload source archives and logs, not the large transient product build trees.
    shutil.rmtree(out / "sources")
    dump(out / "build-completed.json", {"root_mqb_budget_charged": ledger.used,
         "inputs": identities, "builder_sha256": BINARY_SHA["candidate"],
         "instrumented_not_product": True, "request": load(out / "request.json")})


def timing(folder: Path) -> dict:
    rows = []
    for path in (folder / "stdout.bin", folder / "stderr.bin"):
        for line in path.read_bytes().decode("utf-8-sig", errors="strict").splitlines():
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(item, dict) and "phases" in item and "counters" in item:
                rows.append(item)
    require(len(rows) == 1, "missing/duplicate timing vector")
    return rows[0]


def validate_trace(data: dict, stdout: bytes, stderr: bytes) -> dict:
    require(data["schema"] == 1 and type(data["frequency"]) is int and data["frequency"] > 0, "bad trace clock")
    require(data["product_acceptance"] is False and data["release_authorized"] is False, "trace authority changed")
    ticks = data["ticks"]
    require(set(ticks) == set(POINTS), "trace boundaries missing/extra")
    values = [ticks[key] for key in POINTS]
    require(all(type(v) is int and v > 0 for v in values), "invalid boundary")
    require(all(a <= b for a, b in zip(values, values[1:])), "out-of-order boundary")
    require(data["process_times_ok"] is True and data["child_pid"] > 0, "process times unavailable")
    require(data["exit_100ns"] >= data["creation_100ns"] > 0, "process lifetime invalid")
    require(data["stdout_bytes"] == len(stdout) and data["stderr_bytes"] == len(stderr), "buffer length mismatch")
    def delta(a: str, b: str) -> float:
        return (ticks[b] - ticks[a]) * 1000 / data["frequency"]
    return {"call_ms": delta("call_begin", "call_end"),
            "launch_ms": delta("create_begin", "create_end"),
            "wait_ms": delta("wait_begin", "wait_end"),
            "pipe_join_tail_ms": delta("join_begin", "join_end"),
            "observation_ms": delta("observe_begin", "observe_end"),
            "runner_setup_ms": delta("runner_begin", "create_begin"),
            "launch_to_wait_ms": delta("create_end", "wait_begin"),
            "wait_to_join_ms": delta("wait_end", "join_begin"),
            "post_join_ms": delta("join_end", "runner_return"),
            "return_cleanup_ms": delta("runner_return", "call_end"),
            "child_lifetime_ms": (data["exit_100ns"] - data["creation_100ns"]) / 10000,
            "child_cpu_ms": (data["kernel_100ns"] + data["user_100ns"]) / 10000,
            "intervals_overlap": True, "filter_only_ms": None}


def measure(inputs: Path, out: Path) -> None:
    require(os.name == "nt", "Windows measurement required")
    out.mkdir()
    request = admission()
    build = load(inputs / "build-completed.json")
    require(build["request"]["harness_head"] == request["harness_head"]
            and build["request"]["run"] == request["run"], "build/observe run mismatch")
    dump(out / "request.json", request)
    read_original(inputs / "original919.zip")
    executables, before = {}, {}
    for mode in ("original", "instrumented"):
        for side in ("baseline", "candidate"):
            exe = inputs / mode / side / "mqb.exe"
            expected = BINARY_SHA[side] if mode == "original" else build["inputs"][side]["binary_sha256"]
            require(sha(exe.read_bytes()) == expected, "measured executable mismatch")
            executables[(mode, side)] = exe
            before[mode + "-" + side] = expected
    dump(out / "identity-before.json", before)
    fixture = out / "fixture"
    shutil.copytree(inputs / "fixture-sources", fixture)
    env = os.environ.copy()
    env.pop(TRACE_ENV, None)
    ledger = Calls(out / "calls", 17)
    ledger.run("prepare-fixture", [str(executables[("original", "candidate")]), *ARGS, "--timings=json"], fixture, env)
    prep = timing(out / "calls/prepare-fixture")
    require(prep["counters"]["cl_processes_launched"] == 2 and prep["counters"]["link_processes_launched"] == 1,
            "fixture preparation work differs")
    pristine = out / "pristine"
    shutil.copytree(fixture, pristine)
    fixed = manifest(pristine)
    dump(out / "fixture-manifest.json", fixed)
    schedule = [(mode, pair, side) for mode in ("original", "instrumented")
                for pair, sides in enumerate(PAIRS, 1) for side in sides]
    dump(out / "schedule.json", schedule)
    rows = []
    for mode, pair, side in schedule:
        label = f"{mode}-{pair}-{side}"
        # Only the owned disposable copy is reset. Original archives are never edited.
        shutil.rmtree(fixture)
        shutil.copytree(pristine, fixture)
        require(manifest(fixture) == fixed, "fixture bytes/mtime changed on restore")
        dump(out / "fixture-before" / (label + ".json"), manifest(fixture))
        sample_env = env.copy()
        trace_path = out / "calls" / label / "boundaries.json"
        if mode == "instrumented": sample_env[TRACE_ENV] = str(trace_path)
        result = ledger.run(label, [str(executables[(mode, side)]), *ARGS, "--linker-arg", "/MAP", "--timings=json"], fixture, sample_env)
        folder = out / "calls" / label
        vector = timing(folder)
        counts = vector["counters"]
        require(counts["cl_processes_launched"] == 0 and counts["link_processes_launched"] == 1
                and counts["lib_processes_launched"] == 0, "sample not one link-only action")
        dump(folder / "timing.json", vector)
        dump(folder / "post-fixture.json", manifest(fixture))
        shutil.copytree(fixture, folder / "post-fixture")
        row = {"mode": mode, "pair": pair, "side": side, "timing": vector, "launcher": result}
        if mode == "instrumented":
            row["boundaries"] = validate_trace(load(trace_path), trace_path.with_suffix(".json.stdout").read_bytes(),
                                                 trace_path.with_suffix(".json.stderr").read_bytes())
        rows.append(row)
        dump(folder / "verified.json", row)
    after = {mode + "-" + side: sha(path.read_bytes()) for (mode, side), path in executables.items()}
    dump(out / "identity-after.json", after)
    require(after == before, "executable changed during study")
    dump(out / "completed.json", {"allocation": request["allocation"], "samples": rows,
         "measurement_root_mqb_calls": ledger.used, "build_root_mqb_budget": 4,
         "raw_scores_replace_919": False, "product_acceptance": False, "release_authorized": False,
         "limits": ["diagnostic rebuilds are not original executables", "launcher differs from 919",
                    "join tail is not total overlapping reader work", "GetProcessTimes child lifetime overlaps QPC intervals",
                    "observer I/O is after observed boundaries but inside whole-MQB time",
                    "no historical causal attribution or automatic HOLD removal"]})


def audit(inputs: Path, out: Path, report: Path | None = None) -> None:
    """Re-read original records. This never launches a process or changes old evidence."""
    request, completed = load(out / "request.json"), load(out / "completed.json")
    require(completed["product_acceptance"] is False and completed["release_authorized"] is False
            and completed["raw_scores_replace_919"] is False, "unauthorized success")
    require(completed["measurement_root_mqb_calls"] == 17, "incomplete invocation budget")
    require(load(out / "identity-before.json") == load(out / "identity-after.json"), "identity changed")
    build = load(inputs / "build-completed.json")
    read_original(inputs / "original919.zip")
    expected_ids = {}
    for mode in ("original", "instrumented"):
        for side in ("baseline", "candidate"):
            value = BINARY_SHA[side] if mode == "original" else build["inputs"][side]["binary_sha256"]
            require(sha((inputs / mode / side / "mqb.exe").read_bytes()) == value, "input binary drift")
            expected_ids[mode + "-" + side] = value
    require(load(out / "identity-before.json") == expected_ids, "identity not bound to actual input")
    require(build["root_mqb_budget_charged"] == 4, "build budget mismatch")
    require(request["harness_head"] == build["request"]["harness_head"]
            and request["run"] == build["request"]["run"], "cross-run input")
    expected = [[mode, pair, side] for mode in ("original", "instrumented")
                for pair, sides in enumerate(PAIRS, 1) for side in sides]
    require(load(out / "schedule.json") == expected, "schedule changed")
    require(len(completed["samples"]) == 16, "incomplete sample grid")
    require(len(list((out / "calls").iterdir())) == 17, "extra or missing invocation")
    fixed, rows = load(out / "fixture-manifest.json"), []
    for index, (mode, pair, side) in enumerate(expected):
        label = f"{mode}-{pair}-{side}"
        folder = out / "calls" / label
        start, result = load(folder / "started.json"), load(folder / "result.json")
        require(result["exit_code"] == 0 and start["weight"] == 1
                and start["charged"] == index + 2 and start["ceiling"] == 17, "call/result order invalid")
        require(start["argv"][1:] == ARGS + ["--linker-arg", "/MAP", "--timings=json"], "sample arguments changed")
        require(sha((folder / "stdout.bin").read_bytes()) == result["stdout_sha256"]
                and sha((folder / "stderr.bin").read_bytes()) == result["stderr_sha256"], "raw capture changed")
        require(load(out / "fixture-before" / (label + ".json")) == fixed, "input restore drift")
        recorded_post = load(folder / "post-fixture.json")
        actual_post = manifest(folder / "post-fixture")
        # ZIP extraction does not preserve nanosecond times. Preserve recorded times,
        # compare actual bytes/names here; live restore checked the full timestamps.
        require(set(recorded_post) == set(actual_post) and all(
            recorded_post[k]["sha256"] == actual_post[k]["sha256"]
            and recorded_post[k]["bytes"] == actual_post[k]["bytes"] for k in actual_post), "post fixture bytes differ")
        vector = timing(folder)
        require(vector == load(folder / "timing.json"), "timing record changed")
        require(vector["counters"]["cl_processes_launched"] == 0
                and vector["counters"]["link_processes_launched"] == 1
                and vector["counters"]["lib_processes_launched"] == 0, "wrong work")
        require(vector["cache"]["compile"] == {"hits": 2, "misses": 0}
                and vector["cache"]["link"] == {"hits": 0, "misses": 1}, "cache behavior drift")
        require(all(math.isfinite(v) and v >= 0 for v in vector["phases"].values()), "invalid timing")
        row = {"mode": mode, "pair": pair, "side": side, "timing": vector, "launcher": result}
        if mode == "instrumented":
            trace = folder / "boundaries.json"
            row["boundaries"] = validate_trace(load(trace), (folder / "boundaries.json.stdout").read_bytes(),
                                               (folder / "boundaries.json.stderr").read_bytes())
        require(row == completed["samples"][index] and row == load(folder / "verified.json"), "summary differs from raw")
        rows.append(row)
    paired = []
    for mode in ("original", "instrumented"):
        for pair in range(1, 5):
            a = next(r for r in rows if (r["mode"], r["pair"], r["side"]) == (mode, pair, "baseline"))
            b = next(r for r in rows if (r["mode"], r["pair"], r["side"]) == (mode, pair, "candidate"))
            value = {"mode": mode, "pair": pair,
                     "whole_mqb_delta_ms": b["timing"]["phases"]["total"] - a["timing"]["phases"]["total"],
                     "link_execution_delta_ms": b["timing"]["attribution"]["work"]["link_execution"]
                                                - a["timing"]["attribution"]["work"]["link_execution"]}
            if mode == "instrumented":
                value["boundary_deltas_ms"] = {k: b["boundaries"][k] - a["boundaries"][k]
                                               for k in a["boundaries"] if k.endswith("_ms")
                                               and a["boundaries"][k] is not None}
            paired.append(value)
    dump(report or out / "audit.json", {"raw_integrity": True, "pairs": paired,
         "product_acceptance": False, "release_authorized": False,
         "causal_attribution": "not established; all samples retained; instrumented totals include observer"})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["prepare", "measure", "audit"])
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--inputs", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--report", type=Path, help="new output path for a read-only audit replay")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase != "audit" and out.exists():
        print("Refusing existing output directory; no files modified.", file=sys.stderr)
        return 1
    try:
        if args.phase == "prepare":
            require(args.archive is not None, "--archive required")
            prepare(args.archive.resolve(), out, not args.offline)
        elif args.phase == "audit":
            require(args.inputs is not None, "--inputs required")
            audit(args.inputs.resolve(), out, args.report)
        else:
            require(args.inputs is not None and not args.offline, "live inputs required")
            measure(args.inputs.resolve(), out)
        return 0
    except BaseException as exc:
        failure = args.out.resolve() / "failure.json"
        if args.phase != "audit" and not failure.exists():
            dump(failure, {"error": repr(exc), "complete": False, "no_retry": True,
                          "product_acceptance": False, "release_authorized": False})
        print(repr(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
