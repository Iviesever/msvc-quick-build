"""One-shot Windows restricted-child child-image loader-snaps diagnostic capture (attempt 010).

This diagnostic always remains INCONCLUSIVE for product/IPC qualification.
A new source review and exact
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


ATTEMPT = "producer-loader-snaps-010"
BRANCH = "refs/heads/feat/producer-clean-handoff-20261011"
PROBE = "tests/native/producer_loader_snaps_probe.cpp"
BASE_PROBE = "tests/native/producer_delegation_probe.cpp"
RUNNER = "tests/native/run_producer_loader_snaps_probe.py"
PE_HELPER = "tests/native/producer_loader_snaps_pe.py"
IMAGE_HELPER = "tests/native/producer_loader_snaps_image.py"
PE_TEST = "tests/native/test_producer_loader_snaps_pe.py"
CAPTURE_TEST = "tests/native/test_producer_loader_snaps_capture.py"
COMMON_RUNNER = "tests/native/run_producer_boundary_seal_probe.py"
JOB_RUNNER = "tests/native/producer_probe_job.py"
WORKFLOW = ".github/workflows/producer-boundary-seal-probe.yml"
PLAN = "docs/20261011-013406-producer-handoff/native_startup_plan_010.md"
SOURCE_PATHS = (PROBE, BASE_PROBE, RUNNER, PE_HELPER, IMAGE_HELPER, PE_TEST,
                CAPTURE_TEST, COMMON_RUNNER, JOB_RUNNER, WORKFLOW, PLAN, ".gitattributes")
MAX_NATIVE_CAPTURE_BYTES = 32 * 1024 * 1024
MAX_NATIVE_ROWS = 16384
MAX_IMAGE_BYTES = 8 * 1024 * 1024


def parse_native_summary(raw: bytes, exit_code: int) -> dict:
    """Validate diagnostic capture; never interpret an IPC or producer verdict."""
    if len(raw) > MAX_NATIVE_CAPTURE_BYTES:
        raise ValueError("Startup diagnostic capture exceeds its byte bound")

    def unique_object(pairs):
        row = {}
        for key, value in pairs:
            if key in row:
                raise ValueError("Duplicate JSON key in native capture")
            row[key] = value
        return row

    def invalid_constant(value):
        raise ValueError(f"Non-finite JSON constant in native capture: {value}")

    lines = [line for line in raw.decode("utf-8").splitlines() if line.strip()]
    if not lines or len(lines) > MAX_NATIVE_ROWS:
        raise ValueError("Missing or oversized native diagnostic record sequence")
    rows = [json.loads(line, object_pairs_hook=unique_object,
                       parse_constant=invalid_constant) for line in lines]
    if any(not isinstance(row, dict) for row in rows):
        raise ValueError("Native capture must contain JSONL objects")
    if any(type(row.get("sequence")) is not int or row["sequence"] != index
           for index, row in enumerate(rows, 1)):
        raise ValueError("Native diagnostic sequence is not exact and consecutive")
    summaries = [row for row in rows if row.get("kind") == "summary"]
    if len(summaries) != 1 or rows[-1] is not summaries[0]:
        raise ValueError("Native capture must end with exactly one summary")
    summary = summaries[0]
    if (summary.get("classification") != "INCONCLUSIVE"
            or summary.get("diagnostic_only") is not True
            or type(exit_code) is not int or exit_code != 3):
        raise ValueError("Startup diagnostic cannot return a qualification label")
    if any(summary.get(key) is not False for key in
           ("A1", "A2", "A3", "A4", "A5", "deletion_enabled")):
        raise ValueError("Startup diagnostic attempted to assert product authority")
    for key in ("errors", "ipc_cases_executed", "events_observed", "event_continuations",
                "continuation_successes", "wait_calls", "main_wait_calls", "cleanup_wait_calls",
                "path_wchars_emitted", "debug_string_bytes_requested", "debug_string_bytes_read"):
        if type(summary.get(key)) is not int or summary[key] < 0:
            raise ValueError(f"Missing or invalid diagnostic count: {key}")
    if (summary["ipc_cases_executed"] != 0 or summary["events_observed"] > 1024
            or summary["event_continuations"] > summary["events_observed"]
            or summary["continuation_successes"] > summary["event_continuations"]
            or summary["wait_calls"] > 2048 or summary["main_wait_calls"] > 1792
            or summary["cleanup_wait_calls"] > 256
            or summary["wait_calls"] != summary["main_wait_calls"] + summary["cleanup_wait_calls"]
            or summary["wait_calls"] < summary["events_observed"]
            or summary["path_wchars_emitted"] > 1048576
            or summary["debug_string_bytes_requested"] > 1048576
            or summary["debug_string_bytes_read"] > summary["debug_string_bytes_requested"]):
        raise ValueError("Diagnostic event/IPC count exceeds the registered scope")
    for key in ("startup_observed", "startup_frame_valid", "exit_event_observed",
                "actual_child_exit_observed", "cleanup_requested", "capture_complete",
                "structural_observation_failed", "bound_hit", "child_image_pin_bound",
                "child_image_event_bound", "child_image_final_bound"):
        if type(summary.get(key)) is not bool:
            raise ValueError(f"Missing or invalid diagnostic field: {key}")
    if summary["startup_frame_valid"] and not summary["startup_observed"]:
        raise ValueError("Valid startup frame lacks its observed transport")
    if summary["actual_child_exit_observed"]:
        code = summary.get("client_exit_code")
        if type(code) is not int or not 0 <= code <= 0xffffffff:
            raise ValueError("Observed child exit lacks an actual DWORD status")
    elif "client_exit_code" in summary:
        raise ValueError("Unobserved child exit cannot contain a default code")
    if summary["capture_complete"] and (
            summary["errors"] != 0 or not summary["exit_event_observed"]
            or not summary["actual_child_exit_observed"]
            or summary["events_observed"] != summary["event_continuations"]
            or summary["events_observed"] != summary["continuation_successes"]
            or summary["debug_string_bytes_requested"] != summary["debug_string_bytes_read"]
            or summary["structural_observation_failed"] or summary["bound_hit"]
            or not all(summary[key] for key in ("child_image_pin_bound",
                       "child_image_event_bound", "child_image_final_bound"))):
        raise ValueError("Complete diagnostic capture lacks exit/continuation closure")
    if any(row.get("kind") in ("case_begin", "case_final", "impersonation") for row in rows):
        raise ValueError("Startup-only diagnostic executed an IPC qualification case")
    return summary


def main() -> int:
    sys.dont_write_bytecode = True
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-probe-sha256", required=True)
    parser.add_argument("--expected-base-probe-sha256", required=True)
    parser.add_argument("--expected-runner-sha256", required=True)
    parser.add_argument("--expected-pe-helper-sha256", required=True)
    parser.add_argument("--expected-image-helper-sha256", required=True)
    parser.add_argument("--expected-common-runner-sha256", required=True)
    parser.add_argument("--expected-job-runner-sha256", required=True)
    args = parser.parse_args()
    expected = {
        "GITHUB_REPOSITORY": "Iviesever/msvc-quick-build",
        "GITHUB_REF": BRANCH, "GITHUB_EVENT_NAME": "push",
        "GITHUB_RUN_NUMBER": "10", "GITHUB_RUN_ATTEMPT": "1",
        "RUNNER_ENVIRONMENT": "github-hosted",
        "GITHUB_WORKFLOW_REF": "Iviesever/msvc-quick-build/" + WORKFLOW + "@" + BRANCH,
    }
    if os.name != "nt" or any(os.environ.get(key) != value for key, value in expected.items()):
        raise RuntimeError("Attempt 010 workflow admission mismatch")
    if not re.fullmatch(r"[1-9][0-9]*", os.environ.get("GITHUB_RUN_ID", "")):
        raise RuntimeError("Missing actual workflow run identity")
    if not os.environ.get("ImageVersion") or os.environ.get("ImageOS") != "win22":
        raise RuntimeError("Missing expected Windows 2022 runner image identity")
    repo = Path(__file__).resolve().parents[2]
    # Validate the diagnostic, included base source and all imported helpers
    # before executing any helper. Expected values belong to the reviewed
    # activating workflow, not a local override.
    for relative, digest in (
        (PROBE, args.expected_probe_sha256),
        (BASE_PROBE, args.expected_base_probe_sha256), (RUNNER, args.expected_runner_sha256),
        (PE_HELPER, args.expected_pe_helper_sha256), (IMAGE_HELPER, args.expected_image_helper_sha256),
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
    from producer_loader_snaps_image import hold_loader_snaps_images

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
        "diagnostic_only": True,
    }
    source_rows: list[dict] = []
    tool_rows: list[dict] = []
    image_preparation: dict = {}
    try:
        compiler, selected, tool_rows = selected_tools()
        source_rows = [identity(repo / relative) for relative in SOURCE_PATHS]
        build = output / "build"
        build.mkdir()
        executable = build / "producer_loader_snaps_probe.exe"
        child_executable = build / "producer_loader_snaps_child.exe"
        argv = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MT", "/Z7",
                "/Od", "/W4", "/WX", "/permissive-", str(repo / PROBE),
                "/Fo" + str(build / "producer_loader_snaps_probe.obj"),
                "/Fe" + str(executable), "/link", "/INCREMENTAL:NO", "/DEBUG:NONE"]
        write_json(output / "preflight.json", {
            "schema": 1, "attempt": ATTEMPT, "source_head": actual_head,
            "sources": source_rows,
            "captured_utc_before_compiler": datetime.now(timezone.utc).isoformat(),
            "tools": tool_rows, "selection": selected["selection"], "build_argv": argv,
            "volume": volume, "os": platform.platform(),
            "windows_version": list(sys.getwindowsversion()),
            "image_os": os.environ.get("ImageOS"), "image_version": os.environ.get("ImageVersion"),
            "run": os.environ["GITHUB_RUN_ID"], "run_number": 10, "run_attempt": 1,
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
            "diagnostic_only": True, "ipc_cases_executed_limit": 0,
            "pipe_object_limit_including_baselines": 2,
            "test_named_pipe_limit_including_baselines": 0,
            "anonymous_protocol_pipe_limit": 2, "startup_protocol_bytes_limit": 56,
            "fixture_file_limit": 0, "fixture_payload_bytes_limit": 0,
            "debug_only_this_process": True, "debug_event_limit": 1024,
            "debug_main_event_limit": 960, "debug_cleanup_event_reserve": 64,
            "debug_wait_call_limit": 2048, "debug_main_wait_call_limit": 1792,
            "debug_cleanup_wait_call_reserve": 256,
            "debug_wait_milliseconds_limit": 50, "debug_main_seconds_limit": 20,
            "debug_cleanup_seconds_limit": 5,
            "virtual_query_limit_per_exception_event": 1,
            "virtual_query_scope": "actual-child-current-exception-address-only",
            "debug_string_bytes_per_event_limit": 4096,
            "debug_string_bytes_total_limit": 1048576,
            "module_path_wchars_limit": 4096,
            "emitted_path_wchars_total_limit": 1048576,
            "native_jsonl_acceptance_bytes_limit": MAX_NATIVE_CAPTURE_BYTES,
            "native_jsonl_acceptance_rows_limit": MAX_NATIVE_ROWS,
            "jsonl_limits_are_write_side_truncation": False,
            "parent_image": str(executable), "child_image": str(child_executable),
            "image_bytes_limit_each": MAX_IMAGE_BYTES,
            "additional_child_build_file_limit": 1, "image_copy_write_chunk_limit": 65536,
            "image_copy_write_call_limit": 128, "image_patch_write_call_limit": 1,
            "image_patch_bytes": 4, "image_flush_call_limit": 1,
            "image_held_full_read_pass_limit": 6, "image_inventory_full_read_pass_limit": 2,
            "image_final_pins": "GENERIC_READ / FILE_SHARE_READ / non-inheritable",
            "image_field": "new-child-PE-LoadConfig-GlobalFlagsSet-0-to-2-only",
            "owned_child_diagnostic_cleanup_may_be_requested": True,
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
        # No path-based image hash substitutes for these actual held reads.
        # The context keeps both images pinned through capture and final checks.
        with hold_loader_snaps_images(executable, child_executable, image_preparation) as images:
            parent_pe = images["parent_pe"]
            result["fixture_binary"] = {"path": str(executable), "bytes": parent_pe["bytes"],
                "sha256": parent_pe["sha256"], "physical_identity": images["parent_identity"]}
            child_id = images["child_identity"]
            result["child_binary"] = {"path": str(child_executable),
                "bytes": int(child_id["size_decimal"]), "sha256": images["child_sha256"],
                "physical_identity": child_id}
            write_json(output / "fixture-identity.json", {
                "parent": result["fixture_binary"], "child": result["child_binary"]})
            result["fixture_invocations"] = 1
            native = capture_once([str(executable), "--parent", str(output / "fixture"),
                str(child_executable), child_id["volume_serial_decimal"], child_id["file_id_hex"],
                child_id["size_decimal"]], repo, selected["environment"], 40, "native", output)
        if (image_preparation["errors"] or not image_preparation["post_capture_verified"]
                or not image_preparation["pins_released"]):
            result["inconclusive_reason"] = "IMAGE_PREPARATION_OR_FINAL_PIN_CHECK_FAILED"
            return 1
        result["native_exit"] = native.get("exit_code")
        if (native.get("timed_out") or native.get("job_members_completed") is not True
                or native.get("job_total_processes") != 2 or native.get("errors")
                or native.get("job_termination_requested") is not False):
            result["inconclusive_reason"] = "NATIVE_EXECUTION_INCOMPLETE"
            return 1
        with (output / "native.stdout.bin").open("rb") as stream:
            raw = stream.read(MAX_NATIVE_CAPTURE_BYTES + 1)
        summary = parse_native_summary(raw, native["exit_code"])
        result["native_summary"] = summary
        result["status"] = summary["classification"]
        result["diagnostic_capture_complete"] = summary["capture_complete"]
        # Even a complete diagnostic is not a successful qualification.
        return 1
    except Exception as error:
        result["capture_error"] = f"{type(error).__name__}: {error}"
        return 1
    finally:
        changed = []
        readback_errors = []
        if image_preparation:
            write_json(output / "image-preparation.json", image_preparation)
            result["image_preparation_ready"] = image_preparation["ready_for_native"]
            result["image_pins_post_capture_verified"] = image_preparation["post_capture_verified"]
            result["image_pins_released"] = image_preparation["pins_released"]
            if image_preparation["errors"]:
                readback_errors.append({"path": str(output / "image-preparation.json"),
                    "errors": image_preparation["errors"]})
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
                        image = path.name in ("producer_loader_snaps_probe.exe", "producer_loader_snaps_child.exe")
                        maximum = MAX_IMAGE_BYTES if image else (
                            MAX_NATIVE_CAPTURE_BYTES if path.name == "native.stdout.bin" else None)
                        row = identity(path, maximum_bytes=maximum)
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
        fixture_budget_invalid = (len(fixture_files) != 0
                                  or result["retained_fixture_payload_bytes"] != 0)
        build_files = {row["path"] for row in retained if row["path"].startswith("build/")}
        allowed_build_files = {"build/producer_loader_snaps_probe.exe",
            "build/producer_loader_snaps_probe.obj", "build/producer_loader_snaps_child.exe"}
        build_budget_invalid = (not build_files <= allowed_build_files
            or (result["fixture_invocations"] == 1 and build_files != allowed_build_files))
        result["retained_build_file_count"] = len(build_files)
        result["build_budget_invalid"] = build_budget_invalid
        result["readback_errors"] = readback_errors
        if changed or readback_errors or fixture_budget_invalid or build_budget_invalid:
            result["status_before_final_validation"] = result["status"]
            result["status"] = "INCONCLUSIVE"
            result["inconclusive_reason"] = "FINAL_IDENTITY_READBACK_OR_BUDGET_INVALID"
        result["fixture_budget_invalid"] = fixture_budget_invalid
        result["retained_files_before_result"] = retained
        result["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(output / "result.json", result)
        print(json.dumps({"attempt": ATTEMPT, "status": result["status"],
                          "A1": False, "A2": False, "A4": False}), flush=True)
        if changed or readback_errors or fixture_budget_invalid or build_budget_invalid:
            raise RuntimeError("Exact identity/readback or fixture budget unavailable; attempt invalid")


if __name__ == "__main__":
    raise SystemExit(main())
