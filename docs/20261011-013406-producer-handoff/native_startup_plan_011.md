# One-shot actual-event bcrypt snapshot diagnostic 011

Status: `IMPLEMENTATION_AND_REVIEW_IN_PROGRESS_NOT_REGISTERED_NOT_EXECUTED`.

This is the concrete continuation of [handoff 011](native_startup_handoff_011.md)
and [closed 010](native_startup_result_010.md), under the user's instruction to
check the latest state and complete its next handoff. The existing isolated
branch starts at `0a7509e48dbd829bae3795a7bcca0ffcf387a83d`. The historical
handoff, 001-010 code and evidence remain unchanged. Source review and a fully
read-back exact-commit registration precede the only activating update.

## Question and selected acquisition

010 identifies bcrypt DLL_PROCESS_ATTACH failure in its actual child, but has
no held bcrypt identity or bytes. 011 acquires the exact file bytes belonging
to its own future actual C's unique bcrypt LOAD_DLL event. It does not repair
startup, qualify IPC, identify an API failure merely from a static import, or
retroactively supplement the original 010 object.

Select the existing stdout transport with a **1 MiB whole-file limit** and
64 KiB chunks. The earlier 8 MiB hex candidate does not fit the preserved
conservative envelope. A new native binary side file would introduce another
writer, name and handle lifetime, so it is not selected. Files exceeding the
new limit are refused whole; there is no truncation accepted as a snapshot,
pathname fallback, alternate DLL, retry or automatic cap increase.

Add a new 010-derived `producer_bcrypt_snapshot_probe.cpp`, reusing the frozen
008 client exactly. Keep fresh-R restriction, DefaultDACL, actual process/thread
DACLs, atomic child Job admission, its one-active-process limit, two inherited
anonymous channels and unchanged client argc/protocol. No new child handle,
client command, fixture data leaf, ordinary MQB, cost, ETW or M1b/service run.
The sole additional native handle is S's non-inheritable retained DLL read pin.

For the actual child's one bcrypt LOAD_DLL event, retain its event index,
PID/TID, base, hFile and reference to the already recorded bounded image path.
The basename is only a selector; the event object is the acquisition authority.
Absent/ambiguous paths, invalid handles or duplicate candidates are incomplete.

Observe FileIdInfo, FileStandardInfo, FileAttributeTagInfo, type and handle
flags through the event handle. Admit only a positive regular disk file within
1 MiB, no directory/reparse/delete-pending state, and a positive stable link
count. A Windows DLL may have more than one hard link. ReOpenFile the same
object for GENERIC_READ / FILE_SHARE_READ / FILE_FLAG_OPEN_REPARSE_POINT;
explicitly clear inheritance and verify it. There is no access/privilege/ACL
expansion or name-based reopen. A failed call remains evidence of refusal.

Compare event-before, pin-after-open and event-after-first-read physical
observations. Read the entire event file after a checked seek to zero, recording
each requested/returned count and first-pass bytes as lowercase hex, plus one
explicit one-byte EOF probe. The existing event hFile owner closes exactly
once before its ContinueDebugEvent; S's separate pin remains alive.

After bounded event draining, check pin-before-final, seek/read the entire pin,
compare every byte with the held first pass, probe EOF, and check pin-after-final.
Compare all five identities, lengths, positive link counts and relevant file
properties. Explicitly close the pin and retain the close outcome. Partial,
changed, absent, ambiguous, over-budget or late evidence cannot be complete.

## Combined collector budget proof

The independently checked 010 source envelope is 16,271 rows / 26,496,000 bytes.
It already counts original event-file closes, main/cleanup waits, path escaping,
loader strings, fixed preparation and failure reserves. No measured smaller
010 output is used to replace that worst-case proof.

