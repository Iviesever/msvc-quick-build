"""Read-only observer API envelopes, not call stacks or compiler/RPC causality.

Keep a public API's Win32 return separate from file-event NTSTATUS. A successful
query can contain failed internal opens; another process on the same path is not
that query. Historical records without these boundaries cannot be reconstructed.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import verify_pdb_file_trace as trace
import verify_pdb_invocations as invocation


def uint(value, name):
    n = trace.integer(value, name)
    trace.need(n <= 0xffffffff, "invalid DWORD: " + name)
    return n


def group_calls(group, kind, name, capture, events, owner):
    log = group.get("observer")
    trace.need(type(log) is dict and type(log.get("schema")) is int and log["schema"] == 1,
               "missing/invalid observer boundary (historical data not backfilled)")
    trace.need(log.get("clock") == "QPC" and trace.integer(log.get("frequency"), "frequency") == capture["qpc_frequency"] and
               log.get("clock_ok") is True and log.get("overflow") is False and
               log.get("safe_to_transfer_write_lease") is False, "observer clock/cap/authority invalid")
    pid, created = uint(log.get("owner_pid"), "pid"), trace.integer(log.get("owner_created_filetime"), "creation")
    trace.need(pid > 0 and created > 0 and (pid, created) == owner, "observer is not the native coordinator")
    path = trace.canonical(group["path"], capture)
    rows = log.get("calls")
    trace.need(type(rows) is list and 1 <= len(rows) <= 8, "observer calls missing or over limit")
    output = []
    previous = capture["child"]["before_launch_qpc"]
    for row in rows:
        low = trace.integer(row.get("before_qpc"), "before_qpc")
        high = trace.integer(row.get("after_qpc"), "after_qpc")
        tid = uint(row.get("tid"), "tid")
        trace.need(tid > 0 and previous <= low <= high <= capture["child"]["after_wait_qpc"], "observer range/order invalid")
        previous = high
        status = uint(row.get("native_status"), "native_status")
        starts = [e for e in events if e["provider"] == "process" and e["id"] == 1 and
                  e["data"].get("ProcessID") == pid and e["data"].get("CreateTime") == created and e["qpc"] <= low]
        trace.need(len(starts) == 1, "observer process lifetime absent/ambiguous")
        trace.need(not any(e["provider"] == "process" and e["data"].get("ProcessID") == pid and
                   starts[0]["qpc"] <= e["qpc"] <= high and (e["id"] == 2 or
                   (e["id"] == 1 and e["data"].get("CreateTime") != created)) for e in events), "observer process lifetime changed")
        threads = [e for e in events if e["provider"] == "process" and e["id"] == 3 and
                   e["data"].get("ProcessID") == pid and e["data"].get("ThreadID") == tid and e["qpc"] <= low]
        trace.need(len(threads) == 1, "observer thread lifetime absent/ambiguous")
        trace.need(not any(e["provider"] == "process" and e["id"] in (3, 4) and
                   e["data"].get("ThreadID") == tid and threads[0]["qpc"] < e["qpc"] <= high
                   for e in events), "observer thread exited/reused")
        output.append(dict(group=name, path=path, api=row.get("api"), owner_pid=pid, owner_created_filetime=created,
                           tid=tid, before_qpc=low, after_qpc=high, native_status=status))
    apis = [r["api"] for r in rows]
    if kind == "metadata":
        opened = uint(group.get("open_error"), "open_error")
        expected = ["CreateFileW"] + (["GetFileInformationByHandleEx"] if opened == 0 else [])
        trace.need(apis == expected and rows[0]["native_status"] == opened, "metadata API/result mismatch")
        if opened == 0:
            trace.need(rows[1]["native_status"] == uint(group.get("identity_error"), "identity_error"), "identity result mismatch")
        else:
            trace.need(group.get("identity_error") is None and group.get("file_id") is None and group.get("volume_serial") is None,
                       "failed metadata open invented identity")
    else:
        trace.need(kind == "query" and apis[0] == "RmStartSession", "invalid query start")
        if rows[0]["native_status"]:
            trace.need(len(rows) == 1, "failed start dispatched more query APIs")
            terminal = rows[0]["native_status"]
        else:
            trace.need(len(rows) >= 3 and apis[1] == "RmRegisterResources" and apis[-1] == "RmEndSession", "query lifecycle incomplete")
            lists = rows[2:-1]
            if rows[1]["native_status"]:
                trace.need(not lists, "failed registration dispatched query")
                terminal = rows[1]["native_status"]
            else:
                trace.need(1 <= len(lists) <= 4 and all(r["api"] == "RmGetList" for r in lists) and
                           all(r["native_status"] == 234 for r in lists[:-1]), "invalid original GetList attempt sequence")
                terminal = lists[-1]["native_status"]
        trace.need(uint(group.get("query_error"), "query_error") == terminal, "query result replaced original API return")
    return output


def correlate(calls, pairs):
    rows = []
    for event in pairs:
        identity = event.get("process_identity")
        exact, overlap = [], []
        for index, call in enumerate(calls):
            if identity is None or (identity.get("pid"), identity.get("created_filetime")) != (call["owner_pid"], call["owner_created_filetime"]):
                continue
            if identity.get("issuing_tid") != call["tid"] or event["path"] != call["path"]:
                continue
            if call["before_qpc"] <= event["begin_qpc"] <= event["end_qpc"] <= call["after_qpc"]:
                exact.append(index)
            elif event["begin_qpc"] <= call["after_qpc"] and call["before_qpc"] <= event["end_qpc"]:
                overlap.append(index)
        rows.append(dict(event=event, contained_call_indices=exact, partial_overlap_call_indices=overlap,
                         classification="one-recorded-call" if len(exact) == 1 else ("ambiguous" if exact else "unmatched"),
                         compiler_failure_proven=False, causal_attribution_proven=False))
    return rows


# Query-off is an explicit experimental arm, never an inferred successful
# empty result. The CLI supplies expected mode independently of recorded data.
QUERY_FIELDS = (
    ("warm_pdb_owner_error", "warm_pdb_owners"),
    ("active_pdb_owner_error", "active_pdb_owners"),
    ("A_pdb_owner_at_request_error", "A_pdb_owners_at_request"),
    ("A_pdb_owner_after_A_error", "A_pdb_owners_after_A"),
)
QUERY_FLAGS = ("B_pdb_service_identity_observed", "A_pdb_service_owner_after_A")


def validate_query_policy(policy, observation, mode):
    trace.need(mode in ("rm-on", "rm-off"), "unknown expected query mode")
    enabled = mode == "rm-on"
    trace.need(type(policy) is dict and type(policy.get("schema")) is int and policy["schema"] == 1 and
               policy.get("rm_queries_enabled") is enabled and
               type(policy.get("expected_query_slots")) is int and policy["expected_query_slots"] == 4 and
               policy.get("historical_cause_resolved") is False and policy.get("safe_to_transfer_write_lease") is False,
               "query policy identity/slots/authority mismatch")
    trace.need(policy.get("profile") == observation.get("profile") == "zi-debug" and
               policy.get("origin") == observation.get("origin") == "preexisting" and
               observation.get("ending") == "drain" and observation.get("endpoint_mode") == "default",
               "query contrast shape mismatch")
    for error_field, owners_field in QUERY_FIELDS:
        trace.need(error_field in observation and owners_field in observation, "query observation fields missing")
        if enabled:
            uint(observation[error_field], error_field)
            trace.need(type(observation[owners_field]) is list, "attempted query owners not an array")
        else:
            trace.need(observation[error_field] is None and observation[owners_field] is None,
                       "unattempted query invented result or empty owner set")
    for flag in QUERY_FLAGS:
        trace.need(flag in observation and (type(observation[flag]) is bool if enabled else observation[flag] is None),
                   "query-off invented resource identity")
    return enabled


def query_group_calls(group, name, capture, events, owner, enabled):
    if enabled is None:  # Original investigation format, not a query contrast.
        return group_calls(group, "query", name, capture, events, owner)
    trace.need(group.get("attempted") is enabled, "query arm disagrees with attempted flag")
    trace.need(all(field in group for field in ("query_error", "owners", "observer")), "query slot fields missing")
    if not enabled:
        trace.need(all(group[field] is None for field in ("query_error", "owners", "observer")),
                   "unattempted query fabricated a native API/outcome")
        return []
    trace.need(type(group["owners"]) is list, "attempted query owner inventory invalid")
    return group_calls(group, "query", name, capture, events, owner)


def analyse(root, native_root, capture, events, audited, query_mode=None):
    expected = {f"observer-query-{n}.json" for n in range(4)}
    trace.need({p.name for p in root.glob("observer-query-*.json")} == expected, "observer query inventory incomplete/unexpected")
    begin = trace.load(root / "B/warm.invocation-begin.json")
    owner = (begin["owner_pid"], begin["owner_created_filetime"])
    rootkey = trace.canonical(native_root, capture).rstrip("\\")
    calls = []
    enabled = None
    observation = trace.load(root / "observation.json")
    if query_mode is not None:
        enabled = validate_query_policy(trace.load(root / "query-policy.json"), observation, query_mode)
    else:
        trace.need(not (root / "query-policy.json").exists(), "explicit expected query mode required")
    process_events = [e for e in events if e["provider"] == "process"]
    for n, side in enumerate(("B", "B", "A", "A")):
        group = trace.load(root / f"observer-query-{n}.json")
        trace.need(trace.canonical(group["path"], capture) == rootkey + "\\" + side.lower() + "\\compiler.pdb", "query target mismatch")
        calls += query_group_calls(group, f"query-{n}", capture, process_events, owner, enabled)
        if enabled is not None:
            error_field, owners_field = QUERY_FIELDS[n]
            trace.need(group["query_error"] == observation[error_field] and group["owners"] == observation[owners_field],
                       "query slot disagrees with original observation")
    for phase in ("before-A", "after-A", "after-B"):
        snapshot = trace.load(root / f"pdb-{phase}.json")
        for kind, filename in (("pdb", "compiler.pdb"), ("pch", "common.pch")):
            for side in ("A", "B"):
                group = snapshot[kind][side]
                trace.need(trace.canonical(group["path"], capture) == rootkey + "\\" + side.lower() + "\\" + filename, "metadata target mismatch")
                calls += group_calls(group, "metadata", f"{phase}/{side}/{kind}", capture, process_events, owner)
    # The coordinator calls these APIs synchronously. Do not silently select one
    # of overlapping/tampered intervals, even if it would explain a status.
    ordered = sorted(calls, key=lambda r: r["before_qpc"])
    trace.need(all(a["after_qpc"] <= b["before_qpc"] for a, b in zip(ordered, ordered[1:])), "coordinator API intervals overlap")
    matched = correlate(calls, [e for e in audited["target_open_pairs"] if e["path"].startswith(rootkey + "\\")])
    return dict(schema=1, observer_evidence_complete=True, observer_calls=calls, events=matched,
                query_mode=query_mode, attempted_query_slots=(None if enabled is None else (4 if enabled else 0)),
                unattempted_query_slots=(None if enabled is None else (0 if enabled else 4)),
                nonzero_events=[e for e in matched if e["event"]["ntstatus"] != 0],
                unmatched_events=sum(not e["contained_call_indices"] for e in matched),
                historical_cause_resolved=False, causal_attribution_proven=False, safe_to_transfer_write_lease=False)


def self_test():
    capture = dict(qpc_frequency=1000, device_root="\\Device\\V", dos_root="C:", child=dict(before_launch_qpc=1, after_wait_qpc=1000))
    events = [dict(provider="process", id=1, qpc=10, data=dict(ProcessID=20, CreateTime=100)),
              dict(provider="process", id=3, qpc=11, data=dict(ProcessID=20, ThreadID=21))]
    group = dict(path="C:\\A\\compiler.pdb", query_error=0, observer=dict(schema=1, clock="QPC", frequency=1000,
        owner_pid=20, owner_created_filetime=100, clock_ok=True, overflow=False, safe_to_transfer_write_lease=False,
        calls=[dict(api=api, tid=21, native_status=0, before_qpc=100+i*10, after_qpc=105+i*10)
               for i, api in enumerate(("RmStartSession", "RmRegisterResources", "RmGetList", "RmEndSession"))]))
    tests = [
        ("complete-query", True, lambda g, e: None),
        ("missing-boundary", False, lambda g, e: g.pop("observer")),
        ("clock-failed", False, lambda g, e: g["observer"].update(clock_ok=False)),
        ("overflow", False, lambda g, e: g["observer"].update(overflow=True)),
        ("wrong-frequency", False, lambda g, e: g["observer"].update(frequency=999)),
        ("boolean-schema", False, lambda g, e: g["observer"].update(schema=True)),
        ("wrong-process", False, lambda g, e: g["observer"].update(owner_pid=99)),
        ("wrong-creation", False, lambda g, e: g["observer"].update(owner_created_filetime=101)),
        ("wrong-thread", False, lambda g, e: g["observer"]["calls"][0].update(tid=22)),
        ("wrong-thread-lifetime", False, lambda g, e: e.append(dict(e[1], id=4, qpc=102))),
        ("process-reused", False, lambda g, e: e.append(dict(e[0], qpc=101, data=dict(ProcessID=20, CreateTime=101)))),
        ("reversed-interval", False, lambda g, e: g["observer"]["calls"][0].update(after_qpc=99)),
        ("outside-capture", False, lambda g, e: g["observer"]["calls"][-1].update(after_qpc=1001)),
        ("wrong-order", False, lambda g, e: g["observer"]["calls"][1].update(before_qpc=102)),
        ("string-return", False, lambda g, e: g["observer"]["calls"][0].update(native_status="0")),
        ("missing-end", False, lambda g, e: g["observer"]["calls"].pop()),
        ("wrong-query-result", False, lambda g, e: g.update(query_error=5)),
        ("invented-api", False, lambda g, e: g["observer"]["calls"][2].update(api="unknown")),
        ("lease-authority", False, lambda g, e: g["observer"].update(safe_to_transfer_write_lease=True)),
        ("failed-query-preserved", True, lambda g, e: (g.update(query_error=5), g["observer"]["calls"][2].update(native_status=5))),
    ]
    rows = []
    for name, expected, change in tests:
        g, e = copy.deepcopy((group, events)); change(g, e); error = None
        try: group_calls(g, "query", "q", capture, e, (20, 100))
        except (ValueError, KeyError, TypeError) as exc: error = str(exc)
        rows.append(dict(name=name, expected_accepted=expected, accepted=error is None, passed=(error is None)==expected, error=error))
    # File failures and public API returns are preserved independently of ETW.
    metadata = dict(path=group["path"], open_error=2, identity_error=None, file_id=None, volume_serial=None,
                    observer=copy.deepcopy(group["observer"]))
    metadata["observer"]["calls"] = [dict(api="CreateFileW", tid=21, native_status=2, before_qpc=100, after_qpc=105)]
    for name, expected, mutate in [
        ("missing-file-native-error", True, lambda g: None),
        ("invented-missing-file-identity", False, lambda g: g.update(file_id="0"*32)),
        ("metadata-native-mismatch", False, lambda g: g.update(open_error=32)),
    ]:
        g=copy.deepcopy(metadata); mutate(g); error=None
        try: group_calls(g,"metadata","m",capture,events,(20,100))
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=expected,accepted=error is None,passed=(error is None)==expected,error=error))
    good = copy.deepcopy(metadata)
    good.update(open_error=0, identity_error=0, file_id="a"*32, volume_serial="1")
    good["observer"]["calls"][0]["native_status"] = 0
    good["observer"]["calls"].append(dict(api="GetFileInformationByHandleEx", tid=21, native_status=0, before_qpc=110, after_qpc=115))
    for name, expected, mutate in [
        ("successful-metadata-pair", True, lambda g: None),
        ("missing-metadata-query", False, lambda g: g["observer"]["calls"].pop()),
        ("metadata-query-return-mismatch", False, lambda g: g.update(identity_error=5)),
        ("metadata-query-failure-preserved", True, lambda g: (g.update(identity_error=5), g["observer"]["calls"][1].update(native_status=5))),
    ]:
        g=copy.deepcopy(good); mutate(g); error=None
        try: group_calls(g,"metadata","m",capture,events,(20,100))
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=expected,accepted=error is None,passed=(error is None)==expected,error=error))
    for name, status, index in (("failed-RM-start",5,0),("failed-RM-registration",5,1),("more-data-attempt-preserved",234,2)):
        g=copy.deepcopy(group); records=g["observer"]["calls"]; records[index]["native_status"]=status
        if index==0: g["observer"]["calls"]=records[:1];g["query_error"]=status
        elif index==1: g["observer"]["calls"]=records[:2]+records[-1:];g["query_error"]=status
        else:
            records[-1].update(before_qpc=140,after_qpc=145)
            records.insert(-1,dict(api="RmGetList",tid=21,native_status=0,before_qpc=130,after_qpc=135))
        error=None
        try: group_calls(g,"query","q",capture,events,(20,100))
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=True,accepted=error is None,passed=error is None,error=error))
    calls = group_calls(group,"query","q",capture,events,(20,100))
    event = dict(path="c:\\a\\compiler.pdb",begin_qpc=111,end_qpc=113,ntstatus=0xc0000043,
                 process_identity=dict(pid=20,created_filetime=100,issuing_tid=21))
    for name, expected, modify in [
        ("query-success-internal-sharing-failure", [1], lambda e: None),
        ("compiler-same-path-time-is-not-observer", [], lambda e: e["process_identity"].update(pid=30)),
        ("different-thread", [], lambda e: e["process_identity"].update(issuing_tid=22)),
        ("different-creation", [], lambda e: e["process_identity"].update(created_filetime=101)),
        ("different-path", [], lambda e: e.update(path="c:\\b\\compiler.pdb")),
        ("partial-interval", [], lambda e: e.update(end_qpc=119)),
        ("unattributed-process", [], lambda e: e.update(process_identity=None)),
    ]:
        e=copy.deepcopy(event);modify(e);r=correlate(calls,[e])[0]
        ok=r["contained_call_indices"]==expected and r["event"]["ntstatus"]==0xc0000043 and not r["causal_attribution_proven"]
        rows.append(dict(name=name,passed=ok,classification=r["classification"]))
    ambiguous=correlate(calls+[calls[1]],[event])[0]
    rows.append(dict(name="ambiguous-candidates-retained",passed=ambiguous["classification"]=="ambiguous" and ambiguous["contained_call_indices"]==[1,4]))
    policy = dict(schema=1, profile="zi-debug", origin="preexisting", expected_query_slots=4,
                  rm_queries_enabled=False, historical_cause_resolved=False, safe_to_transfer_write_lease=False)
    off_observation = dict(profile="zi-debug", origin="preexisting", ending="drain", endpoint_mode="default")
    for names in QUERY_FIELDS:
        for field in names: off_observation[field] = None
    for flag in QUERY_FLAGS: off_observation[flag] = None
    for name, accepted, mutate in [
        ("query-off-null-observations", True, lambda p, o: None),
        ("query-off-empty-owner-set-refused", False, lambda p, o: o.update(warm_pdb_owners=[])),
        ("query-off-zero-error-refused", False, lambda p, o: o.update(warm_pdb_owner_error=0)),
        ("query-off-false-identity-refused", False, lambda p, o: o.update(B_pdb_service_identity_observed=False)),
        ("query-off-missing-null-field", False, lambda p, o: o.pop("active_pdb_owners")),
        ("query-off-missing-policy-mode", False, lambda p, o: p.pop("rm_queries_enabled")),
        ("query-off-wrong-mode", False, lambda p, o: p.update(rm_queries_enabled=True)),
        ("query-policy-string-bool", False, lambda p, o: p.update(rm_queries_enabled="false")),
        ("query-policy-bool-schema", False, lambda p, o: p.update(schema=True)),
        ("query-policy-wrong-slots", False, lambda p, o: p.update(expected_query_slots=3)),
        ("query-policy-wrong-profile", False, lambda p, o: p.update(profile="pch-release")),
        ("query-policy-wrong-origin", False, lambda p, o: o.update(origin="A-started")),
        ("query-policy-cause-claim", False, lambda p, o: p.update(historical_cause_resolved=True)),
        ("query-policy-lease-claim", False, lambda p, o: p.update(safe_to_transfer_write_lease=True)),
    ]:
        pol, obs = copy.deepcopy((policy, off_observation)); mutate(pol, obs); error = None
        try: validate_query_policy(pol, obs, "rm-off")
        except (ValueError, KeyError, TypeError) as exc: error = str(exc)
        rows.append(dict(name=name, expected_accepted=accepted, accepted=error is None, passed=(error is None)==accepted, error=error))
    on_policy = dict(policy, rm_queries_enabled=True)
    on_observation = copy.deepcopy(off_observation)
    for error_field, owners_field in QUERY_FIELDS: on_observation[error_field] = 0; on_observation[owners_field] = []
    for flag in QUERY_FLAGS: on_observation[flag] = False
    for name, accepted, mutate in [
        ("query-on-real-results", True, lambda p, o: None),
        ("query-on-nonzero-native-return-preserved", True, lambda p, o: o.update(warm_pdb_owner_error=5)),
        ("query-on-null-error-refused", False, lambda p, o: o.update(warm_pdb_owner_error=None)),
        ("query-on-null-owners-refused", False, lambda p, o: o.update(warm_pdb_owners=None)),
    ]:
        pol, obs = copy.deepcopy((on_policy, on_observation)); mutate(pol, obs); error = None
        try: validate_query_policy(pol, obs, "rm-on")
        except (ValueError, KeyError, TypeError) as exc: error = str(exc)
        rows.append(dict(name=name, expected_accepted=accepted, accepted=error is None, passed=(error is None)==accepted, error=error))
    off_group = dict(path=group["path"], attempted=False, query_error=None, owners=None, observer=None)
    for name, accepted, mutate in [
        ("query-slot-not-attempted", True, lambda g: None),
        ("query-slot-fake-success", False, lambda g: g.update(query_error=0)),
        ("query-slot-empty-list", False, lambda g: g.update(owners=[])),
        ("query-slot-fake-api-log", False, lambda g: g.update(observer=group["observer"])),
        ("query-slot-missing-observer", False, lambda g: g.pop("observer")),
        ("query-slot-wrong-flag", False, lambda g: g.update(attempted=True)),
    ]:
        g=copy.deepcopy(off_group); mutate(g); error=None
        try:
            result=query_group_calls(g,"q",capture,events,(20,100),False)
            trace.need(result==[],"query-off fabricated native calls")
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=accepted,accepted=error is None,passed=(error is None)==accepted,error=error))
    for name, accepted, mutate in [
        ("query-slot-on-original-log", True, lambda g: None),
        ("query-slot-on-missing-native-log", False, lambda g: g.update(observer=None)),
    ]:
        g=copy.deepcopy(group); g.update(attempted=True,owners=[]); mutate(g); error=None
        try: query_group_calls(g,"q",capture,events,(20,100),True)
        except (ValueError,KeyError,TypeError) as exc: error=str(exc)
        rows.append(dict(name=name,expected_accepted=accepted,accepted=error is None,passed=(error is None)==accepted,error=error))
    return dict(synthetic_only=True,cases=rows,passed=all(r["passed"] for r in rows))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test",action="store_true");p.add_argument("--trace",type=Path);p.add_argument("--fixture",type=Path)
    p.add_argument("--native-root");p.add_argument("--profile",choices=("pch-release","modules-debug","zi-debug"));p.add_argument("--origin",choices=("A-started","preexisting"),default="A-started");p.add_argument("--output",required=True,type=Path)
    p.add_argument("--query-mode", choices=("rm-on","rm-off"))
    a=p.parse_args();trace.need(not a.output.exists(),"refuse to overwrite audit")
    report=dict(observer_evidence_complete=False,historical_cause_resolved=False,safe_to_transfer_write_lease=False)
    try:
        if a.self_test: report=self_test();ok=report["passed"]
        else:
            capture,decode=trace.load(a.trace/"capture.json"),trace.load(a.trace/"decode.json")
            trace.need((a.trace/"events.etl").stat().st_size==decode["etl_bytes"],"ETL size mismatch")
            trace.need(trace.load(a.trace/"readiness.json")==capture["readiness"],"readiness record mismatch")
            events=[json.loads(line) for line in (a.trace/"events.jsonl").read_text(encoding="utf-8-sig").splitlines()]
            native=a.native_root or str(a.fixture)
            audited=trace.audit(capture,decode,events,native,require_readiness=True)
            original=invocation.analyse(a.fixture,native,a.profile,capture,events,audited,origin=a.origin)
            report=analyse(a.fixture,native,capture,events,audited,query_mode=a.query_mode)
            report["original_control_ok"]=original["original_control_ok"]
            report["original_failed_invocations"]=original["original_failed_invocations"]
            ok=original["original_control_ok"] and not original["recovery_failed"]
    except (ValueError,KeyError,TypeError,OSError) as e: report["error"]=str(e);ok=False
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(accepted=ok,output=str(a.output))))
    return 0 if ok else 1


if __name__=="__main__": raise SystemExit(main())
