"""One-shot Windows WRITE_RESTRICTED IPC qualification capture (attempt 007).

This fixture cannot issue a producer receipt. A new source review and exact
commit registration must precede activating the branch-only workflow. The
outer capture Job is a cleanup envelope; the native child has its own Job.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys


ATTEMPT = "producer-delegation-007"
BRANCH = "refs/heads/feat/producer-clean-handoff-20261011"
PROBE = "tests/native/producer_delegation_probe.cpp"
RUNNER = "tests/native/run_producer_delegation_probe.py"
COMMON_RUNNER = "tests/native/run_producer_boundary_seal_probe.py"
JOB_RUNNER = "tests/native/producer_probe_job.py"
WORKFLOW = ".github/workflows/producer-boundary-seal-probe.yml"
PLAN = "docs/20261011-013406-producer-handoff/native_delegation_plan_007.md"
SOURCE_PATHS = (PROBE, RUNNER, COMMON_RUNNER, JOB_RUNNER, WORKFLOW, PLAN)
NATIVE_CODES = {"WRITE_RESTRICTED_IPC_DISQUALIFIED": 2,
                "FIXED_ROUTES_REFUSED_ONLY": 0, "INCONCLUSIVE": 3}
FIXED_CASES = [
    {"case": 0, "server_mode": "duplex", "client_access": 0x40000000,
     "client_sqos": 0x00120000, "restricting_sid_in_pipe_dacl": False},
    {"case": 1, "server_mode": "outbound", "client_access": 0x80000000,
     "client_sqos": 0x00120000, "restricting_sid_in_pipe_dacl": False},
    {"case": 2, "server_mode": "outbound", "client_access": 0x80000000,
     "client_sqos": 0x00160000, "restricting_sid_in_pipe_dacl": False},
    {"case": 3, "server_mode": "duplex", "client_access": 0x40000000,
     "client_sqos": 0x00120000, "restricting_sid_in_pipe_dacl": True},
]


def parse_native_summary(raw: bytes, exit_code: int) -> dict:
    """Interpret only a complete fixed-route result, without granting A2."""
    def unique_object(pairs):
        row = {}
        for key, value in pairs:
            if key in row:
                raise ValueError("Duplicate JSON key in native capture")
            row[key] = value
        return row

    def invalid_constant(value):
        raise ValueError(f"Non-finite JSON constant in native capture: {value}")

    rows = [json.loads(line, object_pairs_hook=unique_object,
                       parse_constant=invalid_constant)
            for line in raw.decode("utf-8").splitlines() if line.strip()]
    if not rows or any(not isinstance(row, dict) for row in rows):
        raise ValueError("Native capture must contain JSONL objects")
    summaries = [row for row in rows if row.get("kind") == "summary"]
    if len(summaries) != 1 or rows[-1] is not summaries[0]:
        raise ValueError("Native capture must end with exactly one summary")
    summary = summaries[0]
    classification = summary.get("classification")
    if (classification not in NATIVE_CODES or type(exit_code) is not int
            or exit_code != NATIVE_CODES[classification]):
        raise ValueError("Native classification/exit mismatch")
    if any(summary.get(key) is not False for key in ("A1", "A2", "A4")):
        raise ValueError("Probe attempted to assert product authority")
    for key in ("errors", "client_job_total", "client_job_active", "cases_completed"):
        if type(summary.get(key)) is not int or summary[key] < 0:
            raise ValueError(f"Missing or invalid native count: {key}")
    if summary["cases_completed"] > len(FIXED_CASES):
        raise ValueError("Native case count exceeds the fixed route budget")
    for key in ("positive_control", "client_natural_exit", "acl_revoked",
                "write_connection_refused"):
        if type(summary.get(key)) is not bool:
            raise ValueError(f"Missing or invalid native closure field: {key}")
    if classification != "INCONCLUSIVE":
        if (summary["errors"] != 0 or summary["positive_control"] is not True
                or summary["client_natural_exit"] is not True
                or summary["client_job_total"] != 1 or summary["client_job_active"] != 0
                or summary["acl_revoked"] is not True
                or summary["cases_completed"] != len(FIXED_CASES)):
            raise ValueError("Conclusive label lacks complete fixed-route evidence")
        if any(row.get("kind") in ("error", "fatal") for row in rows):
            raise ValueError("Native error record cannot support a conclusive result")
        if (classification == "FIXED_ROUTES_REFUSED_ONLY"
                and summary["write_connection_refused"] is not True):
            raise ValueError("Fixed-route refusal lacks the ordinary write control")
        counterexample = any(
            row.get("kind") == "case_final" and type(row.get("case")) is int
            and row["case"] in (0, 1, 2)
            and all(row.get(key) is True for key in (
                "client_opened", "impersonated", "write_opened",
                "post_completion_mutation_verified"))
            for row in rows)
        if (classification == "WRITE_RESTRICTED_IPC_DISQUALIFIED") != counterexample:
            raise ValueError("Native classification lacks matching raw route evidence")
    return summary


def main() -> int:
    sys.dont_write_bytecode = True
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-probe-sha256", required=True)
    parser.add_argument("--expected-runner-sha256", required=True)
    parser.add_argument("--expected-common-runner-sha256", required=True)
    parser.add_argument("--expected-job-runner-sha256", required=True)
    args = parser.parse_args()
    expected = {
        "GITHUB_REPOSITORY": "Iviesever/msvc-quick-build",
        "GITHUB_REF": BRANCH, "GITHUB_EVENT_NAME": "push",
        "GITHUB_RUN_NUMBER": "7", "GITHUB_RUN_ATTEMPT": "1",
        "RUNNER_ENVIRONMENT": "github-hosted",
        "GITHUB_WORKFLOW_REF": "Iviesever/msvc-quick-build/" + WORKFLOW + "@" + BRANCH,
    }
    if os.name != "nt" or any(os.environ.get(key) != value for key, value in expected.items()):
        raise RuntimeError("Attempt 007 workflow admission mismatch")
    if not re.fullmatch(r"[1-9][0-9]*", os.environ.get("GITHUB_RUN_ID", "")):
        raise RuntimeError("Missing actual workflow run identity")
    if not os.environ.get("ImageVersion") or os.environ.get("ImageOS") != "win22":
        raise RuntimeError("Missing expected Windows 2022 runner image identity")
    repo = Path(__file__).resolve().parents[2]
    # Validate both imported files before executing either module. The expected
    # values belong to the reviewed activating workflow, not a local override.
    for relative, digest in (
        (PROBE, args.expected_probe_sha256), (RUNNER, args.expected_runner_sha256),
        (COMMON_RUNNER, args.expected_common_runner_sha256),
        (JOB_RUNNER, args.expected_job_runner_sha256),
    ):
        if (not re.fullmatch(r"[a-f0-9]{64}", digest)
                or hashlib.sha256((repo / relative).read_bytes()).hexdigest() != digest):
            raise RuntimeError(f"Reviewed source hash mismatch: {relative}")
    from run_producer_boundary_seal_probe import (
        TELEMETRY_IMAGE_LIMIT, capture_once, hold_fixture_telemetry_image,
        identity, require_local_ntfs, selected_tools, write_json,
    )

    volume = require_local_ntfs(repo)
    actual_head = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                          cwd=repo, timeout=5).decode("ascii").strip()
    if actual_head != os.environ.get("GITHUB_SHA") or not re.fullmatch(r"[a-f0-9]{40}", actual_head):
        raise RuntimeError("Checkout is not the exact activating commit")
    output = repo / f"{ATTEMPT}-{os.environ['GITHUB_RUN_ID']}-1"
    output.mkdir(exist_ok=False)
    result = {
        "schema": 1, "attempt": ATTEMPT, "source_head": actual_head,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "status": "INCONCLUSIVE", "fixture_builds": 0, "fixture_invocations": 0,
        "A1": False, "A2": False, "A4": False, "A3": False, "A5": False,
        "deletion_enabled": False, "ordinary_msvc_qualified": False,
        "canonical_evidence_sealed": False,
        "outer_job_scope": "cleanup-envelope-not-native-producer-owner",
    }
    source_rows: list[dict] = []
    tool_rows: list[dict] = []
    try:
        compiler, selected, tool_rows = selected_tools()
        source_rows = [identity(repo / relative) for relative in SOURCE_PATHS]
        build = output / "build"
        build.mkdir()
        executable = build / "producer_delegation_probe.exe"
        argv = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MT", "/Z7",
                "/Od", "/W4", "/WX", "/permissive-", str(repo / PROBE),
                "/Fo" + str(build / "producer_delegation_probe.obj"),
                "/Fe" + str(executable), "/link", "/INCREMENTAL:NO", "/DEBUG:NONE"]
        write_json(output / "preflight.json", {
            "schema": 1, "attempt": ATTEMPT, "source_head": actual_head,
            "sources": source_rows,
            "captured_utc_before_compiler": datetime.now(timezone.utc).isoformat(),
            "tools": tool_rows, "selection": selected["selection"], "build_argv": argv,
            "volume": volume, "os": platform.platform(),
            "windows_version": list(sys.getwindowsversion()),
            "image_os": os.environ.get("ImageOS"), "image_version": os.environ.get("ImageVersion"),
            "run": os.environ["GITHUB_RUN_ID"], "run_number": 7, "run_attempt": 1,
            "workflow_ref": os.environ["GITHUB_WORKFLOW_REF"],
            "registered_workflow_id": "380958963",
            "compiler_root_wait_seconds": 90, "compiler_job_process_counts": [1, 2],
            "native_capture_wait_seconds": 40, "capture_cleanup_seconds_limit": 10,
            "fixture_watchdog_seconds": 30, "fixture_processes": 2,
            "helper_processes": 0, "auxiliary_watchdog_threads": 1,
            "native_child_job_processes": 1,
            "native_startup_attribute_count": 2,
            "native_startup_attributes": ["PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
                                          "PROC_THREAD_ATTRIBUTE_JOB_LIST"],
            "outer_job_scope": result["outer_job_scope"],
            "fixed_cases": FIXED_CASES, "pipe_object_limit_including_baselines": 10,
            "test_named_pipe_limit_including_baselines": 8,
            "anonymous_protocol_pipe_limit": 2,
            "fixture_file_limit": 1, "fixture_payload_bytes_limit": 4096,
            "deletion_enabled": False,
            "ordinary_mqb_calls": 0, "cost_calls": 0, "etw_calls": 0,
        })
        telemetry_image = compiler.with_name("vctip.exe")
        telemetry_identity = next(row for row in tool_rows if row["path"] == str(telemetry_image))
        result["fixture_telemetry_image_pin"] = pin_record = {}
        with hold_fixture_telemetry_image(telemetry_image, telemetry_identity, pin_record):
            result["fixture_builds"] = 1
            compiled = capture_once(argv, repo, selected["environment"], 90, "compile", output)
        if pin_record["errors"] or pin_record["released"] is not True:
            result["inconclusive_reason"] = "FIXTURE_IMAGE_PIN_FAILED"
            return 1
        if (compiled.get("timed_out") or compiled.get("exit_code") != 0
                or compiled.get("job_members_completed") is not True or compiled.get("errors")
                or compiled.get("job_termination_requested") is not False
                or compiled.get("job_total_processes") not in (1, 2)):
            result["inconclusive_reason"] = "BUILD_FAILED_OR_TIMED_OUT"
            return 1
        try:
            executable.with_suffix(".pdb").lstat()
        except FileNotFoundError:
            result["expected_link_pdb_absent"] = True
        else:
            result["expected_link_pdb_absent"] = False
        if not result["expected_link_pdb_absent"]:
            result["inconclusive_reason"] = "UNEXPECTED_LINK_PDB"
            return 1
        result["fixture_binary"] = identity(executable)
        write_json(output / "fixture-identity.json", result["fixture_binary"])
        result["fixture_invocations"] = 1
        native = capture_once([str(executable), str(output / "fixture")], repo,
                              selected["environment"], 40, "native", output)
        result["native_exit"] = native.get("exit_code")
        if (native.get("timed_out") or native.get("job_members_completed") is not True
                or native.get("job_total_processes") != 2 or native.get("errors")
                or native.get("job_termination_requested") is not False):
            result["inconclusive_reason"] = "NATIVE_EXECUTION_INCOMPLETE"
            return 1
        summary = parse_native_summary((output / "native.stdout.bin").read_bytes(),
                                       native["exit_code"])
        result["native_summary"] = summary
        result["status"] = summary["classification"]
        return 0 if result["status"] == "FIXED_ROUTES_REFUSED_ONLY" else 1
    except Exception as error:
        result["capture_error"] = f"{type(error).__name__}: {error}"
        return 1
    finally:
        changed = []
        readback_errors = []
        for row in source_rows + tool_rows:
            try:
                path = Path(row["path"])
                if identity(path, maximum_bytes=TELEMETRY_IMAGE_LIMIT
                            if path.name.lower() == "vctip.exe" else None) != row:
                    changed.append(row["path"])
            except Exception as error:
                readback_errors.append({"path": row["path"],
                    "error": f"{type(error).__name__}: {error}"})
        result["source_or_tool_identity_changes"] = changed
        retained = []
        try:
            for path in sorted(output.rglob("*")):
                try:
                    if path.is_file():
                        row = identity(path)
                        row["path"] = path.relative_to(output).as_posix()
                        retained.append(row)
                except Exception as error:
                    readback_errors.append({"path": str(path),
                        "error": f"{type(error).__name__}: {error}"})
        except Exception as error:
            readback_errors.append({"path": str(output),
                "error": f"{type(error).__name__}: {error}"})
        fixture_files = [row for row in retained if row["path"].startswith("fixture/")]
        result["retained_fixture_file_count"] = len(fixture_files)
        result["retained_fixture_payload_bytes"] = sum(row["bytes"] for row in fixture_files)
        fixture_budget_invalid = (len(fixture_files) > 1
                                  or result["retained_fixture_payload_bytes"] > 4096)
        if result["status"] != "INCONCLUSIVE" and len(fixture_files) != 1:
            fixture_budget_invalid = True
        result["readback_errors"] = readback_errors
        if changed or readback_errors or fixture_budget_invalid:
            result["status_before_final_validation"] = result["status"]
            result["status"] = "INCONCLUSIVE"
            result["inconclusive_reason"] = "FINAL_IDENTITY_READBACK_OR_BUDGET_INVALID"
        result["fixture_budget_invalid"] = fixture_budget_invalid
        result["retained_files_before_result"] = retained
        result["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(output / "result.json", result)
        print(json.dumps({"attempt": ATTEMPT, "status": result["status"],
                          "A1": False, "A2": False, "A4": False}), flush=True)
        if changed or readback_errors or fixture_budget_invalid:
            raise RuntimeError("Exact identity/readback or fixture budget unavailable; attempt invalid")


if __name__ == "__main__":
    raise SystemExit(main())
