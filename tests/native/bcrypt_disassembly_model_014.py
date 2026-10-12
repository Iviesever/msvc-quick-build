"""Frozen manifest and bounded JSON/target model for the 014 static observation."""
import hashlib
import json
import posixpath
import re

LIMITS = {
    "owner_seconds": 12, "child_cpu_seconds": 2,
    "child_address_space_bytes": 268435456, "child_file_bytes": 131072,
    "child_core_bytes": 0, "child_open_files": 64, "manifest_bytes": 32768,
    "image_bytes": 262144, "index_bytes": 327680, "admission_bytes": 32768,
    "identity_bytes": 67108864, "identity_total_bytes": 67108864,
    "identity_chunk_bytes": 65536, "source_count": 5, "source_bytes": 32768,
    "identity_roles": 14, "alias_count": 12, "absent_path_count": 8,
    "path_chars": 256, "tool_argv_count": 18, "owner_stderr_bytes": 65,
    "owner_stdout_bytes": 0, "target_bytes": 391, "instructions": 391,
    "instruction_bytes": 15, "raw_line_bytes": 512, "assembly_chars": 384,
    "report_bytes": 524288, "marker_bytes": 1024, "report_wrapper_bytes": 16384,
}
ENV = {"LANG": "C", "LC_ALL": "C", "TZ": "UTC", "DEBUGINFOD_URLS": ""}
SOURCE_NAMES = (
    "bcrypt_disassembly_model_014.py", "bcrypt_disassembly_listing_014.py",
    "bcrypt_disassembly_io_014.py", "bcrypt_disassembly_owner_014.py",
    "run_bcrypt_disassembly_014.py",
)
ACTUAL_IMAGE = {"bytes": 170984, "sha256": "663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb"}
ACTUAL_INDEX = {"bytes": 271920, "sha256": "ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e"}
ACTUAL_ADMISSION = {"bytes": 20232, "sha256": "c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b"}
ACTUAL_TARGET_SHA256 = "e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44"
ACTUAL_TOOL = {"path": "/usr/bin/x86_64-linux-gnu-objdump", "bytes": 390848,
               "sha256": "7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5"}
ACTUAL_PYTHON = {"bytes": 30894944, "sha256": "fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7"}
ACTUAL_TARGET = {"runtime_function_index": 169, "section_index": 0,
                 "file_start": 42384, "file_end": 42775, "rva_start": 42384,
                 "rva_end": 42775, "preferred_vma_start": 6442493328,
                 "sha256": ACTUAL_TARGET_SHA256}
U64 = (1 << 64) - 1


