"""Bounded context from one frozen-parser call; no files, native code or PE admission.

The owner verifies exact module/source/input identity and its deadline before
calling diagnose. Only code identities and locals of the failing call are used
here. No parser mapping/read method is called to obtain additional context.
"""

import hashlib
import json
from types import FunctionType, ModuleType, SimpleNamespace

MAX_TRACEBACK_NODES = 64
MAX_CHAIN_RVAS = 32
MAX_ENVELOPE_BYTES = 528
MAX_CONTEXT_WORK = 16384
MAX_CORE_BYTES = 8192
_RESERVE = 1024
_GUARDS = {427: (1, "info in (0, 1)"),
           438: (3, "info == 0 and frame & 15 != 0"),
           440: (10, "info in (0, 1)")}


class _Unavailable(ValueError):
    pass


def _need(condition, reason="CONTEXT_INVALID"):
    if not condition:
        raise _Unavailable(reason)


class _Work:
    def __init__(self):
        self.used = 0

    def spend(self, units):
        _need(type(units) is int and 0 <= units <= MAX_CONTEXT_WORK - _RESERVE - self.used,
              "CONTEXT_WORK_LIMIT")
        self.used += units


def _uint(values, key, maximum, work):
    work.spend(1)
    _need(type(values) is dict and key in values, "MISSING_SCALAR")
    value = values[key]
    _need(type(value) is int and 0 <= value <= maximum, "SCALAR_TYPE_OR_RANGE")
    return value


def _bind(module):
    _need(type(module) in (ModuleType, SimpleNamespace), "MODULE_BINDING")
    values = vars(module)
    image, error = values.get("_Image"), values.get("PeValidationError")
    _need(type(image) is type and type(error) is type and issubclass(error, ValueError), "MODULE_BINDING")
    functions = {"inspect": values.get("inspect_bcrypt_pe"), "require": values.get("_require"),
                 "unwind": vars(image).get("unwind_info"), "exceptions": vars(image).get("exceptions")}
    _need(all(type(value) is FunctionType for value in functions.values()), "MODULE_BINDING")
    return {"call": functions["inspect"], "image_type": image, "error_type": error,
            **{key: value.__code__ for key, value in functions.items()}}


def _blob(data, offset, size, work):
    _need(type(offset) is int and type(size) is int and 0 <= offset <= len(data)
          and 0 < size <= MAX_ENVELOPE_BYTES and size <= len(data) - offset, "RAW_BOUNDS")
    work.spend(size * 4)  # Copy, hash and two hex characters per source byte.
    raw = data[offset:offset + size]
    return raw, {"file_offset": offset, "bytes": size, "raw_hex": raw.hex(),
                 "sha256": hashlib.sha256(raw).hexdigest()}


def _root_entry(local, data, work):
    directory = local.get("directory")
    offset = _uint(local, "offset", len(data), work)
    index = _uint(local, "index", 16383, work)
    count = _uint(local, "count", 16384, work)
    base = _uint(directory, "file_offset", len(data), work)
    size = _uint(directory, "bytes", 512 * 1024, work)
    rva = _uint(directory, "rva", 0x7fffffff, work)
    begin, end, unwind = (_uint(local, name, 0x7fffffff, work) for name in ("begin", "end", "unwind"))
    _need(0 <= index < count and size == count * 12 and offset == base + index * 12
          and 12 <= base + size - offset and 0 < begin < end and unwind > 0, "ROOT_ENTRY_BOUNDS")
    raw, result = _blob(data, offset, 12, work)
    work.spend(12)
    _need(tuple(int.from_bytes(raw[i:i + 4], "little") for i in (0, 4, 8)) == (begin, end, unwind),
          "ROOT_ENTRY_BYTES")
    result.update(index=index, rva=rva + index * 12, begin_rva=begin, end_rva=end, unwind_rva=unwind)
    return result


