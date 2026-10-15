"""Linux descendant drain for a dedicated, initially childless supervisor.

Not a process sandbox: no prevention of fd transfer, external file writers,
ptrace, hostile native code or supervisor death. Never activate in a shared
interpreter. setsid/double-fork alone cannot evade ancestor-subreaper adoption.
Only the supervisor's own children are inspected; no host-wide PID scan.
"""
from __future__ import annotations
import ctypes
import os
import signal
import sys
import time

from bcrypt_guarded_owner_017 import Refused, need

WALL = 0x40000000  # Linux __WALL, including non-SIGCHLD clone children.
WAIT = os.WEXITED | os.WNOHANG | os.WNOWAIT | WALL if hasattr(os, 'WEXITED') else 0
CHILD_CAP, SCAN_CAP, PROC_CAP, DRAIN_NS = 16, 256, 256, 1_000_000_000


def _policy():
    need(sys.platform == 'linux' and hasattr(os, 'pidfd_open') and
         hasattr(os, 'P_PIDFD') and hasattr(signal, 'pidfd_send_signal'), 'SCOPE_PLATFORM')
    need(signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL, 'SCOPE_SIGCHLD')
    with os.scandir('/proc/self/task') as tasks:
        first = next(tasks, None)
        need(first is not None and first.name == str(os.getpid()) and
             next(tasks, None) is None, 'SCOPE_THREADS')
    # This optional procfs interface must exist BEFORE any workload starts.
    # Its contents are only candidates; empty text is never ECHILD proof.
    try:
        _children()
    except OSError:
        raise Refused('SCOPE_CHILD_INTERFACE_UNAVAILABLE') from None


def _prctl():
    # Current process's loaded libc namespace, never find_library or host search.
    fn = ctypes.CDLL(None, use_errno=True).prctl
    fn.restype = ctypes.c_int
    return fn


def _get(fn):
    value = ctypes.c_int(-1)
    need(fn(37, ctypes.byref(value), 0, 0, 0) == 0 and value.value in (0, 1), 'SCOPE_PRCTL')
    return value.value


def _set(fn, value):
    need(fn(36, ctypes.c_ulong(value), 0, 0, 0) == 0 and _get(fn) == value, 'SCOPE_PRCTL')


def _peek():
    # None means at least one live child. ECHILD alone means no children.
    return os.waitid(os.P_ALL, 0, WAIT)


def _children():
    with open(f'/proc/self/task/{os.getpid()}/children', 'rb', buffering=0) as stream:
        raw = stream.read(PROC_CAP + 1)
    need(len(raw) <= PROC_CAP and all(c in b'0123456789 ' for c in raw), 'SCOPE_CHILD_LIST')
    words = raw.split()
    need(len(words) <= CHILD_CAP and all(1 <= len(w) <= 10 for w in words), 'SCOPE_CHILD_CAP')
    result = [int(w) for w in words]
    need(len(result) == len(set(result)) and
         all(1 < p <= 2147483647 and p != os.getpid() for p in result), 'SCOPE_CHILD_LIST')
    return result


