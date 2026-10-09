"""Closed fixed-byte producers for synthetic transport contracts only."""
import json
import os
import signal
import sys
import time


MODES = ("normal", "empty_stderr", "dual", "nonzero", "timeout",
         "ignore_term", "closed_pipes", "busy")


def fixture_spec(mode):
    """Return fresh expected identities and fixed bytes, without running a child."""
    if type(mode) is not str or mode not in MODES:
        raise ValueError("unknown fixed-substitute mode")
    identities = [{"kind": "fixed-substitute", "mode": mode, "record": index}
                  for index in range(4 if mode == "dual" else 1)]
    lines = []
    for identity in identities:
        value = "x" * (48 * 1024) if mode == "dual" else "固定字节：雪"
        line = json.dumps({**identity, "value": value}, ensure_ascii=False,
                          sort_keys=True, separators=(",", ":")).encode("utf-8")
        lines.append(line + (b"\r\n" if mode == "normal" else b"\n"))
    stderr = (mode + " fixed diagnostic").encode("ascii") + b"\x00\n"
    if mode == "empty_stderr":
        stderr = b""
    elif mode == "dual":
        stderr += b"D" * (192 * 1024 - len(stderr))
    return {"stdout": b"".join(lines), "stderr": stderr, "records": identities}


def _write_all(descriptor, raw):
    offset = 0
    while offset < len(raw):
        count = os.write(descriptor, raw[offset:])
        if count <= 0:
            raise OSError("fixed substitute short write")
        offset += count


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in MODES:
        return 2
    mode = sys.argv[1]
    signal.signal(signal.SIGTERM, signal.SIG_IGN if mode == "ignore_term"
                  else signal.SIG_DFL)
    spec = fixture_spec(mode)
    # The large second pipe must drain before this producer can write stdout.
    if mode == "dual":
        _write_all(2, spec["stderr"])
        _write_all(1, spec["stdout"])
    else:
        _write_all(1, spec["stdout"])
        _write_all(2, spec["stderr"])
    if mode == "nonzero":
        return 7
    if mode == "closed_pipes":
        os.close(1)
        os.close(2)
    if mode in ("timeout", "ignore_term", "closed_pipes", "busy"):
        while True:
            if mode == "busy":
                _write_all(1, spec["stdout"])
                time.sleep(0.001)
            else:
                time.sleep(1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
