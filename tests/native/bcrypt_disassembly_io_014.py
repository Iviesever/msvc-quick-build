"""Bounded identity reads and exclusive retained files for observation 014.

The five owned file-content bounds are not a host filesystem sandbox.
Only a reaped child makes raw capture stable; the owner enforces that gate.
"""

import hashlib
import os
from pathlib import Path
import resource
import signal
import stat

from bcrypt_disassembly_model_014 import Refusal, require, LIMITS


def _path(value):
    require(type(value) is str and 0 < len(value) <= LIMITS["path_chars"]
            and all(32 <= ord(c) <= 126 for c in value), "PATH")
    path = Path(value)
    require(path.is_absolute() and str(path) == value, "PATH")
    return path


def _canonical(value):
    path = _path(value)
    try:
        require(str(path.resolve(strict=True)) == value, "PATH_CANONICAL")
    except (OSError, RuntimeError):
        raise Refusal("PATH_CANONICAL") from None
    return path


def _identity(st):
    return st.st_dev, st.st_ino, st.st_mode, st.st_nlink


def _snapshot(st):
    return _identity(st) + (st.st_size, st.st_mtime_ns, st.st_ctime_ns)


def verify_identity(record, cap, keep=False):
    """Hash a fixed regular file with one chunk, retaining only small inputs."""
    require(type(cap) is int and 0 < cap <= LIMITS["identity_bytes"]
            and type(keep) is bool, "IDENTITY_CAP")
    require(not keep or cap <= LIMITS["index_bytes"], "IDENTITY_KEEP_CAP")
    require(type(record) is dict and set(record) == {"path", "bytes", "sha256"},
            "IDENTITY_FIELDS")
    require(type(record["bytes"]) is int and 0 < record["bytes"] <= cap,
            "IDENTITY_SIZE")
    digest = record["sha256"]
    require(type(digest) is str and len(digest) == 64
            and all(c in "0123456789abcdef" for c in digest), "IDENTITY_HASH")
    path = _canonical(record["path"])
    fd = None
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode), "IDENTITY_REGULAR")
        require(before.st_size == record["bytes"], "IDENTITY_SIZE")
        require(_snapshot(os.stat(path, follow_symlinks=False)) == _snapshot(before),
                "IDENTITY_CHANGED")
        remaining, hashed, chunks = before.st_size, hashlib.sha256(), []
        while remaining:
            raw = os.read(fd, min(remaining, LIMITS["identity_chunk_bytes"]))
            require(bool(raw), "IDENTITY_SHORT_READ")
            hashed.update(raw)
            if keep:
                chunks.append(raw)
            remaining -= len(raw)
        require(not os.read(fd, 1), "IDENTITY_SIZE")
        after = os.fstat(fd)
        require(_snapshot(before) == _snapshot(after), "IDENTITY_CHANGED")
        _canonical(record["path"])
        require(_snapshot(os.stat(path, follow_symlinks=False)) == _snapshot(after),
                "IDENTITY_CHANGED")
        require(hashed.hexdigest() == digest, "IDENTITY_HASH")
        return b"".join(chunks) if keep else None
    except OSError:
        raise Refusal("IDENTITY_IO") from None
    finally:
        if fd is not None:
            os.close(fd)


def verify_environment(manifest):
    """Check fixed path aliases and absence, including dangling symlinks."""
    try:
        aliases, absent = manifest["aliases"], manifest["absent_paths"]
        require(type(aliases) is list and len(aliases) <= LIMITS["alias_count"]
                and type(absent) is list and len(absent) <= LIMITS["absent_path_count"],
                "ENVIRONMENT_SHAPE")
        pinned = {item["path"] for group in ("identities", "sources")
                  for item in manifest[group].values()}
        seen = set()
        for item in aliases:
            require(type(item) is dict and set(item) == {"path", "target"}, "ALIAS_FIELDS")
            alias, target = _path(item["path"]), _canonical(item["target"])
            require(item["path"] not in seen and str(target) in pinned, "ALIAS_TARGET")
            seen.add(item["path"])
            require(alias.resolve(strict=True) == target, "ALIAS_TARGET")
        require(len(set(absent)) == len(absent), "ABSENT_PATHS")
        for value in absent:
            path = _path(value)
            try:
                path.lstat()
            except FileNotFoundError:
                continue
            raise Refusal("ABSENT_PATH_EXISTS")
    except (OSError, RuntimeError, KeyError, TypeError):
        raise Refusal("ENVIRONMENT_IO") from None


def child_limits():
    """Only a reviewed single-threaded Popen preexec callback may call this."""
    pairs = ((resource.RLIMIT_FSIZE, "child_file_bytes"),
             (resource.RLIMIT_AS, "child_address_space_bytes"),
             (resource.RLIMIT_CPU, "child_cpu_seconds"),
             (resource.RLIMIT_CORE, "child_core_bytes"),
             (resource.RLIMIT_NOFILE, "child_open_files"))
    for kind, key in pairs:
        bound = LIMITS[key]
        resource.setrlimit(kind, (bound, bound))
        require(resource.getrlimit(kind) == (bound, bound), "CHILD_LIMIT")
    for name in ("SIGPIPE", "SIGXFZ", "SIGXFSZ"):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), signal.SIG_DFL)


