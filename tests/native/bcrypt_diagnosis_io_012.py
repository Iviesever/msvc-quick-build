"""Fixed manifest, identity and exclusive evidence IO for offline diagnosis 012."""

import hashlib
import json
import os
from pathlib import Path
import stat
import sys

MANIFEST_SCHEMA = "bcrypt-pe-diagnosis-012-manifest-v1"
MARKER_SCHEMA = "bcrypt-pe-diagnosis-012-consumed-v1"
CALL_ID = "012-refusal-context-1"
PARSER_SHA256 = "941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba"


def require(value, code):
    if not value:
        raise ValueError(code)


def encode(value):
    return json.dumps(value, allow_nan=False, ensure_ascii=True, sort_keys=True,
                      separators=(",", ":")).encode("ascii")


def decode(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            require(key not in value, "JSON_DUPLICATE")
            value[key] = item
        return value

    def constant(_value):
        raise ValueError("JSON_CONSTANT")

    try:
        return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)
    except (UnicodeError, RecursionError, json.JSONDecodeError):
        raise ValueError("JSON_INVALID") from None


def read_max(path, maximum):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), "FILE_TYPE")
        raw = stream.read(maximum + 1)
    require(len(raw) <= maximum, "FILE_SIZE")
    return raw


def path_value(value, *, existing=True):
    require(type(value) is str and 0 < len(value) <= 512 and value.isascii()
            and "\0" not in value, "PATH")
    path = Path(value)
    require(path.is_absolute() and str(path.resolve(strict=existing)) == value, "PATH")
    return path


def description(value, maximum, *, extra=()):
    require(type(value) is dict and set(value) == {"path", "bytes", "sha256", *extra},
            "IDENTITY_FIELDS")
    path_value(value["path"])
    require(type(value["bytes"]) is int and 0 < value["bytes"] <= maximum,
            "IDENTITY_SIZE")
    digest = value["sha256"]
    require(type(digest) is str and len(digest) == 64
            and all(c in "0123456789abcdef" for c in digest), "IDENTITY_HASH")


def manifest(path, expected_sha, owner_path, limits):
    require(sys.platform == "linux" and sys.implementation.name == "cpython", "PLATFORM")
    path_value(path)
    raw = read_max(path, limits["manifest_bytes"])
    require(hashlib.sha256(raw).hexdigest() == expected_sha, "MANIFEST_HASH")
    value = decode(raw)
    fields = {"schema", "stage", "call_id", "input", "sources", "python",
              "argv_prefix", "output_dir", "limits"}
    require(type(value) is dict and set(value) == fields, "MANIFEST_FIELDS")
    require(value["schema"] == MANIFEST_SCHEMA and value["stage"] == "refusal_context"
            and value["call_id"] == CALL_ID, "MANIFEST_STAGE")
    require(type(value["limits"]) is dict and value["limits"] == limits
            and all(type(v) is int for v in value["limits"].values()), "LIMITS")
    description(value["input"], limits["input_bytes"], extra=("init_rva",))
    require(type(value["input"]["init_rva"]) is int
            and 0 <= value["input"]["init_rva"] <= 0xFFFFFFFF, "INIT_RVA")
    sources = value["sources"]
    require(type(sources) is dict and set(sources) == {"owner", "io", "context", "parser"},
            "SOURCE_FIELDS")
    for source in sources.values():
        description(source, limits["source_bytes"])
    require(sources["owner"]["path"] == str(owner_path)
            and sources["io"]["path"] == str(owner_path.with_name("bcrypt_diagnosis_io_012.py")),
            "SOURCE_PATH")
    require(sources["parser"]["bytes"] == 31555
            and sources["parser"]["sha256"] == PARSER_SHA256, "FROZEN_PARSER")
    python = value["python"]
    description(python, limits["python_bytes"], extra=("implementation", "version"))
    require(python["path"] == str(Path(sys.executable).resolve(strict=True))
            and python["implementation"] == "cpython"
            and type(python["version"]) is list
            and [type(v) for v in python["version"]] == [int, int, int, str, int]
            and python["version"] == list(sys.version_info), "PYTHON_RUNTIME")
    require(value["argv_prefix"] == [python["path"], "-I", "-S", "-B", str(owner_path),
                                     "--worker"], "ARGV")
    output = path_value(value["output_dir"], existing=False)
    path_value(str(output.parent))
    return value


