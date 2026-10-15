"""Existing direct transport plus real Linux descendant drain and bounded readback.

Not the final authorized bootstrap: no arbitrary qualification booleans turn
this into bcrypt-outer-completion-017-v1. The remaining bridge must bind source,
module, input and external-writer policy before issuing that final receipt.
"""
from __future__ import annotations
import os
import stat
import time

from bcrypt_admission_contract_017 import fingerprint
from bcrypt_completion_receipt_017 import ARTIFACT_CAPS
from bcrypt_guarded_owner_017 import need
from bcrypt_outer_transport_017 import OwnerTransport, _stamp
from bcrypt_owner_backend_017 import absolute
from bcrypt_writer_scope_017 import DescendantScope


def _read_directory(path, names, caps, exact):
    """No symlinks, hardlinks, special files, extras or path/descriptor drift."""
    path = absolute(path)
    need(os.path.realpath(path) == path, 'SCOPE_READ_PATH')
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    result = {}; snapshots = {}
    try:
        start = _stamp(os.fstat(fd))
        need(start[:4] == _stamp(os.stat(path, follow_symlinks=False))[:4], 'SCOPE_READ_PATH')
        if exact:
            with os.scandir(fd) as entries:
                found = set()
                for entry in entries:
                    need(entry.name in names and entry.name not in found, 'SCOPE_READ_MEMBERS')
                    found.add(entry.name)
            need(found == set(names), 'SCOPE_READ_MEMBERS')
        for name in names:
            cap = caps[name]
            child = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
            try:
                before = os.fstat(child)
                need(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and
                     0 <= before.st_size <= cap, 'SCOPE_READ_SIZE')
                need(_stamp(before) == _stamp(os.stat(name,dir_fd=fd,follow_symlinks=False)), 'SCOPE_READ_CHANGED')
                data = bytearray()
                while len(data) < before.st_size:
                    chunk = os.read(child, min(4096, before.st_size-len(data)))
                    need(bool(chunk), 'SCOPE_READ_SHORT'); data.extend(chunk)
                need(not os.read(child, 1), 'SCOPE_READ_SIZE')
                snapshots[name] = _stamp(os.fstat(child))
                need(_stamp(before) == snapshots[name], 'SCOPE_READ_CHANGED')
                result[name] = bytes(data)
            finally: os.close(child)
        for name, stamp in snapshots.items():
            need(stamp == _stamp(os.stat(name,dir_fd=fd,follow_symlinks=False)), 'SCOPE_READ_CHANGED')
        need(start == _stamp(os.fstat(fd)) == _stamp(os.stat(path,follow_symlinks=False)) and
             os.path.realpath(path) == path, 'SCOPE_READ_PATH')
        return result
    finally: os.close(fd)


class ScopedOwnerTransport:
    """Dedicated supervisor only; no adoption in a shared Python interpreter.

    Start subreaper before the existing Popen. After its direct-root handling,
    drain adopted descendants even if they created another session. A leftover
    child always refuses the run, even when the cleanup subsequently succeeds.
    The old direct receipt is immutable and still does not assert quiescence.
    """
    def __init__(self, raw, expected_sha):
        self.transport = OwnerTransport(raw, expected_sha)
        need(self.transport.spec['wall_ms'] <= 14000, 'SCOPE_WALL_BUDGET')
        self.scope = DescendantScope()
        self.used = False; self.result = None; self.start = None

    def run(self):
        need(not self.used, 'REUSED'); self.used = True
        self.start = time.monotonic_ns()
        self.scope.activate()
        transport = None; body_error = None
        try:
            transport = self.transport.run()
        except BaseException:
            body_error = 'SCOPE_TRANSPORT_EXCEPTION'
        finally:
            scope = self.scope.drain()
        elapsed = time.monotonic_ns()-self.start
        ok = (body_error is None and transport['status'] == 'owner_transport_complete_unreviewed'
              and scope['status'] == 'descendant_scope_complete_unreviewed' and elapsed < 16_000_000_000)
        self.result = dict(schema='bcrypt-scoped-transport-017-v1',
            status='scoped_transport_complete_unreviewed' if ok else 'refused',
            reason=body_error or (transport or {}).get('reason') or scope['reason'] or
                   (None if ok else 'SCOPE_DEADLINE'), transport=transport, scope=scope,
            elapsed_ns=elapsed, writers_quiescent=False, observation_accepted=False,clears_hold=False)
        return self.result

    def read_artifacts(self, backend_dir):
        """Six fixed byte streams, only after an observed clean descendant drain.

        Bounded path/file checks are not atomic against an unrelated writer.
        The final controller must independently qualify exclusive file ownership;
        this method intentionally produces no global writers_quiescent=true.
        """
        need(self.result is not None and self.result['status']=='scoped_transport_complete_unreviewed', 'SCOPE_NOT_CLEAN')
        self.scope.assert_empty()
        need(time.monotonic_ns()-self.start < 16_000_000_000, 'SCOPE_DEADLINE')
        direct = self.transport.spec['output_dir']
        backend_dir = absolute(backend_dir)
        need(direct != backend_dir and not direct.startswith(backend_dir+'/') and
             not backend_dir.startswith(direct+'/'), 'SCOPE_READ_OVERLAP')
        names = [n for n in ARTIFACT_CAPS if n != 'owner-final.json']
        files = _read_directory(backend_dir,names,ARTIFACT_CAPS,True)
        files.update(_read_directory(direct,['owner-final.json'],ARTIFACT_CAPS,False))
        self.scope.assert_empty()
        need(time.monotonic_ns()-self.start < 16_000_000_000, 'SCOPE_DEADLINE')
        return files, {name:fingerprint(raw) for name,raw in files.items()}
