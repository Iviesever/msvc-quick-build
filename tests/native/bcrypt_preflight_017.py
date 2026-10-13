"""Pinned metadata-only takeover for bcrypt checkpoint 017.

This module never accepts an image pathname, selects machine-code bytes, loads
Windows modules, or launches a decoder. A passing result is NOT execution
admission. The previous final closure, not its source-only commit, is the base.
"""
from __future__ import annotations

import hashlib
import json
import math
import struct
from typing import Any, Mapping

BASELINE = "f63c3eca924e6f2a3a4f30e95f3381bdee64dffc"
SOURCE_016 = "51bbce58ae37b36521be6425de55e784e8ceee72"
IMAGE_SHA256 = "663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb"
IMAGE_BYTES = 170984
IMAGE_BASE = 0x180000000
START, END, ROW, UNWIND = 0x1117C, 0x111E3, 302, 0x1DD4C
CALL_RVA = 0xDB84
CALL_HEX = "e8f3350000"
PINS = {
    "index": (271920, "ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e"),
    "caller": (3583, "8a8513ad908a2db62f816639c0a08ed6b6d2413fde181ea8aab39937faec951c"),
    "runtime": (17743, "c5ce6f84cefa7963d36e0cb835b6e8c19e10421239ae5af0e6f8b23f6e3f52ae"),
    "closure": (16812, "527bdbb6608be358185a97fc3c4a9a46c45fc5c7ad45d007c8d92060395e2dbb"),
    "proposal": (8180, "39ba3c6010c14777508edea602c3f4186e730127565e12dcaba2de713a3911bb"),
}


class Refused(ValueError):
    """A failed prerequisite; callers must not convert it to authorization."""


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise Refused(reason)


def same(actual: Any, expected: Any, reason: str) -> None:
    """Typed equality: JSON true must not impersonate integer one."""
    if type(actual) is not type(expected):
        raise Refused(reason)
    if isinstance(expected, dict):
        require(actual.keys() == expected.keys(), reason)
        for key in expected:
            same(actual[key], expected[key], reason)
    elif isinstance(expected, list):
        require(len(actual) == len(expected), reason)
        for left, right in zip(actual, expected):
            same(left, right, reason)
    else:
        require(actual == expected, reason)


def strict_json(raw: bytes) -> dict[str, Any]:
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key")
            result[key] = value
        return result

    def nonfinite(_: str) -> None:
        raise Refused("non-finite JSON number")

    def finite_float(text: str) -> float:
        value = float(text)
        require(math.isfinite(value), "non-finite JSON number")
        return value

    try:
        value = json.loads(raw, object_pairs_hook=pairs, parse_constant=nonfinite, parse_float=finite_float)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as exc:
        raise Refused("invalid JSON") from exc
    require(type(value) is dict, "JSON root is not an object")
    return value


def _mapped(index: dict[str, Any], rva: int, size: int) -> tuple[int, dict[str, Any]]:
    require(type(rva) is int and type(size) is int and size > 0, "invalid extent")
    matches = []
    for section in index["sections"]:
        begin = section["virtual_address"]
        backed = min(section["raw_size"], section["virtual_size"], section["file_backed_size"])
        if begin <= rva and rva + size <= begin + backed:
            offset = section["raw_file_offset"] + rva - begin
            require(0 <= offset < offset + size <= IMAGE_BYTES, "mapping beyond image")
            matches.append((offset, section))
    require(len(matches) == 1, "extent has no unique file-backed mapping")
    return matches[0]


