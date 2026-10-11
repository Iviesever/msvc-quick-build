# Prerequisite 007: observe the actual client startup failure

Status before activation: `DIAGNOSTIC_SOURCE_PREPARATION_ONLY`.
This is a new, single, bounded diagnostic execution derived from source
commit `a6f3557f27f9b061551f1e2f8351f16be499cee5`.
It is not a rerun or replacement of the closed
[006 attempt](native_delegation_result_006.md).

## Evidence and question

006 compiled and resumed the restricted client, but the report pipe broke
before the first client report. The actual client exit code and inner Job
accounting were not read before cleanup. Outer Job TotalProcesses was 3
instead of the allowed 2; the additional count has no observed identity.
Thus the currently supported diagnosis is a missing startup/exit observation,
not a specific bad HANDLE_FLAG_INHERIT, ACL, CRT, loader or error-reporting
condition.

007 asks where this one existing client stops. It adds observations at the
component boundary and preserves the original predicates. No candidate
boundary repair is bundled into the diagnostic change. If startup proceeds,
the original four fixed cases run and retain all their original checks.

## Exact delta from 006

The source remains `tests/native/producer_delegation_probe.cpp`; the runner
remains `tests/native/run_producer_delegation_probe.py`. The unchanged
`run_producer_boundary_seal_probe.py` and `producer_probe_job.py` retain their
previous compiler selection, full-byte capture, telemetry pin and cleanup
behavior. The runner changes only the exact attempt/run identity and plan
reference. The native implementation changes only the following diagnostics
and its attempt/name strings.

### Fixed protocol observations

Use the existing two anonymous protocol handles, with no new pipe, file,
process, token, privileged handle or fallback channel. Add protocol command 0
for these two exact binary snapshots:

| Snapshot | Size | Raw fields |
| --- | ---: | --- |
| Startup | 56 bytes | Protocol magic/command/phase, argc, actual parsed input/output handle values, separate GetHandleInformation success/error/flags for each handle |
| First command | 40 bytes | Protocol magic/command/phase, first command ReadFile success/error/byte count, all four original Command fields |

The client attempts the startup snapshot before any existing handle-validity,
distinctness or HANDLE_FLAG_INHERIT rejection. Each API error is captured
immediately. The original validation remains in force afterward. The first
command snapshot is emitted after that ReadFile and before any of its
existing size/magic/reserved/command rejection. The server logs complete raw
snapshot values before checking the fixed format and original prerequisites.
The server receives startup, sends the original leaf command, receives the
first-command snapshot, and then reads the unchanged three leaf reports.
There is no retry, tolerant parser, synthetic success or alternative channel.

The new 96 bytes plus the original maximum 928 bytes equal 1,024 protocol
bytes, preserving the pre-registered 1 KiB maximum. Handle and FILETIME values
remain lossless decimal strings in JSONL. If the client never reaches its
entry path or cannot write its report handle, the server's retained actual
process handle is the source of exit evidence; missing startup is not
relabelled as a normal exit.

### Actual process and Job observations before cleanup

Create a diagnostic guard only after the actual child process/thread handles
exist. Its lifetime ends before those handles and the owned child Job are
closed. On exceptional unwind, perform a single WaitForSingleObject on that
actual process handle with at most 1,000 milliseconds, then one
GetExitCodeProcess, one GetProcessTimes and one basic-accounting query on
that exact child Job. Record each API result/error, the exact process handle,
lossless creation/exit/kernel/user times and Job TotalProcesses,
ActiveProcesses and TotalTerminatedProcesses. These are observations, not a
completion capability. Do not substitute a PID lookup or aggregate outer
Job count for the actual process handle. Do not terminate or retry the child
as part of this diagnostic; existing abnormal cleanup remains owned by the
existing guard chain.

Before the existing single ResumeThread, also record one basic-accounting
query on the exact Job. This distinguishes its pre-resume baseline from the
failure snapshot. Cancel the unwind guard on the already verified successful
normal path before normal process/Job handle closes. A failed or still-active
diagnostic is retained as such and never promoted to natural completion.

Microsoft documents that GetExitCodeProcess returns the entry return code,
termination value or unhandled exception status only after termination:
[GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess).
The Job total also includes failed associations due to a limit violation:
[Job accounting](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information).
Neither API identifies a previously unobserved third process image.

## Preserved candidate and interpretation

All token flags, identity checks, protected process/thread/leaf ACLs, inherited
handle lists, atomic JOB_LIST birth, ActiveProcessLimit=1, disabled host
privileges, parent/child ownership, named-pipe modes and SQOS values remain
exactly as specified in [006](native_delegation_plan_006.md). No admission
grant is widened. No leaf baseline or positive-control predicate is relaxed.
The server must still obtain its actual thread token only through
ImpersonateNamedPipeClient, close query handles and revert before later use.
Every conclusive result still requires all four cases, the full R-admitted
positive control, natural client exit 0, observed inner Job total 1/active 0/
terminated 0, exact ACL revocation, and retained-writer mutation/readback.

Outer native Job total must remain exactly 2. A repeated count of 3 remains
`NATIVE_EXECUTION_INCOMPLETE` even if added diagnostics identify a cause.
Unknown startup, early exit, malformed protocol, missing control or any
capture/source/budget failure remains INCONCLUSIVE. A diagnostic observation
is useful evidence without being a successful IPC falsification or A2 proof.
The original labels and meaning are retained: finite-route refusal cannot
qualify an ordinary MSVC owner; a completed counterexample disqualifies the
candidate; no result issues a product receipt.

## One-shot registration and budget

Independent review must inspect the concrete diff and exact source hashes.
Create its immutable commit without activating the branch, register the
commit/tree and hashes in issue #198, read the registration back, verify the
branch lease and completed prior runs, then activate using Connector Git.
Use only workflow `380958963`, next run number **7**, attempt **1**, push on
`feat/producer-clean-handoff-20261011`. No dispatch or old-run retry.

- One hosted Windows Server 2022 workflow, five-minute job limit.
- One compiler-driver invocation, expected compiler Job total 1 or 2;
  existing 90-second root wait, fixed MSVC argv and held vctip image pin.
- One native fixture invocation containing S and one C; no helper process,
  one S watchdog thread, one C-only Job and the same two inherited handles.
- Exactly the same four fixed cases, eight named-pipe objects including
  independent baselines, two anonymous protocol pipes, at most four payload
  bytes on test connections and 1,024 protocol bytes.
- Native watchdog 30 seconds, outer capture 40 seconds, cleanup at most
  10 seconds. The one exceptional 1,000-millisecond child wait stays within
  these limits. No repeated waits, process census, image probing or ETW.
- One zero-data fixture leaf, retained payload cap 4,096 bytes; full raw
  compiler/native streams, Job records, source/tool identities and retained
  artifact hashes. No PDB, recursive deletion, overwrite or sample replacement.

Prior attempts 001-006 stay closed with their original bytes and labels.
Before 007 there are six prerequisite runs, five compiler-driver invocations,
thirteen observed compiler Job total-count units, and two native fixture
invocations. 006's native outer count of 3 is recorded separately and does
not prove three successfully running fixture processes. There are no ordinary
MQB producer calls, cost calls, ETW calls or service studies in this budget.

The frozen analysis-cost JSON remains PREPARATION_ONLY_BLOCKED with its
original zero/null/false controls. PR250 remains HOLD; consumer/cost remains
DEFER; M1b stays disabled. A1/A2/A4 are false and unimplemented; A3/A5 and
deletion stay disabled. Technical diagnosis may continue under the existing
engineering authorization without treating a tool/API name as proof.
