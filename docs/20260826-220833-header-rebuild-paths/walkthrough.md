# Issue #130 walkthrough

## Outcome

MQB now treats a successfully rebuilt first-class PCH as an authoritative invalidation decision for every executable/DLL/static consumer. Consumers skip stale cache loading and filesystem snapshots, compile through the existing executor, write normal fresh cache evidence, and then link or archive once.

The ordinary public-header path was deliberately left on exact per-TU dependency validation. The fresh baseline already placed it ahead of direct Ninja, and introducing invocation-global snapshot memoization would widen the freshness risk for an unproven gain.

## Deterministic behavior

- `IncrementalCompileRequest::force_rebuild` produces only `explicit_rebuild` decision evidence before compilation and does not inspect a deliberately corrupt old cache.
- `force_downstream_rebuild` reaches all target and static-target consumer compile requests.
- A PCH creator is compiled and its owned `.pch` is verified before consumer scheduling begins.
- Every PCH consumer compiles, then exactly one link/archive occurs.
- The next unchanged build is a complete creator/consumer/link no-op.
- Executable, DLL, and static-library PCH compositions are each exercised directly.
- Ordinary corrupt-cache, missing-output, public-header, no-op, and single-TU behavior remains on the existing validator path.

## TDD evidence

Before the product change, the new compile-coordinator test failed with:

```text
FAIL: authoritative explicit rebuild should not inspect stale cache metadata
FAIL: authoritative explicit rebuild should bypass stale cache load warnings
2 test(s) failed
```

The target propagation test also failed before the product change with:

```text
FAIL: upstream rebuild should force every target consumer to compile
FAIL: upstream rebuild should retain explicit evidence on every forced consumer
FAIL: upstream rebuild should compile all four consumers exactly once more
FAIL: post-force warm target should not launch more compiler processes
4 test(s) failed
```

After the minimum implementation, focused Release runs reported:

```text
mqb_orchestration_incremental_tests passed
MQB-native shard 63/64: 1/1 selected tests passed.

mqb_incremental_target_coordinator_tests passed
MQB-native shard 15/64: 2/2 selected tests passed.

mqb_pch_e2e_tests passed
MQB-native shard 3/64: 1/1 selected tests passed.
```

## 100-TU x 5 evidence

Both retained JSON reports use 100 translation units, 5 iterations, 32 workers, Release, x64, the same isolated MSVC environment, CMake 4.1.2, Ninja 1.13.1, and MSVC 19.51.36248. They contain all 80 raw samples, execution order, MQB phase timings, exact cache counters, executable paths, and SHA-256 identities.

| Scenario | Before MQB | After MQB | After Ninja | After MQB delta vs Ninja |
|---|---:|---:|---:|---:|
| ordinary cold | 683.15 ms | 672.61 ms | 732.35 ms | -8.16% |
| ordinary no-op | 31.80 ms | 28.74 ms | 21.80 ms | +31.80% |
| ordinary single TU | 104.39 ms | 97.47 ms | 124.54 ms | -21.74% |
| ordinary public header | 560.50 ms | 516.70 ms | 697.13 ms | -25.88% |
| PCH cold | 989.94 ms | 976.25 ms | 883.76 ms | +10.47% |
| PCH no-op | 33.42 ms | 32.38 ms | 23.13 ms | +39.98% |
| PCH single TU | 100.33 ms | 101.85 ms | 119.72 ms | -14.93% |
| PCH header | 874.50 ms | 875.38 ms | 895.90 ms | -2.29% |

The representative median PCH compile phase changed from 815.975 ms to 812.467 ms, while end-to-end wall-clock changed by +0.88 ms (+0.10%). This is not a measurable wall-clock speedup. Required parallel MSVC compilation dominates the rebuild, and the removed MQB bookkeeping is smaller than ordinary workstation variance. The change is retained because it removes semantically redundant work with deterministic coverage and no widened freshness surface.

Raw evidence:

- [`benchmark-before.json`](benchmark-before.json)
- [`benchmark-after.json`](benchmark-after.json)

## Full verification

```text
C++ responsibility layout contract passed.
Bilingual documentation parity check passed for 17 maintained pairs.
Final Closure cross-stage state/path/environment gate passed.
```

```text
MSVC /external:env ownership boundary and explicit /external:I replacement checks passed.
MSVC /fsanitize=kernel-address WDK/driver ownership boundary check passed.
Real MSVC AddressSanitizer runtime, VCAsan freshness/opt-out, non-incremental LINK, and mqb run checks passed.
Real MSVC LibFuzzer default-runtime, CRT selection, suppression, and warm-cache checks passed.
Real MSVC classic OpenMP runtime freshness, NODEFAULTLIB, Modules, and LLVM ownership checks passed.
Real MSVC transitive .drectve default-library freshness, search reroute, and LTCG checks passed.
Include search resolution freshness checks passed.
__has_include absent-to-present freshness check passed.
Real MSVC linker side-output/repro isolation plus raw WHOLEARCHIVE freshness checks passed.
```

```text
Exact MSVC parameter inventory: compiler=309, linker=114, librarian=21, total=444
MSVC semantic variant inventory: 44 concrete high-risk variants
```

Both configurations built the 73-TU MQB product and the 72-TU shared native-test library. All deterministic native tests passed in both configurations:

```text
Debug:   subshards 0/8 through 7/8, 77/77 tests passed
Release: subshards 0/8 through 7/8, 77/77 tests passed
```

Self-host and package closure:

```text
Stage 1 SHA256: 463a9d01ad72a5ab2d04e6b03f5f7e4c2bd93c5358faa1904903bd9abeb30de5
Stage 2 SHA256: f3f1f5ae72bb1f52c5cdc2d367802b353f8120dcd9104b2347034f60b7caa619
MQB-native self-host closure passed: Stage 0 -> Stage 1 -> Stage 2 used MQB for every build generation.
native installer install / reinstall / uninstall / PATH ownership validation passed
Exact runtime-only package validation passed.
Package SHA256: 67d76eb93b066efd5c358d92b9087ca566f10f85374154df7d1f44750ee1dd92
```

## Scope audit

- No cache schema or public request API changed.
- No compiler/linker flags changed.
- No invalidation was skipped.
- No wall-clock CI threshold was introduced.
- The unrelated Windows Unicode `argv` defect for a Chinese repository path remains outside Issue #130.
