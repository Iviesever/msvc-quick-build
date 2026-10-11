"""Pure, fail-closed validation of diagnostic 011's actual-event DLL bytes.

The second read's byte comparison is an executed native observation: it does
not emit a second byte copy. This validator reconciles that observation with
the frozen source protocol, and independently reconstructs/hash-binds only the
first-pass bytes. A raw diagnostic admission never changes the outer Job gate.
"""

from __future__ import annotations

import hashlib
import json
import math
import re

MAX_IMAGE_BYTES = 1048576
CHUNK_BYTES = 65536
MAX_CAPTURE_BYTES = 33554432
MAX_ROWS = 16384
MAX_COLLECTOR_ROWS = 100

API_FIELDS = set("api_index operation phase handle_decimal ok error value_decimal argument0_decimal argument1_decimal argument2_decimal elapsed_ms deadline_elapsed_ms within_deadline".split())
READ_FIELDS = set("api_index operation phase handle_decimal offset bytes_requested bytes_returned_raw bytes_captured read_count_within_bound ok error eof_probe compared equal elapsed_ms deadline_elapsed_ms within_deadline".split())
IDENTITY_FIELDS = set("phase handle_decimal volume_serial_decimal file_id_hex size_decimal allocation_size_decimal number_of_links delete_pending directory attributes reparse_tag valid_properties".split())
PASS_FIELDS = set("phase boundary expected_bytes read_calls bytes_requested bytes_read eof_observed complete".split())
SELECTED_FIELDS = set("event_index pid tid path_sequence file_handle_decimal image_base_decimal cleanup_phase child_image_already_bound".split())
STATE_FIELDS = set("candidate_count selected_event_index selected_path_sequence selected_pid selected_tid image_base_decimal event_file_handle_decimal pin_handle_decimal image_bytes api_calls read_calls first_read_calls final_read_calls first_bytes_requested first_bytes_read final_bytes_requested final_bytes_read identity_snapshots identity_bindings first_complete final_complete first_eof final_eof event_hfile_closed pin_opened pin_noninherited pin_released failed bound_hit exception_recorded output_failed snapshot_complete rows_emitted".split())


def _need(condition, message):
    if not condition:
        raise ValueError(message)


def _uint(value, maximum=0xffffffffffffffff):
    _need(type(value) is int and 0 <= value <= maximum, "Invalid strict unsigned integer")
    return value


def _decimal(value):
    _need(isinstance(value, str) and re.fullmatch(r"0|[1-9][0-9]{0,19}", value) is not None,
          "Invalid canonical unsigned decimal string")
    _need(int(value) <= 0xffffffffffffffff, "Decimal exceeds UInt64")
    return int(value)


def _same(actual, expected, label):
    _need(type(actual) is type(expected) and actual == expected, label)


def parse_rows(raw: bytes) -> list[dict]:
    _need(type(raw) is bytes and 0 < len(raw) <= MAX_CAPTURE_BYTES, "Raw capture byte bound")
    _need(raw.endswith(b"\n"), "Raw capture lacks complete final line")
    byte_lines = raw.split(b"\n")[:-1]
    lines = [line.decode("utf-8", errors="strict") for line in byte_lines]
    _need(0 < len(lines) <= MAX_ROWS and all(lines), "Raw capture row bound or blank row")
    def unique(pairs):
        value = {}
        for key, item in pairs:
            _need(key not in value, "Duplicate JSON key")
            value[key] = item
        return value
    def invalid_constant(value):
        raise ValueError("Non-finite JSON constant: " + value)
    def finite_float(value):
        number = float(value)
        _need(math.isfinite(number), "Non-finite JSON exponent")
        return number
    rows = [json.loads(line, object_pairs_hook=unique, parse_constant=invalid_constant,
                       parse_float=finite_float) for line in lines]
    for index, row in enumerate(rows, 1):
        _need(type(row) is dict and type(row.get("sequence")) is int
              and row["sequence"] == index and type(row.get("kind")) is str,
              "Raw capture must be exact consecutive JSONL objects")
        if row["kind"].startswith("bcrypt_"):
            payload = 0
            if "raw_hex" in row:
                value = row["raw_hex"]
                _need(type(value) is str and re.fullmatch(r"[0-9a-f]*", value) is not None,
                      "Noncanonical raw hexadecimal payload")
                literal = b'"raw_hex":"' + value.encode("ascii") + b'"'
                _need(byte_lines[index-1].count(literal) == 1, "Hex payload is not literal source encoding")
                if row.get("eof_probe") is False:
                    payload = len(value)
            _need(len(byte_lines[index-1]) + 1 - payload <= 1024,
                  "Raw collector metadata exceeds fixed allowance")
    return rows


