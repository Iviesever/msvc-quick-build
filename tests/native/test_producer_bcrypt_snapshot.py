"""Fail-closed admission for diagnostic 011 and its exact DLL snapshot."""

import json
import unittest

from run_producer_bcrypt_snapshot_probe import parse_native_summary
from producer_bcrypt_snapshot import validate_snapshot
from test_producer_loader_snaps_capture import complete_summary


def snapshot_rows(data=b"MZ\x00\xffdiagnostic-exact-bytes", links=2):
    """Independent complete raw transcript; never starts a native process."""
    rows = []
    api_index = 0
    def emit(kind, **fields):
        row = dict(kind=kind, sequence=len(rows) + 1, **fields)
        rows.append(row)
        return row
    def api(operation, phase, handle, value="1", args=("0", "0", "0")):
        nonlocal api_index
        api_index += 1
        return emit("bcrypt_api", api_index=api_index, operation=operation,
            phase=phase, handle_decimal=handle, ok=True, error=0,
            value_decimal=value, argument0_decimal=args[0],
            argument1_decimal=args[1], argument2_decimal=args[2], elapsed_ms=10,
            deadline_elapsed_ms=20000 if phase in ("event_before", "pin_opened", "first", "event_after") else 5010,
            within_deadline=True)
    def identity(phase, handle):
        for cls, size in ((18, 24), (1, 24), (9, 8)):
            api("GetFileInformationByHandleEx", phase, handle,
                args=(str(cls), str(size), "0"))
        emit("bcrypt_identity", phase=phase, handle_decimal=handle,
            volume_serial_decimal="18446744073709551610", file_id_hex="a1"*16,
            size_decimal=str(len(data)), allocation_size_decimal=str((len(data)+4095)//4096*4096),
            number_of_links=links, delete_pending=False, directory=False,
            attributes=32, reparse_tag=0, valid_properties=True)
        if phase != "event_before":
            emit("bcrypt_binding", phase=phase, reference_phase="event_before",
                same_identity_and_properties=True)
    counts = {}
    def read_pass(phase, handle, after_phase):
        nonlocal api_index
        emit("bcrypt_pass", phase=phase, boundary="begin", expected_bytes=len(data),
            read_calls=0, bytes_requested=0, bytes_read=0, eof_observed=False, complete=False)
        api("SetFilePointerEx", phase, handle, value="0")
        count = 0
        for offset in range(0, len(data), 65536):
            chunk = data[offset:offset+65536]
            api_index += 1
            count += 1
            extra = {"raw_hex": chunk.hex()} if phase == "first" else {}
            emit("bcrypt_read", api_index=api_index, operation="ReadFile", phase=phase,
                handle_decimal=handle, offset=offset, bytes_requested=len(chunk),
                bytes_returned_raw=len(chunk), bytes_captured=len(chunk),
                read_count_within_bound=True, ok=True, error=0, eof_probe=False,
                compared=phase == "final", equal=phase == "final", elapsed_ms=10,
                deadline_elapsed_ms=20000 if phase == "first" else 5010,
                within_deadline=True, **extra)
        api_index += 1
        count += 1
        extra = {"raw_hex": ""} if phase == "first" else {}
        emit("bcrypt_read", api_index=api_index, operation="ReadFile", phase=phase,
            handle_decimal=handle, offset=len(data), bytes_requested=1, bytes_returned_raw=0,
            bytes_captured=0, read_count_within_bound=True, ok=True, error=0,
            eof_probe=True, compared=False, equal=False, elapsed_ms=10,
            deadline_elapsed_ms=20000 if phase == "first" else 5010,
            within_deadline=True, **extra)
        identity(after_phase, handle)
        emit("bcrypt_pass", phase=phase, boundary="end", expected_bytes=len(data),
            read_calls=count, bytes_requested=len(data)+1, bytes_read=len(data),
            eof_observed=True, complete=True)
        counts[phase] = count
    def event(index, code):
        emit("debug_event", event_index=index, event_code=code, pid=7, tid=8,
            wait_call_index=index, cleanup_phase=False)
    def continued(index, code):
        emit("debug_continue", event_index=index, pid=7, tid=8, event_code=code,
            continue_status=65538, ok=True, error=0, exactly_one_call_for_this_event=True)

    def child_identity(label, handle):
        for operation in ("FileIdInfo", "FileStandardInfo", "FileAttributeTagInfo"):
            emit("api", operation="GetFileInformationByHandleEx("+operation+")",
                 label=label, ok=True, error=0)
        emit("child_image_identity", label=label, handle_decimal=handle,
            volume_serial_decimal="1234", file_id_hex="b2"*16,
            end_of_file_decimal="498176", allocation_size_decimal="786432",
            number_of_links=1, delete_pending=False, directory=False,
            attributes=32, reparse_tag=0, read_only_observation=True)
    child_identity("child-image-before-launch", "80")
    emit("client_created", pid=7, tid=8, flags=134742022,
         inherited_handle_count=2, resumed=False)
    emit("client_job_snapshot", label="before-client-resume", ok=True,
        error=0, job_handle_decimal="90", total_processes=1,
        active_processes=1, total_terminated_processes=0)
    event(1, 3)
    emit("debug_process_created", borrowed_process_handle_decimal="81",
        borrowed_thread_handle_decimal="82", actual_pi_process_handle_decimal="83",
        image_base_decimal="140698148864000", start_address_decimal="140698149063820",
        event_process_thread_handles_owned_by_windows=True)
    child_identity("actual-create-process-image", "84")
    emit("actual_child_image_binding", same_as_held_child_image_pin=True,
        child_pin_already_bound=True, event_hfile_is_actual_image_observation=True,
        A2_or_A4_authority=False)
    emit("debug_image_file", label="create-process-image", handle_decimal="84",
        path_observed=True, path_text_emitted=True, path="C:\\capture\\child.exe")
    emit("close", label="debug-event-image-file", handle_decimal="84", ok=True, error=0)
    continued(1, 3)
    event(2, 6)
    emit("debug_dll_loaded", image_base_decimal="140716013518848", image_file_handle_decimal="88")
    path = emit("debug_image_file", label="load-dll-image", handle_decimal="88",
        path_observed=True, path_text_emitted=True, path="\\\\?\\C:\\Windows\\System32\\bcrypt.dll")
    emit("bcrypt_selected", event_index=2, pid=7, tid=8, path_sequence=path["sequence"],
        file_handle_decimal="88", image_base_decimal="140716013518848",
        cleanup_phase=False, child_image_already_bound=True)
    api("GetFileType", "event_before", "88")
    api("GetHandleInformation", "event_before", "88", value="0")
    identity("event_before", "88")
    api("ReOpenFile", "pin_opened", "88", value="89", args=("2147483648", "1", "2097152"))
    api("SetHandleInformation", "pin_opened", "89", args=("1", "0", "0"))
    api("GetFileType", "pin_opened", "89")
    api("GetHandleInformation", "pin_opened", "89", value="0")
    identity("pin_opened", "89")
    read_pass("first", "88", "event_after")
    emit("close", label="debug-event-image-file", handle_decimal="88", ok=True, error=0)
    continued(2, 6)
    event(3, 5)
    emit("debug_process_exit", exit_code=0xc0000142, diagnostic_cleanup_preceded_event=False)
    continued(3, 5)
    emit("actual_child_exit", ok=True, error=0, process_signaled=True, exit_code_raw=0xc0000142)
    emit("actual_debug_exit_comparison", debug_exit_code=0xc0000142,
        actual_exit_code=0xc0000142, equal=True)
    emit("client_job_snapshot", label="after-debug-exit-before-owned-Job-close", ok=True,
        error=0, job_handle_decimal="90", total_processes=2, active_processes=1, total_terminated_processes=0)
    emit("close", label="client-only-job", handle_decimal="90", ok=True, error=0)
    identity("pin_before_final", "89")
    read_pass("final", "89", "pin_after_final")
    api("CloseHandle", "release", "89")
    emit("bcrypt_pin_release", handle_decimal="89", attempted=True, released=True, within_deadline=True)
    state = emit("bcrypt_snapshot", candidate_count=1, selected_event_index=2,
        selected_path_sequence=path["sequence"], selected_pid=7, selected_tid=8,
        image_base_decimal="140716013518848", event_file_handle_decimal="88", pin_handle_decimal="89",
        image_bytes=len(data), api_calls=api_index, read_calls=counts["first"]+counts["final"],
        first_read_calls=counts["first"], final_read_calls=counts["final"],
        first_bytes_requested=len(data)+1, first_bytes_read=len(data),
        final_bytes_requested=len(data)+1, final_bytes_read=len(data),
        identity_snapshots=5, identity_bindings=4, first_complete=True, final_complete=True,
        first_eof=True, final_eof=True, event_hfile_closed=True, pin_opened=True,
        pin_noninherited=True, pin_released=True, failed=False, bound_hit=False,
        exception_recorded=False, output_failed=False, snapshot_complete=True,
        rows_emitted=sum(r["kind"].startswith("bcrypt_") for r in rows)+1)
    child_identity("child-image-after-drain-and-owned-Job-close", "80")
    summary = complete_summary()
    summary.update(sequence=len(rows)+1, events_observed=3, event_continuations=3,
        continuation_successes=3, wait_calls=3, main_wait_calls=3,
        bcrypt_snapshot_complete=True, bcrypt_candidate_count=1,
        bcrypt_api_calls=api_index, bcrypt_read_calls=state["read_calls"],
        bcrypt_rows_emitted=state["rows_emitted"])
    rows.append(summary)
    return rows


def encoded_rows(rows):
    for index, row in enumerate(rows, 1):
        row["sequence"] = index
    return b"".join(json.dumps(r, separators=(",", ":")).encode()+b"\n" for r in rows)


class BcryptSnapshotTests(unittest.TestCase):
    def reject_changed(self, kind, key, value, phase=None):
        rows = snapshot_rows()
        row = next(r for r in rows if r["kind"] == kind and (phase is None or r.get("phase") == phase))
        row[key] = value
        with self.assertRaises(ValueError):
            validate_snapshot(encoded_rows(rows))

    def test_complete_snapshot_preserves_binary_bytes_and_hardlinks(self):
        data = b"\x00\xff\x00MZ" * 17000
        result = validate_snapshot(encoded_rows(snapshot_rows(data)))
        self.assertEqual(result["image"], data)
        self.assertEqual(result["identity"]["number_of_links"], 2)
        self.assertFalse(result["product_qualified"])

    def test_exact_one_mib_limit_and_full_two_pass_accounting(self):
        data = b"\xab" * 1048576
        result = validate_snapshot(encoded_rows(snapshot_rows(data)))
        self.assertEqual(result["image"], data)
        self.assertEqual(result["state"]["read_calls"], 34)

    def test_whole_file_over_limit_is_refused(self):
        with self.assertRaises(ValueError):
            validate_snapshot(encoded_rows(snapshot_rows(b"x" * 1048577)))

    def test_api_sequence_gap_is_rejected(self):
        self.reject_changed("bcrypt_api", "api_index", 9)

    def test_wrong_selected_child_is_rejected(self):
        self.reject_changed("bcrypt_selected", "pid", 9)

    def test_wrong_event_handle_is_rejected(self):
        self.reject_changed("bcrypt_selected", "file_handle_decimal", "999")

    def test_other_dll_path_cannot_supply_bcrypt(self):
        rows = snapshot_rows()
        next(r for r in rows if r["kind"] == "debug_image_file" and r["label"] == "load-dll-image")["path"] = "C:\\Windows\\System32\\other.dll"
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_identity_content_or_link_changes_are_rejected(self):
        for key, value in (("file_id_hex", "00"*16), ("size_decimal", "15"),
                           ("number_of_links", 3), ("attributes", 128)):
            with self.subTest(key=key):
                self.reject_changed("bcrypt_identity", key, value, "pin_after_final")

    def test_invalid_file_state_is_rejected(self):
        for key, value in (("number_of_links", 0), ("directory", True),
                           ("delete_pending", True), ("reparse_tag", 1),
                           ("allocation_size_decimal", "-1")):
            with self.subTest(key=key):
                self.reject_changed("bcrypt_identity", key, value, "event_before")

    def test_partial_read_is_not_retried_or_admitted(self):
        self.reject_changed("bcrypt_read", "bytes_returned_raw", 1, "first")

    def test_data_offset_overlap_is_rejected(self):
        self.reject_changed("bcrypt_read", "offset", 1, "first")

    def test_raw_byte_length_mismatch_is_rejected(self):
        self.reject_changed("bcrypt_read", "raw_hex", "00", "first")

    def test_changed_second_pass_is_rejected(self):
        self.reject_changed("bcrypt_read", "equal", False, "final")

    def test_missing_eof_is_rejected(self):
        rows = snapshot_rows()
        rows.remove(next(r for r in rows if r["kind"] == "bcrypt_read" and r["phase"] == "final" and r["eof_probe"]))
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_first_eof_cannot_contain_an_extra_byte(self):
        rows = snapshot_rows()
        row = next(r for r in rows if r["kind"] == "bcrypt_read" and r["phase"] == "first" and r["eof_probe"])
        row.update(bytes_returned_raw=1, bytes_captured=1, raw_hex="ff")
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_pin_inheritance_cannot_be_hidden_by_final_flag(self):
        rows = snapshot_rows()
        next(r for r in rows if r["kind"] == "bcrypt_api" and r["operation"] == "GetHandleInformation" and r["phase"] == "pin_opened")["value_decimal"] = "1"
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_failed_close_is_rejected(self):
        self.reject_changed("bcrypt_api", "ok", False, "release")

    def test_close_after_continue_is_rejected(self):
        rows = snapshot_rows()
        close = next(r for r in rows if r["kind"] == "close" and r.get("handle_decimal") == "88")
        rows.remove(close)
        at = next(i for i,r in enumerate(rows) if r["kind"] == "debug_continue" and r["event_index"] == 2)
        rows.insert(at+1, close)
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_expired_api_cannot_claim_timely(self):
        self.reject_changed("bcrypt_api", "elapsed_ms", 25000)

    def test_failed_identity_binding_is_rejected(self):
        self.reject_changed("bcrypt_binding", "same_identity_and_properties", False)

    def test_snapshot_count_mismatch_is_rejected(self):
        self.reject_changed("bcrypt_snapshot", "api_calls", 2)

    def test_integer_bool_and_unknown_fields_are_rejected(self):
        self.reject_changed("bcrypt_read", "bytes_captured", True)
        self.reject_changed("bcrypt_selected", "unknown_field", 1)

    def test_duplicate_candidate_is_not_a_complete_single_snapshot(self):
        self.reject_changed("bcrypt_snapshot", "candidate_count", 2)

    def test_duplicate_json_key_and_global_sequence_are_rejected(self):
        raw = encoded_rows(snapshot_rows())
        with self.assertRaises(ValueError): validate_snapshot(raw.replace(b'"pid":7', b'"pid":7,"pid":9', 1))
        with self.assertRaises(ValueError): validate_snapshot(raw.replace(b'"sequence":1,', b'"sequence":2,', 1))

    def test_authority_or_incomplete_summary_is_rejected(self):
        for key,value in (("A2",True),("capture_complete",False),("bcrypt_snapshot_complete",False)):
            with self.subTest(key=key): self.reject_changed("summary",key,value)


    def test_debug_exit_payload_and_actual_comparison_are_required(self):
        for kind, key, value in (("debug_process_exit", "exit_code", 0),
                ("actual_debug_exit_comparison", "equal", False),
                ("actual_debug_exit_comparison", "debug_exit_code", 0),
                ("actual_child_exit", "error", 5)):
            with self.subTest(kind=kind, key=key): self.reject_changed(kind, key, value)

    def test_client_created_is_actual_process_identity(self):
        self.reject_changed("client_created", "pid", 999)
        self.reject_changed("client_created", "tid", 999)
        rows = [r for r in snapshot_rows() if r["kind"] != "client_created"]
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_child_exe_physical_identity_is_required_and_reconciled(self):
        for label in ("child-image-before-launch", "actual-create-process-image",
                      "child-image-after-drain-and-owned-Job-close"):
            with self.subTest(label=label):
                rows = snapshot_rows()
                next(r for r in rows if r["kind"] == "child_image_identity" and r["label"] == label)["file_id_hex"] = "cc"*16
                with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_final_deadline_cannot_allocate_more_than_cleanup_budget(self):
        rows = snapshot_rows()
        for r in rows:
            if r["kind"] in ("bcrypt_api", "bcrypt_read") and r["phase"] in ("pin_before_final", "final", "pin_after_final", "release"):
                r["deadline_elapsed_ms"] = 25000
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_collector_elapsed_time_cannot_move_backwards(self):
        self.reject_changed("bcrypt_api", "elapsed_ms", 9, "pin_opened")

    def test_raw_metadata_whitespace_is_counted(self):
        raw = encoded_rows(snapshot_rows()).replace(b'"kind":"bcrypt_api",',
            b'"kind":"bcrypt_api",'+b' '*1500, 1)
        with self.assertRaises(ValueError): validate_snapshot(raw)

    def test_exponent_overflow_is_nonfinite_json(self):
        raw = encoded_rows(snapshot_rows()).replace(b'"kind":"debug_process_exit",',
            b'"kind":"debug_process_exit","unused":1e999,', 1)
        with self.assertRaises(ValueError): validate_snapshot(raw)

    def test_owned_job_close_error_and_handle_are_reconciled(self):
        for key, value in (("error", 5), ("handle_decimal", "999")):
            with self.subTest(key=key):
                rows = snapshot_rows()
                next(r for r in rows if r["kind"] == "close" and r["label"] == "client-only-job")[key] = value
                with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_load_dll_continue_uses_the_original_status(self):
        rows = snapshot_rows()
        next(r for r in rows if r["kind"] == "debug_continue" and r["event_code"] == 6)["continue_status"] = 0
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_all_final_identity_queries_follow_owned_job_close(self):
        rows = snapshot_rows()
        queries = [r for r in rows if r["kind"] == "bcrypt_api" and r["phase"] == "pin_before_final"]
        for row in queries: rows.remove(row)
        at = next(i for i,r in enumerate(rows) if r["kind"] == "close" and r["label"] == "client-only-job")
        rows[at:at] = queries
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))

    def test_pin_cannot_be_closed_during_retained_lifetime(self):
        rows = snapshot_rows()
        at = next(i for i,r in enumerate(rows) if r["kind"] == "client_job_snapshot" and r["label"] == "after-debug-exit-before-owned-Job-close")
        rows.insert(at, dict(kind="close", label="unexpected-pin-close",
                            handle_decimal="89", ok=True, error=0))
        with self.assertRaises(ValueError): validate_snapshot(encoded_rows(rows))


