# Retained external no-op boundary diagnostic

**English | [简体中文](EXTERNAL_NOOP_BOUNDARY_ZH.md)**

## Scope and execution authority

This is an independent diagnostic of PR #207's retained #701 evidence, not a
product optimization, repeat benchmark score or HOLD waiver. The original
+3.0963 ms / +26.70024178525% result remains a hard failure. The executable gate
in [Performance Governance](PERFORMANCE_GOVERNANCE.md) remains unchanged.

The collector must be reviewed and frozen before a separate execution allocation.
The `External no-op boundary contracts` workflow runs synthetic Python tests and
Windows parser/helper checks only. It never invokes MQB, the study, MSVC or ETW.
Opening, synchronizing, marking Ready or merging this tooling PR does not start
the diagnostic. Merely possessing the script is not an execution allocation.

## Immutable inputs and fixed plan

Only the original #701/run35681224762, attempt1 ZIP is accepted:
`940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3`.
The original before/after identities, both source archives and A/B executables
are checked. A is `9c23cde3450a7c10c2edca75838923601c9a4fd300617928b04263d128d19b58`;
B is `76b55ca6176eb816131dad7de624170bcc3b21631b13190f0e943034d00655a9`.
No reconstruction, new seed build or alternative binary is permitted.

Before dispatch, preparation saves the original ZIP, both executable copies,
collector-source snapshots and the entire fixed plan in an entirely new root.
Two methods × four alternating A/B pairs × two sides × one priming plus one
no-op =32 MQB calls. One B/B pair per method adds8 calls: maximum40,20 separate
fixtures. Pair parity alternates both side order and method order. Every fixture
uses the original two timings source strings, UTF-8 without BOM and CRLF, and
`main.cpp helper.cpp --output timing_bench -j 1`. There is no extra warmup,
internal-timings run, output-program run, adaptive retry or refill. In particular,
the old benchmark's intervening timings-enabled call is NOT reproduced; this
is the previously registered two-call diagnostic design, not historical replay.

## Distinct measurement boundaries

The legacy-adjacent method retains PowerShell `&`, merged text-line capture and
Push/Pop-Location. QPC reads separate the outside interval from its native-call
and capture envelope. It has no child handle/PID or OS lifetime measurement:
those fields remain null, not zero or invented process times.

The Process method sets an explicit working directory and ArgumentList, disables
shell execution, and starts both redirected byte-stream reads concurrently before
waiting. It records QPC start-return, wait-return and pipe-drain boundaries.
After root exit, the held process handle supplies GetProcessTimes creation/exit
and kernel/user counters. These FILETIME units and QPC ticks are separate clock
domains; never subtract across them. Root CPU sums root threads, not descendants,
and can exceed wall elapsed. The process lifetime is not pure application CPU.
Raw stdout/stderr bytes remain separate; replacement decoding is for diagnostic
prefix checks only. Legacy merged lines are not a byte-exact stream archive.

The Process method stops after180seconds root wait or10seconds drain wait;
best-effort owned-tree termination and5seconds root wait are recorded on failure.
Root exit does not prove all descendants exited. The synchronous legacy method
has no per-call interrupt: any later executor must have a fixed job timeout and
retain unfinished started records. File count/size and wait limits do not promise
a deadline for arbitrary filesystem operations or output growth. No service,
antivirus, machine policy or process priority is modified.

## Failure preservation and review

Each call saves its input/file manifest and started record before invocation;
its original result/output precedes after-snapshot and semantic validation.
Binary files are read-pinned during the study. Source, plan and collector hashes
are rechecked. A no-op must retain three up-to-date lines, no compile/link/timing
record, and identical observed bytes/metadata, also matching the preceding prime.
This is a semantic/output/filesystem test, not a complete child-process trace.

Any first invalid identity, dispatch, exit, snapshot, timer/API or write stops the
remaining plan. A valid prefix is not completion; unfinished/missing data is not
filled. Old evidence/output roots cannot be overwritten or resumed. The Python
auditor produces within-method raw pairs and summaries only, leaves cause null
and `clears_hold=false`, and keeps B/B controls without cost subtraction.
Snapshots, hashing, Python validation, queries and disk capture may perturb the
study. They do not make atomic filesystem or exclusive producer-ownership claims.
No method, order or new green result automatically explains or replaces #701.

After an explicit reviewed allocation only:

```powershell
./tests/native/collect_external_noop_boundary.ps1 -ArtifactPath ORIGINAL.zip `
  -OutputRoot NEW_DIRECTORY -ExecuteRegisteredStudy
```

Re-audit retained results without executing MQB, using a new output path:

```powershell
python tests/native/external_noop_boundary.py audit NEW_DIRECTORY --output NEW_AUDIT.json
```

## Primary API contracts

[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
describes root creation/exit/CPU values and100-nanosecond units.
[RedirectStandardOutput](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processstartinfo.redirectstandardoutput?view=net-10.0)
explains redirected-pipe deadlocks and concurrent reading requirements.
[PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
defines the separately retained static section/import inspection; static changes
alone do not establish the cause of a timing difference.
