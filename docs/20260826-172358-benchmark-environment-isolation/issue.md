# Benchmark environment isolation and cache-transition evidence

## Original intent

Continue the merged build-system comparison work by collecting a publishable
`100 TU x 5 iterations` local data set, locating MQB's cold/PCH bottlenecks,
and optimizing only after the measurements identify a reproducible cause.

The user subsequently authorized correcting the persistent Windows environment,
rerunning the formal measurement, and optimizing MQB product code against the
corrected evidence.

## Observed problem

The first local report is internally correct but not yet suitable as a product
performance conclusion. The launching shell contains MSVC metadata from an
older VS2022 environment plus a custom include directory. The benchmark then
imports VS18 on top of that ambient state.

On this machine the layered environment prevents MQB's persistent Visual Studio
toolchain cache from being reused. Every invocation repeats vcvars discovery.
For the flat generated fixture, the discovery path also changes the directory
used as quoted-include freshness evidence, so the 100 translation units that
include `common.hpp` are rebuilt on a nominal no-op invocation.

Reproduction evidence:

- original 100-TU no-op median: about 2.0 seconds;
- repeated polluted-environment invocation: 1 compile hit / 100 misses;
- the only source without an include (`main.cpp`) remains a hit;
- with clean MSVC metadata and the same MQB binary/source set: 101 compile hits,
  one link hit, and about 102 milliseconds wall time;
- moving active `.mqb` state outside the watched source directory also prevents
  the false invalidation, confirming the interaction between repeated toolchain
  discovery and include-directory freshness.

## Goal

Make the comparison harness establish one clean, explicit x64 MSVC environment
for both MQB and CMake/Ninja, and make each measured MQB transition prove its
expected cache behavior rather than accepting a correct executable after an
unexpected rebuild.

## Acceptance criteria

1. A poisoned launching shell cannot leak stale MSVC include/library metadata
   into the benchmark's effective toolchain environment.
2. Both build systems still run under the same imported x64 MSVC environment.
3. MQB timing evidence is captured for every sample.
4. `ordinary-no-op` and `pch-no-op` have zero compile misses and a link hit.
5. A single-TU mutation rebuilds only the intended ordinary consumer set.
6. Shared-header and PCH-header transitions rebuild the expected dependent set.
7. The 8-TU correctness fixture and the formal 100-TU x 5 run both complete,
   execute every output program successfully, and retain raw JSON evidence.
8. Existing bilingual methodology remains aligned with the actual harness.

## Non-goals

- Do not tune MQB product code before the corrected benchmark identifies a
  remaining product bottleneck.
- Do not add performance pass/fail thresholds based on hosted-runner wall time.
- Do not claim MQB universally outperforms CMake/Ninja.
- Do not change compiler/linker policy, TU contents, or the comparison matrix.

## Constraints

- Windows x64 and MSVC only.
- Keep CMake configure time separate and keep timed Ninja invocation direct.
- Preserve alternating execution order and identical source trees.
- Use a small, reviewable change set with regression evidence before and after.

## Resolution evidence

- The 8-TU red run failed at `ordinary-no-op` with eight unexpected compile misses.
- After process-local MSVC isolation, all eight scenarios satisfied their exact
  compile/link hit and miss contracts.
- The corrected 100-TU baseline exposed about 78 ms in duplicate-source
  resolution. The implementation compared every source with every previous
  source through `std::filesystem::equivalent()`.
- Replacing that quadratic physical probe with the repository's authoritative
  Windows path-identity key reduced invocation resolution to about 1.3 ms.
- In the final five-iteration run, ordinary no-op improved from 102.73 ms to
  28.41 ms and ordinary single-TU rebuild improved from 169.74 ms to 97.38 ms.
- Debug self-build and all four isolated native-test shards passed (77/77).

Cold and PCH-header builds remain slower than direct Ninja build timings. The
reports keep that result visible; this change does not claim those gaps are
resolved or that MQB is universally faster.
