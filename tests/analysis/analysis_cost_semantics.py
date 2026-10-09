"""Strict semantics of one frozen cost-producer call; no process or file I/O.

These rules describe the preserved Linux x86_64 producer, not a general codec
schema. A validated stream is not evidence of a real measurement or permission
to perform one. The outer driver must bind source, call and original bytes.
"""
from dataclasses import dataclass
import hashlib
import json


_UINT32_MAX = (1 << 32) - 1
_UINT64_MAX = (1 << 64) - 1
_INT64_MAX = (1 << 63) - 1
_STDOUT_BYTES = 64 * 1024
_RECORD_BYTES = 8192
_CONTRACT_BYTES = 8192
_RECORDS = {"exe": 1, "dll": 1, "static": 1, "reuse_save_failure": 2,
            "snapshot_128k": 1, "history_256": 256, "text_7m": 1,
            "items_60000": 1, "bad_text_tail": 1, "bad_items_tail": 1}
_OPERATIONS = ("live", "project", "encode", "decode", "model", "chain")
_PREPARED = {"kind", "case", "records", "wire_bytes", "public_preparation_calls",
             "model_equal", "checks"}
_ALLOCATION = ("new_calls", "requested_bytes", "requested_peak_live",
               "requested_live_at_return", "largest_request", "key_callbacks")
_OBSERVATION = {"kind", "case", "operation", "trial", "instrumented", "ns",
                "process_hwm_before_kib", "process_hwm_after_kib", "consumed", *_ALLOCATION}


class CostSemanticError(Exception):
    """Output or frozen contract failed semantic admission; retain its bytes."""


@dataclass(frozen=True)
class FrozenCostContract:
    canonical: bytes

    @property
    def sha256(self):
        return hashlib.sha256(self.canonical).hexdigest()

    def document(self):
        return json.loads(self.canonical)


