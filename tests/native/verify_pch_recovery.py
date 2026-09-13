#!/usr/bin/env python3
"""Fixed PCH creator/consumer failure -> caller repair -> warm CLI regression.

Only --self-test is portable. Native evidence requires real Windows/MSVC and
keeps each first failure; no retry, inter-phase cache deletion or PDB inspection.
This supplements, rather than replaces, mqb_pch_e2e_tests and #186's collector.
"""
from __future__ import annotations

import argparse
import copy
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

from verify_foreground_recovery import artifacts, check_mutation, digest, save_json

PHASES = ("cold", "warm", "failure", "repaired", "repaired-warm")
MODES = ("creator", "consumer")
TARGET = "pch_recovery"


def snapshot(project: Path) -> dict:
    result = artifacts(project)
    for p in sorted((project / ".mqb").rglob("*.pch")):
        result[p.relative_to(project).as_posix()] = {
            "sha256": digest(p), "size": p.stat().st_size, "mtime_ns": p.stat().st_mtime_ns}
    return result


def pch_state(state: dict) -> dict:
    return {p: value for p, value in state.items() if p.startswith(".mqb/pch/")}


def header(value: int, configuration: str, broken: bool = False) -> str:
    # /MTd defines _DEBUG. Assert the actual compilation environment in the PCH
    # creator and again in main.cpp, rather than naming the test host Release.
    expected = "true" if configuration == "Debug" else "false"
    return ("#pragma once\n"
            f"inline constexpr int pch_value = {value};\n"
            f"inline constexpr bool expected_debug = {expected};\n"
            "#ifdef _DEBUG\nstatic_assert(expected_debug, \"MQB_PCH_CONFIG_MISMATCH\");\n"
            "#else\nstatic_assert(!expected_debug, \"MQB_PCH_CONFIG_MISMATCH\");\n#endif\n"
            + ('static_assert(false, "MQB_PCH_EXPECTED_CREATOR_FAILURE");\n' if broken else ""))


MAIN = r'''#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _DEBUG
static_assert(expected_debug, "MQB_PCH_CONSUMER_CONFIG_MISMATCH");
#else
static_assert(!expected_debug, "MQB_PCH_CONSUMER_CONFIG_MISMATCH");
#endif
int worker_value();
int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "") || std::strcmp(argv[2], "two words")) return 91;
    const char* token = std::getenv("MQB_PCH_TOKEN");
    if (!token || std::strcmp(token, "inherited-token")) return 92;
    char input[80]{};
    if (!std::fgets(input, sizeof(input), stdin) || std::strcmp(input, "pch-input\n")) return 93;
    const int value = worker_value();
    FILE* events = std::fopen("run-events.txt", "ab");
    if (!events) return 94;
    const bool written = std::fprintf(events, "%d\n", value) > 0;
    if (std::fclose(events) || !written) return 95;
    std::printf("MQB_PCH_%d_OUT\n", value);
    std::fprintf(stderr, "MQB_PCH_%d_ERR\n", value);
    return value;
}
'''