def _basename_is_bcrypt(path):
    return (type(path) is str and "\x00" not in path and len(path) < 4096
            and re.split(r"[\\/]", path)[-1].lower() == "bcrypt.dll")


class _Transcript:
    def __init__(self, rows):
        self.rows = [r for r in rows if r["kind"].startswith("bcrypt_")]
        _need(0 < len(self.rows) <= MAX_COLLECTOR_ROWS, "Collector row bound")
        self.cursor = 0
        self.api_index = 0
        self.final_deadline = None
        self.last_elapsed = 0

    def row(self, kind, fields):
        _need(self.cursor < len(self.rows), "Missing collector record: " + kind)
        row = self.rows[self.cursor]
        self.cursor += 1
        _need(row["kind"] == kind and set(row) == fields | {"kind", "sequence"},
              "Unexpected collector order/schema: " + kind)
        return row

    def timing(self, row, phase):
        _same(row["within_deadline"], True, "Late collector operation")
        elapsed = _uint(row["elapsed_ms"], 25000)
        deadline = _uint(row["deadline_elapsed_ms"], 25000)
        _need(elapsed >= self.last_elapsed, "Collector clock moved backwards")
        self.last_elapsed = elapsed
        _need(elapsed < deadline, "Operation completed at/after deadline")
        if phase in ("event_before", "pin_opened", "first", "event_after"):
            _same(deadline, 20000, "First-pass deadline changed")
        else:
            _need(deadline - elapsed <= 5000, "Final phase exceeds shared cleanup budget")
            if self.final_deadline is None:
                self.final_deadline = deadline
            _same(deadline, self.final_deadline, "Final deadline was reset")

    def operation(self, row, name, phase, handle):
        self.api_index += 1
        _same(row["api_index"], self.api_index, "API index is not consecutive")
        _need(self.api_index <= 58, "Principal API budget exhausted")
        _same(row["operation"], name, "Wrong API")
        _same(row["phase"], phase, "Wrong API phase")
        _same(row["handle_decimal"], handle, "Wrong held file handle")
        _same(row["ok"], True, "Failed API")
        _same(row["error"], 0, "API error retained in complete transcript")
        self.timing(row, phase)

    def api(self, name, phase, handle, expected_value="1", args=("0", "0", "0")):
        row = self.row("bcrypt_api", API_FIELDS)
        self.operation(row, name, phase, handle)
        for index, value in enumerate(args):
            _same(row[f"argument{index}_decimal"], value, "Wrong API argument")
        _decimal(row["value_decimal"])
        if expected_value is not None:
            _same(row["value_decimal"], expected_value, "Wrong API outcome")
        return row


