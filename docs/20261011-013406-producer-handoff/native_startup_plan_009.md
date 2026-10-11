# Prerequisite 009: bounded startup debug-event diagnosis

Status before activation: `STARTUP_DIAGNOSTIC_SOURCE_PREPARATION`.
Parent: `31a064f164df5224a0c9a111650564a117f8b4fa`.
This is a new, explicitly non-qualifying observation environment. The
[008 single default-DACL hypothesis](native_delegation_result_008.md) closed
with actual C DLL_INIT_FAILED despite complete successful DACL readbacks.
No further ACL adjustment is justified by that result alone.

## Question and primary-source basis

Which actual startup events, debug strings, exceptions and exit status occur
before this restricted child can send its first startup frame? In particular,
can a concrete module or initialization failure location be observed without
guessing another permission change?

The next fixture uses `DEBUG_ONLY_THIS_PROCESS`, not an attached system-wide
debugger or another executable. Microsoft documents it as a creation flag
supported by CreateProcessAsUser that makes the creating thread the debugger
for the new process:
[Process Creation Flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags).
Only that same creating S thread calls
[WaitForDebugEventEx](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-waitfordebugeventex)
and [ContinueDebugEvent](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent).
Events suspend the target until continued, so this is observability with an
explicitly different execution environment, not an ordinary IPC result.

[Debugging Events](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events)
describes the creation, image-load, exception and exit events and their handle
lifetimes. Image/DLL event file handles must be closed by the debugger; event
process/thread handles are system-managed and close on the corresponding exit
continuation. The original CreateProcessAsUser process handle remains separately
owned through post-event wait/exit readback. These handles must not be confused
or double-closed.

## Separate source and unchanged security construction

`tests/native/producer_startup_probe.cpp` is a new diagnostic translation unit.
It includes the frozen 008 probe with its `wmain` explicitly renamed, to reuse
the reviewed token/ACL helpers and the original client startup path. The 008
CPP and runner remain unchanged. The new runner separately binds the included
008 CPP hash before compilation and records all seven execution sources.
The renamed 008 parent entry is never called by the diagnostic parent; the
original client entry is used only for the actual `--client` branch.

The diagnostic parent retains the concrete WRITE_RESTRICTED construction:
flags 9, exact fresh single R, source and actual C ProcUnique distinction,
same user/authentication context, actual primary-token query, permitted child
privilege check, S's privilege disable and no-thread-token check. It retains
the 008 additive default DACL and all its before/after/actual-C/unchanged-S
readbacks. No token impersonation or privilege enablement is added.

The explicit protected process/thread DACL remains OWNER RIGHTS READ_CONTROL
plus the conditional S ProcUnique all-access ACE. No SYSTEM, unconditional
user, R, desktop or window-station grant is added. Immediately before resume,
read actual process and thread DACLs using their already owned handles and
record bounded validated descriptors/SDDL. This exposes what was applied;
it does not presume that the absence of a SYSTEM ACE is a failure cause.

Keep CREATE_SUSPENDED, CREATE_NO_WINDOW, EXTENDED_STARTUPINFO_PRESENT, the
two-entry inherited anonymous HANDLE_LIST and atomic child JOB_LIST birth.
The sole additional creation flag is DEBUG_ONLY_THIS_PROCESS. The inner Job
still has KILL_ON_JOB_CLOSE and active-process limit 1. Original actual C token
and source/query-handle ownership checks remain before resume.

The diagnostic creates no leaf file and no test named pipe. The original
client receives its fixed path arguments but cannot open the leaf before the
first command; this parent never sends a leaf or IPC command. The existing
two anonymous channels support only observation of the first 56-byte startup
frame. This run deliberately executes zero W0/R1/R2/P3 cases.

## Bounded event and startup capture

Use one creating S thread for the debug loop, the actual C and one existing
watchdog thread. No additional program, debugger thread, process attach,
symbol download, ETW, loader-flags registry change or machine service is used.

- Main diagnostic interval: at most 20 seconds, based on a monotonic absolute
  deadline. Each WaitForDebugEventEx wait is at most 50 milliseconds.
- At most 128 returned debug events in total: main observation stops at 112,
  reserving 16 for owned cleanup and exit drainage. Every acquired event has
  one explicit continuation; do not leave a taken event suspended.
- Record event type, actual event PID/TID, process/image bases, module handles
  and bounded file paths, exception code/flags/address/first-chance, and exit.
  Address-sized and lifetime integer identities are lossless decimal strings.
- For each actual C EXCEPTION event only, at most one read-only
  [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex)
  may query that event's exception address through the retained actual C
  handle. Record its returned metadata/AllocationBase and any association
  with the active, previously observed image mapping. There is no address
  scan, content read, heap/stack walk, or inference that the associated module
  contains the failing initializer.
- A single standard first-chance startup breakpoint may use DBG_CONTINUE.
  All other exceptions use DBG_EXCEPTION_NOT_HANDLED. Do not mark the observed
  initialization failure handled to make C proceed.
- Read debug-string bytes only from the separately owned actual C handle,
  at the event-supplied address: at most 4,096 bytes per event and 16,384 bytes
  in total. Record the original low-16-bit length, Unicode flag, requested
  bounded prefix, actual read count, raw byte hex and truncation/errors.
  Microsoft's [OUTPUT_DEBUG_STRING_INFO contract](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-output_debug_string_info)
  specifies the length as bytes, including when the Unicode flag is set;
  do not multiply it by two. The low-16-bit reported length is not a general
  guarantee of the full original string.