def check(mode: str, phase: str, config: str, obs: dict, events: list[str], cold: dict) -> list[str]:
    errors: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    out, err = obs["stdout"], obs["stderr"]
    before, after = obs["before"], obs["after"]
    expected = 4 if phase == "failure" else (41 if mode == "creator" else 43) if phase.startswith("repaired") else 37
    require(obs["exit_code"] == expected, f"expected exit {expected}")
    require(obs["events"] == events, "exact foreground event history")
    exe = f".mqb/bin/{TARGET}.exe"
    prefix = f".mqb/pch/{TARGET}/{config.lower()}/x64/"
    if phase == "failure":
        require(f"MQB_PCH_EXPECTED_{mode.upper()}_FAILURE" in out + err, "original compiler diagnostic missing")
        require("[run]" not in out and "[link]" not in out, "failed build handed off or linked stale state")
        require(exe in before and after.get(exe) == before[exe], "failure changed the old executable")
        if mode == "creator":
            require("[compile]" not in out, "creator failure continued into consumers")
            consumers = {p: v for p, v in before.items() if p.endswith(".obj") and not p.startswith(".mqb/pch/")}
            require(bool(consumers) and all(after.get(p) == v for p, v in consumers.items()),
                    "creator failure changed consumer objects")
    else:
        require(f"MQB_PCH_{expected}_OUT" in out and f"MQB_PCH_{expected}_ERR" in err,
                "wrong program generation or missing diagnostics")
        require(f"[run] {TARGET}.exe" in out, "foreground handoff missing")
        require(exe in after, "executable identity missing")
        require(prefix + "project.pch" in after and prefix + "creator.obj" in after,
                "actual target-configuration PCH pair missing")
        require(not any(p.startswith(".mqb/pch/") and not p.startswith(prefix) for p in after),
                "unexpected PCH target configuration")
        if phase.endswith("warm"):
            require(not any(token in out for token in ("[pch]", "[compile]", "[link]")), "warm invocation rebuilt")
            require(bool(before) and before == after, "warm PCH/object/executable identity changed")
        else:
            require("[compile] worker.cpp" in out and f"[link] {TARGET}.exe" in out, "cold/repair did not rebuild consumer and link")
            require(out.count(f"[link] {TARGET}.exe") == 1, "target must link exactly once")
            if phase == "cold" or mode == "creator":
                require("[pch]" in out and "[compile] main.cpp" in out, "creator generation did not rebuild both consumers")
                if all(token in out for token in ("[pch]", "[compile] main.cpp", "[compile] worker.cpp", "[link]")):
                    require(out.index("[pch]") < min(out.index("[compile] main.cpp"), out.index("[compile] worker.cpp"))
                            and max(out.index("[compile] main.cpp"), out.index("[compile] worker.cpp")) < out.index("[link]"),
                            "creator/consumer/link order violated")
            else:
                require("[up-to-date] main.cpp" in out and "[compile] main.cpp" not in out,
                        "consumer-only repair unnecessarily rebuilt main")
            if phase == "repaired":
                require(exe in after and exe in cold and after[exe]["sha256"] != cold[exe]["sha256"],
                        "repair reused old executable bytes")
                if mode == "creator":
                    pch = prefix + "project.pch"
                    require(pch in after and pch in cold and after[pch]["sha256"] != cold[pch]["sha256"],
                            "creator repair reused old PCH bytes")
    if phase.endswith("warm") or (mode == "consumer" and phase != "cold"):
        require("[up-to-date] pch" in out and "[pch]" not in out, "expected PCH reuse missing")
        require(bool(pch_state(before)) and pch_state(before) == pch_state(after), "unchanged PCH pair was rewritten")
    return errors


