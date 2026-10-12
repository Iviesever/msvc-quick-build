"""One static inspection; optional scalar context from that same failed call.

The owner verifies exact source/input identities before calling this module.
Context is a bounded traceback snapshot, not native execution or an unwind
interpretation. It never maps, reads, decodes or reparses the supplied bytes.
Successful indexes remain complete; the owner bounds their serialization.
"""

from types import FunctionType


MAX_METADATA_BYTES = 8192
_MAX_TRACEBACK_NODES = 64
_ROOT_KEYS = ("index", "offset", "begin", "end", "unwind")
_UNWIND_KEYS = ("rva", "offset", "version", "flags", "count", "frame", "slot",
                "code_offset", "operation", "opcode", "info")
_POLICY_ID = "AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED"
_POLICY_NOTICE = "CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED"
_FUNCTIONS = ("inspect_bcrypt_pe", "_require", "_end", "_disjoint")
_METHODS = ("__init__", "spend", "read", "map", "string", "headers", "certificates",
            "thunks", "imports", "runtime_function", "unwind_info", "exceptions",
            "unknown_iat_slots", "check_metadata_ranges")


def _bind(parser):
    """Capture code identities before inspect, including its mapped helpers."""
    inspector, image_type = parser.inspect_bcrypt_pe, parser._Image
    codes = {}
    members = [(name, getattr(parser, name, None)) for name in _FUNCTIONS]
    members += [("_Image." + name, getattr(image_type, name, None)) for name in _METHODS]
    for label, function in members:
        if type(function) is FunctionType:
            code = function.__code__
            lines = frozenset(line for _, _, line in code.co_lines() if line is not None)
            codes[id(code)] = (code, label, lines)
    return inspector, parser.PeValidationError, image_type, codes


def _snapshot(locals_dict, keys):
    result = {}
    for key in keys:
        value = locals_dict.get(key)
        if type(value) is int and 0 <= value <= (1 << 64) - 1:
            result[key] = value
    return result


def _context(binding, data, error):
    _, error_type, image_type, codes = binding
    if type(error) is not error_type:
        return None
    node, count, image = error.__traceback__, 0, None
    sites, root, unwind = [], None, None
    while node is not None:
        count += 1
        if count > _MAX_TRACEBACK_NODES:
            return None
        frame = node.tb_frame
        entry = codes.get(id(frame.f_code))
        if entry is None or entry[0] is not frame.f_code:
            if image is not None:
                return None
            node = node.tb_next
            continue
        _, label, lines = entry
        if not (type(node.tb_lineno) is int and 0 < node.tb_lineno <= 0xFFFFFFFF
                and node.tb_lineno in lines):
            return None
        values = frame.f_locals
        if label == "inspect_bcrypt_pe":
            if image is not None or values.get("data") is not data:
                return None
            candidate = values.get("image")
            if type(candidate) is not image_type or candidate.data is not data:
                return None
            image = candidate
        elif image is None:
            return None
        if label.startswith("_Image.") and values.get("self") is not image:
            return None
        if label == "_Image.exceptions":
            root = _snapshot(values, _ROOT_KEYS)
        elif label == "_Image.unwind_info":
            unwind = _snapshot(values, _UNWIND_KEYS)
        sites.append({"function": label, "line": node.tb_lineno})
        node = node.tb_next
    if image is None:
        return None
    ignored = image.v1_set_fpreg_nonzero_opinfo_ignored
    if type(ignored) is not bool:
        return None
    # Compact ASCII metadata is <= 512 + 64*72 + 16*48 + 212 = 6100 B.
    # Only fixed keys/labels, uint32 lines, uint64 snapshots and one bool occur.
    return {"traceback_nodes": count, "sites": sites, "root": root, "unwind": unwind,
            "compatibility_policy": {"id": _POLICY_ID, "nonzero_opinfo_ignored": ignored,
                                     "notice": _POLICY_NOTICE}}


def _reason(error):
    value = error.reason
    if type(value) is str and 0 < len(value) <= 64:
        if all("A" <= char <= "Z" or "0" <= char <= "9" or char == "_" for char in value):
            return value
    return None


def observe(parser, data, init_rva):
    """Call one owner-verified parser once and keep its complete static index."""
    binding = _bind(parser)
    result = {"schema": "bcrypt-pe-core-013-v1", "status": "parser_error", "parser_calls": 1,
              "reason": "PARSER_EXCEPTION", "exception_type": "Exception", "refusal_context": None,
              "static_index_admitted": False, "index": None}
    try:
        index = binding[0](data, init_rva)
    except Exception as error:
        if type(error) is binding[1]:
            reason = _reason(error)
            if reason is not None:
                result.update(status="parser_refused", reason=reason, exception_type="PeValidationError")
                try:
                    result["refusal_context"] = _context(binding, data, error)
                except Exception:
                    result["refusal_context"] = None
        error.__traceback__ = None
    else:
        result.update(status="indexed", reason=None, exception_type=None,
                      static_index_admitted=True, index=index)
    return result
