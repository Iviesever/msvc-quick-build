"""Bounded Linux pipe supervision and evidence-file primitives.

Only the higher-level frozen substitute driver chooses commands. Pipe EOF is
recorded independently of the EOF of a subsequently opened spool file.
"""
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import stat
import subprocess
import time


MAX_METADATA_BYTES = 256 * 1024
REAPER_POLICY = "exclusive-driver-waitpid-sigchld-default"
_ROLES = ("stdout", "stderr")


class TransportError(ValueError):
    pass


def require_reaper_policy():
    # The caller must also keep external waiters away for the whole call. This
    # check observes the signal disposition; it cannot prove that ownership.
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise TransportError("sigchld_default_required")


def _message(error):
    if type(error) is str:
        return error[:256]
    try:
        return f"{type(error).__name__}:{error}"[:256]
    except BaseException:
        return type(error).__name__[:256]


def directory_identity(path):
    info = Path(path).lstat()
    if not stat.S_ISDIR(info.st_mode):
        raise TransportError(f"not_original_directory:{Path(path).name}")
    return info.st_dev, info.st_ino


def check_directories(mapping):
    for path, expected in mapping.items():
        if directory_identity(path) != tuple(expected):
            raise TransportError(f"directory_replaced:{Path(path).name}")


def canonical_bytes(value):
    raw = json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                     separators=(",", ":")).encode("utf-8") + b"\n"
    if len(raw) > MAX_METADATA_BYTES:
        raise TransportError("metadata_limit")
    return raw


def stream_stats(raw):
    return {"byte_count": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "lf_count": raw.count(b"\n"), "ends_with_lf": raw.endswith(b"\n")}


def _fingerprint(info):
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise TransportError("not_unaliased_regular_file")
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def read_regular(path, maximum=MAX_METADATA_BYTES):
    """Bounded read with link and observed-replacement rejection."""
    if type(maximum) is not int or not 0 < maximum <= 16 * 1024 * 1024:
        raise TransportError("invalid_read_limit")
    path = Path(path)
    before = path.lstat()
    expected = _fingerprint(before)
    if before.st_size > maximum:
        raise TransportError("file_limit")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        if _fingerprint(os.fstat(descriptor)) != expected:
            raise TransportError("file_replaced_before_read")
        parts, count = [], 0
        while True:
            raw = os.read(descriptor, min(64 * 1024, maximum + 1 - count))
            if not raw:
                break
            parts.append(raw)
            count += len(raw)
            if count > maximum:
                raise TransportError("file_limit")
        if (_fingerprint(os.fstat(descriptor)) != expected or
                _fingerprint(path.lstat()) != expected or count != before.st_size):
            raise TransportError("file_changed_during_read")
        return b"".join(parts)
    finally:
        os.close(descriptor)


def file_identity(path, maximum=16 * 1024 * 1024):
    """Hash one regular source in <=64 KiB chunks without retaining its bytes."""
    if type(maximum) is not int or not 0 < maximum <= 64 * 1024 * 1024:
        raise TransportError("invalid_identity_limit")
    path = Path(path)
    before = path.lstat()
    expected = _fingerprint(before)
    if before.st_size > maximum:
        raise TransportError("file_limit")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        if _fingerprint(os.fstat(descriptor)) != expected:
            raise TransportError("file_replaced_before_read")
        digest, count = hashlib.sha256(), 0
        while True:
            raw = os.read(descriptor, min(64 * 1024, maximum + 1 - count))
            if not raw:
                break
            count += len(raw)
            if count > maximum:
                raise TransportError("file_limit")
            digest.update(raw)
        if (_fingerprint(os.fstat(descriptor)) != expected or
                _fingerprint(path.lstat()) != expected or count != before.st_size):
            raise TransportError("file_changed_during_read")
        return {"path": str(path), "bytes": count, "sha256": digest.hexdigest()}
    finally:
        os.close(descriptor)


def _open_new(path):
    return Path(path).open("xb", buffering=0)


def _write_exact(writer, raw):
    count = writer.write(raw)
    if type(count) is not int or count != len(raw):
        raise TransportError("short_write")


def _close_writer(writer, on_error):
    # Attempt every operation, retaining a later sync/close error as well.
    for operation in (writer.flush, lambda: os.fsync(writer.fileno()), writer.close):
        try:
            operation()
        except BaseException as error:
            on_error(error)


def save_new(path, raw):
    if type(raw) is not bytes or len(raw) > MAX_METADATA_BYTES:
        raise TransportError("metadata_bytes_or_limit")
    path = Path(path)
    parent = {path.parent: directory_identity(path.parent)}
    writer, errors = None, []
    try:
        check_directories(parent)
        writer = _open_new(path)
        check_directories(parent)
        _write_exact(writer, raw)
    except BaseException as error:
        errors.append(error)
    finally:
        if writer is not None:
            _close_writer(writer, errors.append)
    if errors:
        raise TransportError(_message(errors[0])) from errors[0]
    check_directories(parent)
    if read_regular(path) != raw:
        raise TransportError("saved_bytes_changed")


