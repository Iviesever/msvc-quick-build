"""Calibrate a deliberately blocked cold B PDB; never waive original controls.

This uses the existing ETL decoder/auditor, not another recorder. Native conflict
calibration is not a reproduction of the historical warm/concurrent C1041.
"""
from __future__ import annotations
import argparse
import copy
import json
import re
from pathlib import Path
import verify_pdb_file_trace as trace
import verify_pdb_invocations as invocation


def failure_chain(begin, end, summary, first, recovery, pairs, arm, target):
    trace.need(arm in ("baseline", "conflict"), "unknown expected arm")
    for row in (begin, end, summary):
        trace.need(type(row.get("schema")) is int and row["schema"] == 1 and
                   row.get("safe_to_transfer_write_lease") is False, "invalid calibration schema/authority")
    for row in (begin, summary):
        trace.need(row.get("kind") == "injected-cold-B-PDB-calibration" and row.get("arm") == arm and
                   row.get("historical_cause_resolved") is False, "calibration identity/cause mismatch")
    trace.need(summary.get("original_positive_control") is False and summary.get("original_service_survived") is True,
               "injected calibration relabeled or seed service lost")
    for key, value in (("desired_access", 0xc0010000), ("share_mode", 0), ("creation_disposition", 1), ("open_error", 0)):
        trace.need(type(begin.get(key)) is int and begin[key] == value, "holder native contract changed: " + key)
    for key in ("delete_error", "close_error", "held_bytes_before_release"):
        trace.need(type(end.get(key)) is int and end[key] == 0, "placeholder cleanup failed or bytes changed")
    trace.need(isinstance(begin.get("volume_serial"), str) and begin["volume_serial"].isdigit() and
               isinstance(begin.get("file_id"), str) and re.fullmatch(r"[0-9a-f]{32}", begin["file_id"]) is not None,
               "holder physical file identity missing")
    trace.need(type(end.get("absence_error")) is int and end["absence_error"] == 2, "placeholder not absent after held-object deletion")
    owner = (trace.integer(begin.get("owner_pid"), "holder pid"), trace.integer(begin.get("owner_created_filetime"), "holder creation"))
    trace.need(all(owner) and trace.integer(begin.get("owner_tid"), "holder thread") > 0, "invalid holder identity")
    service = begin.get("service")
    trace.need(type(service) is list and len(service) == 1, "service identity ambiguous")
    service_id = (trace.integer(service[0].get("pid"), "service pid"), trace.integer(service[0].get("created"), "service creation"))
    trace.need(all(service_id) and service_id != owner, "service aliases coordinator")
    timeline = [trace.integer(begin.get(k), k) for k in ("open_before_qpc", "open_after_qpc")]
    timeline += [trace.integer(end.get(k), k) for k in ("delete_before_qpc", "delete_after_qpc", "release_before_qpc", "release_after_qpc", "remove_after_qpc")]
    trace.need(timeline == sorted(timeline) and timeline[0] > 0, "invalid holder lifecycle order")
    for row, field in ((first, "original_B_exit"), (recovery, "recovery_B_exit")):
        trace.need(invocation.signed_code(summary.get(field)) == invocation.signed_code(row.get("exit_code")), "original result replaced")
        trace.need(type(row.get("before_run_qpc")) is int and type(row.get("after_run_qpc")) is int and
                   row["before_run_qpc"] <= row["after_run_qpc"], "invalid compile interval")
    trace.need(recovery["exit_code"] == 0 and end["remove_after_qpc"] < recovery["before_run_qpc"] and
               first["after_run_qpc"] < recovery["before_run_qpc"], "recovery occurred before release/original completion")
    children = first.get("candidate_tool_processes")
    trace.need(type(children) is list and len(children) == 1, "first compiler identity ambiguous")
    child_id = (children[0]["pid"], children[0]["created_filetime"])
    trace.need(child_id not in (owner, service_id) and all(child_id), "compiler aliases holder/service")
    relevant = [p for p in pairs if p["path"] == target and
                first["before_run_qpc"] <= p["begin_qpc"] <= p["end_qpc"] <= first["after_run_qpc"]]
    matched = []
    for p in relevant:
        identity = p.get("process_identity") or {}
        actor = (identity.get("pid"), identity.get("created_filetime"))
        if p["ntstatus"] == 0xc0000043 and actor in (child_id, service_id) and \
                begin["open_after_qpc"] <= p["begin_qpc"] <= p["end_qpc"] <= end["release_before_qpc"]:
            matched.append(p)
    if arm == "conflict":
        trace.need(first["exit_code"] != 0 and bool(set(first["diagnostic_codes"]) & {"C1041", "C1090", "C2471"}),
                   "expected original B PDB compiler failure not observed")
        trace.need(begin["open_after_qpc"] < first["before_run_qpc"] <= first["after_run_qpc"] < end["delete_before_qpc"],
                   "placeholder did not span original failed compile")
        trace.need(bool(matched), "no compiler/service sharing failure during original B and held placeholder")
    else:
        trace.need(first["exit_code"] == 0 and end["remove_after_qpc"] < first["before_run_qpc"],
                   "baseline original compile failed or holder was not released")
    return dict(calibration_verified=True, expected_arm=arm, original_B_exit=first["exit_code"],
                recovery_B_exit=recovery["exit_code"], matched_compiler_or_service_conflicts=matched,
                all_original_B_target_events=relevant, original_positive_control=False,
                historical_cause_resolved=False, safe_to_transfer_write_lease=False)


