"""Versioned manifest, bounded codec and exclusive IO for offline index 013."""

import hashlib
import os
from pathlib import Path
import stat
import sys

MANIFEST_SCHEMA = "bcrypt-pe-index-013-manifest-v1"
MARKER_SCHEMA = "bcrypt-pe-index-013-consumed-v1"
CALL_ID = "013-complete-index-1"
codec = None  # Injected only after source-before-import verification.
PARSER_SHA256 = "f2d9ee012615b8d901a90c59b9a422a9993c98ac6fb8c929d5cf8a6ac2cb2bf3"


def require(value, code):
    if not value:
        raise ValueError(code)


def profiles():
    return {name: dict(getattr(codec, name.upper() + "_LIMITS"))
            for name in ("core", "report", "manifest", "marker")}


def encode(value, maximum):
    if maximum == codec.CORE_LIMITS["bytes"]:
        profile = codec.CORE_LIMITS
    elif maximum == codec.REPORT_LIMITS["bytes"]:
        profile = codec.REPORT_LIMITS
    elif maximum == codec.MARKER_LIMITS["bytes"]:
        profile = codec.MARKER_LIMITS
    else:
        require(0 < maximum <= codec.MANIFEST_LIMITS["bytes"], "ENCODE_PROFILE")
        profile = dict(codec.MANIFEST_LIMITS, bytes=maximum)
    return codec.encode(value, limits=profile)


def decode(raw):
    return codec.decode(raw, limits=codec.MANIFEST_LIMITS)


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
              "argv_prefix", "output_dir", "limits", "codec_profiles"}
    require(type(value) is dict and set(value) == fields, "MANIFEST_FIELDS")
    require(value["schema"] == MANIFEST_SCHEMA and value["stage"] == "complete_index"
            and value["call_id"] == CALL_ID, "MANIFEST_STAGE")
    require(type(value["limits"]) is dict and value["limits"] == limits
            and all(type(v) is int for v in value["limits"].values()), "LIMITS")
    require(value["codec_profiles"] == profiles()
            and all(type(v) is int for p in value["codec_profiles"].values() for v in p.values()),
            "CODEC_PROFILES")
    description(value["input"], limits["input_bytes"], extra=("init_rva",))
    require(type(value["input"]["init_rva"]) is int
            and 0 <= value["input"]["init_rva"] <= 0xFFFFFFFF, "INIT_RVA")
    sources = value["sources"]
    require(type(sources) is dict and set(sources) == {"owner", "io", "observation", "parser", "codec"},
            "SOURCE_FIELDS")
    for source in sources.values():
        description(source, limits["source_bytes"])
    require(sources["owner"]["path"] == str(owner_path)
            and sources["io"]["path"] == str(owner_path.with_name("bcrypt_index_io_013.py")),
            "SOURCE_PATH")
    require(sources["parser"]["bytes"] == 32061
            and sources["parser"]["sha256"] == PARSER_SHA256, "VERSIONED_PARSER")
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
    raw = encode(value, maximum)
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
    require(raw == encode(marker_value(sha, os.getppid()), 1024), "WORKER_MARKER")


INDEX_FIELDS = {"schema", "compatibility_policy", "bytes", "sha256", "header",
                "sections", "data_directories", "entrypoint", "certificates", "imports",
                "delay_imports", "iat_index", "unknown_iat_slots", "runtime_functions",
                "unwind_info", "runtime_execution_observed", "unknowns", "work_units", "limits"}
ARRAY_CAPS = {"sections": 96, "data_directories": 16, "certificates": 64,
              "imports": 128, "delay_imports": 128, "iat_index": 4096,
              "unknown_iat_slots": 8192, "runtime_functions": 16384,
              "unwind_info": 16384, "unknowns": 32}


def core_value(value, manifest_value):
    """Validate the envelope and identity; pinned parser supplies PE semantics."""
    try:
        fields = {"schema", "status", "parser_calls", "reason", "exception_type",
                  "refusal_context", "static_index_admitted", "index"}
        require(type(value) is dict and set(value) == fields, "CORE_SCHEMA")
        require(value["schema"] == "bcrypt-pe-core-013-v1"
                and value["status"] in ("indexed", "parser_refused", "parser_error")
                and type(value["parser_calls"]) is int and value["parser_calls"] == 1,
                "CORE_SCHEMA")
        if value["status"] == "indexed":
            require(value["reason"] is None and value["exception_type"] is None
                    and value["refusal_context"] is None
                    and value["static_index_admitted"] is True, "CORE_SCHEMA")
            index = value["index"]
            require(type(index) is dict and set(index) == INDEX_FIELDS
                    and index["schema"] == "bcrypt-pe-index-013-v1"
                    and index["runtime_execution_observed"] is False, "CORE_SCHEMA")
            item = manifest_value["input"]
            require(type(index["bytes"]) is int and index["bytes"] == item["bytes"]
                    and index["sha256"] == item["sha256"]
                    and type(index["entrypoint"]) is dict
                    and type(index["entrypoint"]["rva"]) is int
                    and index["entrypoint"]["rva"] == item["init_rva"]
                    and index["entrypoint"]["loader_init_rva"] == item["init_rva"], "CORE_IDENTITY")
            policy = index["compatibility_policy"]
            require(type(policy) is dict and set(policy) == {"id", "nonzero_opinfo_ignored", "notice"}
                    and policy["id"] == "AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED"
                    and type(policy["nonzero_opinfo_ignored"]) is bool
                    and policy["notice"] == "CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED",
                    "CORE_SCHEMA")
            for key, maximum in ARRAY_CAPS.items():
                require(type(index[key]) is list and len(index[key]) <= maximum, "CORE_SCHEMA")
            require(len(index["data_directories"]) == 16
                    and type(index["header"]) is dict and type(index["limits"]) is dict
                    and type(index["work_units"]) is int
                    and 0 <= index["work_units"] <= 4194304, "CORE_SCHEMA")
        else:
            require(value["index"] is None and value["static_index_admitted"] is False, "CORE_SCHEMA")
            if value["status"] == "parser_refused":
                reason = value["reason"]
                require(type(reason) is str and 0 < len(reason) <= 64
                        and all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789" for c in reason)
                        and value["exception_type"] == "PeValidationError", "CORE_SCHEMA")
                context = value["refusal_context"]
                require(context is None or type(context) is dict, "CORE_SCHEMA")
                if context is not None:
                    encode(context, 8192)
            else:
                require(value["reason"] == "PARSER_EXCEPTION"
                        and value["exception_type"] == "Exception"
                        and value["refusal_context"] is None, "CORE_SCHEMA")
        encode(dict(value, index=None), 16384)
    except (ValueError, KeyError, TypeError, RecursionError):
        raise ValueError("CORE_SCHEMA") from None
    return value


def core_result(raw, manifest_value):
    require(type(raw) is bytes and 0 < len(raw) <= codec.CORE_LIMITS["bytes"], "CORE_SIZE")
    value = codec.decode(raw, limits=codec.CORE_LIMITS)
    return core_value(value, manifest_value)
