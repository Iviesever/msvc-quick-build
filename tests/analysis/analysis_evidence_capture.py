"""Bounded, single-writer capture for analysis NDJSON and opaque stderr.

This module executes no processes. A new driver must supply its frozen call plan,
bounded binary readers, and an external copy of the returned result seal hash.
It neither resumes a failed capture nor repairs evidence from its backup.
"""
from dataclasses import asdict, dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import stat


class CaptureError(Exception):
    """An invalid capture; existing evidence must be retained."""


@dataclass(frozen=True)
class Limits:
    """Defaults are also hard ceilings; a caller may only lower them."""
    max_calls: int = 256
    max_records_per_call: int = 256
    max_stream_bytes: int = 1024 * 1024
    max_total_bytes: int = 64 * 1024 * 1024
    chunk_bytes: int = 64 * 1024
    max_record_bytes: int = 64 * 1024
    max_plan_bytes: int = 1024 * 1024
    max_journal_line_bytes: int = 64 * 1024
    max_journal_bytes: int = 8 * 1024 * 1024

    def __post_init__(self):
        for name, field in self.__dataclass_fields__.items():
            value = getattr(self, name)
            if type(value) is not int or not 0 < value <= field.default:
                raise ValueError(f"invalid limit: {name}")


_ROLES = ("stdout", "stderr")
_RECEIPT_KEYS = {"byte_count", "sha256", "lf_count", "ends_with_lf", "eof",
                 "capture_error", "overflow_probe_hex"}


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _is_sha(value):
    return type(value) is str and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _valid_exit_code(value):
    # Include negative POSIX status claims and unsigned Windows DWORD results.
    return value is None or type(value) is int and -(2 ** 31) <= value <= 2 ** 32 - 1


def _encode(value, maximum):
    output = bytearray()
    encoder = json.JSONEncoder(ensure_ascii=True, allow_nan=False, sort_keys=True,
                               separators=(",", ":"))
    for part in encoder.iterencode(value):
        raw = part.encode("ascii")
        if len(output) + len(raw) + 1 > maximum:
            raise CaptureError("metadata_limit")
        output.extend(raw)
    output.extend(b"\n")
    return bytes(output)


def _decode(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise CaptureError("duplicate_json_key")
            result[key] = value
        return result

    def constant(_):
        raise CaptureError("nonfinite_json_number")

    def finite(value):
        parsed = float(value)
        if not math.isfinite(parsed):
            raise CaptureError("nonfinite_json_number")
        return parsed

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_constant=constant, parse_float=finite)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise CaptureError("invalid_json") from error


def _freeze(identity, plan, limits):
    if type(limits) is not Limits:
        raise ValueError("limits must be a Limits instance")
    limits.__post_init__()
    if type(identity) is not dict or not 0 < len(identity) <= 32:
        raise ValueError("identity must contain 1..32 string fields")
    for key, value in identity.items():
        if (type(key) is not str or not 0 < len(key) <= 128 or
                type(value) is not str or not 0 < len(value) <= 4096):
            raise ValueError("invalid identity field")
    if type(plan) is not list or not 0 < len(plan) <= limits.max_calls:
        raise ValueError("invalid call plan size")
    names = set()
    for call in plan:
        if type(call) is not dict or set(call) != {"call_id", "stdout_records", "stderr_empty"}:
            raise ValueError("invalid call specification")
        name = call["call_id"]
        if type(name) is not str or not 0 < len(name) <= 128 or name in names:
            raise ValueError("invalid or duplicate call_id")
        names.add(name)
        if type(call["stderr_empty"]) is not bool:
            raise ValueError("stderr_empty must be boolean")
        records = call["stdout_records"]
        if type(records) is not list or len(records) > limits.max_records_per_call:
            raise ValueError("invalid expected record count")
        seen = set()
        for record in records:
            if type(record) is not dict or not 0 < len(record) <= 32:
                raise ValueError("record identities must be nonempty objects")
            for key, value in record.items():
                if type(key) is not str or not 0 < len(key) <= 128:
                    raise ValueError("invalid record identity key")
                if type(value) not in (str, int, bool, type(None)):
                    raise ValueError("record identities require string/integer/boolean/null values")
                if type(value) is str and len(value) > 4096:
                    raise ValueError("record identity string too long")
                if type(value) is int and value.bit_length() > 64:
                    raise ValueError("record identity integer too large")
            encoded = _encode(record, limits.max_record_bytes)
            if encoded in seen:
                raise ValueError("duplicate expected record identity")
            seen.add(encoded)
    encoded = _encode({"format": 1, "identity": identity, "plan": plan,
                       "limits": asdict(limits)}, limits.max_plan_bytes)
    return encoded, _decode(encoded)


