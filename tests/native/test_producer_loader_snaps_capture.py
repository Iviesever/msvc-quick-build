"""Admission failures specific to the loader-snaps diagnostic capture."""

import json
import unittest

from run_producer_loader_snaps_probe import parse_native_summary


def complete_summary():
    return {"sequence": 1, "kind": "summary", "classification": "INCONCLUSIVE",
        "diagnostic_only": True, "errors": 0, "ipc_cases_executed": 0,
        "events_observed": 15, "event_continuations": 15, "continuation_successes": 15,
        "startup_observed": False, "startup_frame_valid": False,
        "exit_event_observed": True, "actual_child_exit_observed": True,
        "client_exit_code": 0xc0000142, "cleanup_requested": False,
        "capture_complete": True, "structural_observation_failed": False,
        "child_image_pin_bound": True, "child_image_event_bound": True,
        "child_image_final_bound": True, "bound_hit": False,
        "wait_calls": 15, "main_wait_calls": 15, "cleanup_wait_calls": 0,
        "path_wchars_emitted": 400, "debug_string_bytes_requested": 64,
        "debug_string_bytes_read": 64,
        "A1": False, "A2": False, "A3": False, "A4": False, "A5": False,
        "deletion_enabled": False}


class LoaderCaptureAdmissionTests(unittest.TestCase):
    def encoded(self, **changes):
        value = complete_summary()
        value.update(changes)
        return json.dumps(value).encode() + b"\n"

    def test_complete_diagnostic_remains_inconclusive(self):
        self.assertEqual(parse_native_summary(self.encoded(), 3)["classification"], "INCONCLUSIVE")

    def test_complete_requires_all_three_physical_bindings(self):
        for key in ("child_image_pin_bound", "child_image_event_bound", "child_image_final_bound"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                parse_native_summary(self.encoded(**{key: False}), 3)

    def test_budget_truncation_cannot_claim_complete(self):
        with self.assertRaises(ValueError):
            parse_native_summary(self.encoded(bound_hit=True), 3)

    def test_incomplete_capture_keeps_false_binding_without_qualification(self):
        value = parse_native_summary(self.encoded(capture_complete=False,
            child_image_event_bound=False, bound_hit=True), 3)
        self.assertFalse(value["capture_complete"])
        self.assertEqual(value["classification"], "INCONCLUSIVE")

    def test_each_registered_counter_is_enforced(self):
        invalid = ({"events_observed": 1025}, {"wait_calls": 2049},
            {"main_wait_calls": 1793}, {"cleanup_wait_calls": 257},
            {"path_wchars_emitted": 1048577}, {"debug_string_bytes_requested": 1048577})
        for changes in invalid:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                parse_native_summary(self.encoded(**changes), 3)

    def test_wait_counts_and_byte_counts_must_reconcile(self):
        for changes in ({"wait_calls": 16}, {"debug_string_bytes_read": 65},
                        {"continuation_successes": 14}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                parse_native_summary(self.encoded(**changes), 3)

    def test_booleans_do_not_substitute_for_numeric_counts(self):
        with self.assertRaises(ValueError):
            parse_native_summary(self.encoded(wait_calls=True), 3)

    def test_duplicate_json_key_is_not_silently_replaced(self):
        raw = self.encoded().rstrip()[:-1] + b', "capture_complete": false}\n'
        with self.assertRaises(ValueError):
            parse_native_summary(raw, 3)

    def test_success_exit_or_product_authority_is_refused(self):
        with self.assertRaises(ValueError):
            parse_native_summary(self.encoded(), 0)
        with self.assertRaises(ValueError):
            parse_native_summary(self.encoded(A4=True), 3)


if __name__ == "__main__":
    unittest.main()