class BcryptSummaryTests(unittest.TestCase):
    def raw(self, **changes):
        row = complete_summary()
        row.update(bcrypt_candidate_count=0, bcrypt_api_calls=0,
                   bcrypt_read_calls=0, bcrypt_rows_emitted=0)
        row.update(changes)
        return json.dumps(row).encode() + b"\n"

    def test_old_complete_summary_cannot_supply_new_snapshot_authority(self):
        with self.assertRaises(ValueError):
            parse_native_summary(self.raw(), 3)

    def test_overall_complete_requires_snapshot_complete(self):
        with self.assertRaises(ValueError):
            parse_native_summary(self.raw(bcrypt_snapshot_complete=False), 3)

    def test_incomplete_diagnostic_remains_inconclusive_without_snapshot(self):
        result = parse_native_summary(self.raw(capture_complete=False,
            bcrypt_snapshot_complete=False), 3)
        self.assertFalse(result["capture_complete"])
        self.assertFalse(result["bcrypt_snapshot_complete"])
        self.assertEqual(result["classification"], "INCONCLUSIVE")

    def test_incomplete_counter_types_and_bounds_remain_strict(self):
        for key, value in (("bcrypt_candidate_count", True), ("bcrypt_api_calls", 59),
                ("bcrypt_read_calls", -1), ("bcrypt_rows_emitted", 101)):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    parse_native_summary(self.raw(capture_complete=False,
                        bcrypt_snapshot_complete=False, **{key:value}), 3)


if __name__ == "__main__":
    unittest.main()
