"""Budget-two scheduler query contrast. Diagnostic-only; never grants merge/release authority.
Explicit scheduler adapters reuse unchanged low-level event/identity validators.
The old direct-drain entry points are not widened or relabelled.
"""
from __future__ import annotations
import argparse, difflib, hashlib, json, re, sys
from pathlib import Path
import verify_pdb_file_trace as trace
import verify_pdb_invocations as inv
import verify_pdb_observers as obs
from verify_pdb_invocations import signed_code, control_ok, bind_span
from verify_pdb_observers import uint, QUERY_FIELDS, QUERY_FLAGS, query_group_calls, group_calls, correlate

BASE = "55f57a84ad938da10d0e28b4578cd1aef6d7f903"

def prepare(repo: Path, plan: Path, output: Path):
    trace.need(not output.exists(), "refuse to overwrite generated input")
    p = trace.load(plan)
    trace.need(p["base_commit"] == BASE and p["schema"] == 1, "wrong preparation plan")
    text = (repo / p["source_path"]).read_text(encoding="utf-8")
    raw = text.encode()
    got = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    trace.need(got == p["input_blob"], "original probe identity mismatch")
    original = text
    for old, new in p["replacements"]:
        trace.need(text.count(old) == 1, "ambiguous/missing replacement")
        text = text.replace(old, new)
    trace.need(hashlib.sha256(text.encode()).hexdigest() == p["output_sha256"], "derived probe identity mismatch")
    output.mkdir(parents=True)
    (output / "derived-probe.cpp").write_bytes(text.encode())
    (output / "derived-probe.patch").write_text("".join(difflib.unified_diff(original.splitlines(True), text.splitlines(True), fromfile=p["source_path"], tofile="derived-probe.cpp")), encoding="utf-8")
    (output / "preparation.json").write_text(json.dumps(p, indent=2)+"\n", encoding="utf-8")

def validate_scheduler(s):
    trace.need(s.get("api") == "BoundedWorkScheduler::run_with_admission_stop", "wrong scheduler")
    for k in ("scheduler_succeeded", "stop_requested", "admission_stop_observed", "stopped_before_all_items", "event_observed", "forwarded_during_callback"):
        trace.need(s.get(k) is True, "missing scheduler boundary: " + k)
    for k in ("worker_count", "started_count"):
        trace.need(type(s.get(k)) is int and s[k] == 1, "wrong scheduler count")
    trace.need(type(s.get("bridge_wait_error")) is int and s["bridge_wait_error"] == 0 and s.get("callback_exception") == "" and s.get("scheduler_error_code") is None and s.get("safe_to_transfer_write_lease") is False, "scheduler error/authority")

def analyse_scheduler(root: Path, native_root: str, profile: str, capture: dict,
            events: list[dict], event_audit: dict, origin: str = "A-started") -> dict:
    observation = trace.load(root / "observation.json")
    trace.need(observation.get("profile") == profile and profile == "modules-release" and origin == "A-started" and
               observation.get("origin") == origin and observation.get("ending") == "scheduler-drain" and observation.get("scheduler_api_used") is True and
               observation.get("endpoint_mode") == "default", "wrong original case identity")
    trace.need(observation.get("safe_to_transfer_write_lease") is False and
               observation.get("safe_to_integrate_cancellation") is False, "invalid original safety fields")
    envelope = trace.load(root / "default-envelope.json")
    trace.need(all(envelope.get(f) is True for f in ("cleanup_verified", "endpoint_override_absent", "outer_lifecycle_verified"))
               and type(envelope.get("remaining_servers")) is list and not envelope["remaining_servers"], "cleanup unproven")
    for f in ("A_exit", "B0_exit", "B1_exit", "B_link_exit", "B_run_exit", "recovery_compile_exit"):
        signed_code(observation.get(f))
    expected = ["A/warm", "A/work0", "B/warm", "B/work0", "B/work1", "B/recovery"]
    if profile == "modules-release":
        expected += [f"{side}/{'prefix' if profile == 'pch-release' else 'provider'}" for side in ("A", "B")]
    if origin == "preexisting":
        expected.append("seed/warm")
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
            required = ["/FS", "/Zi", "/O2", "/MT"] if profile == "modules-release" else ["/FS", "/Zi", "/Od", "/MTd"]
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
    validate_scheduler(trace.load(root / "A/scheduler.json"))
    drain = trace.load(root / "A/drain.json")
    for field in ("work_compiles_dispatched", "first_compile_exit", "pending_compile_exit"):
        signed_code(drain.get(field))
    trace.need(drain.get("safe_to_transfer_write_lease") is False and type(drain.get("stop_observed")) is bool, "invalid drain state")
    first = next(r for r in rows if r["stem"] == "A/work0")
    trace.need(first["exit_code"] == drain["first_compile_exit"], "original A failure replaced by drain outcome")
    passed = control_ok(observation, drain) and all(r["exit_code"] == 0 for r in rows if r["phase"] == "original")
    trace.need(capture["child"]["exit_code"] == (0 if passed else 1), "fixture outcome disagrees with original control")
    return {"schema": 1, "evidence_complete": True, "original_control_ok": passed, "profile": profile, "origin": origin,
            "physical_snapshots": snapshots, "invocations": rows, "original_failed_invocations": [r["stem"] for r in rows if r["phase"] == "original" and r["exit_code"] != 0],
            "recovery_failed": any(r["exit_code"] != 0 for r in rows if r["phase"] == "recovery"),
            "all_recorded_real_failed_opens": event_audit["real_failed_opens"],
            "historical_cause_resolved": False, "causal_attribution_proven": False, "safe_to_transfer_write_lease": False}


