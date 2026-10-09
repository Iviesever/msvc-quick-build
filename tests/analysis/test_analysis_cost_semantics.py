"""Contract tests for fixed cost-output semantics; never execute a producer.

The prepared literals below describe the frozen historical harness shape, not
new measurements. Source references: cost_harness.cpp SHA-256
d0b7d1025a2d2d010fc2df562977ca3ec96a478ae8960672503c9847d6ac3632,
lines 151-156, 174-212 and 228-284; cost-preparation/manifest.json SHA-256
e6d5e0202e708290f63c4c6ca41d24a73d567d282bfc96653eacf237aa9bf6ea.
Allocation, time and HWM values used here are deliberately fixed substitutes.
"""

import copy
import hashlib
import importlib
import json
import unittest


try:
    COST = importlib.import_module("analysis_cost_semantics")
    IMPORT_FAILURE = None
except ModuleNotFoundError as error:
    COST = None
    IMPORT_FAILURE = str(error)


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1
OPERATIONS = ("live", "project", "encode", "decode", "model", "chain")
ALLOCATION_FIELDS = (
    "new_calls", "requested_bytes", "requested_peak_live",
    "requested_live_at_return", "largest_request", "key_callbacks",
)

# case: (record count, wire byte count, normal-mode check count).
PREPARED_LITERALS = {
    "exe": (1, 3405, 115),
    "dll": (1, 3429, 118),
    "static": (1, 3174, 106),
    "reuse_save_failure": (2, 6878, 203),
    "snapshot_128k": (1, 135422, 115),
    "history_256": (256, 304248, 8207),
    "text_7m": (1, 7343780, 129),
    "items_60000": (1, 363373, 115),
    "bad_text_tail": (1, 7343781, 129),
    "bad_items_tail": (1, 363374, 115),
}

# Expected sentinel values for live/project/encode/decode/model/chain. These
# explicit fixture values are not a general formula for arbitrary codec models.
CONSUMED_LITERALS = {
    "exe": (2, 2, 3454, 2, 2, 3460),
    "dll": (2, 2, 3478, 2, 2, 3484),
    "static": (2, 2, 3223, 2, 2, 3229),
    "reuse_save_failure": (4, 3, 6927, 3, 4, 6937),
    "snapshot_128k": (2, 2, 135471, 2, 2, 135477),
    "history_256": (512, 257, 304301, 257, 512, 305327),
    "text_7m": (2, 2, 7343829, 2, 2, 7343835),
    "items_60000": (2, 2, 363422, 2, 2, 363428),
}


def prepared_literal(case="exe", instrumented=False):
    records, wire_bytes, checks = PREPARED_LITERALS[case]
    return {
        "kind": "prepared", "case": case, "records": records,
        "wire_bytes": wire_bytes, "public_preparation_calls": 5,
        "model_equal": True, "checks": checks + (3 if instrumented else 0),
    }


def consumed_literal(case="exe", operation="live"):
    if case.startswith("bad_"):
        return PREPARED_LITERALS[case][1] - 1
    return CONSUMED_LITERALS[case][OPERATIONS.index(operation)]


def contract_arguments(case="exe", operation="live", instrumented=False):
    return {
        "call_id": "fixed-cost-call", "case": case, "operation": operation,
        "instrumented": instrumented,
        "prepared": prepared_literal(case, instrumented),
        "expected_consumed": consumed_literal(case, operation),
    }