| Added operation or record | Maximum |
| --- | ---: |
| Whole DLL bytes / chunk bytes | 1,048,576 / 65,536 |
| Data chunks per pass | 16 |
| Complete passes / one-byte EOF probes | 2 / 2 |
| ReadFile calls, including EOF | 34 |
| Requested file bytes, including EOF | 2,097,154 |
| Identity observations | 5 |
| GetFileInformationByHandleEx calls | 15 |
| ReOpenFile / SetFilePointerEx | 1 / 2 |
| GetFileType / GetHandleInformation | 2 / 2 |
| SetHandleInformation / pin CloseHandle | 1 / 1 |
| Total added principal Win32 calls | 58 |
| Added GetTickCount64 observations / retained conservative cap | <=120 / 160 |
| Added GetLastError / SetLastError | <=58 / 2 |
| Added complete JSONL rows, all paths | 100 |
| Added first-pass hex payload | 2,097,152 bytes |
| Added per-row metadata allowance | 102,400 bytes |
| Extra original scope/summary allowance | 4,096 bytes |

The planned row decomposition is 58 API rows, five identity rows, one selection,
one duplicate refusal, four identity comparisons, four pass boundary records,
one collector refusal, two deadline records, one exception, one final record,
one release detail and one collector scope: **80 rows**. The 100-row envelope
retains 20 unspent rows. The actual collector has no separate scope row: its
scope fields extend the frozen scope record, giving a conservative 79 new
rows. Source review reconciles all emitting sites and exceptional paths. No hidden `require`-emitting
hex conversion or nested Record construction is allowed in the new collector.
First-pass ReadFile and its hex payload share one row. Existing event hFile
close records are counted by 010, not spent twice.

Combined rows: **16,371 <= 16,384**, leaving 13. Combined bytes:
**26,496,000 + 2,097,152 + 102,400 + 4,096 = 28,699,648 <= 33,554,432**.
Metadata excludes the separately counted raw hex; labels/keys and numeric
representations are bounded, and the collector reuses the recorded path
instead of emitting it again. Every new record must fit the 1,024-byte
metadata allowance, with first-pass data payload separated explicitly. The
maximum checked new record is 879 bytes including CRLF; EOF overflow bytes
are metadata, never an extra data-payload allocation. The raw validator
counts original line bytes, including whitespace, without reserialization.

All original main/cleanup/event/string/path budgets remain in force. The
collector checks the active deadline before and after bounded operations;
first pass shares the existing 20-second main interval, second pass and release
share cleanup <=5 seconds and absolute entry+25 seconds. It cannot reset either
clock. Close still occurs after a failed check, and lateness is recorded as
incomplete. GetTickCount64 checks emit no per-check rows; only first deadline
refusals emit. Clock and error-helper observations are separately counted in
the table; they are not hidden within the 58 primary operations. These synchronous APIs have no claimed individual completion
guarantee: the unchanged 30-second watchdog and outer 40+10-second ownership
cleanup remain the last resort. A watchdog exit supplies no complete capture.

## Pure validators and offline boundary

The new snapshot validator strictly parses bounded UTF-8 consecutive JSONL,
rejects duplicate keys and non-finite numbers, binds actual C/event/path/handle
records, and validates exact schema, call outcomes, physical identities,
offsets, sizes, bytes, EOFs, both passes, no duplicate and checked pin release.
Malformed/partial/mismatched/refused/budget-exhausted cases must fail meaningful
tests before native activation. Boundary tests include the exact 1 MiB maximum,
an over-limit whole file, changed second-pass content/identity, missing EOF,
wrong-event association, and false success/qualification fields. It reconciles
actual client PID/create TID, retained/actual/final child EXE physical identity,
ContinueDebugEvent status, actual/debug exit payloads, the original owned Job
handle/close, monotonic time and the shared final five-second budget. All final
queries follow Job close, with no contradictory close of the retained pin.
The first bytes are independently reconstructed and hashed; the second pass
is supported by native full-byte comparison records and the reviewed source,
not a separately emitted second byte copy.

Only an independently admitted raw snapshot can become an exclusively created
`bcrypt-image.bytes` offline artifact, outside the zero-leaf C fixture. Hash
and fully read it back; preserve raw stdout and original result without edits.
This is a separate diagnostic review result, never a retrofitted native_summary.