def validate_snapshot(raw: bytes) -> dict:
    """Return a complete source-protocol-bound snapshot, never qualification."""
    from run_producer_loader_snaps_probe import parse_native_summary as parse_frozen_summary
    rows = parse_rows(raw)
    summary = parse_frozen_summary(raw, 3)
    _same(summary.get("capture_complete"), True, "Original diagnostic capture incomplete")
    _same(summary.get("bcrypt_snapshot_complete"), True, "DLL snapshot not complete")
    transcript = _Transcript(rows)
    selected = transcript.row("bcrypt_selected", SELECTED_FIELDS)
    for key in ("event_index", "pid", "tid", "path_sequence"):
        _need(_uint(selected[key], 0xffffffff) > 0, "Missing actual event identity")
    _same(selected["cleanup_phase"], False, "Initial acquisition during cleanup")
    _same(selected["child_image_already_bound"], True, "Actual child image not yet bound")
    event_handle = selected["file_handle_decimal"]
    _need(0 < _decimal(event_handle) < 0xffffffffffffffff, "Invalid event hFile")
    _need(_decimal(selected["image_base_decimal"]) > 0, "Missing DLL base")

    events = [r for r in rows if r["kind"] == "debug_event"]
    continues = [r for r in rows if r["kind"] == "debug_continue"]
    _same(len(events), summary["events_observed"], "Event count mismatch")
    _same(len(continues), len(events), "Missing/duplicate continuation")
    for index, event in enumerate(events, 1):
        _same(event.get("event_index"), index, "Event indices not consecutive")
        _same(event.get("pid"), selected["pid"], "Foreign process event")
        matches = [r for r in continues if r.get("event_index") == index]
        _same(len(matches), 1, "Missing or repeated event continuation")
        continued = matches[0]
        for key in ("event_index", "pid", "tid", "event_code"):
            _same(continued.get(key), event.get(key), "Continuation identity mismatch")
        _same(continued.get("ok"), True, "Continuation failed")
        _same(continued.get("error"), 0, "Continuation error")
        _same(continued.get("exactly_one_call_for_this_event"), True, "Continuation authority mismatch")
        if event.get("event_code") == 1:
            _need(type(continued.get("continue_status")) is int
                  and continued["continue_status"] in (0x10002, 0x80010001), "Invalid exception continuation status")
        else:
            _same(continued.get("continue_status"), 0x10002, "Nonexception event continuation status changed")
        next_sequence = events[index]["sequence"] if index < len(events) else len(rows)+1
        _need(event["sequence"] < continued["sequence"] < next_sequence, "Continuation order")

    chosen = [e for e in events if e["event_index"] == selected["event_index"]]
    _same(len(chosen), 1, "Missing selected event")
    event = chosen[0]
    _same(event.get("event_code"), 6, "Selected event is not LOAD_DLL")
    _same(event.get("tid"), selected["tid"], "Selected TID mismatch")
    _same(event.get("cleanup_phase"), False, "Selected event is cleanup")
    continued = next(r for r in continues if r["event_index"] == selected["event_index"])
    window = rows[event["sequence"]:continued["sequence"]-1]
    loads = [r for r in window if r["kind"] == "debug_dll_loaded"]
    _same(len(loads), 1, "Missing selected LOAD_DLL metadata")
    _same(loads[0].get("image_base_decimal"), selected["image_base_decimal"], "DLL base mismatch")
    _same(loads[0].get("image_file_handle_decimal"), event_handle, "Event object mismatch")
    _need(selected["path_sequence"] <= len(rows), "Path reference outside transcript")
    path = rows[selected["path_sequence"]-1]
    _need(path in window and path["kind"] == "debug_image_file"
          and path["sequence"] < selected["sequence"] < continued["sequence"], "Path/event order mismatch")
    _same(path.get("handle_decimal"), event_handle, "Path object mismatch")
    _same(path.get("path_observed"), True, "Path unavailable")
    _same(path.get("path_text_emitted"), True, "Path text missing")
    _need(_basename_is_bcrypt(path.get("path")), "Selected basename is not bcrypt.dll")
    _same(sum(r["kind"] == "debug_image_file" and r.get("label") == "load-dll-image"
              and _basename_is_bcrypt(r.get("path")) for r in rows), 1, "Multiple bcrypt candidates")
    create_events = [e for e in events if e.get("event_code") == 3]
    _need(len(create_events) == 1 and create_events[0]["sequence"] < event["sequence"], "Actual child create not bound before DLL")
    clients = [r for r in rows if r["kind"] == "client_created"]
    _need(len(clients) == 1 and clients[0]["sequence"] < create_events[0]["sequence"],
          "Missing unique actual process creation")
    client = clients[0]
    _same(client.get("pid"), selected["pid"], "Selected process differs from actual created client")
    _same(client.get("tid"), create_events[0].get("tid"), "Create event thread differs from actual PI thread")
    _same(client.get("resumed"), False, "Client was not created suspended")
    _same(client.get("inherited_handle_count"), 2, "Client inherited handle set changed")
    _same(client.get("flags"), 134742022, "Restricted client creation flags changed")
    create_continue = next(r for r in continues if r["event_index"] == create_events[0]["event_index"])
    create_window = rows[create_events[0]["sequence"]:create_continue["sequence"]-1]
    created_metadata = [r for r in create_window if r["kind"] == "debug_process_created"]
    _need(len(created_metadata) == 1 and created_metadata[0].get("event_process_thread_handles_owned_by_windows") is True,
          "Missing actual debug process metadata")
    for key in ("borrowed_process_handle_decimal", "borrowed_thread_handle_decimal", "actual_pi_process_handle_decimal"):
        _need(0 < _decimal(created_metadata[0].get(key)) < 0xffffffffffffffff, "Invalid process/thread handle")

    exe_identities = {}
    exe_reference = None
    exe_fields = "volume_serial_decimal file_id_hex end_of_file_decimal allocation_size_decimal number_of_links delete_pending directory attributes reparse_tag read_only_observation".split()
    for label in ("child-image-before-launch", "actual-create-process-image", "child-image-after-drain-and-owned-Job-close"):
        matches = [r for r in rows if r["kind"] == "child_image_identity" and r.get("label") == label]
        _same(len(matches), 1, "Missing/duplicate child EXE physical identity")
        item = matches[0]
        _need(0 < _decimal(item.get("handle_decimal")) < 0xffffffffffffffff, "Invalid child image handle")
        _decimal(item.get("volume_serial_decimal"))
        _decimal(item.get("allocation_size_decimal"))
        _need(0 < _decimal(item.get("end_of_file_decimal")) <= 8388608, "Child EXE byte bound")
        _need(type(item.get("file_id_hex")) is str and re.fullmatch(r"[0-9a-f]{32}", item["file_id_hex"]) is not None,
              "Invalid child EXE FileIdInfo")
        for key, expected in dict(number_of_links=1, delete_pending=False, directory=False,
                                  reparse_tag=0, read_only_observation=True).items():
            _same(item.get(key), expected, "Invalid child EXE physical property")
        _need(_uint(item.get("attributes"), 0xffffffff) & (0x10 | 0x400) == 0, "Invalid child EXE attributes")
        for operation in ("FileIdInfo", "FileStandardInfo", "FileAttributeTagInfo"):
            observations = [r for r in rows[:item["sequence"]-1] if r["kind"] == "api" and r.get("label") == label
                            and r.get("operation") == "GetFileInformationByHandleEx("+operation+")"]
            _need(len(observations) == 1 and observations[0].get("ok") is True,
                  "Child EXE identity query missing or failed")
            _same(observations[0].get("error"), 0, "Child EXE identity query error")
        properties = {k:item[k] for k in exe_fields}
        if exe_reference is None: exe_reference = properties
        else: _same(properties, exe_reference, "Actual/retained child EXE identity differs")
        exe_identities[label] = item
    initial_exe = exe_identities["child-image-before-launch"]
    actual_exe = exe_identities["actual-create-process-image"]
    final_exe = exe_identities["child-image-after-drain-and-owned-Job-close"]
    _need(initial_exe["sequence"] < client["sequence"] and actual_exe in create_window,
          "Child EXE identity observation order")
    _same(final_exe["handle_decimal"], initial_exe["handle_decimal"], "Held child EXE pin changed")
    create_paths = [r for r in create_window if r["kind"] == "debug_image_file" and r.get("label") == "create-process-image"]
    _need(len(create_paths) == 1 and create_paths[0].get("handle_decimal") == actual_exe["handle_decimal"]
          and create_paths[0].get("path_observed") is True, "Actual CREATE_PROCESS hFile missing")
    create_closes = [r for r in create_window if r["kind"] == "close" and r.get("label") == "debug-event-image-file"
                     and r.get("handle_decimal") == actual_exe["handle_decimal"]]
    _need(len(create_closes) == 1 and create_closes[0]["sequence"] > create_paths[0]["sequence"]
          and create_closes[0].get("ok") is True, "Actual CREATE_PROCESS hFile release missing")
    _same(create_closes[0].get("error"), 0, "Actual CREATE_PROCESS hFile close error")
    bindings = [r for r in rows[:event["sequence"]-1] if r["kind"] == "actual_child_image_binding"]
    _need(len(bindings) == 1 and bindings[0].get("same_as_held_child_image_pin") is True
          and bindings[0].get("child_pin_already_bound") is True
          and bindings[0].get("event_hfile_is_actual_image_observation") is True
          and bindings[0].get("A2_or_A4_authority") is False, "Missing actual child physical image binding")
    _need(actual_exe["sequence"] < bindings[0]["sequence"] < create_closes[0]["sequence"],
          "Actual child image binding occurs outside CREATE_PROCESS event")

    reference = None
    def identity(phase, handle):
        nonlocal reference
        for cls, size in ((18, 24), (1, 24), (9, 8)):
            transcript.api("GetFileInformationByHandleEx", phase, handle,
                           args=(str(cls), str(size), "0"))
        row = transcript.row("bcrypt_identity", IDENTITY_FIELDS)
        _same(row["phase"], phase, "Identity phase mismatch")
        _same(row["handle_decimal"], handle, "Identity handle mismatch")
        _decimal(row["volume_serial_decimal"])
        size = _decimal(row["size_decimal"])
        _need(0 < size <= MAX_IMAGE_BYTES, "Whole DLL size outside limit")
        _decimal(row["allocation_size_decimal"])
        _need(type(row["file_id_hex"]) is str and re.fullmatch(r"[0-9a-f]{32}", row["file_id_hex"]) is not None,
              "Invalid FileIdInfo representation")
        _need(_uint(row["number_of_links"], 0xffffffff) > 0, "Nonpositive link count")
        _same(row["delete_pending"], False, "Pending deletion")
        _same(row["directory"], False, "Directory file object")
        _need(_uint(row["attributes"], 0xffffffff) & (0x10 | 0x400) == 0, "Directory/reparse attributes")
        _same(row["reparse_tag"], 0, "Reparse tag")
        _same(row["valid_properties"], True, "Invalid physical properties")
        properties = {k:v for k,v in row.items() if k not in ("kind", "sequence", "phase", "handle_decimal")}
        if reference is None:
            reference = properties
        else:
            _same(properties, reference, "Physical identity/properties changed")
            binding = transcript.row("bcrypt_binding", {"phase", "reference_phase", "same_identity_and_properties"})
            _same(binding["phase"], phase, "Binding phase mismatch")
            _same(binding["reference_phase"], "event_before", "Wrong binding reference")
            _same(binding["same_identity_and_properties"], True, "Identity binding failed")
        return row

    transcript.api("GetFileType", "event_before", event_handle)
    flags = transcript.api("GetHandleInformation", "event_before", event_handle, expected_value=None)
    _need(_decimal(flags["value_decimal"]) <= 0xffffffff, "Handle flags exceed DWORD")
    identity("event_before", event_handle)
    reopened = transcript.api("ReOpenFile", "pin_opened", event_handle, expected_value=None,
                              args=("2147483648", "1", "2097152"))
    pin = reopened["value_decimal"]
    _need(0 < _decimal(pin) < 0xffffffffffffffff and pin != event_handle, "Invalid separate pin")
    transcript.api("SetHandleInformation", "pin_opened", pin, args=("1", "0", "0"))
    transcript.api("GetFileType", "pin_opened", pin)
    flags = transcript.api("GetHandleInformation", "pin_opened", pin, expected_value=None)
    _need(_decimal(flags["value_decimal"]) <= 0xffffffff and int(flags["value_decimal"]) & 1 == 0, "Pin inherited")
    identity("pin_opened", pin)
    size = int(reference["size_decimal"])
    chunks = []
    def read_pass(phase, handle, after_phase):
        start = transcript.row("bcrypt_pass", PASS_FIELDS)
        for key, value in {"phase":phase, "boundary":"begin", "expected_bytes":size,
                "read_calls":0, "bytes_requested":0, "bytes_read":0, "eof_observed":False, "complete":False}.items():
            _same(start[key], value, "Pass begin accounting")
        transcript.api("SetFilePointerEx", phase, handle, expected_value="0")
        requests = [(offset, min(CHUNK_BYTES, size-offset), False) for offset in range(0, size, CHUNK_BYTES)]
        requests.append((size, 1, True))
        for offset, requested, eof in requests:
            fields = READ_FIELDS | ({"raw_hex"} if phase == "first" else set())
            row = transcript.row("bcrypt_read", fields)
            transcript.operation(row, "ReadFile", phase, handle)
            for key, expected in {"offset":offset, "bytes_requested":requested,
                    "bytes_returned_raw":0 if eof else requested, "bytes_captured":0 if eof else requested,
                    "read_count_within_bound":True, "eof_probe":eof,
                    "compared":phase == "final" and not eof, "equal":phase == "final" and not eof}.items():
                _same(row[key], expected, "Read/EOF/byte comparison mismatch: " + key)
            if phase == "first":
                value = row["raw_hex"]
                _need(type(value) is str and len(value) == 2*row["bytes_captured"]
                      and re.fullmatch(r"[0-9a-f]*", value) is not None, "Malformed captured bytes")
                if not eof:
                    chunks.append(bytes.fromhex(value))
        identity(after_phase, handle)
        end = transcript.row("bcrypt_pass", PASS_FIELDS)
        for key, expected in {"phase":phase, "boundary":"end", "expected_bytes":size,
                "read_calls":len(requests), "bytes_requested":size+1,
                "bytes_read":size, "eof_observed":True, "complete":True}.items():
            _same(end[key], expected, "Pass end accounting mismatch")
        return end, len(requests)
    first_end, count = read_pass("first", event_handle, "event_after")
    closes = [r for r in window if r["kind"] == "close" and r.get("label") == "debug-event-image-file"
              and r.get("handle_decimal") == event_handle]
    _need(len(closes) == 1 and first_end["sequence"] < closes[0]["sequence"] < continued["sequence"]
          and closes[0].get("ok") is True and type(closes[0].get("error")) is int and closes[0]["error"] == 0,
          "Event hFile close must succeed before exactly one continuation")
    exits = [e for e in events if e.get("event_code") == 5]
    _need(len(exits) == 1 and exits[0]["sequence"] > continued["sequence"], "Missing actual child exit")
    exit_continue = next(r for r in continues if r["event_index"] == exits[0]["event_index"])
    exit_results = [r for r in rows if r["kind"] == "actual_child_exit"]
    _need(len(exit_results) == 1 and exit_results[0].get("ok") is True
          and exit_results[0].get("process_signaled") is True
          and exit_results[0]["sequence"] > exit_continue["sequence"], "Actual process exit not observed after continuation")
    _same(exit_results[0].get("exit_code_raw"), summary["client_exit_code"], "Actual exit status mismatch")
    _same(exit_results[0].get("error"), 0, "Actual exit query error")
    exit_payloads = [r for r in rows[exits[0]["sequence"]:exit_continue["sequence"]-1] if r["kind"] == "debug_process_exit"]
    _same(len(exit_payloads), 1, "Missing/duplicate debug exit payload")
    _same(exit_payloads[0].get("exit_code"), summary["client_exit_code"], "Debug exit payload differs from actual status")
    comparisons = [r for r in rows if r["kind"] == "actual_debug_exit_comparison"]
    _need(len(comparisons) == 1 and comparisons[0]["sequence"] > exit_results[0]["sequence"],
          "Missing actual/debug exit reconciliation")
    for key, value in dict(debug_exit_code=summary["client_exit_code"],
                           actual_exit_code=summary["client_exit_code"], equal=True).items():
        _same(comparisons[0].get(key), value, "Actual/debug exit comparison failed")
    jobs = [r for r in rows if r["kind"] == "close" and r.get("label") == "client-only-job"]
    _need(len(jobs) == 1 and jobs[0].get("ok") is True and jobs[0]["sequence"] > comparisons[0]["sequence"],
          "Owned Job close order incomplete")
    _same(jobs[0].get("error"), 0, "Owned Job close error")
    for label in ("before-client-resume", "after-debug-exit-before-owned-Job-close"):
        snapshots = [r for r in rows if r["kind"] == "client_job_snapshot" and r.get("label") == label]
        _need(len(snapshots) == 1 and snapshots[0].get("ok") is True,
              "Missing actual owned Job snapshot")
        snapshot = snapshots[0]
        _same(snapshot.get("error"), 0, "Owned Job snapshot error")
        _same(snapshot.get("job_handle_decimal"), jobs[0].get("handle_decimal"), "Owned Job handle changed")
        for key in ("total_processes", "active_processes", "total_terminated_processes"):
            _uint(snapshot.get(key), 0xffffffff)
        if label == "before-client-resume":
            _need(client["sequence"] < snapshot["sequence"] < create_events[0]["sequence"], "Pre-resume Job snapshot order")
        else:
            _need(comparisons[0]["sequence"] < snapshot["sequence"] < jobs[0]["sequence"], "Final owned Job snapshot order")
    _need(transcript.cursor < len(transcript.rows)
          and transcript.rows[transcript.cursor]["sequence"] > jobs[0]["sequence"],
          "Final identity queries preceded owned Job close")
    before_final = identity("pin_before_final", pin)
    _need(before_final["sequence"] > jobs[0]["sequence"], "Final read preceded owned Job close")
    final_end, final_count = read_pass("final", pin, "pin_after_final")
    close = transcript.api("CloseHandle", "release", pin)
    _need(close["sequence"] > final_end["sequence"], "Pin released before final read")
    _need(not any(r["kind"] == "close" and r.get("handle_decimal") == pin
                  and reopened["sequence"] < r["sequence"] < close["sequence"] for r in rows),
          "Contradictory close of retained pin")
    release = transcript.row("bcrypt_pin_release", {"handle_decimal", "attempted", "released", "within_deadline"})
    for key, expected in {"handle_decimal":pin, "attempted":True, "released":True, "within_deadline":True}.items():
        _same(release[key], expected, "Pin release incomplete")
    state = transcript.row("bcrypt_snapshot", STATE_FIELDS)
    _need(final_exe["sequence"] > state["sequence"], "Final child EXE identity preceded DLL snapshot closure")
    expected_state = dict(candidate_count=1, selected_event_index=selected["event_index"],
        selected_path_sequence=selected["path_sequence"], selected_pid=selected["pid"], selected_tid=selected["tid"],
        image_base_decimal=selected["image_base_decimal"], event_file_handle_decimal=event_handle,
        pin_handle_decimal=pin, image_bytes=size, api_calls=transcript.api_index,
        read_calls=count+final_count, first_read_calls=count, final_read_calls=final_count,
        first_bytes_requested=size+1, first_bytes_read=size, final_bytes_requested=size+1, final_bytes_read=size,
        identity_snapshots=5, identity_bindings=4, first_complete=True, final_complete=True,
        first_eof=True, final_eof=True, event_hfile_closed=True, pin_opened=True, pin_noninherited=True,
        pin_released=True, failed=False, bound_hit=False, exception_recorded=False, output_failed=False,
        snapshot_complete=True, rows_emitted=len(transcript.rows))
    for key, expected in expected_state.items():
        _same(state[key], expected, "Snapshot closure count/state mismatch: " + key)
    _same(transcript.cursor, len(transcript.rows), "Extra collector records after closure")
    for key, expected in dict(bcrypt_candidate_count=1, bcrypt_api_calls=transcript.api_index,
            bcrypt_read_calls=count+final_count, bcrypt_rows_emitted=len(transcript.rows)).items():
        _same(summary.get(key), expected, "Native summary collector mismatch")
    image = b"".join(chunks)
    _same(len(image), size, "Reconstructed image length mismatch")
    return {"image": image, "sha256": hashlib.sha256(image).hexdigest(),
            "identity": reference, "selected": selected, "state": state,
            "raw_sha256": hashlib.sha256(raw).hexdigest(), "raw_rows": len(rows),
            "product_qualified": False,
            "second_pass_bytes_source": "native complete comparison records; no second raw byte copy"}
