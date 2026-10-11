"""One-shot Windows probe capture; Job accounting never grants A2 authority.

The caller supplies a reviewed executable, arguments and environment. Captures
are new raw files, opened before launch and retained even on infrastructure
failure. The caller must reject errors, timeout or incomplete Job accounting.
This helper does not discover, terminate or certify external/shared services.
"""
from __future__ import annotations

import ctypes
import os
from pathlib import Path
import subprocess
import time


DWORD = ctypes.c_uint32
HANDLE = ctypes.c_void_p


class _BasicLimits(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_int64),
        ("PerJobUserTimeLimit", ctypes.c_int64), ("LimitFlags", DWORD),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", DWORD),
        ("Affinity", ctypes.c_size_t), ("PriorityClass", DWORD),
        ("SchedulingClass", DWORD),
    ]


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _BasicLimits),
                ("IoInfo", ctypes.c_uint64 * 6),
                ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t)]


class _Accounting(ctypes.Structure):
    _fields_ = [("TotalUserTime", ctypes.c_int64),
                ("TotalKernelTime", ctypes.c_int64),
                ("ThisPeriodTotalUserTime", ctypes.c_int64),
                ("ThisPeriodTotalKernelTime", ctypes.c_int64),
                ("TotalPageFaultCount", DWORD), ("TotalProcesses", DWORD),
                ("ActiveProcesses", DWORD), ("TotalTerminatedProcesses", DWORD)]


class _ProcessIds(ctypes.Structure):
    _fields_ = [("NumberOfAssignedProcesses", DWORD),
                ("NumberOfProcessIdsInList", DWORD),
                ("ProcessIdList", ctypes.c_size_t * 8)]


class _FileTime(ctypes.Structure):
    _fields_ = [("low", DWORD), ("high", DWORD)]