def _canonical(value):
    return (json.dumps(value, ensure_ascii=True, allow_nan=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("ascii")


def _integer(value, maximum=_UINT64_MAX, minimum=0):
    return type(value) is int and minimum <= value <= maximum


def _sha(value):
    return type(value) is str and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _decode(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise CostSemanticError("duplicate_json_key")
            value[key] = item
        return value

    def reject_number(_):
        raise CostSemanticError("noninteger_json_number")

    def integer(text):
        if len(text.lstrip("-")) > 20:
            raise CostSemanticError("integer_representation_limit")
        value = int(text)
        if abs(value) > _UINT64_MAX:
            raise CostSemanticError("integer_representation_limit")
        return value

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_float=reject_number, parse_constant=reject_number,
                          parse_int=integer)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise CostSemanticError("invalid_json") from error


def _prepared_valid(value, case):
    return (type(value) is dict and set(value) == _PREPARED and
            type(value["kind"]) is str and value["kind"] == "prepared" and
            type(value["case"]) is str and value["case"] == case and
            _integer(value["records"], minimum=1) and value["records"] == _RECORDS[case] and
            _integer(value["wire_bytes"], minimum=1) and
            _integer(value["public_preparation_calls"]) and value["public_preparation_calls"] == 5 and
            value["model_equal"] is True and _integer(value["checks"], _UINT32_MAX, 1))


def _arguments_valid(call_id, case, operation, instrumented, prepared, expected_consumed):
    if (type(call_id) is not str or not 0 < len(call_id) <= 128 or
            type(case) is not str or case not in _RECORDS or
            type(operation) is not str or operation not in _OPERATIONS or
            type(instrumented) is not bool or not _prepared_valid(prepared, case) or
            not _integer(expected_consumed, minimum=1)):
        return False
    if case.startswith("bad_"):
        return operation == "decode" and expected_consumed == prepared["wire_bytes"] - 1
    if operation in ("project", "decode"):
        # The exact historical harness retains one entry. This is not a claim
        # about all archives. Other result shapes require an explicit sentinel.
        return expected_consumed == prepared["records"] + 1
    return True


def freeze_cost_contract(*, call_id, case, operation, instrumented, prepared, expected_consumed):
    """Freeze expected values before receiving any output, not from observations."""
    if not _arguments_valid(call_id, case, operation, instrumented, prepared, expected_consumed):
        raise ValueError("invalid_cost_call_contract")
    document = {"format": 1, "kind": "cost-output-contract", "trials": 5,
                "call_id": call_id, "case": case, "operation": operation,
                "instrumented": instrumented, "prepared": prepared,
                "expected_consumed": expected_consumed}
    raw = _canonical(document)
    if len(raw) > _CONTRACT_BYTES:
        raise ValueError("cost_contract_limit")
    return FrozenCostContract(raw)


def _thaw(contract, expected_sha256):
    if (type(contract) is not FrozenCostContract or type(contract.canonical) is not bytes or
            not 0 < len(contract.canonical) <= _CONTRACT_BYTES or not _sha(expected_sha256)):
        raise CostSemanticError("frozen_contract_and_external_sha_required")
    if contract.sha256 != expected_sha256:
        raise CostSemanticError("cost_contract_sha_mismatch")
    document = _decode(contract.canonical)
    keys = {"format", "kind", "trials", "call_id", "case", "operation", "instrumented",
            "prepared", "expected_consumed"}
    if (type(document) is not dict or set(document) != keys or
            type(document["format"]) is not int or document["format"] != 1 or
            document["kind"] != "cost-output-contract" or
            type(document["trials"]) is not int or document["trials"] != 5 or
            not _arguments_valid(**{key: document[key] for key in keys - {"format", "kind", "trials"}}) or
            _canonical(document) != contract.canonical):
        raise CostSemanticError("invalid_frozen_cost_contract")
    return document


def _check_observation(row, contract, trial, previous_hwm):
    if type(row) is not dict or set(row) != _OBSERVATION:
        raise CostSemanticError("observation_fieldset")
    expected = {"kind": "observation", "case": contract["case"], "operation": contract["operation"],
                "trial": trial, "instrumented": contract["instrumented"]}
    if any(type(row[key]) is not type(value) or row[key] != value for key, value in expected.items()):
        raise CostSemanticError("observation_identity_or_order")
    if not _integer(row["consumed"], minimum=1) or row["consumed"] != contract["expected_consumed"]:
        raise CostSemanticError("consumed_identity")
    before, after = row["process_hwm_before_kib"], row["process_hwm_after_kib"]
    if (not _integer(before, _INT64_MAX, 1) or not _integer(after, _INT64_MAX, 1) or
            not previous_hwm <= before <= after):
        raise CostSemanticError("process_hwm_type_or_regression")
    if not contract["instrumented"]:
        if not _integer(row["ns"], _INT64_MAX) or any(row[key] is not None for key in _ALLOCATION):
            raise CostSemanticError("normal_mode_metrics")
    else:
        if row["ns"] is not None or any(not _integer(row[key]) for key in _ALLOCATION):
            raise CostSemanticError("instrumented_mode_metrics")
        calls, total, peak, live, largest = (row[key] for key in _ALLOCATION[:5])
        if (not live <= peak <= total or largest > peak or
                calls == 0 and any((total, peak, live, largest)) or
                total > calls * largest):
            raise CostSemanticError("requested_allocation_relations")
        if contract["case"].startswith("bad_"):
            if row["key_callbacks"] != 0:
                raise CostSemanticError("bad_tail_key_callbacks")
            # This exact nonowning tail refusal destroys scoped Error owners
            # before the lambda returns size_t and observe snapshots stats.
            # Neither the public Error allocation nor its cumulative requested
            # bytes are zero-cost claims.
            if live != 0:
                raise CostSemanticError("bad_tail_retained_epoch_payload")
    return after


def validate_cost_output(raw, *, contract, expected_contract_sha256):
    """Validate one bounded complete prepared + five-observation byte stream.

    LF and CRLF are admitted. All raw bytes, including each terminating LF,
    count toward the caps. Success attests these semantics against the supplied
    frozen contract, not producer authenticity, timing quality or product cost.
    """
    document = _thaw(contract, expected_contract_sha256)
    if type(raw) is not bytes or not 0 < len(raw) <= _STDOUT_BYTES:
        raise CostSemanticError("cost_stdout_type_or_limit")
    if not raw.endswith(b"\n"):
        raise CostSemanticError("unterminated_cost_output")
    lines = raw.split(b"\n")[:-1]
    if len(lines) != 6:
        raise CostSemanticError("prepared_and_five_observations_required")
    if any(not line or len(line) + 1 > _RECORD_BYTES for line in lines):
        raise CostSemanticError("cost_record_limit")
    rows = [_decode(line) for line in lines]
    if (not _prepared_valid(rows[0], document["case"]) or
            any(type(rows[0][key]) is not type(value) or rows[0][key] != value
                for key, value in document["prepared"].items())):
        raise CostSemanticError("prepared_identity_or_values")
    previous_hwm = 0
    for trial, row in enumerate(rows[1:]):
        previous_hwm = _check_observation(row, document, trial, previous_hwm)
    return {"complete": True, "verified_observations": 5,
            **{key: document[key] for key in ("call_id", "case", "operation", "instrumented")}}
