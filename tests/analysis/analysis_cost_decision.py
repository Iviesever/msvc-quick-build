"""Closed static verifier of one independently reviewed, blocked preparation.

There is no runner, editable grant, upgrade path or future-execution protocol
API. Both the exact source-pinned artifact and an external SHA are required.
"""
import hashlib
import json


PROTOCOL_SHA256 = "f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae"
_MAX_BYTES = 32768
_FUTURE = {"purpose", "candidate", "comparator", "binaries", "inputs", "matrix",
           "acceptance", "observer", "global_budgets", "registration"}


class DecisionPreparationError(Exception):
    """The exact frozen preparation was not established; no permission follows."""


def audit_preparation(raw, *, expected_sha256):
    """Verify the fixed artifact while retaining BLOCKED, zero budgets and HOLD.

    The expected hash must come from the independently retained registration or
    review. Rehashing edited text cannot change the built-in artifact identity.
    Success covers this preparation only, not historical stream integrity or
    actual input/producer authenticity. This function performs no I/O.
    """
    if type(raw) is not bytes or not 0 < len(raw) <= _MAX_BYTES:
        raise ValueError("bounded_preparation_bytes_required")
    if (type(expected_sha256) is not str or len(expected_sha256) != 64 or
            any(c not in "0123456789abcdef" for c in expected_sha256)):
        raise ValueError("external_preparation_sha_required")
    if (expected_sha256 != PROTOCOL_SHA256 or
            hashlib.sha256(raw).hexdigest() != expected_sha256):
        raise DecisionPreparationError("frozen_preparation_identity_mismatch")
    document = json.loads(raw)
    budgets = document["authorized_real_budgets"]
    future = document["future_selection"]
    blockers = document["blockers"]
    authority = document["authority"]
    acceptance = document["acceptance"]
    if (type(document["format"]) is not int or document["format"] != 1 or
            document["kind"] != "cost-decision-preparation" or
            document["protocol_id"] != "pr250-cost-semantics-preparation-20261009" or
            document["status"] != "PREPARATION_ONLY_BLOCKED" or
            document["real_execution_authorized"] is not False or
            type(budgets) is not dict or len(budgets) != 15 or
            any(type(value) is not int or value != 0 for value in budgets.values()) or
            type(future) is not dict or set(future) != _FUTURE or
            any(value is not None for value in future.values()) or
            type(blockers) is not list or len(blockers) != 7 or
            any(type(value) is not str or not 0 < len(value) <= 4096 for value in blockers) or
            type(authority) is not dict or len(authority) != 4 or
            any(value is not False for value in authority.values()) or
            acceptance["product_cost_decision"] != "HOLD" or
            acceptance["pr250_integration_decision"] != "HOLD" or
            acceptance["real_execution_decision"] != "BLOCKED"):
        raise DecisionPreparationError("frozen_preparation_must_remain_blocked")
    return {"preparation_valid": True, "protocol_sha256": expected_sha256,
            "status": "PREPARATION_ONLY_BLOCKED", "real_execution_authorized": False,
            "product_cost_status": "HOLD", "authorized_real_budgets": budgets,
            "blockers": blockers, "authority": authority}