class Refusal(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def require(condition, code):
    if not condition:
        raise Refusal(code)


def _integer(value, low=0, high=U64):
    require(type(value) is int and low <= value <= high, "INTEGER")


def _codec_limits(cap, max_nodes):
    _integer(cap, 1, LIMITS["report_bytes"])
    _integer(max_nodes, 1, 32768)


def _measure(value, cap, max_nodes):
    """Before encoding: <=max_nodes visits, <=cap character visits, depth <=16."""
    _codec_limits(cap, max_nodes)
    total, nodes, active = 1, 0, set()  # The final LF is included from the start.

    def add(size):
        nonlocal total
        total += size
        require(total <= cap, "JSON_BYTES")

    def walk(item, depth):
        nonlocal nodes
        nodes += 1
        require(nodes <= max_nodes and depth <= 16, "JSON_GRAPH")
        kind = type(item)
        if kind is str:
            require(len(item) <= cap, "JSON_STRING")
            add(2)
            for character in item:
                n = ord(character)
                require(not 0xD800 <= n <= 0xDFFF, "JSON_UNICODE")
                add(2 if character in '\\"\b\f\n\r\t' else
                    1 if 32 <= n < 127 else 6 if n <= 65535 else 12)
        elif item is None or kind is bool:
            add(4 if item is None or item is True else 5)
        elif kind is int:
            _integer(item, -(1 << 63))
            add(len(str(item)))
        elif kind in (dict, list):
            require(depth < 16 and len(item) <= max_nodes and id(item) not in active,
                    "JSON_GRAPH")
            active.add(id(item))
            add(2 + max(0, len(item) - 1) + (len(item) if kind is dict else 0))
            if kind is dict:
                for key, child in item.items():
                    require(type(key) is str, "JSON_KEY")
                    walk(key, depth + 1)
                    walk(child, depth + 1)
            else:
                for child in item:
                    walk(child, depth + 1)
            active.remove(id(item))
        else:
            raise Refusal("JSON_TYPE")

    walk(value, 0)
    return total


def encode_json(value, cap, max_nodes=32768):
    size = _measure(value, cap, max_nodes)
    raw = (json.dumps(value, ensure_ascii=True, allow_nan=False, sort_keys=True,
                      separators=(",", ":")) + "\n").encode("ascii")
    require(len(raw) == size, "JSON_SIZE_MISMATCH")
    return raw


def decode_json(raw, cap, max_nodes=32768):
    """Bound raw allocation, depth and lexical nodes before calling json.loads."""
    _codec_limits(cap, max_nodes)
    require(type(raw) is bytes and 0 < len(raw) <= cap, "JSON_BYTES")
    depth = nodes = 0
    quoted = escaped = token = False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted, token = True, False
            nodes += 1
        elif byte in (91, 123):
            depth += 1
            nodes += 1
            token = False
        elif byte in (93, 125):
            depth -= 1
            token = False
        elif byte in b" \t\r\n,:":
            token = False
        elif not token:
            nodes += 1
            token = True
        require(0 <= depth <= 16 and nodes <= max_nodes, "JSON_GRAPH")
    require(not quoted and depth == 0, "JSON_SYNTAX")

    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "JSON_DUPLICATE_KEY")
            result[key] = value
        return result

    def integer(text):
        require(len(text) <= 20, "JSON_NUMBER")
        value = int(text)
        _integer(value, -(1 << 63))
        return value

    def no_float(_):
        raise Refusal("JSON_NUMBER")

    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                           parse_int=integer, parse_float=no_float, parse_constant=no_float)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise Refusal("JSON_SYNTAX") from error
    _measure(value, cap, max_nodes)
    return value


def _keys(value, names):
    require(type(value) is dict and set(value) == set(names), "FIELDS")


def _hex(value, size=64):
    require(type(value) is str and len(value) == size and
            all(c in "0123456789abcdef" for c in value), "HEX")


def _path(value):
    require(type(value) is str and 1 < len(value) <= LIMITS["path_chars"] and
            value.startswith("/") and not value.startswith("//") and
            all(32 <= ord(c) < 127 for c in value) and
            posixpath.normpath(value) == value and not value.endswith("/"), "PATH")


def _identity(record, cap):
    _keys(record, ("path", "bytes", "sha256"))
    _path(record["path"])
    _integer(record["bytes"], 0, cap)
    _hex(record["sha256"])


def _target(target):
    _keys(target, ACTUAL_TARGET)
    for key in ACTUAL_TARGET:
        if key != "sha256":
            _integer(target[key])
    _hex(target["sha256"])
    length = target["rva_end"] - target["rva_start"]
    require(1 <= length <= LIMITS["target_bytes"] and
            target["file_end"] - target["file_start"] == length and
            target["preferred_vma_start"] + length <= U64, "TARGET_RANGE")
    return length


def tool_argv(tool_path, target):
    _path(tool_path)
    length = _target(target)
    start = target["preferred_vma_start"]
    return [tool_path, "-D", "-z", "-b", "binary", "-m", "i386:x86-64", "-M",
            "intel", "--insn-width=16", "--wide", "--show-raw-insn",
            "--visualize-jumps=off", "--disassembler-color=off",
            f"--adjust-vma={start:#x}", f"--start-address={start:#x}",
            f"--stop-address={start + length:#x}", "target.bin"]