def _fingerprint(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns, info.st_nlink)


def _directory_identity(path):
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode):
        raise CaptureError(f"not_directory:{path.name}")
    return info.st_dev, info.st_ino


def _read_file(path, maximum, chunk):
    """Read a bounded regular file; reject observed alias/replacement changes."""
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        raise CaptureError(f"not_exclusive_regular_file:{path.name}")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb", buffering=0) as source:
        if _fingerprint(before) != _fingerprint(os.fstat(source.fileno())):
            raise CaptureError(f"file_replaced:{path.name}")
        result = bytearray()
        while True:
            raw = source.read(min(chunk, maximum - len(result) + 1))
            if not raw:
                break
            if len(result) + len(raw) > maximum:
                raise CaptureError(f"file_limit:{path.name}")
            result.extend(raw)
        if (_fingerprint(before) != _fingerprint(os.fstat(source.fileno())) or
                _fingerprint(before) != _fingerprint(path.lstat())):
            raise CaptureError(f"file_changed_during_read:{path.name}")
    return bytes(result)


def _open_new(path):
    return path.open("xb", buffering=0)


def _write_exact(target, raw):
    if target.write(raw) != len(raw):
        raise CaptureError("short_write")


def _sync(target):
    target.flush()
    os.fsync(target.fileno())


def _close(target, on_error=None):
    first = None
    try:
        _sync(target)
    except BaseException as error:
        first = error
        if on_error is not None:
            on_error(error)
    try:
        target.close()
    except BaseException as error:
        if on_error is not None:
            on_error(error)
        if first is None:
            first = error
    if first is not None:
        raise first


def _save_new(path, raw, limits):
    target = _open_new(path)
    first = None
    try:
        _write_exact(target, raw)
    except BaseException as error:
        first = error
    try:
        _close(target)
    except BaseException as error:
        if first is None:
            first = error
    if first is not None:
        raise first
    if _read_file(path, len(raw), limits.chunk_bytes) != raw:
        raise CaptureError(f"saved_file_readback_mismatch:{path.name}")


def _stats(raw):
    return {"byte_count": len(raw), "sha256": _sha(raw),
            "lf_count": raw.count(b"\n"), "ends_with_lf": raw.endswith(b"\n")}


def _records(raw, expected, limits):
    if raw and not raw.endswith(b"\n"):
        raise CaptureError("unterminated_stdout_record")
    lines = raw.split(b"\n")[:-1] if raw else []
    if len(lines) != len(expected):
        raise CaptureError("stdout_record_count")
    for line, identity in zip(lines, expected):
        if not line or len(line) + 1 > limits.max_record_bytes:
            raise CaptureError("stdout_record_size")
        row = _decode(line)
        if type(row) is not dict:
            raise CaptureError("stdout_record_not_object")
        for key, value in identity.items():
            if key not in row or type(row[key]) is not type(value) or row[key] != value:
                raise CaptureError("stdout_record_identity")
    return len(lines)


def _valid_receipt(value, limits):
    return (type(value) is dict and set(value) == _RECEIPT_KEYS and
            type(value["byte_count"]) is int and 0 <= value["byte_count"] <= limits.max_stream_bytes and
            _is_sha(value["sha256"]) and type(value["lf_count"]) is int and
            0 <= value["lf_count"] <= value["byte_count"] and
            type(value["ends_with_lf"]) is bool and type(value["eof"]) is bool and
            (value["capture_error"] is None or
             type(value["capture_error"]) is str and len(value["capture_error"]) <= 256) and
            type(value["overflow_probe_hex"]) is str and
            (value["overflow_probe_hex"] == "" or len(value["overflow_probe_hex"]) == 2 and
             all(c in "0123456789abcdef" for c in value["overflow_probe_hex"])))