def identities(value):
    return {"input": value["input"], **value["sources"], "python": value["python"]}


def verify(value, *, retain=False):
    retained = {}
    for role, item in identities(value).items():
        try:
            raw = read_max(item["path"], item["bytes"])
            require(len(raw) == item["bytes"]
                    and hashlib.sha256(raw).hexdigest() == item["sha256"], "MISMATCH")
        except (OSError, ValueError):
            raise ValueError("IDENTITY_" + role.upper()) from None
        if retain and role != "python":
            retained[role] = raw
    return retained


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def save(path, value, maximum):
    raw = encode(value)
    require(len(raw) <= maximum, "EVIDENCE_SIZE")
    with path.open("xb") as stream:
        require(stream.write(raw) == len(raw), "EVIDENCE_WRITE")
        stream.flush()
        os.fsync(stream.fileno())
    sync_directory(path.parent)
    require(read_max(path, maximum) == raw, "EVIDENCE_READBACK")


def marker_value(sha, pid):
    return {"schema": MARKER_SCHEMA, "manifest_sha256": sha, "owner_pid": pid,
            "call_id": CALL_ID, "parser_call_reservations": 1}


def reserve(value, sha):
    output = Path(value["output_dir"])
    try:
        output.mkdir(mode=0o700)
    except FileExistsError:
        raise ValueError("OUTPUT_EXISTS") from None
    sync_directory(output.parent)
    save(output / "consumed.json", marker_value(sha, os.getpid()), 1024)


def check_marker(value, sha):
    output = Path(value["output_dir"])
    require(not (output / "report.json").exists(), "WORKER_REPORT_EXISTS")
    raw = read_max(output / "consumed.json", 1024)
    require(raw == encode(marker_value(sha, os.getppid())), "WORKER_MARKER")


def core_result(raw):
    require(type(raw) is bytes and 0 < len(raw) <= 8192, "CORE_SIZE")
    try:
        value = decode(raw)
        fields = {"schema", "status", "parser_calls", "reason", "exception_type", "context",
                  "context_unavailable_reason", "context_work_units", "static_index_admitted"}
        require(type(value) is dict and set(value) == fields, "CORE_SCHEMA")
        require(value["schema"] == "bcrypt-refusal-context-012-v1"
                and value["status"] in {"located_refusal", "context_unavailable",
                                        "unexpected_success", "unexpected_exception"}
                and type(value["parser_calls"]) is int and value["parser_calls"] in (0, 1)
                and type(value["context_work_units"]) is int
                and 0 <= value["context_work_units"] <= 16384
                and value["static_index_admitted"] is False, "CORE_SCHEMA")
        for field in ("reason", "exception_type", "context_unavailable_reason"):
            text = value[field]
            require(text is None or (type(text) is str and len(text) <= 128
                                    and text.isascii()), "CORE_SCHEMA")
        if value["status"] == "located_refusal":
            require(value["reason"] == "UNWIND_OPCODE_INVALID"
                    and value["exception_type"] == "PeValidationError"
                    and value["parser_calls"] == 1 and type(value["context"]) is dict
                    and value["context_unavailable_reason"] is None, "CORE_SCHEMA")
        else:
            require(value["context"] is None, "CORE_SCHEMA")
        require(encode(value) == raw, "CORE_SCHEMA")
    except (ValueError, TypeError, RecursionError):
        raise ValueError("CORE_SCHEMA") from None
    return value