def validate_manifest(manifest):
    _measure(manifest, LIMITS["manifest_bytes"], 32768)
    _keys(manifest, ("schema", "mode", "call_id", "source_commit", "output_dir",
                     "identities", "sources", "aliases", "absent_paths", "target",
                     "tool_argv", "environment", "limits"))
    m = manifest
    require(m["schema"] == "bcrypt-disassembly-manifest-014-v1" and
            m["mode"] in ("actual", "synthetic"), "MANIFEST_MODE")
    require(type(m["call_id"]) is str and len(m["call_id"]) <= 64, "CALL_ID")
    require(m["call_id"] == "014-init-static-1" if m["mode"] == "actual" else
            re.fullmatch(r"014-synthetic-[A-Za-z0-9][A-Za-z0-9_-]*", m["call_id"]), "CALL_ID")
    _hex(m["source_commit"], 40)
    _path(m["output_dir"])
    _keys(m["limits"], LIMITS)
    require(all(type(v) is int for v in m["limits"].values()) and m["limits"] == LIMITS, "LIMITS")
    _keys(m["environment"], ENV)
    require(m["environment"] == ENV, "ENVIRONMENT")
    roles = ("image", "index", "admission", "python", "tool", "loader_cache",
             *(f"dep{i:02d}" for i in range(1, 9)))
    _keys(m["identities"], roles)
    _keys(m["sources"], SOURCE_NAMES)
    records, parents, total = [], set(), 0
    for role, record in m["identities"].items():
        _identity(record, LIMITS.get(role + "_bytes", LIMITS["identity_bytes"]))
        records.append(record)
    for name, record in m["sources"].items():
        _identity(record, LIMITS["source_bytes"])
        require(posixpath.basename(record["path"]) == name, "SOURCE_NAME")
        parents.add(posixpath.dirname(record["path"]))
        records.append(record)
    paths = {r["path"] for r in records}
    require(len(parents) == 1 and len(paths) == len(records), "IDENTITY_PATHS")
    total = sum(r["bytes"] for r in records)
    require(total <= LIMITS["identity_total_bytes"] and m["output_dir"] not in paths and
            not any(p.startswith(m["output_dir"] + "/") for p in paths), "IDENTITY_BUDGET")
    require(type(m["aliases"]) is list and len(m["aliases"]) <= LIMITS["alias_count"], "ALIASES")
    occupied = paths | {m["output_dir"]}
    for alias in m["aliases"]:
        _keys(alias, ("path", "target"))
        _path(alias["path"])
        _path(alias["target"])
        require(alias["target"] in paths and alias["path"] not in occupied, "ALIAS_TARGET")
        occupied.add(alias["path"])
    require(type(m["absent_paths"]) is list and len(m["absent_paths"]) <= LIMITS["absent_path_count"], "ABSENT_PATHS")
    for path in m["absent_paths"]:
        _path(path)
        require(path not in occupied, "ABSENT_PATH")
        occupied.add(path)
    _target(m["target"])
    require(type(m["tool_argv"]) is list and m["tool_argv"] ==
            tool_argv(m["identities"]["tool"]["path"], m["target"]), "TOOL_ARGV")
    if m["mode"] == "actual":
        for role, expected in (("image", ACTUAL_IMAGE), ("index", ACTUAL_INDEX),
                               ("admission", ACTUAL_ADMISSION), ("tool", ACTUAL_TOOL),
                               ("python", ACTUAL_PYTHON)):
            require(all(m["identities"][role][k] == v for k, v in expected.items()), "ACTUAL_IDENTITY")
        require(m["target"] == ACTUAL_TARGET, "ACTUAL_TARGET")
    else:
        forbidden = {ACTUAL_IMAGE["sha256"], ACTUAL_INDEX["sha256"], ACTUAL_TARGET_SHA256}
        require(all(r["sha256"] not in forbidden for r in records) and
                m["target"]["sha256"] not in forbidden, "SYNTHETIC_ACTUAL_IDENTITY")
    return manifest