def _validate(documents: Mapping[str, dict[str, Any]]) -> dict[str, Any]:
    """Internal semantic checker. Public admission always verifies PINS first."""
    report, caller = documents["index"], documents["caller"]
    runtime, closure, proposal = (documents[k] for k in ("runtime", "closure", "proposal"))
    same(report["schema"], "bcrypt-pe-index-013-report-v1", "index schema")
    same(report["status"], "complete_index", "index incomplete")
    same(report["static_index_admitted"], True, "index unadmitted")
    same(report["core"]["status"], "indexed", "inner index incomplete")
    same(report["core"]["static_index_admitted"], True, "inner index unadmitted")
    index = report["core"]["index"]
    same(index["schema"], "bcrypt-pe-index-013-v1", "inner schema")
    same(index["bytes"], IMAGE_BYTES, "image length")
    same(index["sha256"], IMAGE_SHA256, "image provenance")
    same(index["header"]["image_base"], IMAGE_BASE, "address domain")
    same(index["header"]["machine"], 0x8664, "machine")
    same(index["header"]["optional_header_magic"], 0x20B, "PE kind")
    same(runtime["decision"], "ADMIT_ACTUAL_STATIC_RESULT_016", "runtime unadmitted")
    same(runtime["source_commit"], SOURCE_016, "runtime source")
    same(runtime["static_result_admitted"], True, "runtime result unadmitted")
    same(closure["decision"], "ADMIT_FINAL_CLOSURE_016", "closure unadmitted")
    same(closure["final_feature_commit"], BASELINE, "source-only or wrong baseline")
    same(closure["source_commit"], SOURCE_016, "closure source")
    same(closure["final_feature_tree"], "2a6a31926ca9b12039b31bcc6f7f26fe7d409d13", "closure tree")
    same(closure["runtime_review_admission"]["sha256"], PINS["runtime"][1], "runtime/closure join")
    for document in (runtime, closure):
        same(document["open_findings"], {"C": 0, "I": 0, "M": 0}, "open prior finding")
    same(caller["status"], "complete_disassembly", "caller incomplete")
    same(caller["source_commit"], SOURCE_016, "caller source")
    same(caller["child_returncode"], 0, "caller exit")
    same(caller["child_reaped"], True, "caller child not reaped")
    same(caller["native_execution_performed"], False, "wrong observation kind")
    old_target = {"file_start": 0xDB3C, "file_end": 0xDB8F,
                  "rva_start": 0xDB3C, "rva_end": 0xDB8F, "section_index": 0,
                  "runtime_function_index": 252, "preferred_vma_start": IMAGE_BASE + 0xDB3C,
                  "sha256": "ff762eb73a125811551bfd8cfeb3b7cf68f26607d045d78311f50d247250df32"}
    same(caller["target"], old_target, "caller target provenance")
    same(runtime["target"], old_target, "runtime target provenance")
    records = caller["instructions"]
    same(records, runtime["instructions"], "caller/runtime instructions disagree")
    require(len(records) == 18, "caller instruction count")
    cursor, fragments = 0xDB3C, []
    for record in records:
        raw = bytes.fromhex(record["raw_hex"])
        require(1 <= len(raw) <= 15, "instruction size")
        same(record["length"], len(raw), "instruction length")
        same(record["rva"], cursor, "caller coverage")
        offset, _ = _mapped(index, cursor, len(raw))
        same(record["file_offset"], offset, "caller file domain")
        same(record["preferred_vma"], IMAGE_BASE + cursor, "caller VMA domain")
        cursor += len(raw)
        fragments.append(raw)
    require(cursor == 0xDB8F, "caller end")
    same(hashlib.sha256(b"".join(fragments)).hexdigest(), caller["target"]["sha256"], "caller byte digest")
    calls = [record for record in records if record["rva"] == CALL_RVA]
    require(len(calls) == 1, "missing/duplicate source call")
    call = bytes.fromhex(calls[0]["raw_hex"])
    same(call.hex(), CALL_HEX, "incoming raw CALL")
    destination = CALL_RVA + 5 + int.from_bytes(call[1:], "little", signed=True)
    require(destination == START, "CALL target is not 1117C")
    rows = index["runtime_functions"]
    require(len(rows) == 390, "runtime table count")
    candidates = [row for row in rows if row["begin_rva"] <= START < row["end_rva"]]
    require(len(candidates) == 1, "ambiguous runtime containment")
    row = candidates[0]
    expected_row = {"begin_file_offset": START, "begin_rva": START,
                    "end_file_offset": END, "end_rva": END, "index": ROW,
                    "section_index": 0, "section_name": ".text",
                    "table_entry_file_offset": 0x22E28, "unwind_info_rva": UNWIND}
    same(row, expected_row, "wrong runtime row/extent")
    offset, section = _mapped(index, START, END - START)
    require(offset == START and section["index"] == 0 and section["name"] == ".text", "target mapping")
    require(section["characteristics"] & 0x20000000 != 0, "non-executable target")
    directory = index["data_directories"][3]
    same(directory["name"], "EXCEPTION", "exception directory")
    require(directory["file_offset"] + ROW * 12 == row["table_entry_file_offset"], "pdata row formula")
    require((ROW + 1) * 12 <= directory["bytes"], "pdata row outside table")
    unwind_matches = [u for u in index["unwind_info"] if u["rva"] == UNWIND]
    require(len(unwind_matches) == 1, "unwind not unique")
    expected_unwind = {"bytes": 8, "chain": None, "chain_depth": 1, "code_slots": 1,
                       "file_offset": UNWIND, "flags": 0, "frame_offset": 0,
                       "frame_register": 0, "handler_file_offset": None,
                       "handler_rva": None, "prolog_size": 4,
                       "raw_hex": "0104010004420000", "rva": UNWIND, "version": 1}
    same(unwind_matches[0], expected_unwind, "wrong unwind/CHAININFO/handler")
    unwind_offset, _ = _mapped(index, UNWIND, 8)
    require(unwind_offset == UNWIND, "unwind file mapping")
    same(proposal["exact_runtime_function_row"], row, "proposal row")
    same(proposal["exact_unwind_record"], expected_unwind, "proposal unwind")
    new_target = {"file_start": START, "file_end": END, "rva_start": START, "rva_end": END,
                  "section_index": 0, "runtime_function_index": ROW,
                  "preferred_vma_start": IMAGE_BASE + START, "sha256": None}
    same(proposal["proposed_target"], new_target, "wrong proposal target or unreviewed digest")
    same(proposal["proposed_target_bytes"], 103, "wrong proposal byte count")
    same(proposal["source_call_instruction"]["raw_hex"], CALL_HEX, "proposal CALL bytes")
    same(proposal["source_call_instruction"]["rva"], CALL_RVA, "proposal CALL address")
    same(proposal["source_call_instruction"]["destination_rva"], START, "proposal CALL destination")
    same(proposal["selection_identity_admitted"], False, "proposal is not selection")
    same(proposal["new_execution_admitted"], False, "proposal is not execution")
    return {"schema": "bcrypt-metadata-preflight-017-v1", "baseline_commit": BASELINE,
            "status": "METADATA_READY_EXECUTION_NOT_ADMITTED", "candidate_bytes": END - START,
            "candidate_sha256": None, "runtime_function_index": ROW,
            "file_range": [START, END], "rva_range": [START, END],
            "preferred_vma_range": [IMAGE_BASE + START, IMAGE_BASE + END],
            "incoming_call": {"rva": CALL_RVA, "raw_hex": CALL_HEX, "destination_rva": destination},
            "required_future_snapshot_checks": [
                {"file_offset": CALL_RVA, "bytes": 5, "raw_hex": CALL_HEX},
                {"file_offset": row["table_entry_file_offset"], "bytes": 12,
                 "raw_hex": struct.pack("<III", START, END, UNWIND).hex()},
                {"file_offset": UNWIND, "bytes": 8, "raw_hex": expected_unwind["raw_hex"]}],
            "image_content_reads": 0, "decoder_launches": 0, "execution_admitted": False,
            "selection_identity_admitted": False, "native_execution_admitted": False,
            "qualification": "Only archived metadata was checked; target bytes and callee behavior remain unknown."}


def preflight(inputs: Mapping[str, bytes]) -> dict[str, Any]:
    """Check all five immutable inputs before parsing any of their JSON."""
    require(set(inputs) == set(PINS), "missing or extra input role")
    for role, (size, digest) in PINS.items():
        raw = inputs[role]
        require(type(raw) is bytes and len(raw) == size, f"{role}: byte count")
        require(hashlib.sha256(raw).hexdigest() == digest, f"{role}: digest")
    try:
        return _validate({role: strict_json(raw) for role, raw in inputs.items()})
    except (KeyError, TypeError, IndexError, OverflowError, ValueError) as exc:
        if isinstance(exc, Refused):
            raise
        raise Refused("inconsistent metadata") from exc
