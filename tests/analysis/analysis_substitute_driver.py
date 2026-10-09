"""Frozen Linux fixed-substitute wiring; no real workload or study entry point."""
from dataclasses import asdict, dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import time

from analysis_evidence_capture import CaptureSession, Limits as CaptureLimits, audit_existing
from analysis_fixed_substitute import fixture_spec
import analysis_substitute_transport as transport


_BASE = {"commit": "d4c0d5f143ea52862329180512caa1cf5ffd0244",
         "tree": "e4cbc65ea1db62d3735397417656b155eff89bf7"}
_MODES = ("normal", "empty_stderr", "dual", "nonzero", "timeout",
          "ignore_term", "closed_pipes", "busy")
_ROLES = ("stdout", "stderr")
_METADATA_BYTES = 256 * 1024
_SOURCE_BYTES = 16 * 1024 * 1024
_IDENTITY_LIMITS = {"source_bytes": _SOURCE_BYTES, "python_bytes": 64 * 1024 * 1024,
                    "chunk_bytes": 64 * 1024}
_REAL_BUDGET = {"codec": 0, "mqb": 0, "msvc": 0, "etw": 0, "performance_study": 0}


class DriverError(Exception):
    """A failed driver operation; retain its existing files."""


@dataclass(frozen=True)
class DriverLimits:
    """Defaults are hard ceilings; every value must be a strict positive int."""
    max_calls: int = 8
    max_stream_bytes: int = 1024 * 1024
    max_total_bytes: int = 8 * 1024 * 1024
    chunk_bytes: int = 32 * 1024
    call_timeout_ms: int = 3000
    terminate_grace_ms: int = 200
    kill_wait_ms: int = 1000
    poll_ms: int = 10
    overall_timeout_ms: int = 20000

    def __post_init__(self):
        for name, field in self.__dataclass_fields__.items():
            value = getattr(self, name)
            if type(value) is not int or not 0 < value <= field.default:
                raise ValueError(f"invalid driver limit: {name}")


@dataclass(frozen=True)
class FrozenProtocol:
    """Canonical immutable input bytes; document() returns an independent copy."""
    canonical: bytes

    @property
    def sha256(self):
        return _sha(self.canonical)

    def document(self):
        return json.loads(self.canonical)


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _is_sha(value):
    return type(value) is str and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _text(value, maximum=128):
    return type(value) is str and 0 < len(value) <= maximum


def _source_identities():
    here = Path(__file__).resolve().parent
    paths = {"driver": here / "analysis_substitute_driver.py",
             "transport": here / "analysis_substitute_transport.py",
             "fixture": here / "analysis_fixed_substitute.py",
             "capture": here / "analysis_evidence_capture.py",
             "python": Path(sys.executable).resolve()}
    result = {}
    for role, path in paths.items():
        maximum = _IDENTITY_LIMITS["python_bytes"] if role == "python" else _SOURCE_BYTES
        result[role] = transport.file_identity(path, maximum)
    return result


def _verify_sources(document):
    transport.require_reaper_policy()
    if _source_identities() != document["sources"]:
        raise DriverError("source_identity_changed")
    if sys.version != document["runtime"]["version"] or sys.platform != "linux":
        raise DriverError("runtime_identity_changed_or_unsupported")


def _capture_limits(limits):
    return CaptureLimits(max_calls=limits.max_calls,
                         max_stream_bytes=limits.max_stream_bytes,
                         max_total_bytes=limits.max_total_bytes,
                         chunk_bytes=limits.chunk_bytes,
                         max_plan_bytes=_METADATA_BYTES)


def _call_spec(call_id, mode, sources):
    fixed = fixture_spec(mode)
    return {"call_id": call_id, "mode": mode,
            "argv": [sources["python"]["path"], "-I", "-B", "-u",
                     sources["fixture"]["path"], mode],
            "expected": {role: transport.stream_stats(fixed[role]) for role in _ROLES},
            "capture": {"call_id": call_id, "stdout_records": fixed["records"],
                        "stderr_empty": not fixed["stderr"]}}