class OutputDirectory:
    """Own one exclusive directory and at most five regular descriptors."""

    def __init__(self, path):
        self.path, self.dirfd, self._files = _path(path), None, {}
        self._closed = False
        require(self.path.name not in ("", ".", ".."), "OUTPUT_PATH")
        parent = _canonical(str(self.path.parent))
        parent_fd = None
        try:
            parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            os.mkdir(self.path.name, mode=0o700, dir_fd=parent_fd)
            self.dirfd = os.open(self.path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                 dir_fd=parent_fd)
            self._directory_identity = _identity(os.fstat(self.dirfd))
            os.fsync(parent_fd)
            self._directory()
            closing, parent_fd = parent_fd, None
            os.close(closing)
        except (OSError, Refusal):
            self.close()
            raise Refusal("OUTPUT_CREATE") from None
        finally:
            if parent_fd is not None:
                os.close(parent_fd)

    @staticmethod
    def _caps():
        return {"consumed.json": LIMITS["marker_bytes"], "target.bin": LIMITS["target_bytes"],
                "tool-stdout.bin": LIMITS["child_file_bytes"],
                "tool-stderr.bin": LIMITS["child_file_bytes"],
                "report.json": LIMITS["report_bytes"]}

    def _directory(self):
        require(not self._closed and self.dirfd is not None, "OUTPUT_CLOSED")
        _canonical(str(self.path))
        require(_identity(os.fstat(self.dirfd)) == self._directory_identity
                and _identity(self.path.lstat()) == self._directory_identity,
                "OUTPUT_DIRECTORY_CHANGED")

    def _name(self, name, cap):
        require(type(name) is str and name in self._caps(), "OUTPUT_NAME")
        require(type(cap) is int and 0 < cap <= self._caps()[name], "OUTPUT_CAP")

    def _new(self, name, mode):
        self._directory()
        require(name not in self._files, "OUTPUT_EXISTS")
        fd = os.open(name, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     mode, dir_fd=self.dirfd)
        self._files[name] = (fd, None)  # Own immediately, including first-fstat failure.
        st = os.fstat(fd)
        self._files[name] = (fd, _identity(st))
        require(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and st.st_size == 0,
                "OUTPUT_REGULAR")
        self._file(name)
        return fd

    def _file(self, name):
        self._directory()
        require(name in self._files, "OUTPUT_NOT_OWNED")
        fd, expected = self._files[name]
        st = os.fstat(fd)
        on_path = os.stat(name, dir_fd=self.dirfd, follow_symlinks=False)
        require(stat.S_ISREG(st.st_mode) and st.st_nlink == 1
                and _identity(st) == expected and _identity(on_path) == expected,
                "OUTPUT_FILE_CHANGED")
        return fd, st

    def create(self, name, data, cap, mode=0o600):
        self._name(name, cap)
        require(type(data) is bytes and len(data) <= cap, "OUTPUT_SIZE")
        require(type(mode) is int and mode in (0o400, 0o600), "OUTPUT_MODE")
        try:
            fd = self._new(name, mode)
            require(os.write(fd, data) == len(data), "OUTPUT_SHORT_WRITE")
            os.fsync(fd)
            os.fsync(self.dirfd)
            require(self.read(name, cap) == data, "OUTPUT_READBACK")
        except OSError:
            raise Refusal("OUTPUT_WRITE") from None

    def open_capture(self, name):
        require(name in ("tool-stdout.bin", "tool-stderr.bin"), "CAPTURE_NAME")
        try:
            return self._new(name, 0o600)
        except OSError:
            raise Refusal("CAPTURE_CREATE") from None

    def read(self, name, cap):
        self._name(name, cap)
        try:
            fd, before = self._file(name)
            require(0 <= before.st_size <= cap, "OUTPUT_SIZE")
            chunks, offset = [], 0
            while offset < before.st_size:
                raw = os.pread(fd, min(LIMITS["identity_chunk_bytes"], before.st_size - offset), offset)
                require(bool(raw), "OUTPUT_SHORT_READ")
                chunks.append(raw)
                offset += len(raw)
            require(not os.pread(fd, 1, offset), "OUTPUT_SIZE")
            _, after = self._file(name)
            require(_snapshot(before) == _snapshot(after), "OUTPUT_CHANGED_DURING_READ")
            return b"".join(chunks)
        except OSError:
            raise Refusal("OUTPUT_READ") from None

    def durable_readback(self, expected_bytes_by_name):
        require(type(expected_bytes_by_name) is dict
                and set(expected_bytes_by_name) == set(self._files), "OUTPUT_READBACK_SET")
        try:
            self._directory()
            for name, raw in expected_bytes_by_name.items():
                self._name(name, self._caps()[name])
                require(type(raw) is bytes and len(raw) <= self._caps()[name], "OUTPUT_SIZE")
                fd, _ = self._file(name)
                os.fsync(fd)
            os.fsync(self.dirfd)
            for name, raw in expected_bytes_by_name.items():
                require(self.read(name, self._caps()[name]) == raw, "OUTPUT_READBACK")
            self._directory()
        except OSError:
            raise Refusal("OUTPUT_DURABILITY") from None

    def close(self):
        if self._closed:
            return
        fds = [entry[0] for entry in self._files.values()]
        if self.dirfd is not None:
            fds.append(self.dirfd)
        self._closed, self._files, self.dirfd = True, {}, None
        failed = False
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                failed = True
        require(not failed, "OUTPUT_CLOSE")