class _Problems:
    def __init__(self, first=None):
        self.first = first
        self.items = []
        self.count = 0
        if first is not None:
            self.add(first)

    def add(self, error):
        message = str(error)[:256]
        if self.first is None:
            self.first = message
        self.count += 1
        if len(self.items) < 32:
            self.items.append(message)


def _check_layout(root, count, allow_result, problems):
    expected_top = {"manifest.json", "journal.jsonl", "raw", "backup"}
    if allow_result:
        expected_top.add("result.json")
    for directory, expected in [(root, expected_top)] + [
            (root / name, {f"{i:06d}.{role}.bin" for i in range(count) for role in _ROLES})
            for name in ("raw", "backup")]:
        try:
            info = directory.lstat()
            if not stat.S_ISDIR(info.st_mode):
                raise CaptureError(f"not_directory:{directory.name}")
            actual = set()
            with os.scandir(directory) as entries:
                for entry in entries:
                    actual.add(entry.name)
                    if len(actual) > len(expected):
                        raise CaptureError(f"extra_files:{directory.name}")
            if actual != expected:
                raise CaptureError(f"file_set_mismatch:{directory.name}")
        except (OSError, CaptureError) as error:
            problems.add(error)


def _audit(root, manifest_bytes, manifest, limits, *, journal_anchor=None,
           receipt_anchor=None, first_failure=None, capture_failures=(), allow_result=False):
    problems = _Problems(first_failure or (capture_failures[0] if capture_failures else None))
    plan = manifest["plan"]
    _check_layout(root, len(plan), allow_result, problems)
    try:
        if _read_file(root / "manifest.json", limits.max_plan_bytes, limits.chunk_bytes) != manifest_bytes:
            problems.add("manifest_identity_mismatch")
    except (OSError, CaptureError) as error:
        problems.add(error)
    journal = b""
    try:
        journal = _read_file(root / "journal.jsonl", limits.max_journal_bytes, limits.chunk_bytes)
    except (OSError, CaptureError) as error:
        problems.add(error)
    if journal_anchor is not None and (_sha(journal), len(journal)) != journal_anchor:
        problems.add("journal_anchor_mismatch")
    if journal and not journal.endswith(b"\n"):
        problems.add("unterminated_journal")
    lines = journal.split(b"\n")[:-1] if journal else []
    expected_events = 4 * len(plan)
    if len(lines) != expected_events:
        problems.add("journal_event_count")
    receipts = {}
    started, finished = set(), set()
    ordered = len(lines) == expected_events
    for sequence, line in enumerate(lines[:expected_events]):
        try:
            if not line or len(line) + 1 > limits.max_journal_line_bytes:
                raise CaptureError("journal_line_limit")
            event = _decode(line)
            index, phase = divmod(sequence, 4)
            kind = ("start", "stream", "stream", "finish")[phase]
            keys = {"seq", "event", "index", "call_id"}
            keys |= {"role", "receipt"} if kind == "stream" else (
                {"exit_code", "failure", "validation_ok"} if kind == "finish" else set())
            if (type(event) is not dict or set(event) != keys or
                    type(event["seq"]) is not int or event["seq"] != sequence or
                    type(event["index"]) is not int or event["index"] != index or
                    event["event"] != kind or event["call_id"] != plan[index]["call_id"]):
                raise CaptureError("journal_order_or_identity")
            if kind == "start":
                started.add(index)
            elif kind == "stream":
                role, receipt = event["role"], event["receipt"]
                if (type(role) is not str or role not in _ROLES or (index, role) in receipts or
                        not _valid_receipt(receipt, limits)):
                    raise CaptureError("journal_stream_receipt")
                receipts[index, role] = receipt
            else:
                if (not _valid_exit_code(event["exit_code"]) or
                        not (event["failure"] is None or type(event["failure"]) is str and
                             len(event["failure"]) <= 256) or
                        type(event["validation_ok"]) is not bool):
                    raise CaptureError("journal_finish_fields")
                finished.add(index)
                if event["exit_code"] != 0 or event["failure"] is not None or not event["validation_ok"]:
                    problems.add("unsuccessful_call")
        except (ValueError, TypeError, OSError, CaptureError) as error:
            ordered = False
            problems.add(error)
    if receipt_anchor is not None and receipts != receipt_anchor:
        problems.add("received_receipt_anchor_mismatch")
    if sum(receipt["byte_count"] for receipt in receipts.values()) > limits.max_total_bytes:
        problems.add("total_stream_limit")
    verified_streams = verified_records = 0
    for index, call in enumerate(plan):
        for role in _ROLES:
            before = problems.count
            receipt = receipts.get((index, role))
            if receipt is None:
                problems.add(f"missing_receipt:{index}:{role}")
            elif not receipt["eof"] or receipt["capture_error"] is not None or receipt["overflow_probe_hex"]:
                problems.add(f"incomplete_stream:{index}:{role}")
            data = {}
            for name in ("raw", "backup"):
                try:
                    data[name] = _read_file(root / name / f"{index:06d}.{role}.bin",
                                            limits.max_stream_bytes, limits.chunk_bytes)
                    if receipt is not None and any(receipt[key] != value for key, value in _stats(data[name]).items()):
                        problems.add(f"stream_receipt_mismatch:{name}:{index}:{role}")
                except (OSError, CaptureError) as error:
                    problems.add(error)
            if len(data) == 2 and data["raw"] != data["backup"]:
                problems.add(f"raw_backup_mismatch:{index}:{role}")
            if "raw" in data:
                try:
                    if role == "stdout":
                        count = _records(data["raw"], call["stdout_records"], limits)
                        if problems.count == before:
                            verified_records += count
                    elif call["stderr_empty"] and data["raw"]:
                        raise CaptureError("nonempty_stderr")
                except CaptureError as error:
                    problems.add(f"{error}:{index}:{role}")
            if problems.count == before:
                verified_streams += 1
    execution_finished = ordered and len(started) == len(finished) == len(plan)
    complete = execution_finished and problems.count == 0
    return {"format": 1, "manifest_sha256": _sha(manifest_bytes),
            "journal_sha256": _sha(journal), "journal_bytes": len(journal),
            "event_count": len(lines), "planned_calls": len(plan),
            "started_calls": len(started), "finished_calls": len(finished),
            "expected_records": sum(len(call["stdout_records"]) for call in plan),
            "verified_streams": verified_streams, "verified_records": verified_records,
            "execution_finished": execution_finished, "complete": complete,
            "integrity_status": "VERIFIED" if complete else "INVALID",
            "first_failure": problems.first, "failure_count": problems.count,
            "failures": problems.items, "capture_failures": list(capture_failures)}