def select_target(manifest, image_bytes, index_bytes, admission_bytes):
    m = validate_manifest(manifest)
    for role, raw in (("image", image_bytes), ("index", index_bytes), ("admission", admission_bytes)):
        record = m["identities"][role]
        require(type(raw) is bytes and len(raw) == record["bytes"] and
                hashlib.sha256(raw).hexdigest() == record["sha256"], "TARGET_INPUT_IDENTITY")
    report = decode_json(index_bytes, LIMITS["index_bytes"])
    admission = decode_json(admission_bytes, LIMITS["admission_bytes"])
    t = m["target"]
    try:
        require(report["schema"] == "bcrypt-pe-index-013-report-v1" and
                report["status"] == "complete_index" and report["static_index_admitted"] is True, "INDEX_STATUS")
        core = report["core"]
        require(core["schema"] == "bcrypt-pe-core-013-v1" and core["status"] == "indexed" and
                core["static_index_admitted"] is True, "INDEX_STATUS")
        index = core["index"]
        require(index["schema"] == "bcrypt-pe-index-013-v1" and
                index["runtime_execution_observed"] is False and
                index["sha256"] == m["identities"]["image"]["sha256"] and
                type(index["bytes"]) is int and index["bytes"] == len(image_bytes), "INDEX_IMAGE")
        header, entry = index["header"], index["entrypoint"]
        for key in ("machine", "optional_header_magic", "address_of_entrypoint", "image_base"):
            _integer(header[key])
        require(header["machine"] == 34404 and header["optional_header_magic"] == 523 and
                header["address_of_entrypoint"] == t["rva_start"] and
                header["image_base"] + t["rva_start"] == t["preferred_vma_start"], "HEADER_MAPPING")
        for key, expected in (("file_offset", t["file_start"]), ("rva", t["rva_start"]),
                              ("loader_init_rva", t["rva_start"]),
                              ("runtime_function_index", t["runtime_function_index"]),
                              ("section_index", t["section_index"])):
            _integer(entry[key])
            require(entry[key] == expected, "ENTRY_MAPPING")
        require(type(index["sections"]) is list and type(index["runtime_functions"]) is list, "INDEX_LISTS")
        section = index["sections"][t["section_index"]]
        function = index["runtime_functions"][t["runtime_function_index"]]
        for key in ("index", "raw_file_offset", "raw_size", "file_backed_size", "virtual_address", "characteristics"):
            _integer(section[key])
        require(section["index"] == t["section_index"] and section["characteristics"] & 0x20000000 and
                0 < section["file_backed_size"] <= section["raw_size"] and
                section["raw_file_offset"] + section["raw_size"] <= len(image_bytes) and
                section["raw_file_offset"] <= t["file_start"] < t["file_end"] <=
                section["raw_file_offset"] + section["file_backed_size"], "SECTION_RANGE")
        for rva, file_offset in ((t["rva_start"], t["file_start"]), (t["rva_end"], t["file_end"])):
            require(rva >= section["virtual_address"] and
                    section["raw_file_offset"] + rva - section["virtual_address"] == file_offset, "SECTION_MAPPING")
        for key, expected in (("index", t["runtime_function_index"]), ("section_index", t["section_index"]),
                              ("begin_rva", t["rva_start"]), ("end_rva", t["rva_end"]),
                              ("begin_file_offset", t["file_start"]), ("end_file_offset", t["file_end"])):
            _integer(function[key])
            require(function[key] == expected, "FUNCTION_MAPPING")
        require(admission["schema"] == "bcrypt013-root-joint-runtime-admission-v1" and
                admission["decision"] == "ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013" and
                admission["status"] == "COMPLETE_INDEX_INDEPENDENTLY_REVIEWED", "INDEX_ADMISSION")
        expected = {"runtime_function_index": t["runtime_function_index"], "section_index": t["section_index"],
                    "file_range": [t["file_start"], t["file_end"]], "rva_range": [t["rva_start"], t["rva_end"]],
                    "function_bytes": t["file_end"] - t["file_start"], "function_sha256": t["sha256"],
                    "image_base_preferred": header["image_base"], "preferred_vma_range":
                    [t["preferred_vma_start"], t["preferred_vma_start"] + t["rva_end"] - t["rva_start"]]}
        concrete = admission["concrete_next_static_target"]
        for key, value in expected.items():
            require(type(concrete[key]) is type(value) and concrete[key] == value, "ADMISSION_TARGET")
            if type(value) is list:
                require(all(type(n) is int for n in concrete[key]), "ADMISSION_TARGET")
    except (KeyError, TypeError, IndexError) as error:
        raise Refusal("INDEX_SHAPE") from error
    result = image_bytes[t["file_start"]:t["file_end"]]
    require(hashlib.sha256(result).hexdigest() == t["sha256"], "TARGET_DIGEST")
    return result