def run_captured_in_job(argv: list[str], cwd: Path, env: dict,
                        timeout_seconds: int, stdout_path: Path,
                        stderr_path: Path) -> dict:
    """Create at most one suspended root; admit it before its primary thread runs.

    Cleanup polls real Job accounting for at most ten seconds. The completion
    field means zero active Job members plus a signaled retained root handle;
    it does not certify each descendant's process-object signaling or service
    quiescence. OS calls and capture fsync have no hard wall-time guarantee.
    """
    started = time.monotonic()
    row = dict(pid=None, thread_id=None, exit_code=None, timed_out=False,
               create_process_calls=0, created_processes=0,
               job_assigned=False, root_resumed=False,
               root_signaled=False, job_termination_requested=False,
               job_total_processes=None, job_active_processes=None,
               job_members_completed=False, cleanup_seconds=0.0,
               completion_scope="job-active-accounting-and-root-handle-only",
               live_member_diagnostic=None,
               A2=False, errors=[])
    captures, inherited = [], []
    stdin = api = kernel = job = process = thread = None
    stage = "capture-open"

    def record(where, error):
        row["errors"].append(dict(stage=where, error=f"{type(error).__name__}: {error}",
                                  winerror=getattr(error, "winerror", None)))

    def checked(value):
        if not value:
            raise ctypes.WinError(ctypes.get_last_error())
        return value

    def accounting():
        info = _Accounting()
        checked(kernel.QueryInformationJobObject(job, 1, ctypes.byref(info),
                                                 ctypes.sizeof(info), None))
        row["job_total_processes"] = info.TotalProcesses
        row["job_active_processes"] = info.ActiveProcesses
        return info

    def observe_root():
        status = kernel.WaitForSingleObject(process, 0)
        if status == 0:
            row["root_signaled"] = True
            row["exit_code"] = api.GetExitCodeProcess(process)
        elif status != 258:
            raise ctypes.WinError(ctypes.get_last_error())

    def diagnose_live_members(deadline):
        # One bounded diagnostic snapshot of this exact owned Job. It is not
        # a complete lifetime census, an image-file hash, or A2 authority.
        snapshot = dict(capacity=8, assigned=None, returned=None, members=[],
                        scope="one-owned-job-snapshot-before-existing-termination")
        row["live_member_diagnostic"] = snapshot
        ids = _ProcessIds()
        checked(kernel.QueryInformationJobObject(job, 3, ctypes.byref(ids),
                                                 ctypes.sizeof(ids), None))
        snapshot["assigned"] = ids.NumberOfAssignedProcesses
        snapshot["returned"] = ids.NumberOfProcessIdsInList
        if (ids.NumberOfAssignedProcesses != ids.NumberOfProcessIdsInList or
                ids.NumberOfProcessIdsInList > 8):
            raise RuntimeError("Owned Job diagnostic list incomplete or over cap")
        for pid in ids.ProcessIdList[:ids.NumberOfProcessIdsInList]:
            if time.monotonic() >= deadline:
                raise TimeoutError("Owned Job diagnostic exhausted existing cleanup deadline")
            member = dict(listed_pid=pid, owned_job_member=None)
            snapshot["members"].append(member)
            handle = None
            where = "open"
            try:
                if not 0 < pid <= 0xFFFFFFFF:
                    raise RuntimeError("Invalid diagnostic process identifier")
                # QUERY_LIMITED_INFORMATION | SYNCHRONIZE; never inheritable.
                handle = checked(kernel.OpenProcess(0x00101000, False, pid))
                where = "job-membership"
                belongs = ctypes.c_int()
                checked(kernel.IsProcessInJob(handle, job, ctypes.byref(belongs)))
                member["owned_job_member"] = bool(belongs.value)
                if not belongs.value:
                    raise RuntimeError("Listed PID no longer belongs to this owned Job")
                where = "process-id"
                member["handle_pid"] = checked(kernel.GetProcessId(handle))
                if member["handle_pid"] != pid:
                    raise RuntimeError("Diagnostic process-handle PID mismatch")
                where = "creation-time"
                creation, exited, kernel_time, user_time = (_FileTime() for _ in range(4))
                checked(kernel.GetProcessTimes(handle, ctypes.byref(creation),
                    ctypes.byref(exited), ctypes.byref(kernel_time), ctypes.byref(user_time)))
                member["creation_filetime"] = (creation.high << 32) | creation.low
                where = "image-name"
                image = ctypes.create_unicode_buffer(32768)
                length = DWORD(len(image))
                checked(kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(length)))
                member["image_path"] = image[:length.value]
                where = "signal-observation"
                member["wait_status"] = kernel.WaitForSingleObject(handle, 0)
                if member["wait_status"] not in (0, 258):
                    raise ctypes.WinError(ctypes.get_last_error())
            except BaseException as error:
                member["error_stage"] = where
                member["error"] = f"{type(error).__name__}: {error}"
                record("live-member-diagnostic-" + where, error)
            finally:
                if handle is not None:
                    try:
                        api.CloseHandle(handle)
                    except BaseException as error:
                        record("close-live-member-diagnostic", error)

    try:
        for label, path in (("stdout", stdout_path), ("stderr", stderr_path)):
            captures.append((label, Path(path).open("xb", buffering=0)))
        stage = "platform-and-input"
        if os.name != "nt":
            raise RuntimeError("Windows CPython is required")
        if (not isinstance(argv, list) or not argv or
                any(not isinstance(arg, str) or "\0" in arg for arg in argv) or
                not Path(argv[0]).is_absolute() or not Path(cwd).is_absolute() or
                not isinstance(env, dict) or any(
                    not isinstance(key, str) or not isinstance(value, str) or
                    "\0" in key or "\0" in value for key, value in env.items()) or
                type(timeout_seconds) is not int or not 0 < timeout_seconds <= 4294967):
            raise ValueError("Explicit executable/cwd and bounded positive timeout required")
        import _winapi as api
        import msvcrt
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, ctypes.c_wchar_p], HANDLE),
            "SetInformationJobObject": ([HANDLE, ctypes.c_int, ctypes.c_void_p, DWORD], ctypes.c_int),
            "QueryInformationJobObject": ([HANDLE, ctypes.c_int, ctypes.c_void_p, DWORD, ctypes.POINTER(DWORD)], ctypes.c_int),
            "AssignProcessToJobObject": ([HANDLE, HANDLE], ctypes.c_int),
            "ResumeThread": ([HANDLE], DWORD),
            "TerminateJobObject": ([HANDLE, DWORD], ctypes.c_int),
            "TerminateProcess": ([HANDLE, DWORD], ctypes.c_int),
            "WaitForSingleObject": ([HANDLE, DWORD], DWORD),
            "OpenProcess": ([DWORD, ctypes.c_int, DWORD], HANDLE),
            "IsProcessInJob": ([HANDLE, HANDLE, ctypes.POINTER(ctypes.c_int)], ctypes.c_int),
            "GetProcessId": ([HANDLE], DWORD),
            "GetProcessTimes": ([HANDLE] + [ctypes.POINTER(_FileTime)] * 4, ctypes.c_int),
            "QueryFullProcessImageNameW": ([HANDLE, DWORD, ctypes.c_wchar_p,
                                            ctypes.POINTER(DWORD)], ctypes.c_int),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(kernel, name)
            function.argtypes, function.restype = arguments, result
        stage = "job-create"
        job = checked(kernel.CreateJobObjectW(None, None))  # Unnamed, not inheritable.
        limits = _ExtendedLimits()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE; no breakaway.
        checked(kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
        stdin = open(os.devnull, "rb", buffering=0)
        stage = "standard-handles"
        owner = api.GetCurrentProcess()
        for stream in (stdin, captures[0][1], captures[1][1]):
            inherited.append(api.DuplicateHandle(owner, msvcrt.get_osfhandle(stream.fileno()),
                                                 owner, 0, True, api.DUPLICATE_SAME_ACCESS))
        startup = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESTDHANDLES,
                                        hStdInput=inherited[0], hStdOutput=inherited[1],
                                        hStdError=inherited[2],
                                        lpAttributeList={"handle_list": inherited[:]})
        stage = "create-suspended"
        row["create_process_calls"] = 1
        row["created_processes"] = None  # Unknown until native creation returns handles.
        process, thread, row["pid"], row["thread_id"] = api.CreateProcess(
            argv[0], subprocess.list2cmdline(argv), None, None, True,
            0x00000004, env, str(cwd), startup)  # CREATE_SUSPENDED; exactly one call.
        row["created_processes"] = 1
        stage = "close-parent-standard-duplicates"
        while inherited:
            api.CloseHandle(inherited.pop())
        stage = "job-admission"
        checked(kernel.AssignProcessToJobObject(job, process))
        row["job_assigned"] = True
        initial = accounting()
        if initial.TotalProcesses != 1 or initial.ActiveProcesses != 1:
            raise RuntimeError("Unexpected membership before primary-thread resume")
        stage = "resume-primary-thread"
        previous = kernel.ResumeThread(thread)
        if previous == 0xFFFFFFFF:
            raise ctypes.WinError(ctypes.get_last_error())
        if previous != 1:
            raise RuntimeError(f"Unexpected primary-thread suspend count: {previous}")
        row["root_resumed"] = True
        stage = "root-wait"
        status = kernel.WaitForSingleObject(process, timeout_seconds * 1000)
        if status == 258:
            row["timed_out"] = True
        elif status != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        observe_root()
    except BaseException as error:
        record(stage, error)
    finally:
        cleanup_started = time.monotonic()
        deadline = cleanup_started + 10.0
        if process is not None and kernel is not None:
            try:
                observe_root()
                if row["job_assigned"]:
                    info = accounting()
                    if info.ActiveProcesses:
                        row["job_termination_requested"] = True
                        if row["root_signaled"] and not row["timed_out"]:
                            record("root-exit-live-members", RuntimeError("Root exited with live Job members"))
                            try:
                                diagnose_live_members(deadline)
                            except BaseException as error:
                                record("live-member-diagnostic", error)
                        checked(kernel.TerminateJobObject(job, 0xC000013A))
                    while True:
                        observe_root()
                        info = accounting()
                        if info.ActiveProcesses == 0 and row["root_signaled"]:
                            row["job_members_completed"] = True
                            break
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise TimeoutError("Owned Job did not drain within ten seconds")
                        time.sleep(min(0.01, remaining))
                elif not row["root_signaled"]:
                    # Admission failed: only this real, still-suspended root is terminated.
                    checked(kernel.TerminateProcess(process, 0xC000013A))
                    remaining_ms = max(0, int((deadline - time.monotonic()) * 1000))
                    status = kernel.WaitForSingleObject(process, remaining_ms)
                    if status != 0:
                        raise TimeoutError("Unadmitted suspended root did not terminate")
                    observe_root()
            except BaseException as error:
                record("owned-process-cleanup", error)
        # Last Job handle close requests kill on cleanup failure; it proves no drain.
        for label, handle in [("job", job), ("thread", thread), ("process", process)] + [
                ("standard-duplicate", handle) for handle in inherited]:
            if handle is not None and api is not None:
                try:
                    api.CloseHandle(handle)
                except BaseException as error:
                    record("close-" + label, error)
        if stdin is not None:
            try:
                stdin.close()
            except BaseException as error:
                record("close-stdin", error)
        row["cleanup_seconds"] = time.monotonic() - cleanup_started
        for label, stream in captures:
            for operation in ("flush", "fsync", "close"):
                try:
                    if operation == "fsync":
                        os.fsync(stream.fileno())
                    else:
                        getattr(stream, operation)()
                except BaseException as error:
                    record(label + "-" + operation, error)
        row["seconds"] = time.monotonic() - started
    return row