class CaptureSession:
    @classmethod
    def create(cls, root, *, identity, plan, limits=Limits()):
        manifest_bytes, manifest = _freeze(identity, plan, limits)
        limits = Limits(**manifest["limits"])
        root = Path(root)
        root.mkdir(exist_ok=False)
        (root / "raw").mkdir()
        (root / "backup").mkdir()
        _save_new(root / "manifest.json", manifest_bytes, limits)
        session = cls()
        session._root, session._manifest_bytes, session._manifest = root, manifest_bytes, manifest
        session._limits = limits
        session._directories = {path: _directory_identity(path)
                                for path in (root, root / "raw", root / "backup")}
        session._journal = _open_new(root / "journal.jsonl")
        session._journal_hash = hashlib.sha256()
        session._journal_bytes = session._sequence = session._next = session._total = 0
        session._active = None
        session._attempted = set()
        session._receipts = {}
        session._first_failure = None
        session._capture_failures = []
        session._journal_failed = session._closed = False
        return session

    def _remember(self, error):
        message = f"{type(error).__name__}:{error}"[:256]
        if self._first_failure is None:
            self._first_failure = message
        if len(self._capture_failures) < 32:
            self._capture_failures.append(message)

    def _guard(self, condition, message):
        if self._closed:
            raise CaptureError("session_sealed")
        if not condition:
            error = CaptureError(message)
            self._remember(error)
            raise error

    def _check_directories(self):
        try:
            for path, expected in self._directories.items():
                if _directory_identity(path) != expected:
                    raise CaptureError(f"directory_replaced:{path.name}")
        except (OSError, CaptureError) as error:
            self._remember(error)
            raise CaptureError("capture_directory_changed") from error

    def _event(self, event, index, **fields):
        if self._journal_failed:
            raise CaptureError("journal_poisoned")
        try:
            self._check_directories()
            raw = _encode({"seq": self._sequence, "event": event, "index": index,
                           "call_id": self._manifest["plan"][index]["call_id"], **fields},
                          self._limits.max_journal_line_bytes)
            if (self._sequence >= 4 * len(self._manifest["plan"]) or
                    self._journal_bytes + len(raw) > self._limits.max_journal_bytes):
                raise CaptureError("journal_limit")
            _write_exact(self._journal, raw)
            _sync(self._journal)
            self._journal_hash.update(raw)
            self._journal_bytes += len(raw)
            self._sequence += 1
        except BaseException as error:
            self._journal_failed = True
            self._remember(error)
            raise CaptureError("journal_write_failed") from error

    def start_call(self, index):
        self._guard(self._first_failure is None, "session_invalid")
        self._guard(type(index) is int and index == self._next and
                    index < len(self._manifest["plan"]) and self._active is None,
                    "call_order")
        self._event("start", index)
        self._active = index
        self._attempted = set()

    def preserve_stream(self, index, role, source):
        """Admit bounded bytes before writing; receipts describe admitted input.

        On overflow only one extra byte is probed and stored as hex in the receipt.
        A short write leaves an INVALID prefix, not a smaller successful receipt.
        """
        self._guard(type(index) is int and self._active == index, "stream_call_order")
        self._guard(type(role) is str and role in _ROLES and role not in self._attempted,
                    "stream_role_or_duplicate")
        self._attempted.add(role)
        digest = hashlib.sha256()
        receipt = {"byte_count": 0, "sha256": digest.hexdigest(), "lf_count": 0,
                   "ends_with_lf": False, "eof": False, "capture_error": None,
                   "overflow_probe_hex": ""}
        opened = []
        first = None
        try:
            for name in ("backup", "raw"):
                self._check_directories()
                opened.append(_open_new(self._root / name / f"{index:06d}.{role}.bin"))
            while True:
                remaining = min(self._limits.max_stream_bytes - receipt["byte_count"],
                                self._limits.max_total_bytes - self._total)
                request = min(self._limits.chunk_bytes, remaining) if remaining else 1
                raw = source.read(request)
                if type(raw) is not bytes or len(raw) > request:
                    raise CaptureError("binary_reader_contract")
                if not raw:
                    receipt["eof"] = True
                    break
                if not remaining:
                    receipt["overflow_probe_hex"] = raw.hex()
                    raise CaptureError("stream_or_total_limit")
                digest.update(raw)
                receipt["byte_count"] += len(raw)
                receipt["lf_count"] += raw.count(b"\n")
                receipt["ends_with_lf"] = raw.endswith(b"\n")
                self._total += len(raw)
                for target in opened:  # preservation copy is written first
                    _write_exact(target, raw)
        except BaseException as error:
            first = error
            self._remember(error)
        finally:
            for target in opened:
                try:
                    _close(target, self._remember)
                except BaseException as error:
                    if first is None:
                        first = error
        receipt["sha256"] = digest.hexdigest()
        if first is not None:
            receipt["capture_error"] = f"{type(first).__name__}:{first}"[:256]
        self._receipts[index, role] = dict(receipt)
        try:
            self._event("stream", index, role=role, receipt=receipt)
        except CaptureError as error:
            if first is None:
                first = error
        if first is not None:
            if not isinstance(first, Exception):
                raise first
            raise CaptureError(f"stream_capture_failed:{role}") from first
        return dict(receipt)

    def finish_call(self, index, *, exit_code, failure=None):
        self._guard(type(index) is int and self._active == index, "finish_call_order")
        self._guard(_valid_exit_code(exit_code) and
                    (failure is None or type(failure) is str and len(failure) <= 256),
                    "finish_arguments")
        validation_ok = True
        try:
            if exit_code != 0 or failure is not None:
                raise CaptureError(f"call_failed:{failure or exit_code}")
            if self._attempted != set(_ROLES):
                raise CaptureError("missing_stream")
            for role in _ROLES:
                receipt = self._receipts.get((index, role))
                if receipt is None or receipt["capture_error"] is not None or not receipt["eof"]:
                    raise CaptureError("incomplete_stream")
                raw = _read_file(self._root / "raw" / f"{index:06d}.{role}.bin",
                                 self._limits.max_stream_bytes, self._limits.chunk_bytes)
                if any(receipt[key] != value for key, value in _stats(raw).items()):
                    raise CaptureError("stream_receipt_mismatch")
                spec = self._manifest["plan"][index]
                if role == "stdout":
                    _records(raw, spec["stdout_records"], self._limits)
                elif spec["stderr_empty"] and raw:
                    raise CaptureError("nonempty_stderr")
        except (OSError, CaptureError) as error:
            validation_ok = False
            self._remember(error)
        try:
            self._event("finish", index, exit_code=exit_code, failure=failure, validation_ok=validation_ok)
        finally:
            self._active = None
            self._next += 1
        if self._first_failure is not None:
            raise CaptureError(f"call_invalid:{self._first_failure}")

    def finalize(self):
        self._guard(True, "")
        self._closed = True
        try:
            _close(self._journal, self._remember)
        except BaseException:
            pass  # Every close/sync exception has already been recorded.
        self._check_directories()
        result = _audit(self._root, self._manifest_bytes, self._manifest, self._limits,
                        journal_anchor=(self._journal_hash.hexdigest(), self._journal_bytes),
                        receipt_anchor=self._receipts, first_failure=self._first_failure,
                        capture_failures=self._capture_failures)
        raw = _encode(result, self._limits.max_plan_bytes)
        try:
            self._check_directories()
            _save_new(self._root / "result.json", raw, self._limits)
        except BaseException as error:
            self._remember(error)
            raise CaptureError("result_publication_failed") from error
        return {**result, "seal_sha256": _sha(raw)}


