# Benchmark environment correction and evidence-driven optimization

## Decision

Use a staged correction. First remove only confirmed stale global MSVC/SDK
entries after exporting a registry backup. Then make the benchmark construct a
clean process-local x64 MSVC environment and verify MQB cache transitions from
its JSON timings. Rerun the formal benchmark before selecting any product-code
optimization.

This is preferred over two alternatives:

- changing only the local machine would leave the public harness vulnerable on
  other developer machines and CI images;
- immediately tuning MQB would optimize data produced by a contaminated test
  environment and could hide a correctness regression.

## Contract

Objective: produce a trustworthy 100-TU comparison, then reduce a remaining
measured MQB product bottleneck without weakening incremental correctness.

Gap: the machine has persistent paths for removed VS2022/SDK installations, and
the harness layers VS18 on top of those values without asserting cache behavior.

Scope: Windows environment cleanup, benchmark environment isolation and timing
evidence, one evidence-selected MQB optimization, focused tests, documentation,
and full native verification.

Done when: stale persistent values are backed up and an administrator cleanup
script is ready for the machine-level values that this process cannot remove; the harness proves
the expected transition counts; corrected 100-TU x 5 before/after reports exist;
the selected optimization has a semantic regression test plus retained
before/after performance evidence; all relevant native and documentation gates
pass.

## Stage 1: recoverable machine correction

Export the machine and user environment registry keys to the task outputs. Remove
only nonexistent MSVC/SDK/Lua values from machine `INCLUDE`, `LIB`,
`VCINSTALLDIR`, and the three confirmed stale compiler/SDK `PATH` entries. Remove
the one nonexistent Qt CMake entry from user `PATH`. Do not add version-pinned
MSVC paths globally; Visual Studio developer environments remain the authority.

Verify the registry no longer contains those paths. Existing processes may keep
their inherited copy until restarted, so later tests must not depend on the app
being relaunched.

## Stage 2: benchmark red/green correction

Extend the harness to capture MQB `--timings=json` data for every sample and
validate transition semantics. The first red run will execute under a deliberately
poisoned launching environment and demonstrate that the current harness accepts
unexpected no-op rebuilds.

The minimal implementation will snapshot relevant process variables, clear stale
MSVC metadata before calling `VsDevCmd.bat`, import one x64 environment, and
restore the caller's process variables in `finally`. Both MQB and Ninja continue
to receive the same effective environment.

Expected transition contracts:

- ordinary no-op: all TUs compile-hit and link-hit;
- ordinary single-TU: exactly one compile miss;
- ordinary public-header: every ordinary TU that includes the header misses;
- PCH no-op: creator and consumers hit and link hits;
- PCH single-TU: exactly one consumer miss;
- PCH header: creator and consumers rebuild.

The report will preserve raw timing/cache evidence without introducing wall-time
performance thresholds.

## Stage 3: corrected measurement

Build the current Release MQB from `main`, run the 8-TU correctness fixture, then
run 100 TUs, five iterations, and eight workers. Keep exact tool hashes,
environment metadata, raw samples, transition evidence, and comparison medians.
Treat the earlier contaminated report as diagnostic evidence, not a baseline for
product claims.

## Stage 4: product optimization

Use the corrected MQB timing breakdown to select the largest repeatable product
cost. Add the smallest semantic regression test around the optimized path-identity
contract, preserve deterministic before/after timing reports as the performance
regression evidence, implement one focused change, and rerun the same measurement.
Do not combine unrelated cleanup or relax freshness checks to gain benchmark time.

Likely candidates are persistent toolchain-cache startup validation or per-TU
warm-cache filesystem work, but the corrected report decides; neither is assumed
in advance.

## Validation and rollback

Validation proceeds from focused PowerShell harness checks to the corrected
formal benchmark, current-MQB self-build, selected native tests, full native test
suite, bilingual documentation parity, and git diff/status audit.

Machine rollback uses the exported registry files. Repository rollback is a
single focused branch diff. No merge, push, pull request, or release is authorized
by this plan.

## Execution result

Stages 2-4 completed. The harness now filters inherited Visual Studio/SDK paths
and MSVC option-injection variables, imports and verifies one selected x64
toolchain, restores its process environment, captures MQB JSON timing records,
and fails on incorrect cache transitions. A deliberately poisoned PATH/CL/LINK
launch passed all eight scenarios and restored the caller environment. Corrected,
same-policy before/after 100-TU x 5 reports were retained.

The selected product optimization is duplicate-source validation in invocation
resolution: an O(n²) sequence of physical filesystem comparisons was replaced
with O(n) membership in the shared Windows path-identity domain. A case-alias
duplicate-source E2E contract covers the user-visible rejection behavior.

For Stage 1, registry backups were exported. The stale user Qt CMake path was
removed and verified. The current process could not write the machine environment
key without elevation, so the task output includes an administrator cleanup
script that re-exports backups and removes only the confirmed nonexistent
machine entries. Repository and benchmark verification do not depend on running
that script because they establish a clean process-local environment.