class DescendantScope:
    """Single-use lifecycle. Existing OwnerTransport must finish before drain.

    Exclusive child reaping is required: no other threads, native SIGCHLD hooks
    or asynchronous waiters. Reset the already-default SIGCHLD disposition in
    this dedicated supervisor before activation (also clears SA_NOCLDWAIT).
    Unknown policy/ownership never permits a guessed-PID signal or empty claim.
    """
    def __init__(self):
        self.used = self.active = self.finished = self.empty = False
        self.pid = os.getpid()
        self.fn = None
        self.result = None

    def activate(self):
        need(not self.used, 'REUSED'); self.used = True
        _policy()
        try:
            _peek()
        except ChildProcessError:
            pass
        else:
            raise Refused('SCOPE_PREEXISTING_CHILD')
        self.fn = _prctl()
        need(_get(self.fn) == 0, 'SCOPE_PREEXISTING_REAPER')
        signal.signal(signal.SIGCHLD, signal.SIG_DFL)
        _set(self.fn, 1); self.active = True

    def drain(self):
        need(self.active and not self.finished and os.getpid() == self.pid, 'SCOPE_STATE')
        self.finished = True
        start = time.monotonic_ns(); deadline = start + DRAIN_NS
        handles = {}; issued = 0
        r = dict(schema='bcrypt-descendant-scope-017-v1', status='refused', reason=None,
                 activated=True, children_reaped=0, exit_codes=[], signal_attempts=0, scans=0,
                 unexpected_children=False, descendant_domain_empty=False,
                 policy_restored=False, cleanup_failed=False, elapsed_ns=None,
                 writers_quiescent=False, observation_accepted=False)
        def fail(code):
            if r['reason'] is None: r['reason'] = code
        try:
            for _ in range(SCAN_CAP):
                r['scans'] += 1
                _policy(); need(_get(self.fn) == 1, 'SCOPE_POLICY_CHANGED')
                need(start <= time.monotonic_ns() < deadline, 'SCOPE_DEADLINE')
                try:
                    event = _peek()
                except ChildProcessError:
                    need(not handles, 'SCOPE_LOST_WAIT_OWNERSHIP')
                    self.empty = True
                    r['descendant_domain_empty'] = True
                    break
                if event is not None:
                    r['unexpected_children'] = True
                    need(r['children_reaped'] < CHILD_CAP, 'SCOPE_CHILD_CAP')
                    pid = event.si_pid
                    need(type(pid) is int and pid > 1, 'SCOPE_WAIT')
                    waited, _status = os.waitpid(pid, os.WNOHANG | WALL)
                    need(waited == pid, 'SCOPE_WAIT')
                    r['children_reaped'] += 1
                    r['exit_codes'].append(os.waitstatus_to_exitcode(_status))
                    if pid in handles: os.close(handles.pop(pid))
                    continue
                # Child-list emptiness is NOT proof: adoption can race a read.
                for pid in _children():
                    if pid in handles: continue
                    need(issued < CHILD_CAP, 'SCOPE_CHILD_CAP')
                    fd = os.pidfd_open(pid, 0)
                    handles[pid] = fd; issued += 1
                    # Confirm wait ownership on the stable handle before signal.
                    os.waitid(os.P_PIDFD, fd, WAIT)
                    r['unexpected_children'] = True
                    r['signal_attempts'] += 1
                    try: signal.pidfd_send_signal(fd, signal.SIGKILL, None, 0)
                    except ProcessLookupError: pass  # Still require a real wait.
                    except OSError:
                        r['cleanup_failed'] = True; fail('SCOPE_SIGNAL')
                time.sleep(min(0.005, max(0, deadline-time.monotonic_ns())/1e9))
            else:
                fail('SCOPE_SCAN_CAP')
        except BaseException as error:
            r['cleanup_failed'] = True
            code = error.args[0] if isinstance(error, Refused) and error.args else 'SCOPE_IO'
            fail(code if type(code) is str and code.startswith('SCOPE_') else 'SCOPE_IO')
        finally:
            for fd in handles.values():
                try: os.close(fd)
                except OSError: r['cleanup_failed'] = True; fail('SCOPE_CLOSE')
            if self.empty:
                try:
                    _policy()
                    need(_get(self.fn) == 1, 'SCOPE_POLICY_CHANGED')
                    try: _peek()
                    except ChildProcessError: pass
                    else: raise Refused('SCOPE_NOT_EMPTY')
                    _set(self.fn, 0); self.active = False; r['policy_restored'] = True
                except BaseException:
                    self.empty = False; r['descendant_domain_empty'] = False
                    r['cleanup_failed'] = True; fail('SCOPE_RESTORE')
            r['elapsed_ns'] = time.monotonic_ns() - start
        if r['unexpected_children']: fail('SCOPE_ORPHANS')
        if not r['descendant_domain_empty']: fail('SCOPE_NOT_EMPTY')
        if r['reason'] is None: r['status'] = 'descendant_scope_complete_unreviewed'
        self.result = dict(r, exit_codes=list(r['exit_codes']))
        return r

    def assert_empty(self):
        """Recheck before/after artifact reads; not a transferable boolean receipt."""
        need(self.finished and self.empty and not self.active and os.getpid() == self.pid and
             self.result['status'] == 'descendant_scope_complete_unreviewed', 'SCOPE_NOT_CLEAN')
        _policy(); need(_get(self.fn) == 0, 'SCOPE_POLICY_CHANGED')
        try: _peek()
        except ChildProcessError: return
        raise Refused('SCOPE_NOT_EMPTY')