def freeze_protocol(*, protocol_id, calls, limits=DriverLimits()):
    """Freeze only the bundled modes, complete output identities and launch policy."""
    if sys.platform != "linux":
        raise DriverError("only_linux_substitute_supervision_is_supported")
    transport.require_reaper_policy()
    if type(limits) is not DriverLimits:
        raise ValueError("limits must be DriverLimits")
    limits.__post_init__()
    limits = DriverLimits(**asdict(limits))
    if not _text(protocol_id):
        raise ValueError("invalid protocol_id")
    if type(calls) is not list or not 0 < len(calls) <= limits.max_calls:
        raise ValueError("invalid ordered calls")
    names = set()
    selected = []
    for call in calls:
        if type(call) is not dict or set(call) != {"call_id", "mode"}:
            raise ValueError("each call requires exactly call_id and mode")
        if not _text(call["call_id"]) or call["call_id"] in names:
            raise ValueError("invalid or duplicate call_id")
        if type(call["mode"]) is not str or call["mode"] not in _MODES:
            raise ValueError("only bundled fixed-substitute modes are allowed")
        names.add(call["call_id"])
        selected.append((call["call_id"], call["mode"]))
    sources = _source_identities()
    document = {
        "format": 1, "kind": "fixed-substitute-contract", "protocol_id": protocol_id,
        "development_base": dict(_BASE), "sources": sources,
        "identity_limits": dict(_IDENTITY_LIMITS),
        "runtime": {"executable": sources["python"]["path"], "version": sys.version,
                    "platform": "linux", "cwd": str(Path(sources["fixture"]["path"]).parent),
                    "environment": {"LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"},
                    "reaper_policy": transport.REAPER_POLICY,
                    "stdin": "DEVNULL", "shell": False, "start_new_session": True},
        "limits": asdict(limits), "capture_limits": asdict(_capture_limits(limits)),
        "calls": [_call_spec(name, mode, sources) for name, mode in selected],
        "launch_budget": len(selected), "real_measurement_budget": dict(_REAL_BUDGET),
    }
    return FrozenProtocol(transport.canonical_bytes(document))


def _thaw(protocol, expected_sha256):
    if (type(protocol) is not FrozenProtocol or type(protocol.canonical) is not bytes or
            not 0 < len(protocol.canonical) <= _METADATA_BYTES or not _is_sha(expected_sha256)):
        raise ValueError("a frozen protocol and external SHA-256 are required")
    if protocol.sha256 != expected_sha256:
        raise DriverError("protocol_hash_mismatch")
    try:
        document = protocol.document()
        if transport.canonical_bytes(document) != protocol.canonical:
            raise ValueError("protocol must be canonical")
        keys = {"format", "kind", "protocol_id", "development_base", "sources", "runtime", "identity_limits",
                "limits", "capture_limits", "calls", "launch_budget", "real_measurement_budget"}
        if (type(document) is not dict or set(document) != keys or
                type(document["format"]) is not int or document["format"] != 1 or
                document["kind"] != "fixed-substitute-contract" or not _text(document["protocol_id"]) or
                document["development_base"] != _BASE or document["real_measurement_budget"] != _REAL_BUDGET or
                document["identity_limits"] != _IDENTITY_LIMITS):
            raise ValueError("invalid fixed-substitute protocol")
        limits = DriverLimits(**document["limits"])
        if document["capture_limits"] != asdict(_capture_limits(limits)):
            raise ValueError("capture policy differs from transport policy")
        sources = document["sources"]
        if type(sources) is not dict or set(sources) != {"driver", "transport", "fixture", "capture", "python"}:
            raise ValueError("invalid source identities")
        for role, source in sources.items():
            maximum = _IDENTITY_LIMITS["python_bytes"] if role == "python" else _SOURCE_BYTES
            if (type(source) is not dict or set(source) != {"path", "bytes", "sha256"} or
                    not _text(source["path"], 4096) or not Path(source["path"]).is_absolute() or
                    type(source["bytes"]) is not int or not 0 < source["bytes"] <= maximum or
                    not _is_sha(source["sha256"])):
                raise ValueError("invalid source identity")
        runtime = document["runtime"]
        if (type(runtime) is not dict or set(runtime) != {
                "executable", "version", "platform", "cwd", "environment", "stdin", "shell", "start_new_session",
                "reaper_policy"} or
                runtime["executable"] != sources["python"]["path"] or not _text(runtime["version"], 4096) or
                runtime["platform"] != "linux" or runtime["cwd"] != str(Path(sources["fixture"]["path"]).parent) or
                runtime["environment"] != {"LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"} or
                runtime["reaper_policy"] != transport.REAPER_POLICY or
                runtime["stdin"] != "DEVNULL" or runtime["shell"] is not False or
                runtime["start_new_session"] is not True):
            raise ValueError("invalid frozen launch policy")
        calls = document["calls"]
        if (type(calls) is not list or not 0 < len(calls) <= limits.max_calls or
                type(document["launch_budget"]) is not int or document["launch_budget"] != len(calls)):
            raise ValueError("invalid frozen call budget")
        names = set()
        for call in calls:
            if (type(call) is not dict or not _text(call.get("call_id")) or
                    call["call_id"] in names or call.get("mode") not in _MODES or
                    call != _call_spec(call["call_id"], call["mode"], sources)):
                raise ValueError("invalid frozen call specification")
            names.add(call["call_id"])
        return document
    except (ValueError, TypeError, KeyError, RecursionError) as error:
        raise DriverError("invalid_protocol_document") from error