def _capture_process(*, index, argv, cwd, env, spool_dir, limits,
                     remaining_total_bytes, overall_deadline_ns, directory_ids):
    """Supervise one frozen substitute and retain both bounded original pipes."""
    started = time.monotonic_ns()
    record = {"index": index, "launch_attempted": False, "launched": False,
              "pid": None, "returncode": None, "reaped": False,
              "wait_status_unavailable": False,
              "started_monotonic_ns": started, "finished_monotonic_ns": None,
              "term_sent": False, "kill_sent": False, "first_failure": None,
              "failures": [], "streams": {}, "transport_ok": False}
    for role in _ROLES:
        record["streams"][role] = {**stream_stats(b""), "pipe_eof": False,
                                  "overflow_probe_hex": "", "capture_error": None}
    digests = {role: hashlib.sha256() for role in _ROLES}
    writers, pipes = {}, {}
    process = selector = None
    remaining = remaining_total_bytes
    stop_started = None
    term_attempted = kill_attempted = False
    selector_failed = False
    active_deadline = min(overall_deadline_ns,
                          started + limits["call_timeout_ms"] * 1_000_000)
    grace_ns = limits["terminate_grace_ms"] * 1_000_000
    kill_ns = limits["kill_wait_ms"] * 1_000_000

    def fail(error):
        nonlocal stop_started
        message = _message(error)
        if record["first_failure"] is None:
            record["first_failure"] = message
            stop_started = time.monotonic_ns()
        if len(record["failures"]) < 32:
            record["failures"].append(message)

    def stream_fail(role, error):
        message = _message(error)
        if record["streams"][role]["capture_error"] is None:
            record["streams"][role]["capture_error"] = message
        fail(f"{role}:{message}")

    def close_pipe(role):
        pipe = pipes.pop(role, None)
        if pipe is None:
            return
        if selector is not None:
            try:
                selector.unregister(pipe)
            except KeyError:
                pass
            except BaseException as error:
                stream_fail(role, error)
        try:
            pipe.close()
        except BaseException as error:
            stream_fail(role, error)

    def send_signal(which):
        nonlocal term_attempted, kill_attempted
        poll_child()
        if not child_waitable():
            return
        if which == signal.SIGTERM:
            term_attempted = True
        else:
            kill_attempted = True
        try:
            os.killpg(process.pid, which)
            record["term_sent" if which == signal.SIGTERM else "kill_sent"] = True
        except ProcessLookupError:
            pass  # Natural exit does not remove the obligation to reap.
        except BaseException as error:
            fail(error)

    def child_waitable():
        return not record["reaped"] and not record["wait_status_unavailable"]

    def poll_child():
        if not child_waitable():
            return
        try:
            pid, status = os.waitpid(process.pid, os.WNOHANG)
            if pid == 0:
                return
            if pid != process.pid or not (os.WIFEXITED(status) or os.WIFSIGNALED(status)):
                record["wait_status_unavailable"] = True
                fail("child_wait_status_invalid")
                return
            code = os.waitstatus_to_exitcode(status)
            record.update(returncode=code, reaped=True)
            process.returncode = code
            if code != 0 and record["first_failure"] is None:
                fail(f"child_exit:{code}")
        except InterruptedError:
            pass
        except ChildProcessError:
            # Popen.poll()/wait() can synthesize zero for ECHILD. Keep the
            # evidence unknown and never signal an ID no longer owned by us.
            record["wait_status_unavailable"] = True
            fail("child_wait_status_unavailable")
        except BaseException as error:
            fail(error)

    def wait_until(deadline):
        while child_waitable():
            poll_child()
            remaining_ns = deadline - time.monotonic_ns()
            if not child_waitable() or remaining_ns <= 0:
                break
            time.sleep(min(limits["poll_ms"] / 1000, remaining_ns / 1_000_000_000))

    def read_pipe(role):
        nonlocal remaining
        pipe = pipes.get(role)
        if pipe is None:
            return
        receipt = record["streams"][role]
        admitted = min(limits["max_stream_bytes"] - receipt["byte_count"], remaining)
        request = min(limits["chunk_bytes"], admitted) if admitted else 1
        try:
            raw = os.read(pipe.fileno(), request)
        except (BlockingIOError, InterruptedError):
            return
        except BaseException as error:
            stream_fail(role, error)
            close_pipe(role)
            return
        if not raw:
            receipt["pipe_eof"] = True
            close_pipe(role)
            return
        if not admitted:
            receipt["overflow_probe_hex"] = raw.hex()
            stream_fail(role, "stream_or_total_limit")
            close_pipe(role)
            return
        # These are admitted original pipe bytes, even if a write fails later.
        receipt["byte_count"] += len(raw)
        receipt["lf_count"] += raw.count(b"\n")
        receipt["ends_with_lf"] = raw.endswith(b"\n")
        digests[role].update(raw)
        receipt["sha256"] = digests[role].hexdigest()
        remaining -= len(raw)
        try:
            check_directories(directory_ids)
            _write_exact(writers[role], raw)
        except BaseException as error:
            stream_fail(role, error)
            close_pipe(role)

    try:
        for role in _ROLES:
            try:
                check_directories(directory_ids)
                writers[role] = _open_new(Path(spool_dir) / f"{index:06d}.{role}.bin")
                check_directories(directory_ids)
            except BaseException as error:
                stream_fail(role, error)
        if time.monotonic_ns() >= active_deadline:
            fail("deadline_before_launch")
        if record["first_failure"] is None:
            selector = selectors.DefaultSelector()
            check_directories(directory_ids)
            require_reaper_policy()
            record["launch_attempted"] = True
            process = subprocess.Popen(
                argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False,
                close_fds=True, start_new_session=True, bufsize=0)
            record.update(launched=True, pid=process.pid)
            for role in _ROLES:
                pipe = getattr(process, role)
                pipes[role] = pipe
                try:
                    os.set_blocking(pipe.fileno(), False)
                    selector.register(pipe, selectors.EVENT_READ, role)
                except BaseException as error:
                    stream_fail(role, error)
                    close_pipe(role)
            while True:
                poll_child()
                now = time.monotonic_ns()
                if record["first_failure"] is None and now >= active_deadline:
                    fail("overall_timeout" if now >= overall_deadline_ns else "call_timeout")
                if not child_waitable() and not pipes:
                    break
                if record["first_failure"] is not None:
                    if child_waitable() and not term_attempted:
                        send_signal(signal.SIGTERM)
                    if (child_waitable() and now >= stop_started + grace_ns
                            and not kill_attempted):
                        send_signal(signal.SIGKILL)
                    deadline = stop_started + grace_ns + kill_ns
                    if now >= deadline:
                        if not record["reaped"]:
                            fail("child_not_reaped_before_deadline")
                        if pipes:
                            fail("pipe_drain_deadline")
                        break
                    next_checkpoint = (stop_started + grace_ns if child_waitable() and not kill_attempted
                                       else deadline)
                else:
                    next_checkpoint = active_deadline
                timeout = min(limits["poll_ms"] / 1000,
                              max(0, next_checkpoint - now) / 1_000_000_000)
                if selector_failed:
                    time.sleep(timeout)
                    ready_roles = tuple(pipes)
                else:
                    try:
                        ready_roles = tuple(key.data for key, _ in selector.select(timeout))
                    except BaseException as error:
                        fail(error)
                        selector_failed = True
                        ready_roles = tuple(pipes)
                for role in ready_roles:
                    read_pipe(role)
    except BaseException as error:
        fail(error)
    finally:
        # The main loop handles normal failures while draining. This also covers
        # unexpected supervisor exceptions between launch and that loop.
        if process is not None:
            poll_child()
            if child_waitable():
                if stop_started is None:
                    fail("supervisor_incomplete")
                if not term_attempted:
                    send_signal(signal.SIGTERM)
                deadline = stop_started + grace_ns + kill_ns
                wait_until(min(stop_started + grace_ns, deadline))
                if child_waitable():
                    if not kill_attempted:
                        send_signal(signal.SIGKILL)
                    wait_until(deadline)
                    if child_waitable():
                        fail("child_not_reaped_before_deadline")
            # Best obtainable nonblocking prefixes after emergency cleanup.
            while pipes and stop_started is not None and time.monotonic_ns() < (
                    stop_started + grace_ns + kill_ns):
                before = sum(r["byte_count"] for r in record["streams"].values())
                for role in tuple(pipes):
                    read_pipe(role)
                after = sum(r["byte_count"] for r in record["streams"].values())
                if after == before:
                    break
        for role in tuple(pipes):
            close_pipe(role)
        if selector is not None:
            try:
                selector.close()
            except BaseException as error:
                fail(error)
        for role, writer in writers.items():
            _close_writer(writer, lambda error, role=role: stream_fail(role, error))
        for role in _ROLES:
            if not record["streams"][role]["pipe_eof"]:
                stream_fail(role, "pipe_eof_not_observed")
        record["finished_monotonic_ns"] = time.monotonic_ns()
        record["transport_ok"] = (record["launched"] and record["reaped"] and
            not record["wait_status_unavailable"] and
            record["returncode"] == 0 and record["first_failure"] is None and
            all(r["pipe_eof"] and r["capture_error"] is None
                for r in record["streams"].values()))
    return record
