# Post-prime causal trace windows / 初次构建后的分段跟踪

See [the original causal trace contract](NOOP_CAUSAL_TRACE.md) for original inputs and event-review semantics.

The original `701-causal-001` is **consumed/stopped**, not pending execution:
[retained run acceptance](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-5793053800).
It recorded 30/32 calls before the kernel-file-limit refusal; its ETL has lost
buffers and missing tail events. Never rerun that workflow or fill its two unused
slots. Its original source, profile, journal checks and dispatch file remain unchanged.

`trace_noop_causal_windows.ps1` is a separate, explicit entry under
[implementation-only registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-5793465929).
It imports the **exact original** four legacy helpers and owned WPR function from
verified source snapshots; it never dot-sources either old entry. The new
`noop_causal_windows.py` uses the original archive verifier, cell plan and call
checks. That collector implementation included no product or benchmark rule
changes, automatic capture, dispatch workflow or execution allocation. The
separate manual wrapper below does not reset any consumed opportunity.

For each of the same 12 cells, the order is now:

1. With no owned WPR session, run the one planned prime. Check the result, record
   the post-prime file manifest and save an independent prime checkpoint.
2. Start a unique named WPR instance in that cell's new directory and emit its
   begin marker. Capture `middle -> final`, or just `final` for O. No WPR command,
   Python process, file hash/manifest or disk-journal write enters the middle/final
   gap; original in-memory result checks and output capture still do.
3. Emit end marker, save collector status, check temporary-file sizes, and attempt
   exactly one owned stop. Save that segment's ETL without deleting earlier traces.
   Only after stopping, check final fixture continuity, capacity and save the cell.

A start-result-journal failure still permits the original owned cleanup. A failed
or uncertain stop blocks the next prime and is never retried by an outer finally.
The first call/control/log/space failure stops the remaining plan and retains its
prefix; no rollback, refill or resume. Hard kills, synchronous native hangs, disk
failure and runner loss still defeat guaranteed cleanup or evidence preservation.

The original profile stays **256 MiB Sequential per kernel file**. Each merged
segment must be nonempty and at most 256 MiB; all retained files in `traces/`
(including temporary files) must be at most **512 MiB** at checked boundaries.
At least **4 GiB free** is checked before each prime/start and after each stop.
These are admission/acceptance checkpoints, **not a hard filesystem quota**:
WPR merging and other processes may consume space between checks. Oversized or
failed traces are retained, not truncated/deleted to satisfy the limit. The final
manual execution wrapper retains external **15-minute step / 20-minute job** on
a fresh, dedicated hosted Windows x64 / PowerShell 7 machine, never a user desktop.

The proposed call plan remains **32 = 12 primes + 8 middles + 12 finals** with four
timed middles. History is now **72 recorded**, proposed cumulative ceiling **104**,
not a new allocation or completed total. At most **60 WPR controls** are planned:
12 starts, 24 markers, 12 status queries, 12 stops. Primes are deliberately
**untraced**; event review should correlate **20 measured process instances**,
not falsely require/claim 32 complete process lifetimes in these segments.

Starting/stopping between cells changes host state and delay after priming. The
old trace's event-byte distribution motivated this smaller window, but does not
predict lossless capture or prove a performance cause. Do not pool this design
with #701, boundary001/002 or causal001, subtract tracing overhead, or label it an
unperturbed historical replay. Retain O/U/T and both directions, including adverse
pairs. Each segment independently needs native loss/schema/timebase, two-marker,
process/thread/stack/I-O and privacy review; a good segment cannot replace a bad one.

Automatic contract CI exercises the actual extracted cell and owned-control
functions using in-process fake MQB/WPR calls, plus synthetic byte-budget metadata,
and parses the entry without running it. Python checks complete and corrupted
**synthetic** journals; a fake ETL may only produce
`window_journal_complete_trace_unreviewed`, `trace_health_verified=false`,
`cause=null`, `clears_hold=false`. This is not live ETW validation. Existing tests
and the consumed manual workflow stay intact. Native regression and a separate
exact-main execution review/allocation must precede any new capture; #207 HOLD
and the original adverse evidence remain. No release or clean/prune authority.

## Isolated manual wrapper / 隔离手动入口

After main #825 acceptance, `.github/workflows/noop-causal-windows-study.yml`
adds only the missing outer host/request/download/time-limit boundary. It invokes
this unchanged window entry exactly once. It does **not** rename or modify the
consumed `noop-causal-study.yml` or allocate another experiment. The distinct
protocol label `701-causal-windows-001` is a reserved admission value, not evidence
of registration, dispatch or execution. Do not run it until this wrapper's exact
PR and resulting main have been accepted and #198 separately records the final
main SHA, profile, input, budget and one execution opportunity.

The guard requires this repository, main, manual event, matching reviewed commit /
workflow SHA and workflow path, known profile digest, GitHub-hosted Windows x64,
and this workflow's run number 1 / attempt 1. It saves allowlisted request fields
before checkout/download; no token or environment dump. Inputs enter scripts only
through environment variables. A consumed/uncertain first request must not be
retried, renamed or replaced by another run. A changed main needs review, not a
casual edit to the commit input. Registration remains a maintainer review boundary;
the workflow does not automatically verify the text of an issue comment.

The sole download is original #701 artifact 10675079360 / run 35681224762 as a raw
ZIP; its 4,880,704 bytes and SHA256 are checked before the collector verifies A/B.
No replacement build is made. Checkout credentials are not persisted. Only the
download action receives the token. The 20-minute job / 15-minute entry step and
4 GiB free check bound admission, not every native operation. Per-cell 256 MiB /
aggregate 512 MiB checks and exact owned stop remain solely in the existing entry.
There is no wrapper-level WPR cancel, stop, retry, continuation or cleanup delete.

An `always()` upload retains only `causal-windows-execution-out/`: allowlisted
request, exact workflow copy, original ZIP, entry/error logs and the recorder's
own evidence. Runner loss, hard timeout or disk failure can still defeat upload
or cleanup. System-wide ETW metadata on this disposable VM requires privacy review
before redistribution; neither the checkout tree nor the full workspace is dumped.
The token is used before trace startup, but that alone is not a secrecy certificate.

Successful control flow requires the existing full journal audit plus exact
32 calls / 12 windows / 20 traced calls, 12 listed trace files and the strict
`window_journal_complete_trace_unreviewed` verdict. Health remains false, cause
null, and HOLD clearance false. No green workflow or synthetic test substitutes
for native review of loss, markers, timebases, process/thread/I-O events and privacy
in every segment. History stays 72; proposed 32 / cumulative 104 is not allocation.

Automatic CI executes the exact inline Python admission with synthetic contexts,
checks wrapper structure and uses PowerShell only to parse the wrapper or exercise
its extracted verdict function against synthetic journals. It never invokes the
manual workflow or either real trace entry. Local environments without PowerShell
explicitly skip that verdict-function test; both hosted platforms must execute it.