- Resolve module names only through provided event hFile handles, with at
  most 4,096 wide characters. Do not reopen arbitrary DLLs by path or scan
  another process; absent/unreadable names stay unavailable.
- Close each CREATE_PROCESS/LOAD_DLL image file handle after its observation.
  Treat event process/thread handles according to the documented system
  ownership, retaining the independent actual CreateProcess result for final
  identity and lifetime observations.
- Use bounded PeekNamedPipe checks on the existing report channel. Never
  block the event loop waiting for startup or the original IPC protocol.
  At most the first 56-byte StartupSnapshot is read. Record transport bytes,
  grammar and observed child fields; a parent value is not a child readback.

No WriteProcessMemory, SetThreadContext, code patching, injected thread,
debug-register change, arbitrary process-memory read, or new access grant is
permitted. Module mapping order alone is not an initialization dependency
trace. Exception addresses may point to a reporting routine rather than the
failed initializer. A concrete attribution requires actual supporting output;
otherwise the failed DLL/access remains UNIDENTIFIED.

## Diagnostic stopping and owned cleanup

Stop substantive capture once the first startup frame is observed, C's exit
is observed, a strict bound is reached, or a structural event/continuation,
non-timeout wait, or startup-transport/grammar error prevents safe continuation.
A failed optional module-path, exception-address VirtualQueryEx, or bounded
debug-string read remains a recorded error and prevents capture_complete;
it does not itself stop the event loop or justify terminating C before its
later events. These failed field queries are not retried and their missing
values are not supplied by a fallback.
If C reached startup and is waiting for the command this diagnostic
will not send, explicitly record that reason and request termination only of
that newly owned C. The same is allowed for this owned C on diagnostic timeout
or failure. No other process or service is targeted.

Continue/drain already-taken and subsequent exit events within the fixed
event budget and at most five further seconds. Both absolute deadlines are
based on the diagnostic parent's entry time: main at 20 seconds, final cleanup
no later than 25 seconds, leaving margin for the existing 30-second watchdog.
Record every cleanup request and API result.
There is no detach-and-continue alternative, no retry, and no exception handler
that converts diagnostic termination into natural completion. If a bound or
failure prevents full event/handle closure, mark the capture incomplete.

After EXIT_PROCESS has been continued, obtain actual wait/exit/times and inner
Job observations from the existing process/Job handles before closing them.
An event exit value and a post-continuation actual-handle exit are separate
observations. Job counters remain counters, including their documented
failed-association caveat; no unnamed additional image is inferred.

## Diagnostic-only output contract

The last native row is `kind=summary`, `classification=INCONCLUSIVE`,
`diagnostic_only=true`, root exit code 3. A1/A2/A3/A4/A5/deletion are all false,
and `ipc_cases_executed` is integer 0. The summary records bounded event and
continuation counts and explicit observed/valid flags for startup and actual
exit, cleanup_requested and capture_complete. `client_exit_code` is present
only after a successful actual child-handle exit observation.

The independent new runner rejects every qualification label or authority,
nonzero IPC case count, duplicate JSON key, non-finite value, invalid scalar
type, nonconsecutive record sequence, excess event count and contradictory
complete-capture claim. Native JSONL parsing is capped at 4 MiB / 4,096 rows.
It retains the original compiler/native outer Job and source/tool/file identity
checks. Final workflow exit remains 1 even for complete diagnosis: this run is
not a successful IPC/producer qualification. Full raw artifacts remain useful
whether the target fails or reaches startup under observation.

## Exact review, registration and execution envelope

Before activation: finish independent source review of the new CPP, included
008 source identity, new runner and changed workflow. Freeze the exact tree,
create an immutable commit, register all hashes and this budget in issue #198,
read it back completely, then refresh the branch lease and eight closed runs.
Only Connector activation on `feat/producer-clean-handoff-20261011` is used.
Only workflow `380958963`, **run number 9 / attempt 1**, push event, may execute.
No dispatch or rerun of 001-008 is included.

One five-minute windows-2022 job; one /MT /Z7 /W4 /WX fixture compiler driver,
Job total 1 or 2, compiler wait 90 seconds, original telemetry-image pin and
readbacks, no PDB. One native fixture invocation, S plus C, required outer Job
total exactly 2 and child active limit 1. One watchdog, two anonymous pipes,
zero named-pipe test instances, zero fixture files or payload bytes, at most
56 bytes of startup protocol. Main debug bound 20 seconds, owned cleanup at
most 5, watchdog 30, outer native wait 40, capture cleanup at most 10. All
token/ACL query buffers remain bounded to 64 KiB and default ACE counts to 128.

Before 009 there were eight prerequisite runs, seven compiler drivers,
compiler Job TotalProcesses summing to seventeen, and four native fixture
invocations. Zero ordinary MQB, cost, ETW or service study is added. Closed
artifacts and records are preserved. Frozen cost PREPARATION_ONLY_BLOCKED,
PR250 HOLD, consumer/cost DEFER, disabled M1b, no 70/42 replay, no A3/A5/deletion,
and pending A1/A2/A4/positive live handoff remain in force.
