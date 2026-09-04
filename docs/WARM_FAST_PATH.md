# Persistent Warm Fast Path

**English | [简体中文](WARM_FAST_PATH_ZH.md)**

MQB's compile/link/archive caches already avoid repeated tool invocations, but a fully warm build can still spend time recursively indexing a project and rereading source/header text before those downstream caches are consulted. The persistent source-discovery cache removes that repeated front-half work without weakening discovery correctness.

## Scope

Smart source discovery persists one best-effort record per discovery root at:

```text
<discovery-root>/.mqb/cache/discovery/source-discovery.mqbcache
```

The public `SourceDiscovery::Request` enables persistent state by default. Direct callers may set `persistent_cache = false` or supply an explicit `cache_file`.

This state is performance-only. Cache read, parse, validation, or write failures never create a build failure; MQB falls back to the ordinary recursive discovery path.

## What is sealed

A successful cacheable discovery records:

- normalized discovery request identity:
  - project/discovery root;
  - entry translation unit;
  - include search order;
  - excluded directories;
  - explicitly included extra sources;
  - excluded sources;
- the selected translation-unit list and ordering;
- the indexed-file count;
- whether the selected closure requires the Modules/P1689 pipeline;
- exact `file_time_type` snapshots for every indexed C/C++ source/header;
- exact directory snapshots for the discovery root and every recursively visited non-excluded directory.

File evidence catches content changes. Directory evidence catches file additions, removals, and renames even when no previously indexed file changed.

## Race-safe sealing

Discovery captures freshness evidence while building the source index, then rechecks every captured file and directory snapshot before the record is eligible for persistence.

If a source/header or traversed directory changes during discovery, that pass remains valid for the current invocation but is not blessed as reusable persistent evidence. The next invocation performs ordinary discovery again.

Warnings caused by unreadable indexed files also prevent sealing. Explicit extra sources must be present in the index before the result can be cached.

## Warm reuse

On a later invocation MQB first performs the inexpensive request/path validation required to preserve existing diagnostics. It then loads the persistent record before reading extra-source text or recursively enumerating/analyzing the project.

Reuse requires:

1. exact normalized request identity;
2. every indexed file still exists as a regular file with the exact recorded timestamp;
3. every recorded directory still exists as a directory with the exact recorded timestamp;
4. a valid versioned cache record.

Any mismatch becomes a normal cache miss.

The `.mqb` state directory is already excluded from smart discovery, so writing or replacing the discovery-cache file cannot invalidate the project directory evidence it protects.

## Cache format

The current binary format is version 1 (`MQBDISC1`). It is bounded when parsing (file size, string size, and path/snapshot counts), rejects malformed or trailing data, stores paths in normalized generic UTF-8 form, and persists native `file_time_type` tick counts exactly.

The format is intentionally private. An incompatible future version should be treated as a cache miss rather than migrated at the cost of build correctness.

## Validation and performance evidence

`source_discovery_cache_tests.cpp` covers cold/warm reuse, header invalidation, source addition/removal through directory evidence, request-identity changes, corrupt-cache fallback/repair, and explicit cache disablement.

The native benchmark harness includes:

- `discovery-cold` — first smart-discovery/build invocation;
- `discovery-no-op` — unchanged warm invocation where persistent evidence should avoid recursive indexing/text analysis;
- `discovery-header` — a header mutation that must invalidate persistent evidence.

`compare_mqb_benchmarks.ps1` reports both total-wall-clock and discovery-phase medians/deltas for every scenario. These measurements are review evidence rather than hosted-runner correctness thresholds.

## Deliberate limits

The first implementation stores one discovery request/result per discovery root. Alternating multiple entry/request identities may therefore replace the prior record and cause extra cache misses, but never stale reuse. A future multi-key cache can improve that workload without changing the freshness contract.

This milestone does not persist MSVC environment/toolchain discovery and does not bypass project configuration parsing. Those are separate fast-path layers with different invalidation and security boundaries.

## Invocation-scoped target filesystem evidence

Ordinary executable/DLL targets and static-library targets share compile-cache **dependency** snapshots across their translation-unit inspections for the duration of one MQB invocation. The table is neither persisted nor reused by another target or process.

The key is the Windows path identity under the existing file-or-directory dependency semantics. Concurrent requests use a single-flight entry: one worker performs the metadata query and other workers receive the exact existence/timestamp result while retaining their own requested path spelling for validation and diagnostics.

Source and object/output snapshots deliberately stay on the historical direct path. Target validation already guarantees those regular-file artifacts are unique, so routing them through a synchronized table cannot produce reuse and would penalize projects with no shared dependencies.

The table is activated only when a target has at least three translation units. Because every reused dependency must be revalidated once, a path observed by exactly two translation units still costs two physical probes (`1` initial + `1` barrier), equal to the historical direct path. Disabling the table for one- and two-TU targets therefore removes synchronization overhead without giving up any possible metadata-I/O reduction.

The dependency table changes warm-path scaling from dependency occurrences toward unique dependency paths without changing cache records, build signatures, compiler recipes, or freshness comparisons. Cache loading and compile validation remain per translation unit.

Every dependency whose first snapshot was reused is probed once more after the compile scheduler has joined all workers. If existence, timestamp, or reliable metadata status changed across that window—or the barrier itself cannot be completed—MQB rejects every result that could depend on the shared observation and conservatively rebuilds the complete target without evidence reuse. A failed optimization therefore becomes extra work, never stale success.

The current boundary is deliberate:

- ordinary and static target compile-cache dependency inspection participates;
- unique source and object/output probes remain direct;
- PCH and Modules/Header Units retain their existing dependency-ordered paths;
- link/archive object snapshot handoff is a separate future optimization;
- the table is invocation-local and does not introduce a daemon, watcher, USN journal, content hash, cache pack, or persistent lock.

`incremental_target_coordinator_tests.cpp` locks the single-flight counter behavior and the mutation-revalidation rejection. `verify_performance_instrumentation.ps1` proves that a 129-TU common-header no-op keeps exact cache/process behavior while each shared dependency performs at most its initial probe plus one revalidation.
