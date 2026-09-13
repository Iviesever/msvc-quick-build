#!/usr/bin/env python3
"""Fixed, non-retrying CLI failure -> repair -> warm-run regression.

The self-test exercises this collector's assertions, NOT MQB or Windows. Native
runs retain raw diagnostics, source generations, cache bytes and artifact
identities. PDB/service ownership and same-timestamp mutation are not asserted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

PHASES = (
    ("cold", "original", 37, False),
    ("warm-source-first", "original", 37, True),
    ("compile-failure", "compile-failure", 4, False),
    ("compile-repaired", "compile-repaired", 41, False),
    ("compile-repaired-warm", "compile-repaired", 41, True),
    ("link-failure", "link-failure", 5, False),
    ("link-repaired", "link-repaired", 43, False),
    ("link-repaired-warm", "link-repaired", 43, True),
)


def save_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifacts(project: Path) -> dict:
    # Do not open PDB/shared-service files or infer service quiescence from hashes.
    return {
        p.relative_to(project).as_posix(): {
            "sha256": digest(p), "size": p.stat().st_size, "mtime_ns": p.stat().st_mtime_ns
        }
        for p in sorted((project / ".mqb").rglob("*"))
        if p.is_file() and p.suffix.lower() in {".exe", ".obj", ".ifc"}
    }


def check_mutation(evidence: dict) -> list[str]:
    """Normal editing must create a fresh input, not a future-dated input."""
    errors = []
    modified = evidence["mtime_ns"]
    previous = evidence["previous_source_mtime_ns"]
    if previous is not None and modified <= previous:
        errors.append("source mutation did not advance its actual filesystem timestamp")
    if modified > evidence["observed_after_write_ns"]:
        errors.append("source mutation is future-dated")
    if any(modified <= item["mtime_ns"] for item in evidence["prior_artifacts"].values()):
        errors.append("source mutation is not newer than the existing target artifacts")
    return errors


def source(variant: str, module: bool) -> str:
    prefix = "import mqb_recovery;\n" if module else "int recovery_value() { return 7; }\n"
    exit_code = {"original": 37, "compile-repaired": 41, "link-repaired": 43}.get(variant, 37)
    result = "MQB_RECOVERY_MISSING_LINK_SYMBOL()" if variant == "link-failure" else str(exit_code)
    text = prefix + r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern int MQB_RECOVERY_MISSING_LINK_SYMBOL();
int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "") || std::strcmp(argv[2], "two words")) return 91;
    const char* token = std::getenv("MQB_RECOVERY_TOKEN");
    if (!token || std::strcmp(token, "inherited-token")) return 92;
    char input[80]{};
    if (!std::fgets(input, sizeof(input), stdin) || std::strcmp(input, "recovery-input\n")) return 93;
    if (recovery_value() != 7) return 94;
    FILE* events = std::fopen("run-events.txt", "ab");
    if (!events) return 95;
    const bool written = std::fputs("@VARIANT@\n", events) >= 0;
    if (std::fclose(events) || !written) return 96;
    std::puts("MQB_RECOVERY_@VARIANT@_OUT");
    std::fputs("MQB_RECOVERY_@VARIANT@_ERR\n", stderr);
    return @RESULT@;
}
'''
    text = text.replace("@VARIANT@", variant).replace("@RESULT@", result)
    if variant == "compile-failure":
        text += '\nstatic_assert(false, "MQB_RECOVERY_EXPECTED_COMPILE_FAILURE");\n'
    return text


def check(phase: tuple, observation: dict, expected_events: list[str]) -> list[str]:
    name, variant, expected_exit, warm = phase
    stdout, stderr = observation["stdout"], observation["stderr"]
    errors = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    require(observation["exit_code"] == expected_exit, f"{name}: expected exit {expected_exit}")
    require(observation["events"] == expected_events, f"{name}: exact foreground side-effect history")
    if expected_exit in {4, 5}:
        require("[run]" not in stdout, f"{name}: failed build must not hand off to old executable")
        needle = "MQB_RECOVERY_EXPECTED_COMPILE_FAILURE" if expected_exit == 4 else "MQB_RECOVERY_MISSING_LINK_SYMBOL"
        require(needle in stdout + stderr, f"{name}: original failure diagnostic missing")
        if expected_exit == 5:
            require("LNK2019" in stdout + stderr or "LNK2001" in stdout + stderr,
                    f"{name}: expected unresolved-symbol linker diagnostic")
    else:
        require(f"MQB_RECOVERY_{variant}_OUT" in stdout, f"{name}: correct generation stdout")
        require(f"MQB_RECOVERY_{variant}_ERR" in stderr, f"{name}: correct generation stderr")
        require("[run] recovery.exe" in stdout, f"{name}: foreground handoff missing")
        require(any(p.endswith("/recovery.exe") for p in observation["after"]),
                f"{name}: executable identity missing")
        if warm:
            require("[compile]" not in stdout and "[link]" not in stdout,
                    f"{name}: warm run unexpectedly compiled or linked")
            require(bool(observation["before"]) and observation["before"] == observation["after"],
                    f"{name}: warm artifact bytes/mtimes changed")
        else:
            require("[compile] main.cpp" in stdout and "[link] recovery.exe" in stdout,
                    f"{name}: cold/repair must compile current consumer and link")
    return errors


def run_case(mqb: Path, root: Path, configuration: str, module: bool) -> dict:
    root.mkdir()
    project = root / "project-\u65e5\u672c space"
    project.mkdir()
    if module:
        (project / "support.ixx").write_text(
            "export module mqb_recovery;\nexport int recovery_value() { return 7; }\n", encoding="utf-8")
    result = {"mode": "named-module" if module else "ordinary", "status": "incomplete", "phases": []}
    expected_events: list[str] = []
    previous_variant = None
    active = None
    try:
        for index, phase in enumerate(PHASES):
            name, variant, expected_exit, warm = phase
            active = {"name": name, "status": "incomplete", "invocation_started": False, "invocation_completed": False}
            result["phases"].append(active)
            record = root / f"{index + 1:02d}-{name}"
            record.mkdir()
            main = project / "main.cpp"
            if variant != previous_variant:
                old_time = main.stat().st_mtime_ns if main.exists() else None
                prior_artifacts = artifacts(project)
                main.write_text(source(variant, module), encoding="utf-8")
                # Use the real write time, never synthesize a future timestamp.
                # A bad fixture fails before invocation; no sleep, retry or
                # weakening of the product's source-vs-output freshness rule.
                mutation = {"previous_source_mtime_ns": old_time,
                            "mtime_ns": main.stat().st_mtime_ns,
                            "observed_after_write_ns": time.time_ns(),
                            "prior_artifacts": prior_artifacts}
                save_json(record / "mutation.json", mutation)
                mutation_errors = check_mutation(mutation)
                if mutation_errors:
                    raise RuntimeError("invalid fixture: " + "; ".join(mutation_errors))
                previous_variant = variant
            for file in project.iterdir():
                if file.suffix in {".cpp", ".ixx"}:
                    shutil.copy2(file, record / file.name)
            inputs = {p.name: {"sha256": digest(p), "mtime_ns": p.stat().st_mtime_ns}
                      for p in project.iterdir() if p.suffix in {".cpp", ".ixx"}}
            sources = ["main.cpp"] + (["support.ixx"] if module else [])
            argv = (["run"] if not warm else []) + sources + [
                "--env", "vs", "--no-discover", "--std", "latest" if module else "c++23",
                "--jobs", "2", "--verbose", "--" + configuration.lower(),
                "--runtime", "MTd" if configuration == "Debug" else "MT", "-o", "recovery",
            ] + (["--run"] if warm else []) + ["--", "", "two words"]
            before = artifacts(project)
            save_json(record / "invocation.json", {
                "executable": str(mqb), "argv": argv, "cwd": str(project),
                "environment_overrides": {"MQB_RECOVERY_TOKEN": "inherited-token"},
                "stdin_hex": b"recovery-input\n".hex(), "inputs": inputs, "before": before,
            })
            environment = dict(os.environ, MQB_RECOVERY_TOKEN="inherited-token")
            # One invocation only. No timeout kill/retry or cache cleanup on failure.
            active["invocation_started"] = True
            completed = subprocess.run([str(mqb), *argv], cwd=project, env=environment,
                                       input=b"recovery-input\n", stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, check=False)
            active["invocation_completed"] = True
            (record / "stdout.bin").write_bytes(completed.stdout)
            (record / "stderr.bin").write_bytes(completed.stderr)
            events_file = project / "run-events.txt"
            events = events_file.read_text(encoding="utf-8").splitlines() if events_file.exists() else []
            if expected_exit not in {4, 5}:
                expected_events.append(variant)
            observation = {
                "exit_code": completed.returncode,
                "stdout": completed.stdout.decode("utf-8", errors="replace"),
                "stderr": completed.stderr.decode("utf-8", errors="replace"),
                "events": events, "before": before, "after": artifacts(project),
            }
            save_json(record / "observation.json", observation)
            # Keep actual executable generations; other generated artifacts are
            # identity records, not archived failure-time PDB/service snapshots.
            executable = project / ".mqb" / "bin" / "recovery.exe"
            if executable.is_file():
                shutil.copy2(executable, record / "recovery.exe")
            for directory in ("cache", "deps"):
                state = project / ".mqb" / directory
                if state.exists():
                    shutil.copytree(state, record / directory)
            errors = check(phase, observation, expected_events)
            active.update(status="failed" if errors else "passed", errors=errors)
            print(f"{configuration}/{result['mode']}/{name}: exit={completed.returncode} errors={errors}", flush=True)
            if errors:
                result["status"] = "failed"
                break  # Dependent phases are NOT retried or credited as passed.
        else:
            result["status"] = "passed"
    except Exception as error:
        result["status"] = "error"
        result["error"] = f"{type(error).__name__}: {error}"
        if active is not None:
            active.update(status="error", error=result["error"])
    result["not_run"] = [p[0] for p in PHASES[len(result["phases"]):]]
    save_json(root / "result.json", result)
    return result


def self_test() -> None:
    # Assertion mutation tests only; no simulated result is native evidence.
    import copy
    artifact = {".mqb/bin/recovery.exe": {"sha256": "test-only", "size": 1, "mtime_ns": 1}}
    good = {"exit_code": 41, "stdout": "[run] recovery.exe\nMQB_RECOVERY_compile-repaired_OUT",
            "stderr": "MQB_RECOVERY_compile-repaired_ERR", "events": ["compile-repaired"],
            "before": artifact, "after": artifact}
    phase = PHASES[4]
    assert not check(phase, good, ["compile-repaired"])
    mutations = [
        ("exit_code", 0), ("stdout", "[run] recovery.exe"), ("stderr", ""),
        ("events", []), ("stdout", good["stdout"] + "[compile] main.cpp"),
        ("stdout", good["stdout"] + "[link] recovery.exe"), ("before", {}), ("after", {}),
    ]
    for key, value in mutations:
        bad = copy.deepcopy(good)
        bad[key] = value
        assert check(phase, bad, ["compile-repaired"]), (key, value)
    for attribute, value in (("sha256", "different"), ("mtime_ns", 2)):
        bad = copy.deepcopy(good)
        bad["after"] = copy.deepcopy(artifact)
        bad["after"][".mqb/bin/recovery.exe"][attribute] = value
        assert check(phase, bad, ["compile-repaired"]), attribute
    for negative in (PHASES[2], PHASES[5]):
        diagnostic = "MQB_RECOVERY_EXPECTED_COMPILE_FAILURE" if negative[2] == 4 else "LNK2019 MQB_RECOVERY_MISSING_LINK_SYMBOL"
        evidence = dict(good, exit_code=negative[2], stdout=diagnostic, stderr="")
        assert not check(negative, evidence, ["compile-repaired"])
        assert check(negative, dict(evidence, stdout=diagnostic + "[run] recovery.exe"), ["compile-repaired"])
        assert check(negative, dict(evidence, stdout=""), ["compile-repaired"])
        assert check(negative, dict(evidence, events=["stale-child"]), ["compile-repaired"])
    mutation = {"previous_source_mtime_ns": 1, "mtime_ns": 3,
                "observed_after_write_ns": 4, "prior_artifacts": {"obj": {"mtime_ns": 2}}}
    assert not check_mutation(mutation)
    assert not check_mutation(dict(mutation, previous_source_mtime_ns=None, prior_artifacts={}))
    for bad in (dict(mutation, mtime_ns=1), dict(mutation, mtime_ns=5),
                dict(mutation, prior_artifacts={"obj": {"mtime_ns": 3}})):
        assert check_mutation(bad), bad
    print("collector self-test: 5 positive controls and 19 rejecting mutations passed; no native execution")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--mqb", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.mqb or not args.output or not args.configuration:
        parser.error("--mqb, --output and --configuration are required for native execution")
    if os.name != "nt":
        parser.error("native execution requires Windows; use --self-test for assertion-only checks")
    mqb = args.mqb.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite an earlier run.
    save_json(output / "plan.json", {
        "schema": 1, "mqb": str(mqb), "mqb_sha256": digest(mqb),
        "configuration": args.configuration, "platform": platform.platform(),
        "python": sys.version, "head_sha": os.environ.get("MQB_EXPECTED_HEAD"),
        "run_id": os.environ.get("GITHUB_RUN_ID"), "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "phases": PHASES, "modes": ["ordinary", "named-module"], "expected_invocations": 16,
        "retry_budget": 0, "cache_cleanup_between_phases": False,
        "pdb_quiescence_proven": False, "release_authorized": False,
    })
    results = [run_case(mqb, output / mode, args.configuration, module)
               for mode, module in (("ordinary", False), ("named-module", True))]
    passed = all(case["status"] == "passed" for case in results)
    save_json(output / "summary.json", {"passed": passed, "cases": results})
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
