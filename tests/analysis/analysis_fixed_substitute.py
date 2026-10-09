"""Closed fixed-byte producers for synthetic transport contracts only."""
import json
import os
import signal
import sys
import time


MODES = ("normal", "empty_stderr", "dual", "nonzero", "timeout",
         "ignore_term", "closed_pipes", "busy", "cost_time", "cost_allocation",
         "cost_bad_tail_time", "cost_bad_tail_allocation", "cost_invalid_time",
         "cost_invalid_allocation")


def _cost_spec(mode):
    # All observation values are synthetic literals. No preparation, codec API,
    # allocator observer or timing operation is performed by these producers.
    instrumented = mode.endswith("allocation")
    bad = mode.startswith("cost_bad_tail_")
    case, operation = ("bad_items_tail", "decode") if bad else ("exe", "live")
    prepared = {"kind": "prepared", "case": case, "records": 1,
                "wire_bytes": 363374 if bad else 3405, "public_preparation_calls": 5,
                "model_equal": True, "checks": 118 if instrumented else 115}
    consumed = prepared["wire_bytes"] - 1 if bad else 2
    rows = [dict(prepared)]
    identities = [{"kind": "prepared", "case": case}]
    for trial in range(5):
        identity = {"kind": "observation", "case": case, "operation": operation,
                    "trial": trial, "instrumented": instrumented}
        row = {**identity, "ns": None if instrumented else (0, 101, 102, 103, 104)[trial],
               "new_calls": 4 if instrumented else None,
               "requested_bytes": 128 if instrumented else None,
               "requested_peak_live": 96 if instrumented else None,
               "requested_live_at_return": (0 if bad else 64) if instrumented else None,
               "largest_request": 64 if instrumented else None,
               "key_callbacks": (0 if bad else 3) if instrumented else None,
               "process_hwm_before_kib": 4096 + trial,
               "process_hwm_after_kib": 4096 + trial, "consumed": consumed}
        rows.append(row)
        identities.append(identity)
    if mode == "cost_invalid_time":
        rows[1]["ns"] = False  # Correct fields/identities and bytes, wrong value type.
    elif mode == "cost_invalid_allocation":
        rows[1]["requested_live_at_return"] = 97  # Exceeds the declared peak of 96.
    stdout = b"".join(json.dumps(row, sort_keys=True, separators=(",", ":")).encode("ascii")
                       + b"\n" for row in rows)
    return {"stdout": stdout, "stderr": b"", "records": identities,
            "cost": {"case": case, "operation": operation, "instrumented": instrumented,
                     "prepared": prepared, "expected_consumed": consumed}}


def fixture_spec(mode):
    """Return fresh expected identities and fixed bytes, without running a child."""
    if type(mode) is not str or mode not in MODES:
        raise ValueError("unknown fixed-substitute mode")
    if mode.startswith("cost_"):
        return _cost_spec(mode)
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