The companion pure PE parser examines only that <=1 MiB file. It bounds all
headers, sections, directories, strings and table walks, validates unique
file-backed RVA mappings, executable entrypoint, imports/delay imports and
exception/unwind ranges. Derive init RVA from this same future run's explicit
loader init/failure address and LOAD_DLL base; do not hardcode 010's 0xA590.
Meaningful synthetic PE tests precede implementation. Nonzero Windows DLL
checksums and a bounded certificate table are not incorrectly rejected by the
stricter fresh-child EXE patch contract.

If snapshot and PE admission succeed, use the existing local objdump only for
bounded read-only static decoding: one exact DLL, <=64 KiB unique code bytes,
<=256 basic blocks and <=32 direct in-module callees, <=30 seconds total tool
time and <=8 MiB combined tool output. Preserve tool path/hash/version, argv,
raw outputs and every reported instruction's bytes, RVA and file offset.
Never load/execute the DLL or fetch symbols/other builds. Stop traversal at
unresolved indirect targets, cross-module implementations, ambiguous decoding
or any cap. Static candidate calls/return tests are not executed-branch or
root-cause evidence. A later internal runtime observation requires its own
concrete evidence-based design.

The disassembler is not automatically invoked by the runner. The available
resolved tool is `/usr/bin/x86_64-linux-gnu-objdump`, 390,848 bytes, SHA-256
`7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5`.
The local installed package records `2.42-4ubuntu2.5`; the one budgeted
`--version` call must verify this without opening a DLL. No disassembler has
been invoked during implementation. After snapshot and pure PE validation,
an immutable offline invocation manifest must bind
the acquired file hash, executable resolved path/hash/version, exact argv,
input RVA/file intervals, maximum invocation count (including a version
query), output files and remaining time/byte/block/function caps before the
first disassembler call. Version identification is verified as one budgeted
invocation; no retry or symbol download is permitted. An unavailable tool or
ambiguous interval ends the offline phase with the retained reason.

## Exact one-shot execution and completion

Existing workflow ID **380958963**, next number **11**, attempt **1** only,
on the retained feature branch via push. Host remains GitHub-hosted windows-2022;
checkout must equal GITHUB_SHA and every executable/imported source is hash-bound
before helper import. One compiler driver and one native fixture invocation;
compiler wait 90 seconds and accepted compiler Job total 1 or 2, active zero.
Parent and fresh loader-snaps child images remain <=8 MiB each; preserve the
existing single-DWORD patch, six held image passes, two final inventory reads
and exact image binding before/after native capture. No local native compile
or native fixture run is part of pure testing.

The exact native outer total-2 gate stays **before native_summary admission**.
If it records 3 again, retain NATIVE_EXECUTION_INCOMPLETE and no native_summary
in result.json. Separate raw review may admit a snapshot but cannot repair
that independent execution result. All A1-A5, deletion, ordinary qualification
and canonical_evidence_sealed remain false even with a complete snapshot.

Before activation, independently review the final exact source, tests, budget,
Win32 lifetimes and preserved blocks; retain actual pure-test results. Create
the immutable commit through the GitHub Connector, then register its exact
commit/tree/parent, source fingerprints, operation/file/time bounds and one-shot
identity in #198. Fully read back the registration, recheck the feature head,
main, closed 001-010 run count and independent Draft/HOLD heads, then update
the branch with expected-head non-force semantics. No rerun or replacement.

Close the one attempt using original ZIP/member integrity, logs, all source and
image bindings, raw event/collector records and independent review. Record the
bounded offline findings only if their input is admitted. Preserve observed
failure and limitations, then write the concrete remaining handoff.

Frozen scope: main `87f86770cfc7e1941603074ace914e0c7faf10d3`, PR250 HOLD,
PR248 draft, consumer/cost DEFER, disabled M1b/service jobs, all 51 other
workflows, production cpp/VERSION, 001-010 evidence, and the 21,000-byte
cost preparation with SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`.

Sources: [LOAD_DLL hFile](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-load_dll_debug_info),
[ReOpenFile](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile),
[FileIdInfo](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info),
[PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
and the preserved 010 source-review budget derivation.