def audit_existing(root, *, identity, plan, limits=Limits(), expected_seal_sha256):
    """Read-only current audit against an externally retained result-file hash."""
    if not _is_sha(expected_seal_sha256):
        raise ValueError("an external SHA-256 seal is required")
    manifest_bytes, manifest = _freeze(identity, plan, limits)
    root = Path(root)
    failure = None
    anchor = None
    seal = None
    capture_failures = []
    try:
        raw = _read_file(root / "result.json", limits.max_plan_bytes, limits.chunk_bytes)
        if _sha(raw) != expected_seal_sha256:
            raise CaptureError("result_seal_mismatch")
        seal = _decode(raw)
        if (type(seal) is not dict or seal.get("format") != 1 or
                seal.get("manifest_sha256") != _sha(manifest_bytes) or
                not _is_sha(seal.get("journal_sha256")) or
                type(seal.get("journal_bytes")) is not int or
                not 0 <= seal["journal_bytes"] <= limits.max_journal_bytes or
                type(seal.get("complete")) is not bool or
                type(seal.get("capture_failures")) is not list or len(seal["capture_failures"]) > 32 or
                any(type(item) is not str or len(item) > 256 for item in seal["capture_failures"])):
            raise CaptureError("invalid_result_seal")
        anchor = seal["journal_sha256"], seal["journal_bytes"]
        capture_failures = seal["capture_failures"]
        if not seal["complete"]:
            failure = str(seal.get("first_failure") or "sealed_invalid_capture")[:256]
    except (OSError, CaptureError) as error:
        failure = str(error)[:256]
    result = _audit(root, manifest_bytes, manifest, limits, journal_anchor=anchor,
                    first_failure=failure, capture_failures=capture_failures, allow_result=True)
    if seal is not None and result["complete"]:
        if any(seal.get(key) != value or type(seal.get(key)) is not type(value)
               for key, value in result.items()):
            result.update(complete=False, integrity_status="INVALID", first_failure="result_fields_mismatch",
                          failure_count=1, failures=["result_fields_mismatch"])
    return {**result, "seal_sha256": expected_seal_sha256}
