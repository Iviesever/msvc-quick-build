"""Read-only old-environment compatibility gate; never executes a tool.

MATCH means only these two files match the old pins, not that checkpoint 017 is
admitted. A fresh manifest, dependencies and independent review are still needed.
"""
from __future__ import annotations

import hashlib
import os
import stat
from typing import Any, Callable

OLD_PINS = {
    "python": {"path": "/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12",
               "bytes": 30894944,
               "sha256": "fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7"},
    "decoder": {"path": "/usr/bin/x86_64-linux-gnu-objdump", "bytes": 390848,
                "sha256": "7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5"},
}
FILE_CAP = 64 * 1024 * 1024
CHUNK = 65536


def probe_file(path: str) -> dict[str, Any]:
    """Finite regular-file read, no symlink leaf or special-file blocking open.

Parent directories belong to the trusted host. This is not an ancestry-pinning
filesystem sandbox or a guarantee about future runtime loading.
"""
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    try:
        fd = os.open(path, flags)
    except FileNotFoundError:
        return {"status": "MISSING"}
    except OSError as exc:
        return {"status": "OPEN_REFUSED", "errno": exc.errno}
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or not 0 <= before.st_size <= FILE_CAP:
            return {"status": "KIND_OR_SIZE_REFUSED"}
        digest, count = hashlib.sha256(), 0
        while count < before.st_size:
            data = os.read(fd, min(CHUNK, before.st_size - count))
            if not data:
                return {"status": "SHORT_READ"}
            digest.update(data)
            count += len(data)
        overflow = os.read(fd, 1)
        after = os.fstat(fd)
        state = lambda s: (s.st_dev, s.st_ino, s.st_mode, s.st_nlink, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        if overflow or state(before) != state(after):
            return {"status": "UNSTABLE_FILE"}
        return {"status": "OBSERVED", "bytes": count, "sha256": digest.hexdigest()}
    except OSError as exc:
        return {"status": "READ_REFUSED", "errno": exc.errno}
    finally:
        os.close(fd)


def check_environment(probe: Callable[[str], dict[str, Any]] = probe_file) -> dict[str, Any]:
    files = []
    for role, expected in OLD_PINS.items():
        observed = probe(expected["path"])
        match = (observed.get("status") == "OBSERVED"
                 and type(observed.get("bytes")) is int
                 and observed["bytes"] == expected["bytes"]
                 and observed.get("sha256") == expected["sha256"])
        files.append({"role": role, "expected": dict(expected), "observed": observed, "matches_old_pin": match})
    return {"schema": "bcrypt-environment-preflight-017-v1",
            "status": "OLD_FILES_MATCH_ONLY" if all(f["matches_old_pin"] for f in files) else "BLOCKED_ENVIRONMENT_CHANGED",
            "files": files, "execution_admitted": False,
            "tool_dependencies_requalified": False, "image_content_reads": 0,
            "decoder_launches": 0, "native_execution_admitted": False,
            "next_gate": "Pin and review the current interpreter, decoder, dependencies and new 017 resource/launch manifest; do not rewrite the 016 manifest."}