class _Problems:
    def __init__(self, initial=()):
        self.count = 0
        self.items = []
        for item in initial:
            self.add(item)

    def add(self, error):
        try:
            message = str(error)[:256]
        except Exception:
            message = type(error).__name__
        self.count += 1
        if len(self.items) < 32:
            self.items.append(message)


class _FailedReader:
    def __init__(self, failure):
        self.failure = failure

    def read(self, _):
        raise DriverError(self.failure)


def _members(path, expected, problems):
    try:
        transport.directory_identity(path)
        # Stop at one unexpected member; no unbounded listing of damaged roots.
        seen = set()
        with os.scandir(path) as entries:
            for member in entries:
                if member.name not in expected or member.name in seen:
                    raise DriverError(f"unexpected_member:{path.name}:{member.name}")
                seen.add(member.name)
        if seen != expected:
            raise DriverError(f"missing_members:{path.name}")
    except Exception as error:
        problems.add(error)


def _valid_record(record, index, limits):
    if (type(record) is not dict or type(record.get("index")) is not int or record["index"] != index or
            any(type(record.get(key)) is not bool for key in
                ("launch_attempted", "launched", "reaped", "wait_status_unavailable", "term_sent", "kill_sent", "transport_ok")) or
            not (record.get("returncode") is None or type(record["returncode"]) is int and
                 -(2 ** 31) <= record["returncode"] <= 2 ** 32 - 1) or
            not (record.get("pid") is None or type(record["pid"]) is int and record["pid"] > 0) or
            any(type(record.get(key)) is not int or record[key] < 0
                for key in ("started_monotonic_ns", "finished_monotonic_ns")) or
            record["finished_monotonic_ns"] < record["started_monotonic_ns"] or
            not (record.get("first_failure") is None or _text(record["first_failure"], 256)) or
            type(record.get("failures")) is not list or len(record["failures"]) > 32 or
            any(not _text(item, 256) for item in record["failures"]) or
            type(record.get("streams")) is not dict or set(record["streams"]) != set(_ROLES)):
        return False
    if (record["launched"] and (not record["launch_attempted"] or record["pid"] is None) or
            record["reaped"] and (not record["launched"] or record["returncode"] is None) or
            record["wait_status_unavailable"] and (not record["launched"] or record["reaped"] or
                                                   record["returncode"] is not None)):
        return False
    for receipt in record["streams"].values():
        if (type(receipt) is not dict or set(receipt) != {
                "byte_count", "sha256", "lf_count", "ends_with_lf", "pipe_eof",
                "overflow_probe_hex", "capture_error"} or
                type(receipt["byte_count"]) is not int or not 0 <= receipt["byte_count"] <= limits.max_stream_bytes or
                not _is_sha(receipt["sha256"]) or type(receipt["lf_count"]) is not int or
                not 0 <= receipt["lf_count"] <= receipt["byte_count"] or
                type(receipt["ends_with_lf"]) is not bool or type(receipt["pipe_eof"]) is not bool or
                not (receipt["capture_error"] is None or _text(receipt["capture_error"], 256)) or
                type(receipt["overflow_probe_hex"]) is not str or
                not (receipt["overflow_probe_hex"] == "" or len(receipt["overflow_probe_hex"]) == 2 and
                     all(c in "0123456789abcdef" for c in receipt["overflow_probe_hex"]))):
            return False
    return True


def _valid_anchor(anchor, index, limits):
    if (type(anchor) is not dict or set(anchor) != {
            "index", "sha256", "launch_attempted", "launched", "reaped", "admitted_bytes"} or
            type(anchor["index"]) is not int or anchor["index"] != index or not _is_sha(anchor["sha256"]) or
            any(type(anchor[key]) is not bool for key in ("launch_attempted", "launched", "reaped")) or
            type(anchor["admitted_bytes"]) is not int or not 0 <= anchor["admitted_bytes"] <= limits.max_total_bytes):
        return False
    return (not anchor["launched"] or anchor["launch_attempted"]) and (not anchor["reaped"] or anchor["launched"])


