"""Bind original tool outcomes to calibrated ETW intervals, without inferring RPC ownership.

A failed compiler control can have complete evidence. Those are separate results.
Overlapping B invocations retain multiple candidate children; never choose by order.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import re
import tempfile
from typing import Any
import verify_pdb_file_trace as trace


def signed_code(value: Any) -> int:
    trace.need(type(value) is int and -(1 << 31) <= value < (1 << 31), "invalid signed tool exit")
    return value


def control_ok(observation: dict, drain: dict) -> bool:
    """No recovery field is used to decide original control success."""
    return (all(type(observation.get(f)) is int and observation[f] == 0 for f in
                ("A_exit", "B0_exit", "B1_exit", "B_link_exit", "B_run_exit"))
            and all(observation.get(f) is True for f in ("lifecycle_ok", "drain_control_ok", "server_survived_A",
                    "A_compiler_overlap_observed", "B_compiler_overlap_observed", "A_observed_compilers_signaled"))
            and observation.get("A_pending_compile_dispatched") is False
            and drain.get("stop_observed") is True and drain.get("work_compiles_dispatched") == 1
            and drain.get("first_compile_exit") == 0 and drain.get("pending_compile_exit") == -2)


def bind_span(begin: dict, end: dict, result: dict, capture: dict, events: list[dict],
              executable: str, label: str) -> list[dict]:
    for row in (begin, end):
        trace.need(type(row.get("schema")) is int and row["schema"] == 1, "invalid invocation schema")
    trace.need(begin.get("clock") == "QPC" and begin.get("label") == label, "wrong span clock/label")
    trace.need(trace.canonical(begin["executable"], capture) == trace.canonical(executable, capture), "span executable mismatch")
    trace.need(trace.integer(begin.get("frequency"), "frequency") == capture["qpc_frequency"], "span clock frequency mismatch")
    dos, device = begin.get("executable_dos_root"), begin.get("executable_device_root")
    trace.need(isinstance(dos, str) and re.fullmatch(r"[A-Za-z]:", dos) is not None and
               isinstance(device, str) and device.casefold().startswith("\\device\\") and
               len(device) > 8 and not device.endswith("\\"), "missing/invalid executable device mapping")
    executable_mapping = dict(dos_root=dos, device_root=device)
    image_key = trace.canonical(executable, executable_mapping)
    trace.need(image_key.startswith(dos.casefold() + "\\"), "executable drive differs from captured mapping")
    low = trace.integer(begin.get("before_run_qpc"), "span begin")
    high = trace.integer(end.get("after_run_qpc"), "span end")
    trace.need(type(end.get("before_run_qpc")) is int and end["before_run_qpc"] == low, "span pairing mismatch")
    trace.need(capture["child"]["before_launch_qpc"] <= low <= high <= capture["child"]["after_wait_qpc"], "span outside child lifetime")
    trace.need(end.get("infrastructure_error") is False and end.get("safe_to_transfer_write_lease") is False,
               "infrastructure failure or invalid lease authority")
    trace.need(signed_code(end.get("exit_code")) == signed_code(result.get("exit_code")) and
               result.get("cancelled") is False and "infrastructure_error" not in result, "original outcome/span mismatch")
    owner = trace.integer(begin.get("owner_pid"), "owner pid")
    created = trace.integer(begin.get("owner_created_filetime"), "owner creation")
    starts = [e for e in events if e["provider"] == "process" and e["id"] == 1]
    owners = [e for e in starts if e["data"].get("ProcessID") == owner and e["data"].get("CreateTime") == created
              and e["qpc"] <= low]
    trace.need(created > 0 and len(owners) == 1, "native invocation owner not uniquely matched to ETW creation")
    # A new same-PID lifetime inside this interval must not inherit parent identity.
    trace.need(not any(e["data"].get("ProcessID") == owner and e["data"].get("CreateTime") != created
                       and owners[0]["qpc"] <= e["qpc"] <= high for e in starts), "invocation owner PID reused")
    exits = [e for e in events if e["provider"] == "process" and e["id"] == 2 and
             e["data"].get("ProcessID") == owner and owners[0]["qpc"] <= e["qpc"] < high]
    trace.need(not exits, "invocation outlives observed owner")
    candidates = []
    for event in starts:
        data = event["data"]
        if data.get("ParentProcessID") != owner or not low <= event["qpc"] <= high:
            continue
        image = data.get("ImageName")
        if isinstance(image, str) and trace.canonical(image, executable_mapping) == image_key:
            candidates.append({"pid": trace.integer(data.get("ProcessID"), "child pid"),
                               "created_filetime": trace.integer(data.get("CreateTime"), "child creation"),
                               "start_qpc": event["qpc"], "image": image})
    trace.need(bool(candidates) and all(c["created_filetime"] > 0 for c in candidates), "tool child lifetime absent")
    return candidates


def analyse(root: Path, native_root: str, profile: str, capture: dict,
            events: list[dict], event_audit: dict) -> dict:
    observation = trace.load(root / "observation.json")
    trace.need(observation.get("profile") == profile and profile in ("pch-release", "modules-debug") and
               observation.get("origin") == "A-started" and observation.get("ending") == "drain" and
               observation.get("endpoint_mode") == "default", "wrong original case identity")
    trace.need(observation.get("safe_to_transfer_write_lease") is False and
               observation.get("safe_to_integrate_cancellation") is False, "invalid original safety fields")
    envelope = trace.load(root / "default-envelope.json")
    trace.need(all(envelope.get(f) is True for f in ("cleanup_verified", "endpoint_override_absent", "outer_lifecycle_verified"))
               and type(envelope.get("remaining_servers")) is list and not envelope["remaining_servers"], "cleanup unproven")
    for f in ("A_exit", "B0_exit", "B1_exit", "B_link_exit", "B_run_exit", "recovery_compile_exit"):
        signed_code(observation.get(f))
    expected = ["A/warm", "A/work0", "B/warm", "B/work0", "B/work1", "B/recovery"]
    expected += [f"{side}/{'prefix' if profile == 'pch-release' else 'provider'}" for side in ("A", "B")]
    if observation["B0_exit"] == observation["B1_exit"] == 0:
        expected.append("B/link")
        if observation["B_link_exit"] == 0:
            expected.append("B/run")
        else:
            trace.need(observation["B_run_exit"] == -2, "failed link unexpectedly ran")
    else:
        trace.need(observation["B_link_exit"] == observation["B_run_exit"] == -2, "failed compile unexpectedly linked/ran")
    actual = {p.relative_to(root).as_posix().removesuffix(".argv.txt") for p in root.rglob("*.argv.txt")}
    trace.need(actual == set(expected), "missing or unexpected original tool invocation")
    for suffix in ("invocation-begin.json", "invocation-end.json", "result.json"):
        inventory = {p.relative_to(root).as_posix().removesuffix("." + suffix) for p in root.rglob("*." + suffix)}
        trace.need(inventory == set(expected) | ({"A"} if suffix == "result.json" else set()), "missing/orphan invocation evidence")
    root_result = trace.load(root / "A.result.json")
    trace.need(signed_code(root_result.get("exit_code")) == observation["A_exit"] and root_result.get("cancelled") is False,
               "A root result mismatch")
    trace.need(capture["child"]["exit_code"] in (0, 1), "unexpected fixture exit")
    for suffix in ("stdout.txt", "stderr.txt"):
        (root / f"A.{suffix}").read_bytes()
    snapshots = []
    for phase in ("before-A", "after-A", "after-B"):
        snapshot = trace.load(root / ("pdb-" + phase + ".json"))
        trace.need(snapshot.get("phase") == phase and snapshot.get("safe_to_transfer_write_lease") is False, "wrong snapshot phase/authority")
        pair = snapshot["pdb"]
        for side in ("A", "B"):
            info = pair[side]
            trace.need(type(info.get("open_error")) is int and info["open_error"] == 0 and
                       type(info.get("identity_error")) is int and info["identity_error"] == 0 and
                       isinstance(info.get("volume_serial"), str) and info["volume_serial"].isdigit() and
                       isinstance(info.get("file_id"), str) and re.fullmatch(r"[0-9a-f]{32}", info["file_id"]) is not None,
                       "PDB physical identity unavailable")
            trace.need(trace.canonical(info["path"], capture) == trace.canonical(native_root + "\\" + side + "\\compiler.pdb", capture), "PDB identity path mismatch")
        trace.need(pair.get("same_file") is False and
                   (pair["A"]["volume_serial"], pair["A"]["file_id"]) != (pair["B"]["volume_serial"], pair["B"]["file_id"]), "A/B PDB alias")
        snapshots.append(snapshot)  # Finite metadata observation, NOT event-time identity or a lock.
    fields = {"B/work0": "B0_exit", "B/work1": "B1_exit", "B/link": "B_link_exit", "B/run": "B_run_exit",
              "B/recovery": "recovery_compile_exit"}
    rows = []
    for stem in expected:
        result = trace.load(root / (stem + ".result.json"))
        begin = trace.load(root / (stem + ".invocation-begin.json"))
        end = trace.load(root / (stem + ".invocation-end.json"))
        arguments = (root / (stem + ".argv.txt")).read_text(encoding="utf-8-sig").splitlines()
        trace.need(bool(arguments), "empty original argv")
        children = bind_span(begin, end, result, capture, events, arguments[0], stem.rsplit("/", 1)[1])
        if stem in fields:
            trace.need(result["exit_code"] == observation[fields[stem]], "observation changed original tool result")
        outputs = [(root / (stem + "." + kind + ".txt")).read_text(encoding="utf-8-sig") for kind in ("stdout", "stderr")]
        if Path(stem).name not in ("link", "run"):
            required = ["/FS", "/Zi", "/O2", "/MT"] if profile == "pch-release" else ["/FS", "/Zi", "/Od", "/MTd"]
            trace.need(all(arg in arguments for arg in required), "original compiler flags changed")
            trace.need(("/bigobj" in arguments) == (stem == "A/work0"), "large-A object flag changed")
        target = trace.canonical(native_root.rstrip("\\/") + "\\" + stem.split("/")[0] + "\\compiler.pdb", capture)
        overlapping = [p for p in event_audit["target_open_pairs"] if p["path"] == target and
                       begin["before_run_qpc"] <= p["begin_qpc"] <= p["end_qpc"] <= end["after_run_qpc"]]
        failures = [p for p in overlapping if p["ntstatus"] != 0]
        # Even one direct child does not assign a shared server's RPCs to it.
        rows.append({"stem": stem, "exit_code": result["exit_code"], "phase": "recovery" if stem == "B/recovery" else "original",
                     "diagnostic_codes": sorted(set(re.findall(r"\bC\d{4}\b", "\n".join(outputs)))),
                     "before_run_qpc": begin["before_run_qpc"], "after_run_qpc": end["after_run_qpc"],
                     "owner_pid": begin["owner_pid"], "owner_created_filetime": begin["owner_created_filetime"],
                     "candidate_tool_processes": children, "unique_direct_child": len(children) == 1,
                     "pdb_open_pairs_in_interval": len(overlapping), "nonzero_pdb_events_in_interval": failures,
                     "shared_service_request_attribution_proven": False})
    drain = trace.load(root / "A/drain.json")
    for field in ("work_compiles_dispatched", "first_compile_exit", "pending_compile_exit"):
        signed_code(drain.get(field))
    trace.need(drain.get("safe_to_transfer_write_lease") is False and type(drain.get("stop_observed")) is bool, "invalid drain state")
    first = next(r for r in rows if r["stem"] == "A/work0")
    trace.need(first["exit_code"] == drain["first_compile_exit"], "original A failure replaced by drain outcome")
    passed = control_ok(observation, drain) and all(r["exit_code"] == 0 for r in rows if r["phase"] == "original")
    trace.need(capture["child"]["exit_code"] == (0 if passed else 1), "fixture outcome disagrees with original control")
    return {"schema": 1, "evidence_complete": True, "original_control_ok": passed, "profile": profile,
            "physical_snapshots": snapshots, "invocations": rows, "original_failed_invocations": [r["stem"] for r in rows if r["phase"] == "original" and r["exit_code"] != 0],
            "recovery_failed": any(r["exit_code"] != 0 for r in rows if r["phase"] == "recovery"),
            "all_recorded_real_failed_opens": event_audit["real_failed_opens"],
            "historical_cause_resolved": False, "causal_attribution_proven": False, "safe_to_transfer_write_lease": False}


def self_test() -> dict:
    capture = {"clock": "QPC", "qpc_frequency": 1000, "dos_root": "C:", "device_root": "\\Device\\V",
               "child": {"before_launch_qpc": 1, "after_wait_qpc": 1000}}
    begin = {"schema": 1, "clock": "QPC", "label": "work0", "executable": "C:\\cl.exe", "owner_pid": 10,
             "owner_created_filetime": 20, "frequency": 1000, "before_run_qpc": 100,
             "executable_dos_root": "C:", "executable_device_root": "\\Device\\V"}
    end = {"schema": 1, "before_run_qpc": 100, "after_run_qpc": 200, "infrastructure_error": False, "exit_code": 0,
           "safe_to_transfer_write_lease": False}
    result = {"exit_code": 0, "cancelled": False}
    events = [{"provider": "process", "id": 1, "qpc": 50, "data": {"ProcessID": 10, "CreateTime": 20}},
              {"provider": "process", "id": 1, "qpc": 120, "data": {"ProcessID": 11, "CreateTime": 21, "ParentProcessID": 10, "ImageName": "\\Device\\V\\cl.exe"}}]
    rows = []
    tests = [
        ("single-child", True, lambda b, e, r, v: None),
        ("original-nonzero-preserved", True, lambda b, e, r, v: (e.update(exit_code=2), r.update(exit_code=2))),
        ("two-B-candidates-not-guessed", True, lambda b, e, r, v: v.append(dict(v[1], data=dict(v[1]["data"], ProcessID=12, CreateTime=22)))),
        ("cross-volume-compiler", True, lambda b, e, r, v: (b.update(executable_device_root="\\Device\\ToolVolume"), v[1]["data"].update(ImageName="\\Device\\ToolVolume\\cl.exe"))),
        ("device-map-missing", False, lambda b, e, r, v: b.pop("executable_device_root")),
        ("device-map-wrong", False, lambda b, e, r, v: b.update(executable_device_root="\\Device\\Wrong")),
        ("dos-map-wrong", False, lambda b, e, r, v: b.update(executable_dos_root="Z:")),
        ("wrong-schema", False, lambda b, e, r, v: b.update(schema=True)),
        ("wrong-label", False, lambda b, e, r, v: b.update(label="work1")),
        ("wrong-frequency", False, lambda b, e, r, v: b.update(frequency=999)),
        ("wrong-executable", False, lambda b, e, r, v: b.update(executable="C:\\other.exe")),
        ("wrong-paired-begin", False, lambda b, e, r, v: e.update(before_run_qpc=99)),
        ("reversed-interval", False, lambda b, e, r, v: e.update(after_run_qpc=90)),
        ("outside-outer-span", False, lambda b, e, r, v: e.update(after_run_qpc=1001)),
        ("result-mismatch", False, lambda b, e, r, v: r.update(exit_code=2)),
        ("string-code", False, lambda b, e, r, v: r.update(exit_code="0")),
        ("unexpected-cancellation", False, lambda b, e, r, v: r.update(cancelled=True)),
        ("infrastructure-error", False, lambda b, e, r, v: e.update(infrastructure_error=True)),
        ("lease-authorized", False, lambda b, e, r, v: e.update(safe_to_transfer_write_lease=True)),
        ("owner-creation-mismatch", False, lambda b, e, r, v: b.update(owner_created_filetime=19)),
        ("owner-PID-reused", False, lambda b, e, r, v: v.append(dict(v[0], qpc=130, data=dict(v[0]["data"], CreateTime=99)))),
        ("owner-exited-during-run", False, lambda b, e, r, v: v.append(dict(v[0], id=2, qpc=130))),
        ("missing-child-start", False, lambda b, e, r, v: v.pop()),
    ]
    for name, expected, change in tests:
        b, e, r, v = copy.deepcopy((begin, end, result, events)); change(b, e, r, v)
        error = None
        try:
            candidates = bind_span(b, e, r, capture, v, "C:\\cl.exe", "work0")
            trace.need(len(candidates) == (2 if name.startswith("two-B") else 1), "candidate ambiguity erased")
        except (ValueError, KeyError, TypeError) as exc:
            error = str(exc)
        rows.append({"name": name, "expected_accepted": expected, "accepted": error is None, "passed": (error is None) == expected, "error": error})
    original = {f: 0 for f in ("A_exit", "B0_exit", "B1_exit", "B_link_exit", "B_run_exit")}
    original.update({f: True for f in ("lifecycle_ok", "drain_control_ok", "server_survived_A", "A_compiler_overlap_observed", "B_compiler_overlap_observed", "A_observed_compilers_signaled")})
    original.update(A_pending_compile_dispatched=False, recovery_compile_exit=0)
    drain = dict(stop_observed=True, work_compiles_dispatched=1, first_compile_exit=0, pending_compile_exit=-2)
    for name, change, expected in [
        ("original-success", {}, True), ("recovery-cannot-hide-A-failure", {"A_exit": 1}, False),
        ("recovery-cannot-hide-B-failure", {"B0_exit": 2}, False),
        ("string-success-not-accepted", {"A_exit": "0"}, False),
        ("recovery-not-an-original-outcome", {"recovery_compile_exit": 2}, True),
    ]:
        actual = control_ok(dict(original, **change), drain)
        rows.append(dict(name=name, expected_accepted=expected, accepted=actual, passed=actual == expected))
    # Full on-disk result/span/diagnostic contracts, not just pure field tests.
    for name, profile, mutation, accepted, original_ok in [
        ("disk-pch-intact", "pch-release", "none", True, True),
        ("disk-module-intact", "modules-debug", "none", True, True),
        ("disk-A-failure-retained", "pch-release", "A-fail", True, False),
        ("disk-B-failure-retained", "modules-debug", "B-fail", True, False),
        ("disk-recovery-separate", "pch-release", "recovery-fail", True, True),
        ("disk-missing-end", "pch-release", "missing-end", False, False),
        ("disk-orphan-begin", "pch-release", "orphan", False, False),
        ("disk-missing-stderr", "pch-release", "missing-diagnostic", False, False),
        ("disk-A-result-masked", "pch-release", "masked", False, False),
        ("disk-argv-exe-mismatch", "pch-release", "argv", False, False),
        ("disk-PDB-alias", "pch-release", "alias", False, False),
        ("disk-PDB-identity-missing", "pch-release", "identity", False, False),
    ]:
        failure = None
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            c = dict(capture, child=dict(capture["child"], exit_code=0, after_wait_qpc=10000))
            o = dict(original, profile=profile, origin="A-started", ending="drain", endpoint_mode="default",
                     safe_to_transfer_write_lease=False, safe_to_integrate_cancellation=False)
            dr = dict(drain, safe_to_transfer_write_lease=False)
            records = ["A/warm", "A/work0", "B/warm", "B/work0", "B/work1", "B/recovery", "B/link", "B/run"]
            records += [f"{side}/{'prefix' if profile == 'pch-release' else 'provider'}" for side in ("A", "B")]
            def store(path, value):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value), encoding="utf-8")
            event_rows = [{"provider": "process", "id": 1, "qpc": 5,
                           "data": {"ProcessID": pid, "CreateTime": pid * 100}} for pid in (10, 20)]
            for i, stem in enumerate(records):
                owner = 10 if stem.startswith("A/") else 20
                code = 2 if ((mutation in ("A-fail", "masked") and stem == "A/work0") or
                             (mutation == "B-fail" and stem == "B/work0") or
                             (mutation == "recovery-fail" and stem == "B/recovery")) else 0
                b = dict(begin, label=stem.split("/")[-1], owner_pid=owner, owner_created_filetime=owner*100,
                         before_run_qpc=100+i*60)
                e = dict(end, before_run_qpc=b["before_run_qpc"], after_run_qpc=b["before_run_qpc"]+40, exit_code=code)
                r = dict(result, exit_code=code)
                for suffix, data in (("invocation-begin.json", b), ("invocation-end.json", e), ("result.json", r)):
                    store(root / (stem + "." + suffix), data)
                flags = ["/FS", "/Zi", "/O2", "/MT"] if profile == "pch-release" else ["/FS", "/Zi", "/Od", "/MTd"]
                if stem == "A/work0": flags.append("/bigobj")
                (root / (stem+".argv.txt")).write_text("C:\\cl.exe\n" + "\n".join(flags), encoding="utf-8")
                for suffix in ("stdout.txt", "stderr.txt"):
                    (root / (stem+"."+suffix)).write_text("synthetic C1041" if code else "", encoding="utf-8")
                event_rows.append({"provider": "process", "id": 1, "qpc": b["before_run_qpc"]+5,
                                   "data": {"ProcessID": 100+i, "ParentProcessID": owner, "CreateTime": 10000+i, "ImageName": "C:\\cl.exe"}})
            if mutation == "A-fail":
                o.update(A_exit=1, lifecycle_ok=False, drain_control_ok=False); dr["first_compile_exit"] = 2; c["child"]["exit_code"] = 1
            if mutation == "B-fail":
                o.update(B0_exit=2, B_link_exit=-2, B_run_exit=-2, drain_control_ok=False); c["child"]["exit_code"] = 1
                for stem in ("B/link", "B/run"):
                    for f in root.glob(stem+".*"): f.unlink()
            if mutation == "recovery-fail": o["recovery_compile_exit"] = 2
            for phase in ("before-A", "after-A", "after-B"):
                pair = {side: dict(path=f"C:\\case\\{side}\\compiler.pdb", open_error=0, identity_error=0,
                    volume_serial="1", file_id=("a" if side == "A" else "b")*32) for side in ("A", "B")}
                pair["same_file"] = False
                if mutation == "alias": pair["B"]["file_id"] = pair["A"]["file_id"]
                if mutation == "identity": pair["A"]["open_error"] = 32
                store(root / ("pdb-"+phase+".json"), dict(phase=phase, pdb=pair, safe_to_transfer_write_lease=False))
            store(root / "observation.json", o); store(root / "A/drain.json", dr)
            store(root / "default-envelope.json", dict(cleanup_verified=True, endpoint_override_absent=True, outer_lifecycle_verified=True, remaining_servers=[]))
            store(root / "A.result.json", dict(exit_code=o["A_exit"], cancelled=False))
            for suffix in ("stdout.txt", "stderr.txt"): (root / ("A."+suffix)).write_text("")
            if mutation == "missing-end": (root / "B/work0.invocation-end.json").unlink()
            if mutation == "orphan": store(root / "B/extra.invocation-begin.json", begin)
            if mutation == "missing-diagnostic": (root / "B/work0.stderr.txt").unlink()
            if mutation == "argv": (root / "B/work0.argv.txt").write_text("C:\\wrong.exe\n")
            try:
                observed = analyse(root, "C:\\case", profile, c, event_rows, dict(target_open_pairs=[], real_failed_opens=[]))
                trace.need(observed["original_control_ok"] == original_ok, "original failure hidden by analysis")
                if mutation == "recovery-fail": trace.need(observed["recovery_failed"], "recovery outcome omitted")
            except (ValueError, KeyError, TypeError, OSError) as exc: failure = str(exc)
        rows.append(dict(name=name, expected_accepted=accepted, accepted=failure is None, passed=(failure is None)==accepted, error=failure))
    return {"synthetic_only": True, "cases": rows, "passed": all(r["passed"] for r in rows)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--trace", type=Path); parser.add_argument("--fixture", type=Path)
    parser.add_argument("--native-root"); parser.add_argument("--profile", choices=("pch-release", "modules-debug"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    trace.need(not args.output.exists(), "refuse to overwrite audit")
    report = {"schema": 1, "evidence_complete": False, "original_control_ok": False,
              "historical_cause_resolved": False, "safe_to_transfer_write_lease": False}
    try:
        if args.self_test:
            report = self_test(); accepted = report["passed"]
        else:
            trace.need(args.trace is not None and args.fixture is not None and args.profile is not None, "missing input paths/profile")
            # Diagnostic inventory survives a trace failure; never hide an original C1041 behind a tracing error.
            report["diagnostic_inventory"] = [{"path": p.relative_to(args.fixture).as_posix(),
                "bytes": p.stat().st_size, "compiler_codes": sorted(set(re.findall(r"\bC\d{4}\b", p.read_text(encoding="utf-8-sig"))))}
                for p in sorted(args.fixture.rglob("*.txt")) if p.name.endswith(("stdout.txt", "stderr.txt", "error.txt"))]
            capture = trace.load(args.trace / "capture.json"); decode = trace.load(args.trace / "decode.json")
            trace.need(trace.load(args.trace / "readiness.json") == capture["readiness"], "readiness copy mismatch")
            trace.need((args.trace / "events.etl").stat().st_size == decode["etl_bytes"], "ETL size mismatch")
            events = [json.loads(line) for line in (args.trace / "events.jsonl").read_text(encoding="utf-8-sig").splitlines()]
            native = args.native_root or str(args.fixture)
            checked = trace.audit(capture, decode, events, native, require_readiness=True)
            report.update(analyse(args.fixture, native, args.profile, capture, events, checked))
            accepted = report["original_control_ok"] and not report["recovery_failed"]
    except (ValueError, TypeError, KeyError, OSError) as exc:
        report["error"] = str(exc); accepted = False
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"accepted": accepted, "output": str(args.output)}))
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