def _extract(binding, data, init_rva, error, work):
    _need(type(error) is binding["error_type"], "EXCEPTION_IDENTITY")
    _need(type(error.reason) is str and error.reason == "UNWIND_OPCODE_INVALID", "REASON_NOT_TARGET")
    nodes, node = [], error.__traceback__
    while node is not None:
        work.spend(1)
        _need(len(nodes) < MAX_TRACEBACK_NODES, "TRACEBACK_LIMIT")
        nodes.append(node)
        node = node.tb_next
    _need(len(nodes) >= 4, "TRACEBACK_IDENTITY")
    work.spend(len(nodes) * 3)
    guards = [i for i, tb in enumerate(nodes) if tb.tb_frame.f_code is binding["unwind"] and tb.tb_lineno in _GUARDS]
    starts = [i for i, tb in enumerate(nodes) if tb.tb_frame.f_code is binding["inspect"]]
    roots = [i for i, tb in enumerate(nodes) if tb.tb_frame.f_code is binding["exceptions"]]
    _need(guards == [len(nodes) - 2] and len(starts) == len(roots) == 1, "TRACEBACK_IDENTITY")
    start, root = starts[0], roots[0]
    _need(root == start + 1 and nodes[start].tb_lineno == 543 and nodes[root].tb_lineno == 483,
          "ROOT_LINEAGE")
    terminal = nodes[-1]
    _need(terminal.tb_frame.f_code is binding["require"] and terminal.tb_lineno == 64, "REQUIRE_IDENTITY")
    required = terminal.tb_frame.f_locals
    _need(required.get("condition") is False and required.get("reason") == error.reason, "REQUIRE_PREDICATE")
    chain = nodes[root + 1:-1]
    _need(1 <= len(chain) <= MAX_CHAIN_RVAS, "CHAIN_LIMIT")
    entry_local = nodes[start].tb_frame.f_locals
    shared = entry_local.get("image")
    _need(type(shared) is binding["image_type"] and shared.data is data and entry_local.get("data") is data
          and type(entry_local.get("init_rva")) is int and entry_local["init_rva"] == init_rva, "INPUT_IDENTITY")
    root_local = nodes[root].tb_frame.f_locals
    _need(root_local.get("self") is shared, "SELF_IDENTITY")
    lineage, next_rva = [], None
    for index, tb in enumerate(chain):
        work.spend(8)
        _need(tb.tb_frame.f_code is binding["unwind"]
              and tb.tb_lineno == (nodes[-2].tb_lineno if index == len(chain) - 1 else 450), "CHAIN_LINEAGE")
        local = tb.tb_frame.f_locals
        _need(local.get("self") is shared, "SELF_IDENTITY")
        rva = _uint(local, "rva", 0x7fffffff, work)
        path = local.get("path")
        _need(type(path) is tuple and len(path) == index, "CHAIN_PATH")
        work.spend(len(path) * 3)
        _need(all(type(item) is int for item in path) and path == tuple(lineage)
              and rva > 0 and rva % 4 == 0 and rva not in path, "CHAIN_PATH")
        _need(index == 0 or rva == next_rva, "CHAIN_TARGET")
        lineage.append(rva)
        if index < len(chain) - 1:
            _need(_uint(local, "flags", 7, work) == 4, "CHAIN_FLAGS")
            next_rva = _uint(local, "chained_rva", 0x7fffffff, work)
    root_entry = _root_entry(root_local, data, work)
    _need(root_entry["unwind_rva"] == lineage[0], "ROOT_UNWIND_IDENTITY")
    # At these three guards, lines 418/419 already mapped and charged the full
    # envelope. offset came from the prior header map. Never map/read again.
    local = chain[-1].tb_frame.f_locals
    fields = {key: _uint(local, key, 255, work) for key in
              ("vf", "prolog", "count", "frame", "version", "flags", "slot", "code_offset", "operation", "opcode", "info")}
    offset = _uint(local, "offset", len(data), work)
    body = _uint(local, "body_size", 516, work)
    size = _uint(local, "size", MAX_ENVELOPE_BYTES, work)
    f = fields
    _need(f["version"] in (1, 2) and f["version"] == f["vf"] & 7 and f["flags"] == f["vf"] >> 3
          and f["flags"] <= 7 and not (f["flags"] & 4 and f["flags"] & 3)
          and (f["frame"] & 15 or f["frame"] == 0), "HEADER_SCALARS")
    _need(body == 4 + ((f["count"] + 1) & ~1) * 2
          and size == body + (12 if f["flags"] & 4 else 4 if f["flags"] & 3 else 0)
          and f["slot"] < f["count"], "ENVELOPE_SCALARS")
    line = chain[-1].tb_lineno
    opcode, predicate = _GUARDS[line]
    _need(f["opcode"] == opcode == f["operation"] & 15 and f["info"] == f["operation"] >> 4,
          "OPCODE_SCALARS")
    _need((f["info"] not in (0, 1)) if line != 438 else (f["info"] != 0 or f["frame"] & 15 == 0),
          "GUARD_PREDICATE")
    # ALLOC_LARGE refuses before assigning current width; do not even read a
    # possible previous iteration's local. Other two guards have current 1.
    width = None if line == 427 else _uint(local, "width", 3, work)
    _need(width is None or width == 1, "CURRENT_WIDTH")
    raw, envelope = _blob(data, offset, size, work)
    slot_offset = 4 + f["slot"] * 2
    work.spend(6)
    _need(raw[:4] == bytes((f["vf"], f["prolog"], f["count"], f["frame"]))
          and raw[slot_offset:slot_offset + 2] == bytes((f["code_offset"], f["operation"])), "ENVELOPE_BYTES")
    envelope["rva"] = lineage[-1]
    leaf = {key: value for key, value in fields.items() if key not in ("vf", "info")}
    leaf.update(rva=lineage[-1], file_offset=offset, body_size=body, size=size, opinfo=f["info"], width=width,
                width_state="not_assigned_for_current_slot" if width is None else "assigned_for_current_slot")
    return {"guard": {"function": "_Image.unwind_info", "line": line, "require_line": 64,
                      "predicate": predicate, "predicate_value": False},
            "root_entry": root_entry, "leaf": leaf, "chain_rvas": lineage, "envelope": envelope,
            "views": {"header": {"offset": 0, "bytes": 4}, "slot": {"offset": slot_offset, "bytes": 2}},
            "source_lineage": {"inspect_line": 543, "exceptions_line": 483, "chain_call_line": 450,
                               "traceback_nodes": len(nodes)}}