def _inspect(root, protocol, document, anchors, capture_seal, *, allow_result, initial=()):
    problems = _Problems(initial)
    limits = DriverLimits(**document["limits"])
    top = {"protocol.json", "spool", "calls", "capture"}
    if allow_result:
        top.add("driver-result.json")
    _members(root, top, problems)
    _members(root / "calls", {f"{i:06d}.json" for i in range(len(anchors))}, problems)
    _members(root / "spool", {f"{i:06d}.{role}.bin" for i in range(len(anchors)) for role in _ROLES}, problems)
    try:
        if transport.read_regular(root / "protocol.json") != protocol.canonical:
            problems.add("saved_protocol_mismatch")
    except Exception as error:
        problems.add(error)
    attempts = launched = reaped = total = 0
    verified_attempts = verified_launched = verified_reaped = verified_bytes = 0
    observations_available = True
    for index, anchor in enumerate(anchors):
        try:
            if not _valid_anchor(anchor, index, limits):
                observations_available = False
                raise DriverError("invalid_call_record_anchor")
            # These controller observations survive a later loss of a call
            # record. Loss reduces verification, not the consumed allocation.
            attempts += anchor["launch_attempted"]
            launched += anchor["launched"]
            reaped += anchor["reaped"]
            total += anchor["admitted_bytes"]
            raw = transport.read_regular(root / "calls" / f"{index:06d}.json")
            if _sha(raw) != anchor["sha256"]:
                raise DriverError(f"call_record_hash_mismatch:{index}")
            record = json.loads(raw)
            if not _valid_record(record, index, limits):
                raise DriverError(f"invalid_call_record:{index}")
            byte_count = sum(r["byte_count"] for r in record["streams"].values())
            if (any(record[key] != anchor[key] for key in ("launch_attempted", "launched", "reaped")) or
                    byte_count != anchor["admitted_bytes"]):
                raise DriverError(f"call_observation_anchor_mismatch:{index}")
            verified_attempts += record["launch_attempted"]
            verified_launched += record["launched"]
            verified_reaped += record["reaped"]
            verified_bytes += byte_count
            if (not record["transport_ok"] or not record["launched"] or not record["reaped"] or
                    record["wait_status_unavailable"] or
                    record["returncode"] != 0 or record["first_failure"] or record["failures"] or
                    record["term_sent"] or record["kill_sent"]):
                problems.add(f"unsuccessful_transport:{index}")
            for role in _ROLES:
                receipt = record["streams"][role]
                if not receipt["pipe_eof"] or receipt["capture_error"] is not None or receipt["overflow_probe_hex"]:
                    problems.add(f"incomplete_original_pipe:{index}:{role}")
                try:
                    spool = transport.read_regular(root / "spool" / f"{index:06d}.{role}.bin", limits.max_stream_bytes)
                    stats = transport.stream_stats(spool)
                    if any(receipt[key] != value for key, value in stats.items()):
                        problems.add(f"received_spool_mismatch:{index}:{role}")
                    if stats != document["calls"][index]["expected"][role]:
                        problems.add(f"fixed_output_mismatch:{index}:{role}")
                    for name in ("raw", "backup"):
                        captured = transport.read_regular(root / "capture" / name / f"{index:06d}.{role}.bin",
                                                          limits.max_stream_bytes)
                        if captured != spool:
                            problems.add(f"spool_capture_mismatch:{name}:{index}:{role}")
                except Exception as error:
                    problems.add(error)
        except Exception as error:
            problems.add(error)
    if total > limits.max_total_bytes:
        problems.add("transport_total_byte_limit")
    count = len(document["calls"])
    execution_finished = observations_available and len(anchors) == count and attempts == launched == reaped == count
    if not execution_finished:
        problems.add("incomplete_planned_process_lifecycle")
    capture_complete = False
    try:
        capture = audit_existing(root / "capture", identity={"protocol_sha256": protocol.sha256,
                                 "kind": "fixed-substitute-contract"},
                                 plan=[call["capture"] for call in document["calls"]],
                                 limits=CaptureLimits(**document["capture_limits"]),
                                 expected_seal_sha256=capture_seal)
        capture_complete = capture["complete"] and capture["integrity_status"] == "VERIFIED"
        if not capture_complete:
            problems.add(f"capture_invalid:{capture['first_failure']}")
    except Exception as error:
        problems.add(error)
    complete = problems.count == 0
    return {"format": 1, "kind": "fixed-substitute-wiring-verdict",
            "protocol_sha256": protocol.sha256, "capture_seal_sha256": capture_seal,
            "call_records": anchors, "planned_calls": count,
            "lifecycle_counts_available": observations_available,
            "launch_attempts": attempts if observations_available else None,
            "launched_calls": launched if observations_available else None,
            "reaped_calls": reaped if observations_available else None,
            "admitted_bytes": total if observations_available else None,
            "verified_launch_attempts": verified_attempts, "verified_launched_calls": verified_launched,
            "verified_reaped_calls": verified_reaped, "verified_admitted_bytes": verified_bytes,
            "execution_finished": execution_finished, "capture_complete": capture_complete,
            "complete": complete, "integrity_status": "VERIFIED" if complete else "INVALID",
            "first_failure": problems.items[0] if problems.items else None,
            "failure_count": problems.count, "failures": problems.items,
            "real_measurement_budget": dict(_REAL_BUDGET)}


