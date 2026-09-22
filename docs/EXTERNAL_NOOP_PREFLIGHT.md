# Non-MQB launcher preflight

**English | [简体中文](EXTERNAL_NOOP_PREFLIGHT_ZH.md)**

## Purpose

This independently bounded preflight tests the real launch functions used by
[the retained no-op diagnostic](EXTERNAL_NOOP_BOUNDARY.md). It does not execute
MQB, download the retained A/B artifact, rebuild either program, collect a new
benchmark score, or clear PR #207's original performance HOLD. Passing this
preflight is not allocation of the separate forty-call study.

The runner extracts only five allow-listed top-level definitions from the
original PowerShell collector, including both launch functions. It does not
execute the collector's script-level guard or load `Invoke-RegisteredStudy`.
The collector's canonical LF text is SHA-pinned; actual checkout bytes, including
Windows CRLF representation, are copied and hashed unchanged. A collector code
change requires a reviewed pin update, not silently testing another launcher.

## Fixed execution budget

There are exactly nine attempts through the launch functions, in this order:

| Attempts | Method | Expected behavior |
|---|---|---|
| 1, 2 | Legacy, Process | Echo argv/cwd and interpreter/source identity |
| 3, 4 | Legacy, Process | Concurrent stdout/stderr pressure, 262,144 bytes per stream |
| 5, 6 | Legacy, Process | Preserve explicit helper exit code 37 |
| 7, 8 | Legacy, Process | Missing executable, no helper marker or invented exit code |
| 9 | Process | Real 180-second root wait failure, retained output and limited cleanup |

Only seven helper processes can start. The helper uses the installed Python
interpreter and standard library, no descendants. Arguments include an empty
string, spaces, a quote, a trailing backslash and Unicode. Each child first
writes its PID/cwd/argv/source marker with exclusive creation. Pressure writes
256 lines of 1,024 bytes to each stream using two threads. Per-stream order is
fixed; merged stream interleaving is not. Legacy evidence is lines, not bytes.

The timeout helper emits two readiness lines and sleeps for 240 seconds. The
unaltered Process launcher must fail at its original 180-second wait, retain
those lines, and report root exit after its existing best-effort cleanup. A
natural helper exit, shortened wait, missing marker, or false descendant proof
fails the test. This does not exercise the separate 10-second pipe-drain timeout,
arbitrary API faults, descendants holding inherited handles, or hostile IO.

The original forty-call MQB study budget is untouched. Each admitted first PR
revision runs this fixed preflight once; a later natural main run is separate
post-merge evidence. No same-head retry, adaptive fill or resume is authorized.
Synthetic contract tests on Linux/Windows start no helper children. The runner
also invokes the Python checker at most eleven times (plan, nine judgments,
final audit); these read/write evidence only and do not call launch functions.
Normal repository native/self-host checks keep their own existing budgets.

## Evidence and failure handling

Use an entirely new root. The runner snapshots the five required source files,
records installed interpreter identity, plan and host settings, then writes each
started record before dispatch. It saves the returned observation and host cwd
before semantic validation. Expected nonzero, missing-target and timeout cases
remain raw failures even when their test verdict is correct. The first unexpected
error stops unused slots; the completion record remains stopped. Nothing erases
unexpected evidence, overwrites a report, repairs a failed fixture or fills slots.

The final audit checks all 53 planned call files, including mode-specific byte
streams and child markers. It recalculates every saved verdict and strictly
checks completion and zero-study flags. During collection the interpreter and
source files are rehashed. Relocated offline auditing verifies archived source
bytes and recorded interpreter before/after identities, not unavailable runner
interpreter bytes. The interpreter installation/standard library are not a
hermetic binary snapshot. This is journal consistency, not authenticated proof
that no unlogged process or concurrent modification existed.

QPC ordering and post-exit root FILETIME/CPU fields are checked separately, not
subtracted across clock domains or interpreted as descendant CPU. Root PID must
match the helper marker for Process success/nonzero cases. Legacy root fields
remain null. Host PowerShell and environment cwd must be restored. No performance
threshold, observation-cost subtraction, root-cause classifier or cleanup
permission is created by this preflight.

## CI and replay

`External no-op helper preflight` runs 21 synthetic tests per platform first and
then the nine-attempt Windows preflight. Its fixed ten-minute job bound also
covers synchronous legacy execution. An `always()` upload retains raw evidence,
including failures; job termination or artifact-service errors can still prevent
upload, in which case the evidence is incomplete, not accepted. No dispatch
trigger, A/B download, retry or `continue-on-error` is included.

After reviewing this fixed helper-only allocation:

```powershell
./tests/native/preflight_external_noop_boundary.ps1 -OutputRoot NEW_DIRECTORY
```

For a relocated artifact, audit without launching any process:

```powershell
python tests/native/external_noop_preflight.py audit EXTRACTED_EVIDENCE --output NEW_AUDIT.json
```

Add `--live-interpreter` only on the original runner to rehash its current
interpreter as well. Exit 0 means this helper preflight's observations match its
plan; exit 2 means invalid/incomplete evidence or refusal to write a new report.
Neither authorizes the original study. Before that study, separately freeze and
review its actual executor, source/input hashes, job timeout and failure upload.

## Primary API references

[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
defines root creation/exit and CPU times; exit data is undefined before exit.
[Redirected stderr](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.standarderror)
documents pipe deadlocks and concurrent reads.
[Process.Kill](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.kill?view=net-10.0)
states that root `WaitForExit`/`HasExited` does not establish all descendants exited.