def _empty():
    return {"schema": "bcrypt-refusal-context-012-v1", "status": "context_unavailable", "parser_calls": 0,
            "reason": None, "exception_type": None, "context": None, "context_unavailable_reason": None,
            "context_work_units": 0, "static_index_admitted": False}


def _short(value):
    return type(value) is str and len(value) <= 128 and value.isascii()


def _finish(result, work):
    """Bound shape, encoded size and work before serializing; reserve small failure output."""
    try:
        result["context_work_units"] = MAX_CONTEXT_WORK  # Reserve its largest decimal width.
        stack, visited, encoded_size = [(result, 0)], 0, 0
        while stack:
            value, depth = stack.pop()
            visited += 1
            work.spend(1)
            _need(visited <= 256 and depth <= 8, "OUTPUT_SHAPE_LIMIT")
            kind = type(value)
            if kind is str:
                _need(len(value) <= MAX_CORE_BYTES and value.isascii(), "OUTPUT_STRING_LIMIT")
                work.spend(len(value))
                encoded_size += 2 + sum(6 if ord(c) < 32 or c == '\x7f' else 2 if c in '\\"' else 1 for c in value)
            elif kind is int:
                _need(0 <= value <= 0xffffffffffffffff, "OUTPUT_INTEGER_RANGE")
                length = len(str(value))
                work.spend(length)
                encoded_size += length
            elif value is None or kind is bool:
                encoded_size += 4 if value is None or value else 5
            elif kind in (dict, list):
                _need(len(value) <= 32, "OUTPUT_CONTAINER_LIMIT")
                encoded_size += 2 + max(0, len(value) - 1) + (len(value) if kind is dict else 0)
                if kind is dict:
                    _need(all(type(key) is str and len(key) <= 64 for key in value), "OUTPUT_KEY_TYPE")
                    stack.extend((item, depth + 1) for pair in value.items() for item in pair)
                else:
                    stack.extend((item, depth + 1) for item in value)
            else:
                raise _Unavailable("OUTPUT_VALUE_TYPE")
            _need(encoded_size <= MAX_CORE_BYTES, "OUTPUT_BYTE_LIMIT")
        work.spend(encoded_size * 2)  # JSON serialization and encoded-byte pass.
        result["context_work_units"] = work.used
        encoded = json.dumps(result, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode("ascii")
        _need(len(encoded) <= MAX_CORE_BYTES, "OUTPUT_BYTE_LIMIT")
        return result
    except (ValueError, TypeError, OverflowError) as error:
        fallback = _empty()
        fallback["parser_calls"] = 1 if result.get("parser_calls") == 1 else 0
        for key in ("reason", "exception_type"):
            if _short(result.get(key)):
                fallback[key] = result[key]
        fallback["context_unavailable_reason"] = str(error) if type(error) is _Unavailable else "OUTPUT_SERIALIZATION"
        work.used += _RESERVE
        fallback["context_work_units"] = work.used
        return fallback


def diagnose(module, image_bytes, init_rva):
    """Make at most one inspection; return only bounded refusal observability."""
    work, result = _Work(), _empty()
    try:
        binding = _bind(module)
        _need(type(image_bytes) is bytes and 0 < len(image_bytes) <= 1048576
              and type(init_rva) is int and 0 < init_rva <= 0xffffffff, "INPUT_TYPE_OR_BOUND")
    except _Unavailable as error:
        result["context_unavailable_reason"] = str(error)
        return _finish(result, work)
    result["parser_calls"] = 1
    try:
        binding["call"](image_bytes, init_rva)
    except Exception as error:
        try:
            name = type(error).__name__
            _need(_short(name), "EXCEPTION_NAME_UNAVAILABLE")
            result["exception_type"] = name
            if type(error) is binding["error_type"]:
                _need(_short(error.reason), "REASON_UNAVAILABLE")
                result["reason"] = error.reason
                result["context"] = _extract(binding, image_bytes, init_rva, error, work)
                result["status"] = "located_refusal"
            else:
                result["status"] = "unexpected_exception"
        except _Unavailable as unavailable:
            result["context_unavailable_reason"] = str(unavailable)
        except Exception:
            result["context_unavailable_reason"] = "CONTEXT_EXTRACTION_FAILED"
        finally:
            error.__traceback__ = None
    else:
        result["status"] = "unexpected_success"
    return _finish(result, work)