def audit_driver_evidence(root, *, protocol, expected_protocol_sha256, expected_driver_seal_sha256):
    """Fresh read-only audit; both expected hashes must come from outside the bundle."""
    document = _thaw(protocol, expected_protocol_sha256)
    if not _is_sha(expected_driver_seal_sha256):
        raise ValueError("an external driver SHA-256 seal is required")
    root = Path(root)
    failures = []
    seal = None
    seal_trusted = False
    anchors = []
    capture_seal = None
    try:
        raw = transport.read_regular(root / "driver-result.json")
        if _sha(raw) != expected_driver_seal_sha256:
            raise DriverError("driver_seal_mismatch")
        seal = json.loads(raw)
        if (type(seal) is not dict or type(seal.get("format")) is not int or seal["format"] != 1 or
                seal.get("kind") != "fixed-substitute-wiring-verdict" or
                seal.get("protocol_sha256") != protocol.sha256 or
                not _is_sha(seal.get("capture_seal_sha256")) or
                type(seal.get("call_records")) is not list or
                len(seal["call_records"]) > len(document["calls"]) or
                type(seal.get("complete")) is not bool or seal.get("lifecycle_counts_available") is not True or
                not all(_valid_anchor(anchor, index, DriverLimits(**document["limits"]))
                        for index, anchor in enumerate(seal["call_records"]))):
            raise DriverError("invalid_driver_seal")
        anchors = seal["call_records"]
        capture_seal = seal["capture_seal_sha256"]
        seal_trusted = True
        if not seal["complete"]:
            failures.append(str(seal.get("first_failure") or "sealed_invalid_driver")[:256])
    except Exception as error:
        failures.append(str(error)[:256])
    result = _inspect(root, protocol, document, anchors, capture_seal,
                      allow_result=True, initial=failures)
    if not seal_trusted:
        result.update(lifecycle_counts_available=False, launch_attempts=None, launched_calls=None,
                      reaped_calls=None, admitted_bytes=None, execution_finished=False)
    if seal is not None and result["complete"]:
        if set(seal) != set(result) or any(type(seal[key]) is not type(value) or seal[key] != value
                                        for key, value in result.items()):
            result.update(complete=False, integrity_status="INVALID", first_failure="driver_verdict_fields_mismatch",
                          failure_count=1, failures=["driver_verdict_fields_mismatch"])
    return {**result, "driver_seal_sha256": expected_driver_seal_sha256}


