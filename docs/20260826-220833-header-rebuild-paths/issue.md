# Optimize public-header and PCH-header rebuild paths

## Original intent

Complete GitHub Issue #130: reduce MQB-owned work when an ordinary public header or the configured PCH header changes, while preserving exact dependency invalidation, PCH ownership, creator-before-consumer ordering, and the existing no-op/single-TU contracts.

## Current gap

The fresh 100-TU x 5 baseline from the exact current `main` tree shows that required compiler work dominates both header-rebuild scenarios:

- `ordinary-public-header`: MQB median 560.50 ms versus direct Ninja 755.96 ms; 100 misses, 1 hit, and 1 link.
- `pch-header`: MQB median 874.50 ms versus direct Ninja 865.81 ms; 102 misses and 1 link.

The corrected baseline already places the ordinary public-header path ahead of direct Ninja, while the PCH-header path is effectively tied within normal workstation variance. This issue is not permission to skip required compilation. The evidence-selected redundant work is narrower: after the PCH creator successfully recompiles, every PCH consumer is guaranteed to require recompilation, but each consumer still loads its old compile cache and snapshots its source, outputs, and complete dependency set before discovering that the newly written `.pch` is newer.

## Contract

### Objective

Add an explicit forced-consumer rebuild path for a successfully rebuilt first-class PCH so downstream consumer compilation can begin without redundant cache/freshness probing.

### Acceptance

1. A rebuilt PCH creator forces every consumer to compile and the target to link exactly once.
2. The creator finishes successfully and its owned `.pch` exists before any consumer starts.
3. Forced consumers retain deterministic `explicit_rebuild` evidence and write normal fresh compile caches.
4. A warm unchanged PCH build keeps the ordinary cache-validation path and remains a no-op.
5. Ordinary public-header invalidation still recompiles every dependent ordinary TU and links once.
6. Missing outputs, stale headers, corrupt caches, ordinary/PCH single-TU, and no-op contracts remain unchanged.
7. Corrected 100-TU x 5 before/after reports retain raw samples, phase timings, exact cache transitions, and tool/environment identity.
8. Debug/Release builds, complete native shards, self-host/package, layout, and bilingual documentation gates pass.

### Non-goals

- Skipping required compiler or linker work.
- Reusing a stale PCH or consumer object.
- Adding a wall-clock CI threshold.
- Introducing an invocation-global filesystem snapshot cache whose freshness semantics span unrelated targets.
- Replacing the incremental coordinator with a new inverse-dependency graph.
- Addressing the separate Windows Unicode `argv` defect observed in a Chinese repository path.

## Constraints

- Use MQB as the only build/test execution authority.
- Keep the product diff focused on the existing incremental compile/target/PCH pipeline.
- Prove the behavior with deterministic tests before relying on benchmark evidence.
- Treat machine-local timing as evidence, not a universal performance claim.