def run_case(mqb: Path, root: Path, config: str, mode: str) -> dict:
    root.mkdir()
    project = root / "project-\u65e5\u672c space"
    project.mkdir()
    (project / "pch.hpp").write_text(header(37, config), encoding="utf-8")
    (project / "main.cpp").write_text(MAIN, encoding="utf-8")
    (project / "worker.cpp").write_text("int worker_value() { return pch_value; }\n", encoding="utf-8")
    result = {"mode": mode, "configuration": config, "status": "incomplete", "phases": []}
    events: list[str] = []
    cold: dict = {}
    active = None
    try:
        for index, phase in enumerate(PHASES):
            active = {"name": phase, "status": "incomplete", "invocation_started": False, "invocation_completed": False}
            result["phases"].append(active)
            record = root / f"{index + 1:02d}-{phase}"
            record.mkdir()
            if phase in ("failure", "repaired"):
                path = project / ("pch.hpp" if mode == "creator" else "worker.cpp")
                prior = snapshot(project)
                previous = path.stat().st_mtime_ns
                text = (header(41 if phase == "repaired" else 37, config, phase == "failure")
                        if mode == "creator" else 'int worker_value() { return pch_value + 6; }\n'
                        if phase == "repaired" else 'static_assert(false, "MQB_PCH_EXPECTED_CONSUMER_FAILURE");\nint worker_value() { return pch_value; }\n')
                path.write_text(text, encoding="utf-8")
                mutation = {"path": path.name, "previous_source_mtime_ns": previous,
                            "mtime_ns": path.stat().st_mtime_ns, "observed_after_write_ns": time.time_ns(),
                            "prior_artifacts": prior}
                save_json(record / "mutation.json", mutation)
                failures = check_mutation(mutation)
                if failures:
                    raise RuntimeError("invalid fixture: " + "; ".join(failures))
            for path in project.iterdir():
                if path.suffix in (".cpp", ".hpp"):
                    shutil.copy2(path, record / path.name)
            argv = ([] if phase.endswith("warm") else ["run"]) + [
                "main.cpp", "worker.cpp", "--env", "vs", "--no-discover", "--std", "c++23",
                "--jobs", "2", "--verbose", "--" + config.lower(),
                "--runtime", "MTd" if config == "Debug" else "MT", "--pch", "pch.hpp", "-o", TARGET,
            ] + (["--run"] if phase.endswith("warm") else []) + ["--", "", "two words"]
            before = snapshot(project)
            save_json(record / "invocation.json", {
                "executable": str(mqb), "argv": argv, "cwd": str(project),
                "environment_overrides": {"MQB_PCH_TOKEN": "inherited-token"}, "stdin_hex": b"pch-input\n".hex(),
                "inputs": {p.name: {"sha256": digest(p), "mtime_ns": p.stat().st_mtime_ns}
                           for p in project.iterdir() if p.suffix in (".cpp", ".hpp")}, "before": before})
            active["invocation_started"] = True
            completed = subprocess.run([str(mqb), *argv], cwd=project, env=dict(os.environ, MQB_PCH_TOKEN="inherited-token"),
                                       input=b"pch-input\n", stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            active["invocation_completed"] = True
            (record / "stdout.bin").write_bytes(completed.stdout)
            (record / "stderr.bin").write_bytes(completed.stderr)
            event_path = project / "run-events.txt"
            if phase != "failure":
                events.append(str((41 if mode == "creator" else 43) if phase.startswith("repaired") else 37))
            obs = {"exit_code": completed.returncode, "stdout": completed.stdout.decode("utf-8", errors="replace"),
                   "stderr": completed.stderr.decode("utf-8", errors="replace"), "before": before, "after": snapshot(project),
                   "events": event_path.read_text(encoding="utf-8").splitlines() if event_path.exists() else []}
            save_json(record / "observation.json", obs)
            executable = project / ".mqb/bin" / f"{TARGET}.exe"
            if executable.is_file():
                shutil.copy2(executable, record / executable.name)
            for directory in ("cache", "deps", "pch"):
                state = project / ".mqb" / directory
                if state.exists():
                    shutil.copytree(state, record / directory, ignore=shutil.ignore_patterns("*.pdb", "*.idb"))
            errors = check(mode, phase, config, obs, events, cold)
            active.update(status="failed" if errors else "passed", errors=errors)
            print(f"{config}/{mode}/{phase}: exit={completed.returncode} errors={errors}", flush=True)
            if errors:
                result["status"] = "failed"
                break
            if phase == "cold":
                cold = obs["after"]
        else:
            result["status"] = "passed"
    except Exception as error:
        result.update(status="error", error=f"{type(error).__name__}: {error}")
        if active is not None:
            active.update(status="error", error=result["error"])
    result["not_run"] = list(PHASES[len(result["phases"]):])
    save_json(root / "result.json", result)
    return result


def self_test() -> None:
    controls = rejected = 0
    for config in ("Debug", "Release"):
        for mode in MODES:
            prefix = f".mqb/pch/{TARGET}/{config.lower()}/x64/"
            state = {p: {"sha256": "original", "size": 1, "mtime_ns": 1} for p in (
                prefix + "project.pch", prefix + "creator.obj", f".mqb/bin/{TARGET}.exe", ".mqb/obj/main.obj", ".mqb/obj/worker.obj")}
            for phase in PHASES:
                value = (41 if mode == "creator" else 43) if phase.startswith("repaired") else 37
                after = copy.deepcopy(state)
                out = "[up-to-date] pch\n[up-to-date] main.cpp\n"
                if phase == "failure":
                    out = (out if mode == "consumer" else "") + f"MQB_PCH_EXPECTED_{mode.upper()}_FAILURE"
                    err, code, events = "", 4, []
                else:
                    if phase == "cold" or (phase == "repaired" and mode == "creator"):
                        out = "[pch] pch.hpp\n[compile] main.cpp\n[compile] worker.cpp\n[link] pch_recovery.exe\n"
                    elif phase == "repaired":
                        out += "[compile] worker.cpp\n[link] pch_recovery.exe\n"
                    if phase == "repaired":
                        after[f".mqb/bin/{TARGET}.exe"]["sha256"] = "new"
                        if mode == "creator":
                            after[prefix + "project.pch"]["sha256"] = "new"
                    out += f"[run] {TARGET}.exe\nMQB_PCH_{value}_OUT\n"
                    err, code, events = f"MQB_PCH_{value}_ERR", value, [str(value)]
                obs = {"exit_code": code, "stdout": out, "stderr": err, "events": events, "before": state, "after": after}
                assert not check(mode, phase, config, obs, events, state), (mode, phase)
                controls += 1
                for key, bad in (("exit_code", 0), ("stdout", ""), ("events", ["stale"]), ("after", {})):
                    changed = copy.deepcopy(obs)
                    changed[key] = bad
                    assert check(mode, phase, config, changed, events, state), (mode, phase, key)
                    rejected += 1
                if phase.endswith("warm"):
                    for token in ("[pch]", "[compile]", "[link]"):
                        assert check(mode, phase, config, dict(obs, stdout=out + token), events, state)
                        rejected += 1
                if phase == "failure":
                    assert check(mode, phase, config, dict(obs, stdout=out + "[run]"), events, state)
                    rejected += 1
                if phase != "failure":
                    assert check(mode, phase, config, dict(obs, stderr=""), events, state)
                    rejected += 1
                if phase == "repaired":
                    changed = copy.deepcopy(obs)
                    changed["after"][f".mqb/bin/{TARGET}.exe"]["sha256"] = "original"
                    assert check(mode, phase, config, changed, events, state)
                    rejected += 1
                if phase.endswith("warm") or (mode == "consumer" and phase != "cold"):
                    for attribute, value in (("sha256", "modified"), ("mtime_ns", 2)):
                        changed = copy.deepcopy(obs)
                        changed["after"][prefix + "project.pch"][attribute] = value
                        assert check(mode, phase, config, changed, events, state)
                        rejected += 1
    print(f"PCH assertion-only self-test: {controls} positive controls, {rejected} rejecting mutations; no native execution")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--mqb", type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.mqb or not args.output or not args.configuration:
        parser.error("--mqb, --configuration and --output are required")
    if os.name != "nt":
        parser.error("native execution requires Windows")
    mqb = args.mqb.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    save_json(output / "plan.json", {
        "schema": 1, "mqb": str(mqb), "mqb_sha256": digest(mqb), "configuration": args.configuration,
        "platform": platform.platform(), "python": sys.version, "head": os.environ.get("MQB_EXPECTED_HEAD"),
        "base": os.environ.get("MQB_EXPECTED_BASE"), "run_id": os.environ.get("GITHUB_RUN_ID"),
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"), "modes": MODES, "phases": PHASES,
        "expected_invocations": 10, "retry_budget": 0, "cache_cleanup_between_phases": False,
        "pdb_quiescence_proven": False, "release_authorized": False})
    results = [run_case(mqb, output / mode, args.configuration, mode) for mode in MODES]
    passed = all(case["status"] == "passed" for case in results)
    save_json(output / "summary.json", {"passed": passed, "cases": results})
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
