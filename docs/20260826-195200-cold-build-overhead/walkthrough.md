# Walkthrough - Reduce Ordinary and PCH Cold-Build Overhead

## Overview

We addressed Issue #129 by optimizing cold-build toolchain discovery in MQB. MQB now validates an ambient Visual Studio developer environment with one `vswhere.exe` query and adopts it without rerunning `vcvarsall.bat`. This preserves installation trust, freshness, explicit discovery overrides, and cache identity while removing the dominant redundant cold-start cost.

## Problem & Root Cause

In the initial 100-TU x 5 baseline benchmark:
- `ordinary-cold`: MQB took **2503.64 ms** (vs CMake+Ninja 754.21 ms).
- `pch-cold`: MQB took **2853.43 ms** (vs CMake+Ninja 904.31 ms).

Instrumentation via MQB `--timings=json` proved that:
- In `ordinary-cold`, actual compilation of 101 translation units across 32 workers took only **537.48 ms**, and linking took **38.40 ms**.
- The remaining **~1835 ms** (~76% of total wall time) was spent before compilation, dominated by `MsvcToolchainLocator::discover()` and its `vcvarsall.bat` environment capture. The final after report retains the same compiler/toolchain/harness identity and reduces that unaccounted startup portion without excluding it from MQB wall time.

## Solution

1. **Ambient Visual Studio Adoption (`adopt_ambient_visual_studio_toolchain`)**:
   - When a project build runs in an active Visual Studio developer environment (such as Developer PowerShell, `VsDevCmd.bat`, VS Code terminal, CI runner, or the benchmark harness), `MsvcToolchainLocator::discover` checks whether the ambient environment is complete and trusted for the requested target architecture before spawning child processes.
   - Rigorously validates:
     - Directory existence and version name of `VCToolsInstallDir`.
     - Exact host and target architecture markers (`VSCMD_ARG_HOST_ARCH` and `VSCMD_ARG_TGT_ARCH`).
     - The ambient MSVC directory is the latest toolset under the Visual Studio installation returned by `vswhere.exe`.
     - Required tools (`cl.exe`, `link.exe`, `lib.exe`) verified as existing regular files.
     - `INCLUDE`, `LIB`, `LIBPATH`, `PATH` presence and cacheability.
     - All include/lib paths are inside the registered Visual Studio root or OS-derived Windows/Windows Kits roots; ambient variables cannot declare their own trust roots.
     - Windows SDK and UCRT version freshness against the machine's installed SDKs (`selected_sdk_version_is_latest`).
   - Explicit `vswhere_path` and `cmd_path` remain authoritative and disable ambient adoption.
   - If valid, adopts the ambient environment as `MsvcToolchain`, saves the project toolchain cache (`.mqb/cache/toolchain/msvc-vs-x64-x64.mqbcache`), and seals the compiler environment identity.
   - If ambient environment is missing, invalid, or architecturally mismatched, safely falls back to standard `vswhere` + `vcvarsall.bat` discovery.

2. **Deterministic Unit Test Coverage**:
   - Added test cases in `cpp/tests/msvc/toolchain/visual_studio_tests.cpp`:
     - Valid ambient Visual Studio environment performs exactly one `vswhere` validation and no `vcvarsall.bat` capture.
     - Missing or mismatched host/target markers reject adoption.
     - Explicit discovery overrides remain authoritative.
     - Unregistered Visual Studio roots, fake SDK roots, untrusted include roots, and stale toolsets reject adoption.
     - The emitted project cache is validated and reusable with zero subprocesses.

## Benchmark Results (100-TU x 5 Iterations)

Both runs were executed on the same machine under identical conditions with 100 translation units, 5 iterations, and full cache-transition verification:

| Scenario | Before MQB Median | After MQB Median | After Ninja Median | MQB vs Ninja Delta | MQB Delta % | Improvement vs Before |
|---|---|---|---|---|---|---|
| `ordinary-cold` | 2503.64 ms | **686.00 ms** | 792.07 ms | **-106.07 ms** | **-13.39%** | **72.60%** (-1817.64 ms) |
| `pch-cold` | 2853.43 ms | **1018.69 ms** | 996.74 ms | +21.96 ms | +2.20% | **64.30%** (-1834.73 ms) |
| `ordinary-single-tu` | 181.73 ms | **97.55 ms** | 124.78 ms | **-27.23 ms** | **-21.82%** | **46.32%** |
| `ordinary-public-header` | 641.50 ms | **523.74 ms** | 764.73 ms | **-240.99 ms** | **-31.51%** | **18.36%** |
| `pch-single-tu` | 175.67 ms | **98.98 ms** | 119.42 ms | **-20.44 ms** | **-17.11%** | **43.66%** |
| `pch-header` | 1092.35 ms | **893.20 ms** | 953.66 ms | **-60.46 ms** | **-6.34%** | **18.23%** |
| `ordinary-no-op` | 104.35 ms | **26.97 ms** | 34.45 ms | **-7.49 ms** | **-21.72%** | **74.15%** |
| `pch-no-op` | 108.62 ms | **30.58 ms** | 34.48 ms | **-3.90 ms** | **-11.30%** | **71.84%** |

The full reports contain 80 raw samples each and exact cache-transition counters for all eight scenarios: [`benchmark-before.json`](benchmark-before.json) and [`benchmark-after.json`](benchmark-after.json). The external CMake, Ninja, `cl.exe`, Visual Studio, SDK, machine, and harness identities match. The candidate MQB hash differs by design. The remaining PCH-cold gap is 21.96 ms (2.20%) on this machine; no cross-machine performance guarantee is claimed.

## Verification Gates

1. **C++ Responsibility Layout**:
   `tests/native/assert_cpp_layout.ps1 -CppRoot cpp` passed.
2. **MQB Self-Builds**:
   - Debug: `native-dev/debug/mqb.exe` built successfully.
   - Release: `native-dev/release/mqb.exe` built successfully.
3. **Full Native Test Suite**:
   `develop.ps1` executed all 77 test suites with 77/77 tests passing.
4. **Bilingual Documentation**:
   `tests/docs/verify_bilingual_docs.ps1` verified 17 maintained documentation pairs.
5. **Exact Cache Transition Enforcement**:
   All 8 scenario transitions across 5 iterations in `benchmark_build_systems.ps1` verified exact compile/link cache counts.
6. **MSVC Parameter Inventories**:
   The exact inventory covered 309 compiler, 114 linker, and 21 librarian options (444 total); the semantic inventory covered 44 concrete high-risk variants across 7 risk classes.
7. **Release Self-Host Closure**:
   A clean Stage 0 -> Stage 1 -> Stage 2 Release self-host completed successfully. Runtime-only package staging and exact package validation remain enforced by the Native Release PR gate.
