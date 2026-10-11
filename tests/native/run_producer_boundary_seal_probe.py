"""One-shot, non-product Windows seal-prerequisite capture (attempt 001).

This deliberately cannot issue a producer receipt. Source review and the exact
commit registration must precede activating its branch-only workflow.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys


BRANCH = "refs/heads/feat/producer-clean-handoff-20261011"
PROBE = "tests/native/producer_boundary_seal_probe.cpp"
RUNNER = "tests/native/run_producer_boundary_seal_probe.py"
WORKFLOW = ".github/workflows/producer-boundary-seal-probe.yml"
PLAN = "docs/20261011-013406-producer-handoff/native_probe_plan.md"
JOB_RUNNER = "tests/native/producer_probe_job.py"


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def write_complete(path: Path, data: bytes) -> None:
    # Windows captures are staging material. Canonical receipt sealing performs
    # directory fsync and independent full readback after artifact transport.
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def write_json(path: Path, value: dict) -> None:
    write_complete(path, (json.dumps(value, ensure_ascii=True, indent=2,
                                    allow_nan=False) + "\n").encode("utf-8"))


def require_local_ntfs(existing: Path) -> dict:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetVolumePathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR,
                                         wintypes.DWORD]
    kernel.GetVolumePathNameW.restype = wintypes.BOOL
    kernel.GetDriveTypeW.argtypes = [wintypes.LPCWSTR]
    kernel.GetDriveTypeW.restype = wintypes.UINT
    kernel.GetVolumeInformationW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR,
        wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
        ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
        wintypes.LPWSTR, wintypes.DWORD]
    kernel.GetVolumeInformationW.restype = wintypes.BOOL
    root = ctypes.create_unicode_buffer(32768)
    if not kernel.GetVolumePathNameW(str(existing), root, len(root)):
        raise ctypes.WinError(ctypes.get_last_error())
    drive_type = kernel.GetDriveTypeW(root.value)
    filesystem = ctypes.create_unicode_buffer(128)
    serial, maximum, flags = wintypes.DWORD(), wintypes.DWORD(), wintypes.DWORD()
    if not kernel.GetVolumeInformationW(root.value, None, 0,
            ctypes.byref(serial), ctypes.byref(maximum), ctypes.byref(flags),
            filesystem, len(filesystem)):
        raise ctypes.WinError(ctypes.get_last_error())
    if drive_type != 3 or filesystem.value.upper() != "NTFS":
        raise RuntimeError("Only a local fixed NTFS volume is admitted")
    return {"root": root.value, "type": drive_type,
            "filesystem": filesystem.value, "serial": serial.value}


def selected_tools() -> tuple[Path, dict, list[dict]]:
    # Deterministic fixture-tool selection, no VS discovery/bootstrap program.
    install = Path(os.environ["ProgramFiles"]) / "Microsoft Visual Studio/2022/Enterprise"
    version_file = install / "VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt"
    version = version_file.read_text(encoding="utf-8-sig").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise RuntimeError("Unexpected existing VS default tools version")
    vc = install / "VC/Tools/MSVC" / version
    binary = vc / "bin/Hostx64/x64"
    kits = Path(os.environ["ProgramFiles(x86)"]) / "Windows Kits/10"
    sdk_candidates = []
    for candidate in (kits / "Include").iterdir():
        if not re.fullmatch(r"10\.0\.\d+\.0", candidate.name):
            continue
        includes = [candidate / part for part in ("ucrt", "shared", "um", "winrt")]
        libraries = [kits / "Lib" / candidate.name / part / "x64"
                     for part in ("ucrt", "um")]
        if all(path.is_dir() for path in includes + libraries):
            sdk_candidates.append((tuple(map(int, candidate.name.split("."))),
                                   candidate.name, includes, libraries))
    if not sdk_candidates:
        raise RuntimeError("No complete installed x64 Windows 10 SDK")
    _, sdk_version, sdk_includes, sdk_libraries = max(sdk_candidates)
    includes = [vc / "include", *sdk_includes]
    libraries = [vc / "lib/x64", *sdk_libraries]
    tool_paths = [binary / name for name in
                  ("cl.exe", "link.exe", "c1xx.dll", "c2.dll", "mspdbcore.dll")]
    library_names = ["libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "oldnames.lib",
                     "libucrt.lib", "kernel32.lib", "advapi32.lib"]
    for name in library_names:
        candidates = [directory / name for directory in libraries
                      if (directory / name).is_file()]
        if len(candidates) != 1:
            raise RuntimeError(f"Ambiguous or missing selected runtime library: {name}")
        tool_paths.append(candidates[0])
    inherited = os.environ.copy()
    for key in ("CL", "_CL_", "LINK", "_LINK_", "INCLUDE", "LIB", "LIBPATH"):
        if inherited.get(key):
            raise RuntimeError(f"Unexpected ambient fixture-tool variable: {key}")
    inherited["INCLUDE"] = os.pathsep.join(map(str, includes))
    inherited["LIB"] = os.pathsep.join(map(str, libraries))
    inherited["PATH"] = str(binary) + os.pathsep + inherited.get("PATH", "")
    selected = {"vc_version": version, "sdk_version": sdk_version,
                "include": list(map(str, includes)), "lib": list(map(str, libraries)),
                "default_version_file": identity(version_file)}
    return binary / "cl.exe", {"environment": inherited, "selection": selected}, [
        identity(path) for path in [version_file, *tool_paths]]


def capture_once(argv: list[str], cwd: Path, env: dict, timeout: int,
                 stem: str, output: Path) -> dict:
    from producer_probe_job import run_captured_in_job

    row = run_captured_in_job(argv, cwd, env, timeout,
                             output / f"{stem}.stdout.bin",
                             output / f"{stem}.stderr.bin")
    row["argv"] = argv
    for stream in ("stdout", "stderr"):
        try:
            row[stream] = identity(output / f"{stem}.{stream}.bin")
        except Exception as error:
            row[stream + "_readback_error"] = f"{type(error).__name__}: {error}"
    write_json(output / f"{stem}.result.json", row)
    print(f"{stem}: exit={row.get('exit_code')}, timed_out={row.get('timed_out')}", flush=True)
    for stream in ("stdout", "stderr"):
        try:
            print((output / f"{stem}.{stream}.bin").read_bytes().decode(
                "utf-8", errors="backslashreplace"), flush=True)
        except Exception as error:
            print(f"{stem} {stream} display unavailable: {type(error).__name__}", flush=True)
    return row


def main() -> int:
    sys.dont_write_bytecode = True
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-probe-sha256", required=True)
    parser.add_argument("--expected-runner-sha256", required=True)
    parser.add_argument("--expected-job-runner-sha256", required=True)
    args = parser.parse_args()
    expected = {"GITHUB_REPOSITORY": "Iviesever/msvc-quick-build",
                "GITHUB_REF": BRANCH, "GITHUB_EVENT_NAME": "push",
                "GITHUB_RUN_NUMBER": "1", "GITHUB_RUN_ATTEMPT": "1"}
    if os.name != "nt" or any(os.environ.get(k) != v for k, v in expected.items()):
        raise RuntimeError("Attempt 001 workflow admission mismatch")
    if not re.fullmatch(r"[1-9][0-9]*", os.environ.get("GITHUB_RUN_ID", "")):
        raise RuntimeError("Missing actual workflow run identity")
    if not os.environ.get("ImageVersion") or os.environ.get("ImageOS") != "win22":
        raise RuntimeError("Missing expected Windows 2022 runner image identity")
    repo = Path(__file__).resolve().parents[2]
    volume = require_local_ntfs(repo)
    actual_head = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                          cwd=repo, timeout=5).decode("ascii").strip()
    if actual_head != os.environ.get("GITHUB_SHA") or not re.fullmatch(r"[a-f0-9]{40}", actual_head):
        raise RuntimeError("Checkout is not the exact activating commit")
    for path, digest in ((PROBE, args.expected_probe_sha256),
                         (RUNNER, args.expected_runner_sha256),
                         (JOB_RUNNER, args.expected_job_runner_sha256)):
        if not re.fullmatch(r"[a-f0-9]{64}", digest) or identity(repo / path)["sha256"] != digest:
            raise RuntimeError(f"Reviewed source hash mismatch: {path}")
    output = repo / f"producer-seal-001-{os.environ['GITHUB_RUN_ID']}-1"
    output.mkdir(exist_ok=False)
    result = {"schema": 1, "attempt": "producer-seal-001", "source_head": actual_head,
              "started_utc": datetime.now(timezone.utc).isoformat(),
              "status": "INCONCLUSIVE", "fixture_builds": 0, "fixture_invocations": 0,
              "A1": False, "A2": False, "A4": False, "A3": False, "A5": False,
              "deletion_enabled": False, "ordinary_msvc_qualified": False,
              "canonical_evidence_sealed": False}
    source_rows: list[dict] = []
    tool_rows: list[dict] = []
    try:
        compiler, selected, tool_rows = selected_tools()
        source_rows = [identity(repo / path) for path in (PROBE, RUNNER, JOB_RUNNER, WORKFLOW, PLAN)]
        build = output / "build"
        build.mkdir()
        executable = build / "producer_boundary_seal_probe.exe"
        argv = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MT", "/Z7",
                "/Od", "/W4", "/WX", "/permissive-", str(repo / PROBE),
                "/Fo" + str(build / "producer_boundary_seal_probe.obj"),
                "/Fe" + str(executable), "/link", "/INCREMENTAL:NO"]
        write_json(output / "preflight.json", {
            "schema": 1, "source_head": actual_head, "sources": source_rows,
            "captured_utc_before_compiler": datetime.now(timezone.utc).isoformat(),
            "tools": tool_rows, "selection": selected["selection"], "build_argv": argv,
            "volume": volume, "os": platform.platform(),
            "windows_version": list(sys.getwindowsversion()),
            "image_os": os.environ.get("ImageOS"), "image_version": os.environ.get("ImageVersion"),
            "run": os.environ["GITHUB_RUN_ID"], "run_number": 1, "run_attempt": 1,
            "fixture_wall_seconds": 30, "fixture_processes": 1, "helper_processes": 0,
            "ordinary_mqb_calls": 0, "cost_calls": 0, "etw_calls": 0})
        result["fixture_builds"] = 1
        compiled = capture_once(argv, repo, selected["environment"], 90, "compile", output)
        if (compiled.get("timed_out") or compiled.get("exit_code") != 0
                or compiled.get("job_members_completed") is not True or compiled.get("errors")
                or compiled.get("job_total_processes") not in (1, 2)):
            result["status"] = "BUILD_FAILED_OR_TIMED_OUT"
            return 1
        result["fixture_binary"] = identity(executable)
        write_json(output / "fixture-identity.json", result["fixture_binary"])
        result["fixture_invocations"] = 1
        native = capture_once([str(executable), str(output / "fixture")], repo,
                              selected["environment"], 40, "native", output)
        result["native_exit"] = native["exit_code"]
        if (native.get("timed_out") or native.get("job_members_completed") is not True
                or native.get("job_total_processes") != 1 or native.get("errors")):
            result["status"] = "NATIVE_EXECUTION_INCOMPLETE"
            return 1
        lines = (output / "native.stdout.bin").read_bytes().decode("utf-8").splitlines()
        rows = [json.loads(line) for line in lines if line.strip()]
        summaries = [row for row in rows if row.get("kind") == "summary"]
        if len(summaries) != 1:
            raise RuntimeError("No single complete native summary; raw streams retained")
        summary = summaries[0]
        classification = summary.get("classification")
        codes = {"NECESSARY_CHECKS_ONLY": 0, "CANDIDATE_DISQUALIFIED": 2, "INCONCLUSIVE": 3}
        if classification not in codes or native["exit_code"] != codes[classification]:
            raise RuntimeError("Native classification/exit mismatch")
        if any(summary.get(key) is not False for key in ("A1", "A2", "A4")):
            raise RuntimeError("Probe attempted to assert product authority")
        result["status"] = classification
        result["native_summary"] = summary
        return 0 if classification == "NECESSARY_CHECKS_ONLY" else 1
    except Exception as error:
        result["capture_error"] = f"{type(error).__name__}: {error}"
        return 1
    finally:
        changed = []
        readback_errors = []
        for row in source_rows + tool_rows:
            try:
                if identity(Path(row["path"])) != row:
                    changed.append(row["path"])
            except Exception as error:
                readback_errors.append({"path": row["path"],
                    "error": f"{type(error).__name__}: {error}"})
        result["source_or_tool_identity_changes"] = changed
        if changed:
            result["status"] = "IDENTITY_CHANGED_INVALID"
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
        result["readback_errors"] = readback_errors
        if readback_errors:
            result["status_before_readback_error"] = result["status"]
            result["status"] = "READBACK_INCOMPLETE_INVALID"
        result["retained_files_before_result"] = retained
        result["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(output / "result.json", result)
        print(json.dumps({"attempt": "producer-seal-001", "status": result["status"],
                          "A1": False, "A2": False, "A4": False}), flush=True)
        if changed or readback_errors:
            raise RuntimeError("Exact identity/readback unavailable; attempt invalid")


if __name__ == "__main__":
    raise SystemExit(main())
