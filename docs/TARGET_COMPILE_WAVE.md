# Target-wide compile inspection and miss-only execution

**English | [简体中文](TARGET_COMPILE_WAVE_ZH.md)**

This step builds on the invocation-local filesystem evidence described in [Persistent Warm Fast Path](WARM_FAST_PATH.md). It separates ordinary executable/DLL and static-library compile inspection from execution without publishing a stale-plan execution API.

## Ownership and phases

`src/orchestration/incremental/TargetCompileWave.hpp` is an orchestration-private owner shared by both target coordinators. For eligible targets it runs parallel, side-effect-free `inspect()` callbacks once, publishes hit results directly in source order, retains each miss's exact request and inspection, and builds a compact source-index list for execution. `execute_inspected()` consumes that inspection without reopening the compile cache or repeating the planner.

Only misses retain full requests and plans after inspection: storage is one pointer slot per TU plus owned request/plan data per miss. No cached decision is persisted or returned as a public execution ticket. Public `inspect()` remains diagnostic data; public `run()` still performs fresh inspection before execution. The private consumer is accessible only to `run()` and the invocation-owned wave.

## Scheduling contract

Inspection retains the established bounded parallelism policy. The execution scheduler receives the number of misses, not the total number of TUs. An all-hit wave never enters the execution scheduler. One miss runs on the caller; multiple misses use the existing worker policy, bounded by their actual count. Inspection failure prevents all execution, including previously planned misses. Execution failure retains the scheduler's stop behavior, source-index error selection, and no-link/no-archive rule.

The one/two-TU and already-forced paths remain combined inspect-and-run paths, avoiding a second phase on those workloads. Fixed `-j1` activates shared evidence once around its inspection loop. Execution explicitly suspends inspection evidence, including nested calls.

**Zero execution tasks is not zero inspection threads.** A fixed `-j4` all-hit large target still creates three inspection background threads. This change does not claim to remove those threads or to improve latency merely because its queue is smaller. Cold and partial rebuilds can use separate inspection and execution worker cohorts; their extra dispatch overhead and reduced overlap require measurement.

## Freshness and failure boundary

The target's shared-dependency revalidation barrier stays after the entire wave, including miss execution. A header can change while a compiler runs, so a pre-execution-only barrier is insufficient. A rejected shared window discards all earlier results and forces the whole target through the historical path with sharing suspended. Only successful completion can reach link/archive.

This retains the existing timestamp-based race model; it is not an atomic filesystem snapshot and does not claim protection from arbitrary timestamp-preserving or post-barrier mutations. Source/output snapshots, cache formats, signatures, recipes, include-search freshness, and Modules/Header Units ordering are unchanged. No compile-output handoff, persistent cache pack, daemon, or watcher is introduced.

## Verification and performance evidence

The existing target coordinator test executable now checks the actual inspection/execution scheduler summaries, cache-open counts, compact-to-source index mapping, original warning retention, static-wave symmetry, fixed `-j1`, inspection failure before execution, and a shared-header mutation injected inside a single missing compiler invocation. The latter requires one initial compile followed by four forced compiles, with exactly one final link. Existing concurrent-failure, duplicate-artifact, shared-evidence, native E2E, and self-host gates remain authoritative.

The paired ABBA harness compares this candidate with its exact merged base. Retain all measured deltas, including cold/partial-rebuild regressions and ordinary small-target controls. Deterministic queue/cache evidence establishes the structural contract; wall-clock results establish whether that structure benefits a particular workload. `VERSION` remains `5.4.0` until the separate v5.5.0 release closure.