def run_frozen_substitutes(root, *, protocol, expected_protocol_sha256, seal_path):
    """Run each frozen fixture at most once, retain failure, publish and freshly audit."""
    document = _thaw(protocol, expected_protocol_sha256)
    _verify_sources(document)
    # Reject an already occupied caller-visible name before resolve can erase a
    # dangling final symlink and turn its missing target into a new allocation.
    for supplied in (Path(root), Path(seal_path)):
        if supplied.exists() or supplied.is_symlink():
            raise FileExistsError(supplied)
    root, seal_path = Path(root).resolve(), Path(seal_path).resolve()
    if seal_path == root or root in seal_path.parents:
        raise ValueError("the driver seal must be retained outside the bundle")
    if root.exists() or root.is_symlink():
        raise FileExistsError(root)
    if seal_path.exists() or seal_path.is_symlink():
        raise FileExistsError(seal_path)
    # Parent existence is intentional: a fresh allocation cannot silently create an ancestor tree.
    transport.directory_identity(root.parent)
    transport.directory_identity(seal_path.parent)
    root.mkdir(exist_ok=False)
    (root / "spool").mkdir()
    (root / "calls").mkdir()
    directory_ids = {path: transport.directory_identity(path)
                     for path in (root, root / "spool", root / "calls")}
    transport.save_new(root / "protocol.json", protocol.canonical)
    limits = DriverLimits(**document["limits"])
    session = CaptureSession.create(root / "capture",
        identity={"protocol_sha256": protocol.sha256, "kind": "fixed-substitute-contract"},
        plan=[call["capture"] for call in document["calls"]],
        limits=CaptureLimits(**document["capture_limits"]))
    deadline = time.monotonic_ns() + limits.overall_timeout_ms * 1_000_000
    problems = _Problems()
    anchors = []
    total = 0
    unsealed_transport_record = False
    for index, call in enumerate(document["calls"]):
        try:
            _verify_sources(document)
            transport.check_directories(directory_ids)
            if time.monotonic_ns() >= deadline:
                raise DriverError("overall_timeout_before_launch")
            session.start_call(index)
        except Exception as error:
            problems.add(error)
            break
        record = None
        unsealed_transport_record = True
        try:
            record = transport._capture_process(index=index, argv=tuple(call["argv"]),
                cwd=document["runtime"]["cwd"], env=dict(document["runtime"]["environment"]),
                spool_dir=root / "spool", limits=asdict(limits),
                remaining_total_bytes=limits.max_total_bytes - total,
                overall_deadline_ns=deadline, directory_ids=directory_ids)
            if not _valid_record(record, index, limits):
                raise DriverError("transport_returned_invalid_record")
            total += sum(receipt["byte_count"] for receipt in record["streams"].values())
            transport.check_directories(directory_ids)
            raw = transport.canonical_bytes(record)
            transport.save_new(root / "calls" / f"{index:06d}.json", raw)
            anchors.append({"index": index, "sha256": _sha(raw),
                            **{key: record[key] for key in ("launch_attempted", "launched", "reaped")},
                            "admitted_bytes": sum(r["byte_count"] for r in record["streams"].values())})
            unsealed_transport_record = False
            if not record["transport_ok"]:
                problems.add(record["first_failure"] or "unsuccessful_transport")
        except Exception as error:
            problems.add(error)
        for role in _ROLES:
            try:
                raw = transport.read_regular(root / "spool" / f"{index:06d}.{role}.bin", limits.max_stream_bytes)
                stats = transport.stream_stats(raw)
                if record is None or any(record["streams"][role][key] != value for key, value in stats.items()):
                    problems.add(f"received_spool_mismatch:{index}:{role}")
                if stats != call["expected"][role]:
                    problems.add(f"fixed_output_mismatch:{index}:{role}")
                reader = io.BytesIO(raw)
            except Exception as error:
                problems.add(error)
                reader = _FailedReader(str(error)[:256])
            try:
                session.preserve_stream(index, role, reader)
            except Exception as error:
                problems.add(error)
        try:
            _verify_sources(document)
        except Exception as error:
            problems.add(error)
        try:
            session.finish_call(index, exit_code=record["returncode"] if record else None,
                                failure=problems.items[0] if problems.items else None)
        except Exception as error:
            problems.add(error)
        if problems.count:
            break
    try:
        captured = session.finalize()
    except Exception as error:
        raise DriverError("capture_result_publication_failed") from error
    if unsealed_transport_record:
        # No published record is not evidence of zero consumed launches. Retain
        # both current prefixes and the invalid capture, then return no verdict.
        raise DriverError("transport_call_record_publication_failed")
    try:
        _verify_sources(document)
    except Exception as error:
        problems.add(error)
    result = _inspect(root, protocol, document, anchors, captured["seal_sha256"],
                      allow_result=False, initial=problems.items)
    try:
        transport.check_directories(directory_ids)
        raw = transport.canonical_bytes(result)
        transport.save_new(root / "driver-result.json", raw)
        seal = _sha(raw)
        transport.save_new(seal_path, (seal + "\n").encode("ascii"))
        if transport.read_regular(seal_path, 65) != (seal + "\n").encode("ascii"):
            raise DriverError("external_seal_readback_mismatch")
    except Exception as error:
        raise DriverError("driver_result_or_external_seal_publication_failed") from error
    return audit_driver_evidence(root, protocol=protocol, expected_protocol_sha256=expected_protocol_sha256,
                                 expected_driver_seal_sha256=seal)
