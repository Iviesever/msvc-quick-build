"""One Linux CPython observation of frozen 011 refusal; never PE index admission.

Usage: this.py ABSOLUTE_MANIFEST EXPECTED_SHA256. Only a clean exit and the
exclusive report jointly complete this observation. OS creation, kill and
durability calls have no absolute wall-return guarantee; a late return fails.
"""

import hashlib
import json
import os
from pathlib import Path
import selectors
import struct
import subprocess
import sys
import time
import types

LIMITS = {"seconds": 5, "core_bytes": 8192, "stream_bytes": 8230,
          "marker_bytes": 1024, "report_bytes": 15360, "parser_calls": 1,
          "traceback_nodes": 64, "chain_rvas": 32, "envelope_bytes": 528,
          "root_entry_bytes": 12, "context_work_units": 16384,
          "manifest_bytes": 16384, "source_bytes": 65536,
          "input_bytes": 1048576, "python_bytes": 67108864}


def _module(name, raw, path):
    module = types.ModuleType(name)
    module.__file__ = path
    exec(compile(raw, path, "exec"), module.__dict__)
    return module


def _bootstrap(path, sha):
    """Verify the focused helper's bytes before executing that helper."""
    if type(sha) is not str or len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        raise ValueError("MANIFEST_HASH")
    with open(path, "rb") as stream:
        raw = stream.read(LIMITS["manifest_bytes"] + 1)
    if len(raw) > LIMITS["manifest_bytes"]:
        raise ValueError("MANIFEST_SIZE")
    if hashlib.sha256(raw).hexdigest() != sha:
        raise ValueError("MANIFEST_HASH")
    owner_path = Path(__file__).resolve(strict=True)
    try:
        item = json.loads(raw)["sources"]["io"]
        io_path = owner_path.with_name("bcrypt_diagnosis_io_012.py")
        if (item["path"] != str(io_path) or type(item["bytes"]) is not int
                or not 0 < item["bytes"] <= LIMITS["source_bytes"]):
            raise ValueError("BOOTSTRAP")
        with io_path.open("rb") as stream:
            data = stream.read(item["bytes"] + 1)
        if len(data) != item["bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ValueError("IDENTITY_IO")
    except (KeyError, TypeError, UnicodeError, RecursionError, json.JSONDecodeError):
        raise ValueError("BOOTSTRAP") from None
    helper = _module("_bcrypt012_io", data, str(io_path))
    return helper, helper.manifest(path, sha, owner_path, LIMITS)


def _deadline(deadline):
    if time.monotonic_ns() >= deadline:
        raise ValueError("DEADLINE")


def _failure(error, fallback):
    value = error.args[0] if isinstance(error, ValueError) and len(error.args) == 1 else None
    if (type(value) is str and 0 < len(value) <= 64
            and all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789" for c in value)):
        return value
    return fallback


def worker(path, sha):
    """Private worker: matching reservation, READY, one GO, then one diagnose."""
    try:
        helper, manifest = _bootstrap(path, sha)
        helper.check_marker(manifest, sha)
        data = helper.verify(manifest, retain=True)
        frozen = _module("_bcrypt012_parser", data["parser"], manifest["sources"]["parser"]["path"])
        context = _module("_bcrypt012_context", data["context"], manifest["sources"]["context"]["path"])
        output = sys.stdout.buffer
        output.write(b"R" + bytes.fromhex(sha))
        output.flush()
        go = sys.stdin.buffer.read(9)
        if len(go) != 9 or go[:1] != b"G":
            return 2
        deadline = struct.unpack("<Q", go[1:])[0]
        if deadline > time.monotonic_ns() + 5_000_000_000:
            return 2
        _deadline(deadline)
        result = context.diagnose(frozen, data["input"], manifest["input"]["init_rva"])
        _deadline(deadline)
        raw = helper.encode(result)
        helper.core_result(raw)
        output.write(b"D" + struct.pack("<I", len(raw)) + raw)
        output.flush()
        return 0
    except Exception:
        return 2


def _drive(process, sha, deadline, state, helper):
    raw = bytearray()
    digest = hashlib.sha256()
    length = None
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while True:
            _deadline(deadline)
            events = selector.select(max(0, (deadline - time.monotonic_ns()) / 1e9))
            _deadline(deadline)
            if not events:
                continue
            chunk = os.read(process.stdout.fileno(), min(4096, 8231 - len(raw)))
            if not chunk:
                if not state["go_sent"] or length is None or len(raw) != 38 + length:
                    raise ValueError("PROTOCOL_EOF")
                _deadline(deadline)
                state["returncode"] = process.wait(timeout=(deadline - time.monotonic_ns()) / 1e9)
                state["reaped"] = True
                _deadline(deadline)
                if state["returncode"] != 0:
                    raise ValueError("PROCESS_EXIT")
                return helper.core_result(bytes(raw[38:]))
            raw.extend(chunk)
            digest.update(chunk)
            state.update(stream_bytes=len(raw), stream_sha256=digest.hexdigest(),
                         stream_prefix_hex=raw[:128].hex())
            if len(raw) > 8230:
                raise ValueError("STREAM_LIMIT")
            if not state["go_sent"]:
                if raw[:1] != b"R":
                    raise ValueError("READY")
                if len(raw) < 33:
                    continue
                if raw[:33] != b"R" + bytes.fromhex(sha):
                    raise ValueError("READY")
                if len(raw) > 33:
                    raise ValueError("EARLY_RESULT")
                state["ready"] = True
                _deadline(deadline)
                process.stdin.write(b"G" + struct.pack("<Q", deadline))
                process.stdin.flush()
                process.stdin.close()
                state["go_sent"] = True
            if len(raw) > 33 and raw[33] != ord("D"):
                raise ValueError("PROTOCOL_TAG")
            if len(raw) >= 38:
                length = struct.unpack("<I", raw[34:38])[0]
                if not 0 < length <= 8192:
                    raise ValueError("CORE_SIZE")
                if len(raw) > 38 + length:
                    raise ValueError("PROTOCOL_LENGTH")


def _stop(process, state):
    """Only the owned PID; no extra cleanup deadline or unbounded wait."""
    if process is None:
        return
    try:
        if process.poll() is None:
            process.kill()
            state["kill_requested"] = True
        try:
            state["returncode"] = process.wait(timeout=0)
            state["reaped"] = True
        except subprocess.TimeoutExpired:
            state["reaped"] = False
    except OSError:
        state["reaped"] = False
    finally:
        for stream in (process.stdin, process.stdout):
            try:
                if stream is not None and not getattr(stream, "closed", False):
                    stream.close()
            except OSError:
                pass


def run_once(path, sha):
    helper, manifest = _bootstrap(path, sha)
    helper.verify(manifest)
    helper.reserve(manifest, sha)
    state = {"owner_pid": os.getpid(), "pid": None, "ready": False, "go_sent": False,
             "stream_bytes": 0, "stream_sha256": hashlib.sha256(b"").hexdigest(),
             "stream_prefix_hex": "", "returncode": None, "reaped": False,
             "kill_requested": False}
    checks = {role: {"bytes": value["bytes"], "sha256": value["sha256"],
                     "post_verified": False} for role, value in helper.identities(manifest).items()}
    process, core, failures, manifest_post_verified = None, None, [], False
    start = time.monotonic_ns()
    deadline = start + 5_000_000_000
    try:
        try:
            process = subprocess.Popen(manifest["argv_prefix"] + [path, sha],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, shell=False, close_fds=True,
                                       cwd="/", env={"LANG": "C", "LC_ALL": "C"})
        except OSError:
            raise ValueError("PROCESS_CREATE") from None
        state["pid"] = process.pid
        _deadline(deadline)
        core = _drive(process, sha, deadline, state, helper)
        if core["status"] != "located_refusal":
            failures.append("CORE_" + core["status"].upper())
    except subprocess.TimeoutExpired:
        failures.append("DEADLINE")
    except ValueError as error:
        failures.append(_failure(error, "WORKER_IO"))
    except Exception:
        failures.append("WORKER_IO")
    finally:
        _stop(process, state)
    try:
        raw = helper.read_max(path, LIMITS["manifest_bytes"])
        helper.require(hashlib.sha256(raw).hexdigest() == sha, "MANIFEST_HASH")
        manifest_post_verified = True
        helper.verify(manifest)
        for value in checks.values():
            value["post_verified"] = True
    except (OSError, ValueError) as error:
        failures.append(_failure(error, "IDENTITY_POST"))
    finish = time.monotonic_ns()
    if finish >= deadline and "DEADLINE" not in failures:
        failures.append("DEADLINE")
    if process is not None and not state["reaped"]:
        failures.append("WORKER_NOT_REAPED")
    report = {"schema": "bcrypt-pe-diagnosis-012-report-v1", "stage": "refusal_context",
              "call_id": manifest["call_id"], "manifest_sha256": sha,
              "manifest_post_verified": manifest_post_verified,
              "status": "failed_consumed" if failures else "located_refusal",
              "failure_code": failures[0] if failures else None, "failures": failures,
              "consumed": True, "parser_call_reservations": 1,
              "parser_calls": core["parser_calls"] if core is not None else None,
              "static_index_admitted": False, "completion_requires_clean_owner_exit": True,
              "identity_checks": checks, "worker": state, "core": core, "limits": LIMITS,
              "timing": {"start_monotonic_ns": start, "deadline_monotonic_ns": deadline,
                         "observed_finish_monotonic_ns": finish,
                         "deadline_exceeded": finish >= deadline,
                         "os_creation_kill_fsync_wall_return_bounded": False}}
    helper.save(Path(manifest["output_dir"]) / "report.json", report, 15360)
    if not failures and time.monotonic_ns() >= deadline:
        raise ValueError("DURABILITY_OR_OBSERVATION_LATE")
    return report


def main():
    try:
        if len(sys.argv) == 4 and sys.argv[1] == "--worker":
            return worker(sys.argv[2], sys.argv[3])
        if len(sys.argv) != 3:
            raise ValueError("ARGUMENTS")
        report = run_once(sys.argv[1], sys.argv[2])
        return 0 if report["status"] == "located_refusal" else 2
    except Exception as error:
        print(_failure(error, "OWNER_IO"), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