def output_rows(arguments):
    rows = [copy.deepcopy(arguments["prepared"])]
    for trial in range(5):
        instrumented = arguments["instrumented"]
        metrics = dict.fromkeys(ALLOCATION_FIELDS, None)
        if instrumented:
            metrics.update({
                "new_calls": 4, "requested_bytes": 80,
                "requested_peak_live": 60,
                "requested_live_at_return": 20, "largest_request": 40,
                "key_callbacks": 3,
            })
            if arguments["case"].startswith("bad_"):
                metrics["key_callbacks"] = 0
                metrics["requested_live_at_return"] = 0
        rows.append({
            "kind": "observation", "case": arguments["case"],
            "operation": arguments["operation"], "trial": trial,
            "instrumented": instrumented,
            "ns": None if instrumented else trial,
            **metrics,
            "process_hwm_before_kib": 1024 + trial,
            "process_hwm_after_kib": 1024 + trial,
            "consumed": arguments["expected_consumed"],
        })
    return rows


def encode_rows(rows, newline=b"\n"):
    return b"".join(
        json.dumps(row, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        + newline for row in rows
    )


def replace_first_key(value, key, replacement):
    """Alter a named contract field without assuming its nesting layout."""
    if isinstance(value, dict):
        if key in value:
            value[key] = replacement
            return True
        return any(replace_first_key(item, key, replacement)
                   for item in value.values())
    if isinstance(value, list):
        return any(replace_first_key(item, key, replacement) for item in value)
    return False


class CostSemanticsTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(
            COST, "analysis_cost_semantics is required: " + str(IMPORT_FAILURE))

    def freeze(self, arguments=None):
        return COST.freeze_cost_contract(**(arguments or contract_arguments()))

    def accept(self, arguments, rows=None, raw=None):
        contract = self.freeze(arguments)
        if raw is None:
            raw = encode_rows(output_rows(arguments) if rows is None else rows)
        result = COST.validate_cost_output(
            raw, contract=contract,
            expected_contract_sha256=contract.sha256)
        self.assertIs(result["complete"], True)
        self.assertIs(type(result["verified_observations"]), int)
        self.assertEqual(result["verified_observations"], 5)
        for key in ("call_id", "case", "operation", "instrumented"):
            self.assertIs(type(result[key]), type(arguments[key]))
            self.assertEqual(result[key], arguments[key])
        return result

    def reject_rows(self, arguments, rows):
        self.reject_raw(arguments, encode_rows(rows))

    def reject_raw(self, arguments, raw):
        contract = self.freeze(arguments)
        with self.assertRaises(COST.CostSemanticError):
            COST.validate_cost_output(
                raw, contract=contract,
                expected_contract_sha256=contract.sha256)

    def test_contract_identity_and_document_are_detached(self):
        arguments = contract_arguments()
        contract = self.freeze(arguments)
        canonical = contract.canonical
        self.assertIs(type(canonical), bytes)
        self.assertEqual(contract.sha256, hashlib.sha256(canonical).hexdigest())
        original_document = copy.deepcopy(contract.document())
        self.assertEqual(original_document, json.loads(canonical))
        arguments["prepared"]["checks"] += 1
        arguments["call_id"] = "changed-after-freeze"
        returned = contract.document()
        returned["extra-untrusted-field"] = True
        self.assertTrue(replace_first_key(returned, "checks", 999))
        self.assertEqual(contract.document(), original_document)
        self.assertEqual(contract.canonical, canonical)

    def test_accepts_all_positive_cases_operations_and_modes(self):
        for case in CONSUMED_LITERALS:
            for operation in OPERATIONS:
                for instrumented in (False, True):
                    with self.subTest(case=case, operation=operation,
                                      instrumented=instrumented):
                        self.accept(contract_arguments(case, operation, instrumented))
        # Non-archive sentinel values are explicitly bound by the caller's
        # frozen contract; they are not inferred as 2 * records or wire bytes.
        for operation in ("live", "model", "encode", "chain"):
            with self.subTest(explicit_sentinel=operation):
                arguments = contract_arguments(operation=operation)
                arguments["expected_consumed"] = 17
                self.accept(arguments)

    def test_accepts_both_bad_tail_cases_in_both_modes(self):
        for case in ("bad_text_tail", "bad_items_tail"):
            for instrumented in (False, True):
                with self.subTest(case=case, instrumented=instrumented):
                    self.accept(contract_arguments(case, "decode", instrumented))

    def test_accepts_numeric_boundaries_crlf_and_zero_size_requests(self):
        for instrumented in (False, True):
            arguments = contract_arguments(instrumented=instrumented)
            arguments["prepared"]["wire_bytes"] = UINT64_MAX
            arguments["prepared"]["checks"] = UINT32_MAX
            arguments["expected_consumed"] = UINT64_MAX
            rows = output_rows(arguments)
            for row in rows[1:]:
                row["process_hwm_before_kib"] = INT64_MAX
                row["process_hwm_after_kib"] = INT64_MAX
                if instrumented:
                    row.update({
                        "new_calls": 1, "requested_bytes": UINT64_MAX,
                        "requested_peak_live": UINT64_MAX,
                        "requested_live_at_return": UINT64_MAX,
                        "largest_request": UINT64_MAX,
                        "key_callbacks": UINT64_MAX,
                    })
                else:
                    row["ns"] = INT64_MAX
            with self.subTest(boundary_mode=instrumented):
                self.accept(arguments, raw=encode_rows(rows, b"\r\n"))
        arguments = contract_arguments(instrumented=True)
        for calls in (0, 3):
            rows = output_rows(arguments)
            for row in rows[1:]:
                row.update(dict.fromkeys(ALLOCATION_FIELDS, 0))
                row["new_calls"] = calls
                row["key_callbacks"] = 9
            with self.subTest(zero_size_calls=calls):
                self.accept(arguments, rows)

    def test_freeze_rejects_invalid_call_identity(self):
        changes = [
            ("call_id", ""), ("call_id", "x" * 129),
            ("call_id", 0), ("call_id", True), ("call_id", None),
            ("case", "unregistered"), ("case", ""), ("case", 1),
            ("operation", "prepare"), ("operation", "unknown"),
            ("operation", None), ("instrumented", 0),
            ("instrumented", 1), ("instrumented", "false"),
            ("instrumented", None),
        ]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                arguments = contract_arguments()
                arguments[key] = value
                with self.assertRaises(ValueError):
                    self.freeze(arguments)
        for operation in ("live", "project", "encode", "model", "chain"):
            with self.subTest(bad_operation=operation):
                with self.assertRaises(ValueError):
                    self.freeze(contract_arguments("bad_items_tail", operation, True))

    def test_freeze_rejects_prepared_fieldsets_and_false_claims(self):
        invalid = [None, [], "prepared"]
        original = prepared_literal()
        for key in original:
            row = dict(original)
            del row[key]
            invalid.append(row)
        invalid.append(dict(original, extra=0))
        for key, value in (("kind", "observation"), ("case", "dll"),
                           ("records", 2), ("public_preparation_calls", 4),
                           ("model_equal", False), ("model_equal", 1)):
            invalid.append(dict(original, **{key: value}))
        for index, prepared in enumerate(invalid):
            with self.subTest(prepared_variant=index):
                arguments = contract_arguments()
                arguments["prepared"] = prepared
                with self.assertRaises(ValueError):
                    self.freeze(arguments)
        for case, count in (("reuse_save_failure", 1), ("history_256", 255)):
            with self.subTest(case=case, records=count):
                arguments = contract_arguments(case)
                arguments["prepared"]["records"] = count
                with self.assertRaises(ValueError):
                    self.freeze(arguments)

    def test_freeze_rejects_prepared_numeric_types_and_ranges(self):
        for key in ("records", "wire_bytes", "public_preparation_calls", "checks"):
            upper = UINT32_MAX if key == "checks" else UINT64_MAX
            for value in (True, False, 1.0, "1", None, -1, 0, upper + 1):
                with self.subTest(key=key, value=value):
                    arguments = contract_arguments()
                    arguments["prepared"][key] = value
                    with self.assertRaises(ValueError):
                        self.freeze(arguments)

    def test_freeze_rejects_invalid_consumed_contracts(self):
        for value in (0, -1, True, False, 2.0, "2", None, UINT64_MAX + 1):
            with self.subTest(expected_consumed=value):
                arguments = contract_arguments()
                arguments["expected_consumed"] = value
                with self.assertRaises(ValueError):
                    self.freeze(arguments)
        for case, operation in (("exe", "project"), ("exe", "decode"),
                                 ("bad_text_tail", "decode"),
                                 ("bad_items_tail", "decode")):
            with self.subTest(case=case, operation=operation):
                arguments = contract_arguments(case, operation)
                arguments["expected_consumed"] += 1
                with self.assertRaises(ValueError):
                    self.freeze(arguments)

    def test_output_rejects_missing_extra_and_nonobject_records(self):
        arguments = contract_arguments()
        original = output_rows(arguments)
        for index in (0, 1):
            for key in original[index]:
                with self.subTest(record=index, missing=key):
                    rows = copy.deepcopy(original)
                    del rows[index][key]
                    self.reject_rows(arguments, rows)
            with self.subTest(record=index, extra=True):
                rows = copy.deepcopy(original)
                rows[index]["extra"] = 0
                self.reject_rows(arguments, rows)
            for value in (None, [], "record", 0, True):
                with self.subTest(record=index, replacement=value):
                    rows = copy.deepcopy(original)
                    rows[index] = value
                    self.reject_rows(arguments, rows)

    def test_output_rejects_prepared_changes_and_type_coercion(self):
        arguments = contract_arguments()
        changes = [
            ("kind", "observation"), ("case", "dll"),
            ("records", 2), ("records", True), ("records", 1.0),
            ("wire_bytes", 3406), ("wire_bytes", 3405.0),
            ("public_preparation_calls", 6), ("public_preparation_calls", 5.0),
            ("model_equal", False), ("model_equal", 1),
            ("checks", 118), ("checks", 115.0),
            ("checks", UINT32_MAX + 1),
        ]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                rows = output_rows(arguments)
                rows[0][key] = value
                self.reject_rows(arguments, rows)

    def test_output_rejects_identity_and_trial_type_coercion(self):
        arguments = contract_arguments()
        changes = [
            (1, "kind", "prepared"), (1, "case", "dll"),
            (1, "case", 0), (1, "operation", "model"),
            (1, "operation", None), (1, "trial", 1),
            (1, "trial", False), (1, "trial", 0.0),
            (2, "trial", True), (2, "trial", 1.0),
            (5, "trial", 5), (1, "instrumented", 0),
            (1, "instrumented", True), (1, "instrumented", None),
        ]
        for index, key, value in changes:
            with self.subTest(record=index, key=key, value=value):
                rows = output_rows(arguments)
                rows[index][key] = value
                self.reject_rows(arguments, rows)

    def test_output_rejects_metric_numeric_types_and_ranges(self):
        for instrumented in (False, True):
            arguments = contract_arguments(instrumented=instrumented)
            keys = ["process_hwm_before_kib", "process_hwm_after_kib", "consumed"]
            keys += list(ALLOCATION_FIELDS) if instrumented else ["ns"]
            for key in keys:
                upper = INT64_MAX if key == "ns" or key.startswith("process_hwm_") else UINT64_MAX
                for value in (True, False, 1.0, "1", None, -1, upper + 1):
                    with self.subTest(mode=instrumented, key=key, value=value):
                        rows = output_rows(arguments)
                        rows[1][key] = value
                        self.reject_rows(arguments, rows)
            for key in ("process_hwm_before_kib", "process_hwm_after_kib", "consumed"):
                with self.subTest(mode=instrumented, positive_only=key):
                    rows = output_rows(arguments)
                    rows[1][key] = 0
                    self.reject_rows(arguments, rows)

    def test_output_rejects_mixed_time_and_allocation_modes(self):
        arguments = contract_arguments()
        for key in ALLOCATION_FIELDS:
            with self.subTest(time_mode_nonnull=key):
                rows = output_rows(arguments)
                rows[1][key] = 0
                self.reject_rows(arguments, rows)
        rows = output_rows(arguments)
        rows[1]["ns"] = None
        self.reject_rows(arguments, rows)
        arguments = contract_arguments(instrumented=True)
        for value in (0, 1, False):
            with self.subTest(allocation_mode_ns=value):
                rows = output_rows(arguments)
                rows[1]["ns"] = value
                self.reject_rows(arguments, rows)

    def test_output_rejects_allocation_relations_and_bad_callbacks(self):
        arguments = contract_arguments(instrumented=True)
        violations = [
            {"requested_live_at_return": 61},
            {"requested_peak_live": 81},
            {"largest_request": 61},
            {"new_calls": 0},
            {"new_calls": 2, "requested_bytes": 81},
            {"new_calls": 1, "requested_bytes": 1,
             "requested_peak_live": 1, "requested_live_at_return": 0,
             "largest_request": 0},
        ]
        for change in violations:
            with self.subTest(violation=change):
                rows = output_rows(arguments)
                rows[1].update(change)
                self.reject_rows(arguments, rows)
        for case in ("bad_text_tail", "bad_items_tail"):
            with self.subTest(bad_callback=case):
                arguments = contract_arguments(case, "decode", True)
                rows = output_rows(arguments)
                rows[1]["key_callbacks"] = 1
                self.reject_rows(arguments, rows)

    def test_output_rejects_changed_sentinels_in_all_operation_kinds(self):
        for case, operations in (("exe", OPERATIONS),
                                  ("bad_text_tail", ("decode",)),
                                  ("bad_items_tail", ("decode",))):
            for operation in operations:
                for instrumented in (False, True):
                    with self.subTest(case=case, operation=operation,
                                      instrumented=instrumented):
                        arguments = contract_arguments(case, operation, instrumented)
                        rows = output_rows(arguments)
                        rows[3]["consumed"] += 1
                        self.reject_rows(arguments, rows)
        arguments = contract_arguments(operation="encode")
        rows = output_rows(arguments)
        rows[1]["consumed"] = rows[0]["wire_bytes"]
        self.reject_rows(arguments, rows)

    def test_bad_tail_scalar_return_cannot_retain_epoch_payload(self):
        # Source-derived for this exact producer: Reader<false> rejects the
        # appended byte, scoped Error owners are destroyed in the lambda, then
        # observe snapshots stats after the lambda has returned only size_t.
        # This does not fix historical new-call/byte totals or imply that the
        # public Error itself is allocation-free.
        for case in ("bad_text_tail", "bad_items_tail"):
            with self.subTest(case=case):
                arguments = contract_arguments(case, "decode", True)
                rows = output_rows(arguments)
                self.accept(arguments, rows)
                rows[1]["requested_live_at_return"] = 1
                self.reject_rows(arguments, rows)

    def test_output_rejects_hwm_regression_within_and_between_trials(self):
        arguments = contract_arguments()
        rows = output_rows(arguments)
        rows[1]["process_hwm_after_kib"] = 1023
        self.reject_rows(arguments, rows)
        rows = output_rows(arguments)
        rows[2]["process_hwm_before_kib"] = 1023
        self.reject_rows(arguments, rows)
        rows = output_rows(arguments)
        rows[2]["process_hwm_before_kib"] = 1023
        rows[2]["process_hwm_after_kib"] = 1023
        self.reject_rows(arguments, rows)

    def test_output_rejects_incomplete_duplicate_and_reordered_records(self):
        arguments = contract_arguments()
        original = output_rows(arguments)
        variants = [
            [], original[:1], original[:-1], original[1:],
            original + [original[-1]], [original[0]] + original,
            original[:2] + [original[1]] + original[3:],
            [original[1], original[0]] + original[2:],
            original[:1] + [original[2], original[1]] + original[3:],
            original[:2] + [original[0]] + original[3:],
        ]
        for index, rows in enumerate(variants):
            with self.subTest(sequence_variant=index):
                self.reject_rows(arguments, rows)

    def test_output_rejects_malformed_json_and_framing(self):
        arguments = contract_arguments()
        raw = encode_rows(output_rows(arguments))
        malformed = [
            b"", raw[:-1], raw[:-1] + b"\r", b"\n" + raw,
            raw + b"\n", b"\xff\n" + raw, b"\xef\xbb\xbf" + raw,
            raw.replace(b'"kind":"prepared"',
                        b'"kind":"prepared","kind":"prepared"', 1),
            raw.replace(b'"trial":0', b'"trial":0,"trial":0', 1),
            raw.replace(b'"ns":0', b'"ns":NaN', 1),
            raw.replace(b'"ns":0', b'"ns":Infinity', 1),
            raw.replace(b'"ns":0', b'"ns":-Infinity', 1),
            raw.replace(b'"ns":0', b'"ns":1e999', 1),
            raw.replace(b"\n", b"{}\n", 1),
            raw.decode("utf-8"), bytearray(raw), None,
        ]
        for index, value in enumerate(malformed):
            with self.subTest(raw_variant=index):
                self.reject_raw(arguments, value)

    def test_record_and_stream_caps_include_all_raw_bytes(self):
        arguments = contract_arguments()
        lines = encode_rows(output_rows(arguments)).splitlines(keepends=True)
        # Like the retained capture helper, record bytes include the final LF.
        padding = 8192 - len(lines[0])
        bounded = lines[0][:-1] + b" " * padding + b"\n"
        self.assertEqual(len(bounded), 8192)
        self.accept(arguments, raw=bounded + b"".join(lines[1:]))
        self.reject_raw(arguments, bounded[:-1] + b" \n" + b"".join(lines[1:]))
        self.reject_raw(arguments, b" " * (64 * 1024) + b"\n")

    def test_hash_binding_and_mutated_contracts_are_revalidated(self):
        arguments = contract_arguments()
        raw = encode_rows(output_rows(arguments))
        contract = self.freeze(arguments)
        for digest in ("0" * 64, "0" * 63, "0" * 65, None, True,
                       contract.sha256.upper()):
            with self.subTest(expected_digest=digest):
                with self.assertRaises(COST.CostSemanticError):
                    COST.validate_cost_output(
                        raw, contract=contract, expected_contract_sha256=digest)
        for invalid_contract in (None, {}, contract.canonical):
            with self.subTest(contract_type=type(invalid_contract).__name__):
                with self.assertRaises(COST.CostSemanticError):
                    COST.validate_cost_output(
                        raw, contract=invalid_contract,
                        expected_contract_sha256=contract.sha256)
        mutations = [b"{}\n", b"[]\n", b"not-json\n"]
        extra = contract.document()
        extra["extra-untrusted-field"] = True
        mutations.append(json.dumps(extra, separators=(",", ":")).encode() + b"\n")
        for key, value in (("case", "unregistered"), ("instrumented", 0),
                           ("checks", True), ("expected_consumed", 0)):
            changed = contract.document()
            self.assertTrue(replace_first_key(changed, key, value), key)
            mutations.append(json.dumps(changed, separators=(",", ":")).encode() + b"\n")
        for index, canonical in enumerate(mutations):
            with self.subTest(mutated_contract=index):
                # A self-consistent new digest cannot authorize a malformed
                # reconstructed FrozenCostContract or bypass its invariants.
                with self.assertRaises(COST.CostSemanticError):
                    mutated = COST.FrozenCostContract(canonical=canonical)
                    COST.validate_cost_output(
                        raw, contract=mutated,
                        expected_contract_sha256=hashlib.sha256(canonical).hexdigest())


if __name__ == "__main__":
    unittest.main()
