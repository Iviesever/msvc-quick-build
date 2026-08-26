# Implementation plan - Header rebuild paths

## Decision

Reuse the existing PCH downstream force signal. When the first-class PCH creator actually recompiles successfully, `Application` passes that fact through `force_downstream_rebuild` into the ordinary/static target request. The target coordinator marks each consumer compile request as forced. The compile coordinator recognizes this authoritative upstream invalidation before loading or validating the old cache and executes the normal compiler/cache-write path with `explicit_rebuild` evidence.

This preserves the required barrier:

`PCH creator compile -> verify owned .pch -> parallel consumer compiles -> link/archive`

## Alternatives considered

1. **Recommended: PCH downstream forced-rebuild fast path.** Smallest semantic surface and directly supported by creator success evidence. It eliminates work that cannot change the decision.
2. **Invocation-scoped shared file-snapshot memoization.** Could help ordinary and PCH dependency validation, but adds synchronization and risks hiding filesystem changes that occur during an invocation.
3. **Batch inverse-dependency planning.** Could classify shared-header invalidation once for the whole graph, but requires a substantially larger planner/cache refactor and is not justified while compiler time dominates.

## Change set

### Incremental compile coordinator

- Treat `IncrementalCompileRequest::force_rebuild` as an authoritative build decision.
- Produce deterministic `explicit_rebuild` validation and the standard one-action compile plan without loading/snapshotting stale cache state.
- Continue through the existing `MsvcCompileExecutor`, dependency reader, and `CompileCacheFile::save` path so outputs and new cache evidence remain identical to an ordinary rebuild.

### Target coordinators and application wiring

- Reuse the existing target-level `force_downstream_rebuild` bit instead of widening the request API.
- Propagate it to every consumer `IncrementalCompileRequest` as `force_rebuild`.
- Set the signal only when the PCH creator reports `compiled=true`; the same signal continues to force the required link/archive.
- Apply the same contract to executable/DLL and static-library targets.

### Deterministic tests

- Extend incremental compile tests with a corrupt/unreadable old cache fixture proving forced rebuild succeeds, records `explicit_rebuild`, launches once, and replaces the cache.
- Extend target/static-target tests to prove target-level forcing reaches every source and produces exactly one link/archive.
- Extend PCH E2E coverage to prove header mutation rebuilds creator before every consumer, recompiles all consumers, links once, and returns to a no-op on the next invocation.
- Retain ordinary public-header transition checks in the benchmark harness.

## Red-green sequence

1. Add the compile-coordinator forced-fast-path test and observe it fail because the current code emits `cache_load_failed` and performs ordinary validation.
2. Add target propagation assertions and observe consumers are not explicitly forced.
3. Implement the minimum fast path and propagation.
4. Run the same focused tests until green, then refactor only duplicated planning/execution code if needed.

## Benchmark evidence

1. Build the exact pre-change Release candidate from `origin/main`.
2. Run `benchmark_build_systems.ps1` with 100 translation units, 5 iterations, and 32 jobs; retain the JSON as `benchmark-before.json`.
3. Build the final Release candidate and repeat with identical policy; retain `benchmark-after.json`.
4. Compare raw samples, tool hashes, environment metadata, cache transitions, and ordinary/PCH header medians and MQB phase timings.
5. Report any improvement honestly and identify the remaining compiler-dominated portion.

## Verification gates

- Focused incremental compile/target/static/PCH tests.
- Debug and Release MQB self-builds.
- Complete native Debug and Release shards.
- Exact MSVC parameter and semantic variant inventories.
- Release self-host and runtime-only package validation.
- C++ responsibility layout and bilingual documentation parity.
- `git diff --check` and final scope audit.