def validate_scheduler_policy(policy, observation, mode):
    trace.need(mode in ("rm-on", "rm-off"), "unknown expected query mode")
    enabled = mode == "rm-on"
    trace.need(type(policy) is dict and type(policy.get("schema")) is int and policy["schema"] == 1 and
               policy.get("rm_queries_enabled") is enabled and
               type(policy.get("expected_query_slots")) is int and policy["expected_query_slots"] == 4 and
               policy.get("historical_cause_resolved") is False and policy.get("safe_to_transfer_write_lease") is False,
               "query policy identity/slots/authority mismatch")
    trace.need(policy.get("profile") == observation.get("profile") == "modules-release" and
               policy.get("origin") == observation.get("origin") == "A-started" and
               observation.get("ending") == "scheduler-drain" and observation.get("endpoint_mode") == "default",
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


def analyse_observers(root, native_root, capture, events, audited, query_mode=None):
    expected = {f"observer-query-{n}.json" for n in range(4)}
    trace.need({p.name for p in root.glob("observer-query-*.json")} == expected, "observer query inventory incomplete/unexpected")
    begin = trace.load(root / "B/warm.invocation-begin.json")
    owner = (begin["owner_pid"], begin["owner_created_filetime"])
    rootkey = trace.canonical(native_root, capture).rstrip("\\")
    calls = []
    enabled = None
    observation = trace.load(root / "observation.json")
    if query_mode is not None:
        enabled = validate_scheduler_policy(trace.load(root / "query-policy.json"), observation, query_mode)
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
    import copy, tempfile
    results=[]
    good=dict(schema=1,profile="modules-release",origin="A-started",rm_queries_enabled=False,expected_query_slots=4,historical_cause_resolved=False,safe_to_transfer_write_lease=False)
    o=dict(profile="modules-release",origin="A-started",ending="scheduler-drain",endpoint_mode="default")
    for pair in QUERY_FIELDS:
        for field in pair: o[field]=None
    for field in QUERY_FLAGS: o[field]=None
    cases=[("valid-off", True, lambda p,o:None),
           ("wrong-profile",False,lambda p,o:p.update(profile="zi-debug")),
           ("wrong-origin",False,lambda p,o:o.update(origin="preexisting")),
           ("direct-not-scheduler",False,lambda p,o:o.update(ending="drain")),
           ("off-not-zero",False,lambda p,o:o.update(warm_pdb_owner_error=0)),
           ("off-not-empty",False,lambda p,o:o.update(warm_pdb_owners=[])),
           ("no-authority",False,lambda p,o:p.update(historical_cause_resolved=True)),
           ("wrong-slots",False,lambda p,o:p.update(expected_query_slots=5)),
           ("bool-schema",False,lambda p,o:p.update(schema=True))]
    for name,expected,change in cases:
        pp,oo=copy.deepcopy((good,o));change(pp,oo);ok=True
        try:validate_scheduler_policy(pp,oo,"rm-off")
        except (ValueError,TypeError,KeyError):ok=False
        trace.need(ok==expected,name);results.append(name)
    good["rm_queries_enabled"]=True
    for e,v in QUERY_FIELDS:o[e]=0;o[v]=[]
    for f in QUERY_FLAGS:o[f]=False
    trace.need(validate_scheduler_policy(good,o,"rm-on") is True,"on mode rejected");results.append("valid-on")
    ss=dict(api="BoundedWorkScheduler::run_with_admission_stop",scheduler_succeeded=True,stop_requested=True,admission_stop_observed=True,stopped_before_all_items=True,event_observed=True,forwarded_during_callback=True,worker_count=1,started_count=1,bridge_wait_error=0,callback_exception="",scheduler_error_code=None,safe_to_transfer_write_lease=False)
    validate_scheduler(ss);results.append("real-scheduler-shape")
    for key,value in (("started_count",2),("worker_count",True),("forwarded_during_callback",False),("scheduler_error_code",0),("safe_to_transfer_write_lease",True)):
        wrong=ss.copy();wrong[key]=value
        try:validate_scheduler(wrong)
        except (ValueError,TypeError,KeyError):results.append("reject-"+key)
        else:raise ValueError("bad scheduler accepted: "+key)
    return dict(passed=True,checks=len(results),tests=results,new_windows_cases=0)

def audit_one(slot, mode):
    root=slot/"fixture"; tr=slot/"trace"
    native=trace.load(slot/"case-start.json")["fixture"]
    cap=trace.load(tr/"capture.json");dec=trace.load(tr/"decode.json")
    trace.need(trace.load(tr/"readiness.json")==cap["readiness"],"readiness copy")
    trace.need((tr/"events.etl").stat().st_size==dec["etl_bytes"],"ETL length")
    events=[json.loads(line) for line in (tr/"events.jsonl").read_text(encoding="utf-8-sig").splitlines()]
    checked=trace.audit(cap,dec,events,native,require_readiness=True)
    a=analyse_scheduler(root,native,"modules-release",cap,events,checked)
    b=analyse_observers(root,native,cap,events,checked,mode)
    return dict(evidence_complete=True,original_control_ok=a["original_control_ok"],invocations=a,observers=b,file_events=checked,historical_cause_resolved=False,authorizes_merge=False)

def compare_required(left: Path, right: Path):
    starts=[trace.load(p/"case-start.json") for p in (left,right)]
    roots=[left/"fixture",right/"fixture"]
    expected={"A/provider","A/warm","A/work0","B/provider","B/warm","B/work0","B/work1","B/recovery"}
    inventories=[]; optional=[]
    for root in roots:
        items={p.relative_to(root).as_posix():p for p in root.rglob("*") if p.is_file() and (p.suffix in (".cpp",".hpp",".ixx") or p.name.endswith(".argv.txt"))}
        optional.append({n:hashlib.sha256(items[n].read_bytes()).hexdigest() for n in ("B/link.argv.txt","B/run.argv.txt") if n in items})
        items={n:p for n,p in items.items() if n not in ("B/link.argv.txt","B/run.argv.txt")}
        trace.need({n.removesuffix(".argv.txt") for n in items if n.endswith(".argv.txt")}==expected,"mandatory compile inventory drift")
        inventories.append(items)
    a,b=inventories;trace.need(set(a)==set(b),"mandatory inputs differ")
    rows=[]
    for name in sorted(a):
        x,y=a[name].read_bytes(),b[name].read_bytes()
        if name.endswith(".argv.txt"):
            x=x.decode("utf-8-sig").replace(starts[0]["fixture"],"<fixture>").encode()
            y=y.decode("utf-8-sig").replace(starts[1]["fixture"],"<fixture>").encode()
        trace.need(x==y,"different mandatory input: "+name)
        rows.append(dict(path=name,sha256=hashlib.sha256(x).hexdigest()))
    tools=[trace.load(p/"actual-tools.json") for p in (left,right)]
    trace.need(tools[0]==tools[1],"actual tool identities differ")
    return dict(mandatory_input_equivalence=True,compared=rows,optional_link_run_inventory=optional,actual_tools_equal=True,historical_cause_resolved=False,authorizes_merge=False)


def main():
    p=argparse.ArgumentParser();p.add_argument("command",choices=("prepare","self-test","audit","compare"));p.add_argument("--repo",type=Path);p.add_argument("--plan",type=Path);p.add_argument("--output",type=Path,required=True);p.add_argument("--slot",type=Path);p.add_argument("--mode",choices=("rm-on","rm-off"));p.add_argument("--left",type=Path);p.add_argument("--right",type=Path);a=p.parse_args()
    if a.command=="prepare":prepare(a.repo,a.plan,a.output);return 0
    trace.need(not a.output.exists(),"refuse overwrite audit")
    r=dict(evidence_complete=False,historical_cause_resolved=False,authorizes_merge=False)
    try:r=self_test() if a.command=="self-test" else compare_required(a.left,a.right) if a.command=="compare" else audit_one(a.slot,a.mode);ok=True
    except (ValueError,TypeError,KeyError,OSError) as e:r["error"]=str(e);ok=False
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print(json.dumps(dict(evidence_complete=ok,output=str(a.output))))
    # Collection completeness is not original B success or release acceptance.
    return 0 if ok else 1
if __name__=="__main__":raise SystemExit(main())
