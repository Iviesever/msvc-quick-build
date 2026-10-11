# Next handoff 011: exact loaded bcrypt bytes and bounded offline analysis

Status: `PROPOSED_NOT_IMPLEMENTED_NOT_REGISTERED_NOT_EXECUTED_BUDGET_GATE_OPEN`.

This is the next design handoff after [closed diagnostic 010](native_startup_result_010.md).
010's actual C loader explicitly reports a bcrypt init routine failure during
DLL_PROCESS_ATTACH, followed by exit 0xC0000142. Its internal failed operation
remains unknown. No 011 source, compilation, registration or activation exists.
The first concrete task is the unresolved combined collector budget below.
These are implementation/review decisions under continued work, not a request
for another generic permission or a reason to repeat 010.

## Evidence gap to close

010 observed a bcrypt LOAD_DLL event at base `0x7ffa4a8f0000`, and a later
explicit failed-init address `0x7ffa4a8fa590`. Their difference `0xA590` is
arithmetic only. The event path and base do not provide that DLL's bytes,
FileIdInfo, hash, PE entrypoint, imports or function boundaries. A later file
with the same System32 pathname, runner label or version is not the original
010 loaded object. This handoff must never make that retrospective claim.

The proposed next evidence is an exact bounded byte snapshot tied to a future
actual C's bcrypt debug-event hFile, then offline analysis of those bytes.
This could locate a bounded static initialization path and its candidate API
calls/return tests. A static path is not evidence that its branch executed,
and an import name or nearby API is not an observed failure cause.

## First gate: combined budgets are not yet proved

The independent design proposed at most 8 MiB of one DLL, emitted as hex chunks
through S's existing stdout. That is a candidate, not an admitted limit.
The existing 010 source proof reserves 26,496,000 output bytes and 16,271 rows
within the fixed 32 MiB / 16,384-row post-capture admission envelope.

| Simple combined accounting | Value |
| --- | ---: |
| Existing conservative bytes | 26,496,000 |
| Added hex payload for 8 MiB | 16,777,216 |
| Combined before collector metadata | 43,273,216 |
| Retained byte cap | 33,554,432 |
| Excess before collector metadata | 9,718,784 |
| Existing conservative rows / remaining rows | 16,271 / 113 |
| File-content rows alone at 64 KiB chunks | 128 |
| Combined before reads/metadata | 16,399, already 15 above the cap |

If the two bounded passes' maximum 258 data/EOF reads each emit one additional
row, simple addition reaches 16,657 rows, 273 above the cap before other
metadata. These calculations show the current conservative proof does not
admit that candidate; they do not assert every actual execution would overflow.
010's measured smaller output is not a substitute for a worst-case proof.

Before the collector passes its implementation/review gate, derive complete
combined payload, JSON escaping, sequence, operation, metadata, error and
cleanup accounting. Either prove a tighter correlated-path bound with exact
source justification or select a narrower transport/file limit that fits.
Count all read/EOF, identity, pin, comparison, release and failure records;
do not silently spend the same remaining rows twice. Preserve 32 MiB / 16,384
rows; do not simply widen the limits to accept 8 MiB. Any alternative transport
must state its additional S handles/files and be reviewed explicitly.

The candidate byte and chunk caps below remain provisional until that proof.
Main/cleanup/absolute-watchdog deadlines and the exact outer process gate must
also include all new operations before a concrete 011 plan is registered.

## Proposed acquisition boundary

1. Select one unique bcrypt LOAD_DLL event belonging to the already-bound actual
   C PID. Keep event sequence, TID, nonzero base, hFile value and bounded path.
   Path is a selector/description; the event hFile is the object evidence.
   Missing/ambiguous/non-disk handles or multiple candidate loads are incomplete,
   not a reason to open a pathname fallback or choose a different DLL.
2. Read checked FileIdInfo, FileStandardInfo, FileAttributeTagInfo, handle flags
   and file type through that hFile. Require a bounded positive regular-file
   size, no reparse or pending-delete/directory state, and consistent identity.
   Record a positive stable link count. Do not blindly copy the fresh-EXE
   link-count-one requirement onto a Windows component DLL, which may have
   multiple hard links.
3. Use documented ReOpenFile on that exact object for a non-inheritable S-only
   GENERIC_READ / FILE_SHARE_READ pin, with explicitly reviewed valid flags.
   Query and compare the pin's volume/FileIdInfo/size/properties. Keep it through
   bounded event draining and final readback. A refusal, query failure or
   incompatible sharing remains incomplete. No pathname retry, privilege
   adjustment, global settings or new child-inherited handle is proposed.
4. While servicing the event, perform one bounded complete read with explicit
   EOF detection and before/after identity checks. Record raw chunk offsets,
   exact requested/returned lengths and all failures. Close the event-owned
   hFile exactly once before its continuation; retaining S's separate pin must
   not leak or substitute for that close. Every event still gets one bounded
   continuation attempt under the reviewed cleanup model.
