#!/usr/bin/env python3
"""Exact-binary reporting contract and paired ABBA evidence (Windows/MSVC).

All cases use the SAME fixture pathname for A/B and an external perf_counter
measurement. Pipe capture is not an interactive terminal; inherited CI handles
are labelled separately. Raw output, adverse samples and unavailable metrics
are retained. This script never changes MQB freshness policy or sanitizes argv.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from typing import Any


OUTPUT_COUNTERS = {"output_lines_emitted", "output_bytes_emitted"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def split_timings(data: bytes) -> tuple[bytes, list[dict[str, Any]]]:
    human: list[bytes] = []
    records: list[dict[str, Any]] = []
    for line in data.splitlines(keepends=True):
        if line.startswith(b'{"type":"mqb.timings"'):
            records.append(json.loads(line))
        else:
            human.append(line)
    return b"".join(human), records


def normalized(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


@dataclass
class Fixture:
    name: str
    root: Path
    arguments: list[str]
    units: int
    pch: bool = False
    static: bool = False

    @property
    def output(self) -> Path:
        return self.root / ".mqb" / "bin" / ("report.lib" if self.static else "report.exe")


def fixture(parent: Path, name: str, units: int, *, static: bool = False,
            modules: bool = False, pch: bool = False) -> Fixture:
    root = parent / name
    root.mkdir()
    (root / "common.hpp").write_text(
        "#pragma once\ninline int common_value() { return 42; }\n", encoding="utf-8")
    if modules:
        (root / "math.ixx").write_text(
            "export module report_math;\nexport int answer() { return 42; }\n", encoding="utf-8")
        (root / "main.cpp").write_text(
            "import report_math;\nint main() { return answer() == 42 ? 0 : 1; }\n", encoding="utf-8")
        sources = ["math.ixx", "main.cpp"]
    else:
        (root / "main.cpp").write_text(
            '#include "common.hpp"\nint main() { return common_value() == 42 ? 0 : 1; }\n',
            encoding="utf-8")
        sources = ["main.cpp"]
        for index in range(units - 1):
            source = f"unit_{index:03d}.cpp"
            (root / source).write_text(
                f'#include "common.hpp"\nint value_{index}() {{ return common_value() + {index}; }}\n',
                encoding="utf-8")
            sources.append(source)
    arguments = ["build", *sources, "--no-discover", "--env", "vs",
                 "--release", "--std", "latest", "-o", "report"]
    if static:
        arguments += ["--type", "static"]
    if pch:
        arguments += ["--pch", "common.hpp"]
    return Fixture(name, root, arguments, units, pch, static)


class Recorder:
    def __init__(self, root: Path):
        self.root = root
        (root / "raw").mkdir(parents=True)
        self.calls: list[dict[str, Any]] = []

    def run(self, executable: Path, case: Fixture, label: str, *, verbose: bool = False,
            timings: bool = False, inherited: bool = False, jobs: str = "auto",
            extra: tuple[str, ...] = (), success: bool = True) -> dict[str, Any]:
        argv = [str(executable), case.arguments[0], *(["--verbose"] if verbose else []), *case.arguments[1:],
                "-j", jobs, *(["--timings=json"] if timings else []), *extra]
        # Redirect both pipes and communicate concurrently to avoid deadlock on
        # large diagnostics. Timeout failure keeps available raw output and kills
        # the child; it is never silently removed from the evidence.
        started = time.perf_counter_ns()
        try:
            completed = subprocess.run(argv, cwd=case.root, timeout=120, check=False,
                                       stdout=None if inherited else subprocess.PIPE,
                                       stderr=None if inherited else subprocess.PIPE)
        except subprocess.TimeoutExpired as error:
            self.save_failure(label, error.stdout or b"", error.stderr or b"")
            raise RuntimeError(f"{label}: MQB timed out") from error
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
        output = completed.stdout or b""
        errors = completed.stderr or b""
        key = f"raw/{len(self.calls):04d}-{label}"
        (self.root / (key + ".stdout")).write_bytes(output)
        (self.root / (key + ".stderr")).write_bytes(errors)
        human_out, out_records = split_timings(output)
        human_err, err_records = split_timings(errors)
        records = out_records + err_records
        row = {"label": label, "argv": argv, "cwd": str(case.root),
               "external_ms": elapsed_ms, "exit_code": completed.returncode,
               "sink": "inherited-ci-handles" if inherited else "captured-pipes",
               "verbose": verbose, "timings_enabled": timings,
               "stdout_file": None if inherited else key + ".stdout",
               "stderr_file": None if inherited else key + ".stderr",
               "stdout_sha256": None if inherited else digest(output),
               "stderr_sha256": None if inherited else digest(errors),
               "human_stdout_sha256": None if inherited else digest(normalized(human_out)),
               "human_stderr_sha256": None if inherited else digest(normalized(human_err)),
               "timing": records[0] if len(records) == 1 else None}
        self.calls.append(row)
        # A partial evidence file is also useful when a contract fails later.
        self.checkpoint()
        if success:
            require(completed.returncode == 0,
                    f"{label}: exit {completed.returncode}\n{errors.decode('utf-8', 'replace')}\n"
                    f"{output.decode('utf-8', 'replace')}")
        if not inherited:
            require(len(records) == int(timings), f"{label}: unexpected timing record count")
        return row

    def save_failure(self, label: str, out: bytes, err: bytes) -> None:
        (self.root / f"raw/timeout-{label}.stdout").write_bytes(out)
        (self.root / f"raw/timeout-{label}.stderr").write_bytes(err)

    def checkpoint(self) -> None:
        (self.root / "calls.json").write_text(json.dumps(self.calls, indent=2), encoding="utf-8")

    def human(self, row: dict[str, Any], stream: str) -> bytes:
        path = row[f"{stream}_file"]
        require(path is not None, "inherited handles have no captured transcript")
        return normalized(split_timings((self.root / path).read_bytes())[0])


def state(case: Fixture) -> list[tuple[str, int, int]]:
    return sorted((str(path.relative_to(case.root)), info.st_size, info.st_mtime_ns)
                  for path in (case.root / ".mqb").rglob("*") if path.is_file()
                  for info in [path.stat()])


def assert_warm(row: dict[str, Any], case: Fixture, *, candidate: bool) -> None:
    timing = row["timing"]
    require(timing is not None, f"{row['label']}: missing warm evidence")
    require(timing["cache"]["compile"] == {"hits": case.units + int(case.pch), "misses": 0},
            f"{row['label']}: not a full compile-cache hit: {timing['cache']}")
    stage = "archive" if case.static else "link"
    require(timing["cache"][stage] == {"hits": 1, "misses": 0}, f"{row['label']}: not a {stage} hit")
    counters = timing["counters"]
    require(all(counters[key] == 0 for key in ["cl_processes_launched", "link_processes_launched",
                                               "lib_processes_launched", "cache_files_written"]),
            f"{row['label']}: warm invocation executed tools or wrote caches")
    if candidate:
        require("target_reporting" in timing["attribution"]["work"], "missing inclusive report field")
        require("reporting" in timing["attribution"]["wall"], "legacy IO field disappeared")


def semantic_counters(row: dict[str, Any]) -> Any:
    timing = row["timing"]
    if timing is None:
        return None  # Unavailable is not a measurement of zero.
    return {"cache": timing["cache"], "counter_breakdown": timing["counter_breakdown"],
            "counters": {key: value for key, value in timing["counters"].items()
                         if key not in OUTPUT_COUNTERS}}


def summarize(pairs: list[dict[str, Any]]) -> dict[str, Any]:
    deltas = [pair["candidate"]["external_ms"] - pair["baseline"]["external_ms"] for pair in pairs]
    percentages = [100 * delta / pair["baseline"]["external_ms"] for delta, pair in zip(deltas, pairs)]
    median = statistics.median(deltas)
    return {"pairs": len(pairs), "paired_median_delta_ms": median,
            "paired_median_delta_pct": statistics.median(percentages),
            "mad_delta_ms": statistics.median(abs(value - median) for value in deltas),
            "p95_delta_ms": sorted(deltas)[math.ceil(0.95 * len(deltas)) - 1],
            "faster_pairs": sum(value < 0 for value in deltas), "paired_deltas_ms": deltas}


def default_contract(recorder: Recorder, candidate: Path, case: Fixture) -> None:
    row = recorder.run(candidate, case, f"contract-{case.name}-default", timings=True)
    assert_warm(row, case, candidate=True)
    human = recorder.human(row, "stdout")
    suffix = "translation unit" if case.units == 1 else "translation units"
    summary = f"[up-to-date] {case.units} {suffix}\n".encode()
    require(summary in human, f"{case.name}: missing correct default summary")
    require(len(human.splitlines()) == 3 + int(case.pch), f"{case.name}: unexpected default report lines")
    require(row["timing"]["counters"]["output_lines_emitted"] == 3 + int(case.pch),
            f"{case.name}: timing record incorrectly included in output counter")
    require(b"[compile]" not in human, f"{case.name}: a no-op compiled")
    detailed = recorder.run(candidate, case, f"contract-{case.name}-verbose", verbose=True, timings=True)
    assert_warm(detailed, case, candidate=True)
    require(summary not in recorder.human(detailed, "stdout"), f"{case.name}: verbose lost TU details")


def mutation_contract(recorder: Recorder, candidate: Path, case: Fixture) -> None:
    # Test default mode itself, not merely verbose compatibility. Preserve an
    # actual source-timestamp mutation; do not disable freshness for this test.
    unit = case.root / "unit_000.cpp"
    unit.write_text(unit.read_text(encoding="utf-8") + "\n// changed\n", encoding="utf-8")
    info = unit.stat()
    os.utime(unit, ns=(info.st_atime_ns, info.st_mtime_ns + 2_000_000_000))
    row = recorder.run(candidate, case, "contract-single-tu-mutation", timings=True)
    require(row["timing"]["cache"]["compile"] == {"hits": 1, "misses": 1}, "default mutation lost freshness")
    human = recorder.human(row, "stdout")
    require(b"[compile] unit_000.cpp [" in human and b"[up-to-date] 1 translation unit\n" in human,
            "default mixed build hid the rebuilt TU/reasons or miscounted reuse")
    require(b"[link] report.exe" in human, "default mixed build hid link work")
    run_case = replace(case, arguments=["run", *case.arguments[1:]])
    row = recorder.run(candidate, run_case, "contract-run")
    require(b"[run] report.exe\n" in recorder.human(row, "stdout"), "run status disappeared")
    case.output.unlink()
    row = recorder.run(candidate, case, "contract-output-repair", timings=True)
    require(row["timing"]["cache"]["compile"] == {"hits": 2, "misses": 0}
            and row["timing"]["cache"]["link"]["misses"] == 1, "missing output was not repaired")
    original = unit.read_bytes()
    unit.write_text("#error reporting_contract_failure\n", encoding="utf-8")
    row = recorder.run(candidate, case, "contract-compiler-failure", success=False)
    require(row["exit_code"] == 4, "compiler failure exit code changed")
    require(b"reporting_contract_failure" in recorder.human(row, "stdout") + recorder.human(row, "stderr"),
            "default mode suppressed compiler error output")
    unit.write_bytes(original)
    recorder.run(candidate, case, "contract-compiler-repair")


def self_test() -> None:
    record = b'{"type":"mqb.timings","phases":{}}\r\n'
    require(split_timings(b"warning\r\n" + record)[0] == b"warning\r\n", "parser removed diagnostics")
    require(len(split_timings(record)[1]) == 1, "parser lost timing record")
    pairs = [{"baseline": {"external_ms": 10}, "candidate": {"external_ms": value}}
             for value in [11, 8, 12, 7]]
    result = summarize(pairs)
    require(result["paired_median_delta_ms"] == -0.5 and result["mad_delta_ms"] == 2
            and result["p95_delta_ms"] == 2 and result["faster_pairs"] == 2, "paired statistics incorrect")
    require(semantic_counters({"timing": None}) is None, "unavailable counter became zero")
    print("Reporting evidence parser/statistics self-test passed.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--base-sha")
    parser.add_argument("--head-sha")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pairs", type=int, default=12, choices=range(4, 41))
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return
    require(os.name == "nt", "Product evidence requires Windows/MSVC; self-tests alone are not a product gate")
    require(all([args.baseline, args.candidate, args.output, args.base_sha, args.head_sha]), "missing evidence identity/paths")
    baseline, candidate, output = args.baseline.resolve(), args.candidate.resolve(), args.output.resolve()
    require(baseline.is_file() and candidate.is_file(), "exact binaries missing")
    require(not output.exists(), "use a fresh output directory; previous evidence must not be overwritten")
    recorder = Recorder(output)
    metadata = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
                "base_sha": args.base_sha, "head_sha": args.head_sha,
                "baseline_binary_sha256": digest(baseline.read_bytes()),
                "candidate_binary_sha256": digest(candidate.read_bytes()),
                "os": platform.platform(), "python": sys.version,
                "runner_image": os.environ.get("ImageOS"), "runner_image_version": os.environ.get("ImageVersion"),
                "measurement": "external perf_counter_ns from subprocess start through completion/drain",
                "terminal_limit": "Captured pipes and inherited CI handles; NOT an interactive-terminal measurement",
                "missing_field_policy": "null/unavailable, never fabricated zero; target_reporting absent in base",
                "pairing": "alternating AB/BA, common fixture and cache pathname, no discarded samples"}
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    all_pairs: list[dict[str, Any]] = []
    summaries = []
    with tempfile.TemporaryDirectory(prefix="mqb-reporting-") as temporary:
        root = Path(temporary)
        cases = [fixture(root, "small", 2), fixture(root, "scale", 129),
                 fixture(root, "static", 129, static=True), fixture(root, "modules", 2, modules=True),
                 fixture(root, "pch", 2, pch=True)]
        for case in cases:
            recorder.run(baseline, case, f"prime-{case.name}-base")
            recorder.run(candidate, case, f"prime-{case.name}-candidate")
            default_contract(recorder, candidate, case)
            before = state(case)
            # Each tuple defines a separate scenario. Do not pool verbosity,
            # instrumentation, job policy or sink modes into a single median.
            modes = [(verbose, timings, False, "auto") for verbose in (False, True) for timings in (False, True)]
            if case.name in ("small", "scale"):
                modes += [(verbose, False, True, "auto") for verbose in (False, True)]
            if case.name == "scale":
                modes += [(verbose, False, False, "1") for verbose in (False, True)]
            for verbose, timings, inherited, jobs in modes:
                scenario = f"{case.name}-{'verbose' if verbose else 'default'}-{'timed' if timings else 'off'}-{'inherited' if inherited else 'pipe'}-j{jobs}"
                print(f"Reporting ABBA: {scenario}", flush=True)
                pairs = []
                for number in range(1, args.pairs + 1):
                    order = ("baseline", "candidate") if number % 2 else ("candidate", "baseline")
                    pair: dict[str, Any] = {"scenario": scenario, "pair": number, "orientation": "AB" if number % 2 else "BA"}
                    for side in order:
                        row = recorder.run(baseline if side == "baseline" else candidate, case,
                                           f"{scenario}-{number}-{side}", verbose=verbose, timings=timings,
                                           inherited=inherited, jobs=jobs)
                        if timings:
                            assert_warm(row, case, candidate=side == "candidate")
                        pair[side] = row
                    if timings:
                        require(semantic_counters(pair["baseline"]) == semantic_counters(pair["candidate"]),
                                f"{scenario}: non-output counter or freshness work counts changed")
                    if not inherited:
                        require(pair["baseline"]["human_stderr_sha256"] == pair["candidate"]["human_stderr_sha256"],
                                f"{scenario}: stderr changed on a no-op")
                        if verbose:
                            require(pair["baseline"]["human_stdout_sha256"] == pair["candidate"]["human_stdout_sha256"],
                                    f"{scenario}: verbose no-op bytes changed")
                    pairs.append(pair)
                    all_pairs.append(pair)
                summaries.append({"scenario": scenario, **summarize(pairs)})
                (output / "comparison.json").write_text(
                    json.dumps({"metadata": metadata, "comparison": summaries, "paired_samples": all_pairs}, indent=2),
                    encoding="utf-8")
            after = state(case)
            require(before == after, f"{case.name}: warm matrix modified project cache/artifact metadata")
            (output / f"{case.name}-warm-state.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
            # Untimed instrumented postcondition also guards the inherited/off runs.
            post = recorder.run(candidate, case, f"post-{case.name}", timings=True)
            assert_warm(post, case, candidate=True)
        mutation_contract(recorder, candidate, cases[0])
    (output / "contract-passed.txt").write_text("Default/verbose, mixed build, repair, run, compiler failure and warm matrix passed.\n", encoding="utf-8")
    print(json.dumps(summaries, indent=2))
    print(f"Reporting evidence: {output}")


if __name__ == "__main__":
    main()