def analyse(root, native_root, capture, events, audited, arm):
    expected = {"seed/warm", "A/warm", "B/work0", "B/recovery"}
    for suffix in ("argv.txt", "result.json", "invocation-begin.json", "invocation-end.json", "stdout.txt", "stderr.txt"):
        actual = {p.relative_to(root).as_posix().removesuffix("." + suffix) for p in root.rglob("*." + suffix)}
        trace.need(actual == expected, "missing or unexpected original tool evidence: " + suffix)
    begin, end = trace.load(root / "holder-begin.json"), trace.load(root / "holder-end.json")
    summary = trace.load(root / "b-pdb-calibration.json")
    trace.need(capture["child"]["before_launch_qpc"] <= begin["open_before_qpc"] <= end["remove_after_qpc"] <=
               capture["child"]["after_wait_qpc"], "holder lifetime outside calibration child")
    target = trace.canonical(native_root.rstrip("/\\") + "\\B\\compiler.pdb", capture)
    trace.need(trace.canonical(begin["path"], capture) == target, "wrong locked target")
    envelope = trace.load(root / "default-envelope.json")
    trace.need(all(envelope.get(k) is True for k in ("cleanup_verified", "endpoint_override_absent", "outer_lifecycle_verified")) and
               type(envelope.get("remaining_servers")) is list and not envelope["remaining_servers"], "outer cleanup unproven")
    trace.need(trace.load(root / "default-preflight.json") == [] and capture["child"]["exit_code"] == 0,
               "dirty host or native calibration failed")
    rows = {}
    for stem in sorted(expected):
        argv = (root / (stem + ".argv.txt")).read_text(encoding="utf-8-sig").splitlines()
        trace.need(argv[0].replace("/", "\\").rsplit("\\", 1)[-1].lower() == "cl.exe", "calibration tool is not cl.exe")
        expected_pdb = trace.canonical(native_root.rstrip("/\\") + "\\" + stem.split("/")[0] + "\\compiler.pdb", capture)
        pdb_args = [arg[3:] for arg in argv if arg.startswith("/Fd")]
        trace.need(len(pdb_args) == 1 and trace.canonical(pdb_args[0], capture) == expected_pdb, "compiler PDB target changed")
        result = trace.load(root / (stem + ".result.json"))
        low = trace.load(root / (stem + ".invocation-begin.json"))
        high = trace.load(root / (stem + ".invocation-end.json"))
        trace.need(all(flag in argv for flag in ("/FS", "/Zi", "/Od", "/MTd")) and "/Z7" not in argv and "/bigobj" not in argv,
                   "calibration compiler flags changed")
        children = invocation.bind_span(low, high, result, capture, events, argv[0], stem.split("/")[-1])
        trace.need(len(children) == 1, "sequential calibration compiler identity ambiguous")
        child = children[0]
        exits = [e for e in events if e["provider"] == "process" and e["id"] == 2 and
                 e["data"].get("ProcessID") == child["pid"] and
                 child["start_qpc"] <= e["qpc"] <= high["after_run_qpc"]]
        trace.need(len(exits) == 1, "compiler completion event missing/ambiguous")
        text = "\n".join((root / (stem + "." + s + ".txt")).read_text(encoding="utf-8-sig") for s in ("stdout", "stderr"))
        rows[stem] = dict(exit_code=result["exit_code"], candidate_tool_processes=children,
                          before_run_qpc=low["before_run_qpc"], after_run_qpc=high["after_run_qpc"],
                          diagnostic_codes=sorted(set(re.findall(r"\bC\d{4}\b", text))))
        trace.need((low["owner_pid"], low["owner_created_filetime"]) == (begin["owner_pid"], begin["owner_created_filetime"]),
                   "tool not launched by recorded holder process")
        if stem in ("seed/warm", "A/warm"):
            trace.need(result["exit_code"] == 0, "seed/A positive control failed")
        if stem == "B/work0" and arm == "conflict":
            trace.need(begin["path"].casefold() in text.casefold(), "failure diagnostic does not name locked B PDB")
    trace.need(rows["A/warm"]["after_run_qpc"] < begin["open_before_qpc"], "placeholder created before positive preparation")
    service = begin["service"][0]
    compiler_begin = trace.load(root / "B/work0.invocation-begin.json")
    mapping = dict(dos_root=compiler_begin["executable_dos_root"], device_root=compiler_begin["executable_device_root"])
    starts = [e for e in events if e["provider"] == "process" and e["id"] == 1 and
              e["data"].get("ProcessID") == service["pid"] and e["data"].get("CreateTime") == service["created"] and
              trace.canonical(e["data"]["ImageName"], mapping) == trace.canonical(service["image"], mapping)]
    trace.need(len(starts) == 1 and starts[0]["qpc"] < begin["open_before_qpc"] and
               service["image"].replace("/", "\\").rsplit("\\", 1)[-1].lower() == "mspdbsrv.exe", "retained service not matched to trace")
    trace.need((root / "B/work0.cpp").read_bytes() == (root / "B/recovery.cpp").read_bytes(), "recovery source differs")
    report = failure_chain(begin, end, summary, rows["B/work0"], rows["B/recovery"], audited["target_open_pairs"], arm, target)
    report["invocations"] = rows
    report["all_recorded_nonzero_events"] = [p for p in audited["target_open_pairs"] if p["ntstatus"] != 0]
    return report


