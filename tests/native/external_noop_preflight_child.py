"""Deterministic non-MQB child. No subprocesses, benchmarks, or cleanup."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import sys
import threading
import time

STREAM_LINES = 256
LINE_BYTES = 1024


def stream_bytes(letter: bytes) -> bytes:
    return (letter * (LINE_BYTES - 1) + b'\n') * STREAM_LINES


def main() -> int:
    mode, marker, *payload = sys.argv[1:]
    if mode not in ('echo', 'streams', 'nonzero', 'timeout'):
        raise ValueError('unknown helper mode')
    identity = dict(schema=1, purpose='non_mqb_helper', pid=os.getpid(),
                    cwd=os.getcwd(), executable=sys.executable, mode=mode,
                    payload=payload, source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    # Written before output/sleep; no replacement or removal of evidence.
    with Path(marker).open('x', encoding='utf-8') as f:
        json.dump(identity, f, ensure_ascii=True, allow_nan=False)
        f.write('\n')
    if mode == 'streams':
        def emit(stream, letter):
            stream.write(stream_bytes(letter)); stream.flush()
        a = threading.Thread(target=emit, args=(sys.stdout.buffer, b'O'))
        b = threading.Thread(target=emit, args=(sys.stderr.buffer, b'E'))
        a.start(); b.start(); a.join(); b.join()
    elif mode == 'timeout':
        sys.stdout.buffer.write(b'PREFLIGHT-READY\n'); sys.stdout.buffer.flush()
        sys.stderr.buffer.write(b'PREFLIGHT-WAITING\n'); sys.stderr.buffer.flush()
        time.sleep(240)  # The unmodified collector must hit its real 180s wait.
        return 98       # Reaching natural exit must FAIL the timeout test.
    else:
        sys.stdout.buffer.write((json.dumps(identity, ensure_ascii=True) + '\n').encode('ascii'))
        sys.stdout.buffer.flush()
        sys.stderr.buffer.write(b'PREFLIGHT-STDERR\n'); sys.stderr.buffer.flush()
    return 37 if mode == 'nonzero' else 0


if __name__ == '__main__':
    sys.exit(main())