5. After draining, read the pinned bytes again, compare every chunk and length,
   verify EOF and final physical properties, and close the pin before the
   original absolute cleanup deadline. Do not classify missing, partial,
   changed, time-limited or budget-limited observations as complete.
6. The provisional stdout transport emits first-pass raw bytes as bounded hex
   records under the global sequence. Python reconstructs one exclusively
   created `bcrypt-image.bytes` artifact outside the zero-leaf C fixture,
   fully hashes and reads it back, and binds it to the actual-event metadata.
   Its precise file, output and memory caps await the combined-budget proof.
   Do not add a native CNG dependency to diagnose CNG initialization.

The Windows contracts are documented by
[LOAD_DLL_DEBUG_INFO](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-load_dll_debug_info),
[ReOpenFile](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile),
[GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex)
and [FILE_ID_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info).
The snapshot would verify observed identity/content and retention, not exclusive
producer provenance or an A2/A4 completion capability. A later same-path copy
does not replace a failed event-handle acquisition.

## Proposed offline analysis after a valid snapshot

Parse only the acquired file. Independently validate complete AMD64 PE32+
headers, directory and section mappings, image/file bounds, entrypoint,
imports, delay imports, and relevant exception/unwind data. Use the future
run's own loader init address and base to derive its RVA and validate a unique
file-backed mapping. Do not use 010's `0xA590` as a hardcoded expected entrypoint.

The design's initial static-analysis envelope is one DLL, at most 64 KiB of
reachable code, 256 basic blocks and 32 direct in-module callees. These caps
also require a concrete offline-tool plan, tests and accounting before use.
Preserve each decoded instruction's raw bytes, file offset and RVA; use PE
import/unwind evidence to support names and function ranges. Do not execute
the archived DLL, install symbols, fetch another build or follow cross-module
code as though it were in the snapshot.

Report candidate initialization calls, their return-value tests and any
bounded static branch to FALSE with exact bytes and references. Stop at an
unresolved indirect target, cross-module boundary, ambiguous parse or an
exhausted cap. Do not relabel reachability as actual runtime execution, a
negative return as an API failure without its contract, or a static path as
the root cause. The next internal runtime observation can only be designed
after the exact-byte analysis identifies a reviewable observation point.

[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain) describes
initialization failure semantics. It does not specify bcrypt's internal failed
operation in this run. Reading GetLastError or volatile return registers at a
later loader-string event is not sufficient to recover that earlier operation.
No software breakpoint patch, IAT hook, injection, process-memory write,
thread-context rewrite, ETW or ACL/privilege expansion is proposed.

## Concrete implementation and execution handoff

The next implementer should first read the 010 result, full raw streams and
the two independent runtime reviews, then:

1. Resolve and independently review the combined collector budget with explicit
   acceptance tests and failure/cleanup bounds; choose the smallest justified
   source slice and immutable new attempt paths.
2. Implement the actual-event object pin and byte collector in a new 011 slice,
   preserve the frozen 010 source, and add meaningful malformed/partial/
   identity/budget/refusal tests before considering activation. Do not alter
   original result.json to admit an outer-gate failure.
3. Write an exact 011 execution plan with source hashes, compiler/native/file/
   output/operation/deadline budgets and one-shot identity. Review included
   sources and all new handle lifetimes. Only then register the immutable
   commit and fully read back the registration before its activating update.
4. If the future snapshot is valid, independently review the raw collector
   chain and exact bytes, run the bounded offline analysis, and close that
   attempt with explicit observed facts and remaining causal gaps. Preserve
   INCONCLUSIVE if the outer count remains 3 against required 2.

No next-run allocation follows solely from this design file. There is no
retry/matrix, duplicate 010 run, same-name fallback or automatic bound growth.
Successful snapshotting or static analysis would still leave actual IPC
delegation/lifetime qualification and ordinary producer A1/A2/A4 pending.

## Reviewed proposal provenance and frozen scope

The delivered evidence package preserves both immutable design records:

- `next-diagnostic-proposal-011.json`: 27,938 B, SHA-256
  `dfda26db6a23e44e53d281bc5561468a13e5e4f0570716e6c9672777de3fc979`.
- `next-diagnostic-proposal-011-budget-addendum.json`: 4,201 B, SHA-256
  `d861949e23f48ffc7d63d23b69a06d8c4e89725296522f77ca737dc70d77eb0c`.

The addendum makes the candidate's unresolved admission gap explicit without
rewriting the original proposal. Both were exclusively created, file/directory
fsynced and fully read back in separate processes. No native action was taken
for either design record.

Keep main `87f86770cfc7e1941603074ace914e0c7faf10d3`, PR250 HOLD, PR248 draft,
consumer/cost DEFER, disabled M1b/service jobs, product cpp/VERSION and all
001-010 source/evidence intact. Retain the byte-identical 21,000-byte cost
preparation (SHA-256 `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`).
All A1-A5/deletion remain false. The disproved 005 metadata-writer dependency
is not reopened, and zero IPC observations remain zero.