def self_test():
    begin = dict(schema=1, kind="injected-cold-B-PDB-calibration", arm="conflict", path="b.pdb", owner_pid=1,
        owner_tid=2, owner_created_filetime=10, desired_access=0xc0010000, share_mode=0, creation_disposition=1,
        open_error=0, volume_serial="1", file_id="a"*32, open_before_qpc=10, open_after_qpc=11,
        service=[dict(pid=3, created=30, image="mspdbsrv.exe")], historical_cause_resolved=False, safe_to_transfer_write_lease=False)
    end = dict(schema=1, delete_before_qpc=48, delete_after_qpc=49, release_before_qpc=50, release_after_qpc=51, remove_after_qpc=52,
               delete_error=0, close_error=0, absence_error=2, held_bytes_before_release=0, safe_to_transfer_write_lease=False)
    summary = dict(schema=1, kind=begin["kind"], arm="conflict", original_B_exit=2, recovery_B_exit=0,
                   original_service_survived=True, original_positive_control=False,
                   historical_cause_resolved=False, safe_to_transfer_write_lease=False)
    first = dict(exit_code=2, diagnostic_codes=["C1041"], before_run_qpc=20, after_run_qpc=40,
                 candidate_tool_processes=[dict(pid=4, created_filetime=40)])
    recovery = dict(exit_code=0, before_run_qpc=60, after_run_qpc=80)
    pairs = [dict(path="b.pdb", begin_qpc=22, end_qpc=23, ntstatus=0xc0000043,
                  process_identity=dict(pid=3, created_filetime=30))]
    mutations = [
        ("complete-service-failure", True, lambda b,e,s,f,r,p: None),
        ("complete-compiler-failure", True, lambda b,e,s,f,r,p: p[0].update(process_identity=dict(pid=4,created_filetime=40))),
        ("coordinator-event-not-compiler", False, lambda b,e,s,f,r,p: p[0].update(process_identity=dict(pid=1,created_filetime=10))),
        ("wrong-process-creation", False, lambda b,e,s,f,r,p: p[0]["process_identity"].update(created_filetime=31)),
        ("missing-target-event", False, lambda b,e,s,f,r,p: p.clear()),
        ("wrong-target", False, lambda b,e,s,f,r,p: p[0].update(path="a.pdb")),
        ("outside-compile", False, lambda b,e,s,f,r,p: p[0].update(begin_qpc=41,end_qpc=42)),
        ("wrong-status", False, lambda b,e,s,f,r,p: p[0].update(ntstatus=0xc0000034)),
        ("premature-release", False, lambda b,e,s,f,r,p: e.update(release_before_qpc=30,release_after_qpc=31,remove_after_qpc=32)),
        ("no-original-failure", False, lambda b,e,s,f,r,p: (f.update(exit_code=0),s.update(original_B_exit=0))),
        ("unrelated-diagnostic", False, lambda b,e,s,f,r,p: f.update(diagnostic_codes=["C1001"])),
        ("recovery-overwrites-original", False, lambda b,e,s,f,r,p: s.update(original_B_exit=0)),
        ("failed-recovery", False, lambda b,e,s,f,r,p: (r.update(exit_code=2),s.update(recovery_B_exit=2))),
        ("early-recovery", False, lambda b,e,s,f,r,p: r.update(before_run_qpc=35)),
        ("failed-close", False, lambda b,e,s,f,r,p: e.update(close_error=6)),
        ("failed-remove", False, lambda b,e,s,f,r,p: e.update(delete_error=32)),
        ("changed-placeholder", False, lambda b,e,s,f,r,p: e.update(held_bytes_before_release=1)),
        ("wrong-share", False, lambda b,e,s,f,r,p: b.update(share_mode=7)),
        ("overwrite-not-create-new", False, lambda b,e,s,f,r,p: b.update(creation_disposition=2)),
        ("missing-file-identity", False, lambda b,e,s,f,r,p: b.pop("file_id")),
        ("relabel-original-control", False, lambda b,e,s,f,r,p: s.update(original_positive_control=True)),
        ("historical-cause-invented", False, lambda b,e,s,f,r,p: s.update(historical_cause_resolved=True)),
        ("lease-authorized", False, lambda b,e,s,f,r,p: e.update(safe_to_transfer_write_lease=True)),
        ("service-died", False, lambda b,e,s,f,r,p: s.update(original_service_survived=False)),
        ("multiple-compiler-candidates", False, lambda b,e,s,f,r,p: f["candidate_tool_processes"].append(dict(pid=5,created_filetime=50))),
    ]
    rows=[]
    for name,expected,change in mutations:
        b,e,s,f,r,p = copy.deepcopy((begin,end,summary,first,recovery,pairs)); change(b,e,s,f,r,p); error=None
        try: failure_chain(b,e,s,f,r,p,"conflict","b.pdb")
        except (ValueError, KeyError, TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=expected,accepted=error is None,passed=(error is None)==expected,error=error))
    for name,failed in (("baseline-original-success",False),("baseline-original-failure",True)):
        b,e,s,f,r,p = copy.deepcopy((begin,end,summary,first,recovery,pairs))
        b["arm"]=s["arm"]="baseline";e.update(delete_before_qpc=11,delete_after_qpc=12,release_before_qpc=12,release_after_qpc=13,remove_after_qpc=14)
        f["exit_code"]=s["original_B_exit"]=2 if failed else 0
        error=None
        try: failure_chain(b,e,s,f,r,p,"baseline","b.pdb")
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=not failed,accepted=error is None,passed=(error is None)==(not failed),error=error))
    return dict(synthetic_only=True,cases=rows,passed=all(r["passed"] for r in rows))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test",action="store_true");p.add_argument("--trace",type=Path);p.add_argument("--fixture",type=Path)
    p.add_argument("--native-root");p.add_argument("--arm",choices=("baseline","conflict"));p.add_argument("--output",required=True,type=Path)
    a=p.parse_args();trace.need(not a.output.exists(),"refuse to overwrite audit")
    report=dict(calibration_verified=False,original_positive_control=False,historical_cause_resolved=False,safe_to_transfer_write_lease=False)
    try:
        if a.self_test: report=self_test();ok=report["passed"]
        else:
            capture,decode=trace.load(a.trace/"capture.json"),trace.load(a.trace/"decode.json")
            trace.need((a.trace/"events.etl").stat().st_size==decode["etl_bytes"] and
                       trace.load(a.trace/"readiness.json")==capture["readiness"],"trace identity mismatch")
            events=[json.loads(line) for line in (a.trace/"events.jsonl").read_text(encoding="utf-8-sig").splitlines()]
            native=a.native_root or str(a.fixture)
            audited=trace.audit(capture,decode,events,native,require_readiness=True)
            report=analyse(a.fixture,native,capture,events,audited,a.arm);ok=True
    except (ValueError,KeyError,TypeError,OSError) as exc: report["error"]=str(exc);ok=False
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(accepted=ok,output=str(a.output))))
    return 0 if ok else 1


if __name__ == "__main__": raise SystemExit(main())
