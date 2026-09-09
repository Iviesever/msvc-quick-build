"""Read-only ETW Create/OperationEnd audit. No Windows calls or third-party packages.

An IRP/FileObject value is a transient kernel identifier, NOT a durable file ID.
Zero reported loss and boundary controls do not prove coverage of every writer.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from typing import Any


class EvidenceError(ValueError):
    pass


def need(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def integer(value: Any, field: str) -> int:
    need(type(value) is int and value >= 0, f"missing/invalid unsigned {field}")
    return value


def load(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def canonical(path: str, capture: dict) -> str:
    need(isinstance(path, str) and bool(path), "missing event path")
    value = path.replace("/", "\\").casefold()
    device = capture["device_root"].casefold()
    if value.startswith(device + "\\"):
        return capture["dos_root"].casefold() + value[len(device):]
    for prefix in ("\\??\\", "\\\\?\\"):
        if value.startswith(prefix):
            value = value[len(prefix):]
    return value


def audit(capture: dict, decode: dict, events: list[dict], case_root: str) -> dict:
    need(type(capture.get("schema")) is int and capture["schema"] == 1 and capture.get("clock") == "QPC", "unknown capture schema/clock")
    integer(capture.get("qpc_frequency"), "qpc_frequency")
    need(capture["qpc_frequency"] > 0, "zero QPC frequency")
    for field in ("stop_status", "events_lost", "log_buffers_lost", "realtime_buffers_lost"):
        need(integer(capture.get(field), field) == 0, f"incomplete capture: {field}")
    for field in ("process_trace_status", "close_trace_status", "decode_errors", "capped_events", "log_header_events_lost"):
        need(integer(decode.get(field), field) == 0, f"incomplete decode: {field}")
    need(0 < integer(decode.get("etl_bytes"), "etl_bytes") < 256 * 1024 * 1024, "missing/capped ETL")
    need(capture.get("failure") is None, "native capture failure")
    need(capture.get("historical_cause_resolved") is False and capture.get("safe_to_transfer_write_lease") is False,
         "trace must not authorize historical cause or writer lease")
    need(integer(decode.get("selected_events"), "selected_events") == len(events), "selected-event count mismatch")
    need(integer(decode.get("total_events"), "total_events") >= len(events), "total-event count mismatch")
    sequences: set[int] = set()
    for event in events:
        sequence = integer(event.get("sequence"), "sequence")
        need(sequence not in sequences, "duplicate event record")
        sequences.add(sequence)
        integer(event.get("qpc"), "qpc")
        need(event.get("decode_error") is None and type(event.get("data")) is dict, "undecoded selected event")
    events = sorted(events, key=lambda e: (e["qpc"], e["sequence"]))
    markers = [capture.get("before"), capture.get("after")]
    need(all(type(m) is dict for m in markers), "missing boundary calibration")
    child = capture.get("child")
    need(type(child) is dict, "child not launched or outcome unavailable")
    for field in ("pid", "created_filetime", "before_launch_qpc", "after_wait_qpc", "exit_code"):
        integer(child.get(field), field)
    need(markers[0]["denied_after_qpc"] < child["before_launch_qpc"] <= child["after_wait_qpc"] < markers[1]["start_qpc"],
         "calibrations do not bracket the child")
    marker_paths = {canonical(m["path"], capture): m for m in markers}
    need(len(marker_paths) == 2, "boundary markers reused")
    root = canonical(case_root, capture).rstrip("\\") + "\\"
    def relevant(event: dict) -> bool:
        path = canonical(event["data"]["FileName"], capture)
        return path in marker_paths or (path.startswith(root) and path.endswith((".pdb", ".pch")))

    # Process and thread lifetime intervals prevent a bare/reused PID from
    # silently acquiring identity. No PID is ever used to authorize termination.
    processes: list[dict] = []
    threads: list[dict] = []
    for event in events:
        if event["provider"] != "process":
            continue
        data = event["data"]; event_id = event["id"]
        if event_id == 1:
            processes.append({"pid": integer(data.get("ProcessID"), "ProcessID"),
                              "created_filetime": integer(data.get("CreateTime"), "CreateTime"),
                              "image": data.get("ImageName"), "start": event["qpc"], "end": None})
        elif event_id == 2:
            candidates = [p for p in processes if p["pid"] == data.get("ProcessID") and p["end"] is None]
            if data.get("CreateTime") is not None:
                candidates = [p for p in candidates if p["created_filetime"] == data["CreateTime"]]
            if len(candidates) == 1:
                candidates[0]["end"] = event["qpc"]
        elif event_id == 3:
            threads.append({"pid": data.get("ProcessID"), "tid": data.get("ThreadID"), "start": event["qpc"], "end": None})
        elif event_id == 4:
            candidates = [t for t in threads if t["pid"] == data.get("ProcessID") and t["tid"] == data.get("ThreadID") and t["end"] is None]
            if len(candidates) == 1:
                candidates[0]["end"] = event["qpc"]
    matches = [p for p in processes if p["pid"] == child["pid"] and p["created_filetime"] == child["created_filetime"]
               and child["before_launch_qpc"] <= p["start"] <= child["after_wait_qpc"]]
    need(len(matches) == 1, "retained child HANDLE identity not matched by process-start event")

    def identity(begin: dict) -> dict | None:
        stamp = begin["qpc"]
        issuing = begin["data"].get("IssuingThreadId", begin["data"].get("ThreadId", begin["tid"]))
        path = canonical(begin["data"]["FileName"], capture)
        if path in marker_paths:
            owner = marker_paths[path]["owner"]
            need(begin["pid"] == owner["pid"] and issuing == owner["tid"], "calibration process/thread mismatch")
            return dict(owner, basis="native retained current-process identity")
        owners = [t["pid"] for t in threads if t["tid"] == issuing and t["start"] <= stamp and (t["end"] is None or stamp <= t["end"])]
        if len(set(owners)) > 1:
            return None
        pid = owners[0] if owners else begin["pid"]
        candidates = [p for p in processes if p["pid"] == pid and p["start"] <= stamp and (p["end"] is None or stamp <= p["end"])]
        if len(candidates) != 1 or not candidates[0]["created_filetime"] or not isinstance(candidates[0]["image"], str):
            return None
        return dict(candidates[0], basis="issuing-thread lifetime" if owners else "event-header process lifetime",
                    header_pid=begin["pid"], issuing_tid=issuing)

    pending: dict[int, dict] = {}
    completed: list[dict] = []
    errors: list[str] = []
    unmatched_ends = 0
    for event in events:
        if event["provider"] != "file":
            continue
        data = event["data"]
        irp = integer(data.get("Irp"), "Irp")
        if event["id"] == 12:
            integer(data.get("FileObject"), "FileObject")
            need(isinstance(data.get("FileName"), str), "Create missing filename")
            if irp in pending and (relevant(pending[irp]) or relevant(event)):
                errors.append("ambiguous outstanding Create IRP")
            pending[irp] = event
        elif event["id"] == 24:
            status = integer(data.get("NtStatus"), "NtStatus")
            need(status <= 0xFFFFFFFF, "invalid NTSTATUS width")
            begin = pending.pop(irp, None)
            if begin is None:
                unmatched_ends += 1  # Other operations also have OpEnd; not invented Create matches.
            elif relevant(begin):
                completed.append({"path": canonical(begin["data"]["FileName"], capture),
                                  "begin_sequence": begin["sequence"], "end_sequence": event["sequence"],
                                  "begin_qpc": begin["qpc"], "end_qpc": event["qpc"],
                                  "irp": irp, "file_object": begin["data"]["FileObject"],
                                  "ntstatus": status, "ntstatus_hex": f"0x{status:08x}",
                                  "create_options": begin["data"].get("CreateOptions"),
                                  "share_access": begin["data"].get("ShareAccess"), "process_identity": identity(begin)})
    for begin in pending.values():
        if relevant(begin):
            errors.append("target Create has no matching OperationEnd")
    need(not errors, "; ".join(errors))
    for marker in markers:
        need(integer(marker.get("win32_error"), "calibration Win32 status") == 32 and
             integer(marker.get("expected_ntstatus"), "calibration NTSTATUS") == 0xC0000043, "wrong controlled failure")
        need(isinstance(marker.get("file_id"), str) and len(marker["file_id"]) == 32 and all(c in "0123456789abcdef" for c in marker["file_id"]),
             "missing held calibration file identity")
        path = canonical(marker["path"], capture)
        failed = [p for p in completed if p["path"] == path and p["ntstatus"] == 0xC0000043 and
                  marker["denied_before_qpc"] <= p["begin_qpc"] <= p["end_qpc"] <= marker["denied_after_qpc"]]
        good = [p for p in completed if p["path"] == path and p["ntstatus"] == 0 and p["end_qpc"] <= marker["denied_before_qpc"]]
        need(len(failed) == 1 and len(good) == 1, "missing/ambiguous positive sharing-failure or successful-open calibration")
    real = [p for p in completed if p["path"].startswith(root)]
    for side in ("a", "b"):
        opens = [p for p in real if p["path"] == root + side + "\\compiler.pdb" and p["process_identity"] is not None
                 and p["process_identity"]["image"].replace("/", "\\").rsplit("\\", 1)[-1].casefold() in ("cl.exe", "mspdbsrv.exe")]
        need(bool(opens), f"no event-time compiler/service process identity for {side.upper()} PDB opens")
    return {"schema": 1, "integrity_checks_passed": True, "calibrated_conflicts": 2, "calibrated_successes": 2,
            "target_open_pairs": completed, "real_open_pairs": len(real),
            "real_failed_opens": [p for p in real if p["ntstatus"] & 0x80000000],
            "unattributed_real_opens": sum(p["process_identity"] is None for p in real),
            "unmatched_other_operation_ends": unmatched_ends,
            "historical_cause_resolved": False, "all_writer_coverage_proven": False, "safe_to_transfer_write_lease": False}


def self_test() -> dict:
    capture = {"schema": 1, "clock": "QPC", "qpc_frequency": 10_000_000, "stop_status": 0, "events_lost": 0,
               "log_buffers_lost": 0, "realtime_buffers_lost": 0, "failure": None, "historical_cause_resolved": False,
               "safe_to_transfer_write_lease": False, "dos_root": "C:", "device_root": "\\Device\\Volume1",
               "child": {"pid": 10, "created_filetime": 1000, "before_launch_qpc": 40, "after_wait_qpc": 90, "exit_code": 0}}
    for name, start in (("before", 10), ("after", 100)):
        capture[name] = {"path": f"C:\\trace\\{name}.pdb", "owner": {"pid": 1, "tid": 2, "created_filetime": 1},
                         "start_qpc": start, "denied_before_qpc": start + 5, "denied_after_qpc": start + 9,
                         "win32_error": 32, "expected_ntstatus": 0xC0000043, "volume": 1, "file_id": "a" * 32}
    events = []
    def event(provider, event_id, time, data, pid=1, tid=2):
        events.append(dict(sequence=len(events) + 1, provider=provider, id=event_id, version=1, pid=pid, tid=tid,
                           qpc=time, decode_error=None, data=data, raw_payload=""))
    def pair(path, time, status, irp, pid=1, tid=2):
        event("file", 12, time, dict(Irp=irp, FileObject=irp + 100, FileName=path, IssuingThreadId=tid), pid, tid)
        event("file", 24, time + 1, dict(Irp=irp, NtStatus=status), pid, tid)
    for name, start in (("before", 10), ("after", 100)):
        pair(capture[name]["path"], start + 1, 0, start)
        pair(capture[name]["path"], start + 6, 0xC0000043, start)  # Legitimate completed IRP reuse.
    event("process", 1, 45, dict(ProcessID=10, CreateTime=1000, ImageName="probe.exe"))
    event("process", 1, 50, dict(ProcessID=20, CreateTime=2000, ImageName="C:\\tools\\cl.exe"))
    event("process", 3, 51, dict(ProcessID=20, ThreadID=21))
    pair("C:\\case\\A\\compiler.pdb", 60, 0, 60, 20, 21)
    pair("C:\\case\\B\\compiler.pdb", 70, 0, 70, 20, 21)
    decode = dict(process_trace_status=0, close_trace_status=0, decode_errors=0, capped_events=0,
                  log_header_events_lost=0, etl_bytes=1024, selected_events=len(events), total_events=len(events))
    mutations = [
        ("intact-controlled-native-status-shapes", True, lambda c, d, e: None),
        ("event-loss", False, lambda c, d, e: c.update(events_lost=1)),
        ("stop-error", False, lambda c, d, e: c.update(stop_status=5)),
        ("decode-error", False, lambda c, d, e: d.update(decode_errors=1)),
        ("capped-file", False, lambda c, d, e: d.update(etl_bytes=256 * 1024 * 1024)),
        ("missing-status", False, lambda c, d, e: e[3]["data"].pop("NtStatus")),
        ("fabricated-zero-status", False, lambda c, d, e: e[3]["data"].update(NtStatus=0)),
        ("string-status", False, lambda c, d, e: e[3]["data"].update(NtStatus="3221225539")),
        ("wrong-IRP", False, lambda c, d, e: e[3]["data"].update(Irp=999)),
        ("wrong-marker-process", False, lambda c, d, e: e[2].update(pid=99)),
        ("wrong-child-creation", False, lambda c, d, e: c["child"].update(created_filetime=42)),
        ("no-process-identity", False, lambda c, d, e: e[9]["data"].update(CreateTime=0)),
        ("duplicate-event", False, lambda c, d, e: e[1].update(sequence=e[0]["sequence"])),
        ("missing-end-marker", False, lambda c, d, e: c.update(after=None)),
        ("safety-authorized", False, lambda c, d, e: c.update(safe_to_transfer_write_lease=True)),
        ("original-child-failure-is-not-collection-failure", True, lambda c, d, e: c["child"].update(exit_code=1)),
    ]
    rows = []
    for name, expected, change in mutations:
        c, d, e = copy.deepcopy((capture, decode, events)); change(c, d, e)
        failure = None
        try:
            audit(c, d, e, "C:\\case")
        except (EvidenceError, KeyError, TypeError) as exc:
            failure = str(exc)
        rows.append(dict(name=name, expected_accepted=expected, accepted=failure is None,
                         passed=(failure is None) == expected, error=failure))
    return {"synthetic_only": True, "cases": rows, "passed": all(r["passed"] for r in rows)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--case-root")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    need(not args.output.exists(), "refusing to overwrite prior audit")
    try:
        if args.self_test:
            result = self_test(); accepted = result["passed"]
        else:
            need(args.trace is not None and args.case_root is not None, "trace and case-root required")
            capture, decode = load(args.trace / "capture.json"), load(args.trace / "decode.json")
            need((args.trace / "events.etl").stat().st_size == decode["etl_bytes"], "ETL size mismatch")
            with (args.trace / "events.jsonl").open(encoding="utf-8-sig") as source:
                events = [json.loads(line) for line in source]
            result = audit(capture, decode, events, args.case_root); accepted = True
    except (EvidenceError, KeyError, TypeError, ValueError, OSError) as exc:
        result = {"integrity_checks_passed": False, "error": str(exc), "historical_cause_resolved": False}; accepted = False
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"accepted": accepted, "output": str(args.output)}, ensure_ascii=False))
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
