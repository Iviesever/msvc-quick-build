"""Bounded GNU binary-slice text validation; this module does not decode x86."""

import hashlib
import re

from bcrypt_disassembly_model_014 import Refusal, require, LIMITS


_TARGET_FIELDS = {"runtime_function_index", "section_index", "file_start",
                  "file_end", "rva_start", "rva_end", "preferred_vma_start", "sha256"}
_UINT64 = (1 << 64) - 1
_LABEL = re.compile(r"([0-9a-f]{1,16}) <\.data>:")
_INSTRUCTION = re.compile(r"([0-9a-f]{1,16}):[ \t]+(.*)")
_BYTE = re.compile(r"([0-9a-f]{2})(?:[ \t]+|$)")
_PREFIX = re.compile(r"rex(?:\.[WRXB]{1,4})?|cs|ss|ds|es|fs|gs|"
                     r"(?:data|addr)(?:16|32|64)|lock|rep|repe|repz|repne|repnz|"
                     r"bnd|notrack|xacquire|xrelease")


def _target(data, spec):
    require(type(data) is bytes and 1 <= len(data) <= LIMITS["target_bytes"],
            "LISTING_TARGET")
    require(type(spec) is dict and len(spec) == len(_TARGET_FIELDS) and
            set(spec) == _TARGET_FIELDS, "LISTING_TARGET")
    for name in _TARGET_FIELDS - {"sha256"}:
        require(type(spec[name]) is int and 0 <= spec[name] <= _UINT64,
                "LISTING_TARGET")
    require(spec["file_end"] - spec["file_start"] == len(data) ==
            spec["rva_end"] - spec["rva_start"], "LISTING_TARGET")
    require(spec["preferred_vma_start"] + len(data) <= _UINT64, "LISTING_TARGET")
    require(type(spec["sha256"]) is str and
            spec["sha256"] == hashlib.sha256(data).hexdigest(), "LISTING_TARGET")


def _assembly(text):
    require(0 < len(text) <= LIMITS["assembly_chars"] and
            all(32 <= ord(char) <= 126 for char in text), "LISTING_ASSEMBLY")
    require("(bad)" not in text, "LISTING_ASSEMBLY")
    words = [word for word in text.split(" ") if word]
    offset = 0
    while offset < len(words) and _PREFIX.fullmatch(words[offset]):
        offset += 1
    require(offset < len(words), "LISTING_ASSEMBLY")
    mnemonic = words[offset]
    require(re.fullmatch(r"[a-z][a-z0-9]*", mnemonic) is not None and
            mnemonic not in {"db", "dw", "dd", "dq"}, "LISTING_ASSEMBLY")


def _instruction(line):
    match = _INSTRUCTION.fullmatch(line)
    require(match is not None, "LISTING_LINE")
    body = match.group(2)
    offset = 0
    chunks = []
    while True:
        byte = _BYTE.match(body, offset)
        if byte is None:
            break
        require(len(chunks) < LIMITS["instruction_bytes"], "LISTING_BYTES")
        chunks.append(byte.group(1))
        offset = byte.end()
    require(chunks, "LISTING_BYTES")
    assembly = body[offset:].strip(" \t")
    _assembly(assembly)
    raw_hex = "".join(chunks)
    return int(match.group(1), 16), bytes.fromhex(raw_hex), raw_hex, assembly


def parse_listing(raw_stdout: bytes, target_bytes: bytes, target_spec: dict) -> list:
    """Return exact six-field records after complete metadata/byte coverage checks.

    ASCII LF is the only line separator. Column padding may vary; assembly
    interior text is preserved. Mnemonic spelling is not an ISA-semantic proof.
    No success is inferred from a truncated or pseudo-data instruction listing.
    """
    require(type(raw_stdout) is bytes and
            0 < len(raw_stdout) < LIMITS["child_file_bytes"], "LISTING_SIZE")
    require(raw_stdout.endswith(b"\n"), "LISTING_LINE")
    _target(target_bytes, target_spec)
    records = []
    state = cursor = position = 0
    while position < len(raw_stdout):
        end = raw_stdout.find(b"\n", position)
        require(0 <= end - position <= LIMITS["raw_line_bytes"], "LISTING_LINE")
        raw_line = raw_stdout[position:end]
        position = end + 1
        require(all(byte == 9 or 32 <= byte <= 126 for byte in raw_line), "LISTING_LINE")
        line = raw_line.strip(b" \t").decode("ascii")
        if not line:
            continue
        if state == 0:
            require(re.fullmatch(r"target\.bin:[ \t]+file format binary", line) is not None,
                    "LISTING_HEADER")
        elif state == 1:
            require(line == "Disassembly of section .data:", "LISTING_HEADER")
        elif state == 2:
            label = _LABEL.fullmatch(line)
            require(label is not None and int(label.group(1), 16) ==
                    target_spec["preferred_vma_start"], "LISTING_HEADER")
        else:
            require(len(records) < LIMITS["instructions"], "LISTING_RECORDS")
            address, raw, raw_hex, assembly = _instruction(line)
            require(address == target_spec["preferred_vma_start"] + cursor,
                    "LISTING_ADDRESS")
            require(cursor + len(raw) <= len(target_bytes) and
                    raw == target_bytes[cursor:cursor + len(raw)], "LISTING_BYTES")
            records.append({"file_offset": target_spec["file_start"] + cursor,
                            "rva": target_spec["rva_start"] + cursor,
                            "preferred_vma": address, "length": len(raw),
                            "raw_hex": raw_hex, "assembly": assembly})
            cursor += len(raw)
            continue
        state += 1
    require(state == 3 and records and cursor == len(target_bytes), "LISTING_COVERAGE")
    return records
