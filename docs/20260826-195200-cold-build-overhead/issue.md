# Reduce ordinary and PCH cold-build overhead

## Original intent

Complete Issue #129: reduce MQB-owned overhead on ordinary and PCH cold builds without weakening correctness, freshness, dependency discovery, or artifact ownership.

## Observed problem

In the baseline 100-TU x 5 reproducible benchmark:
- `ordinary-cold`: MQB median was 2503.64 ms vs CMake+Ninja median 754.21 ms (+1749.43 ms gap).
- `pch-cold`: MQB median was 2853.43 ms vs CMake+Ninja median 904.31 ms (+1949.12 ms gap).

Timing breakdown from MQB `--timings=json` on cold builds reveals:
- `ordinary-cold`: compile phase was 537.48 ms (101 TUs compiled across 32 workers), link phase was 38.40 ms, total reported phase time was 576.40 ms, while overall invocation was 2411.19 ms.
- `pch-cold`: compile phase was 996.78 ms, link phase was 37.88 ms, total reported phase time was 1035.17 ms, while overall invocation was 2818.52 ms.

Approximately 1780-1835 ms (~65-76% of cold-build wall time) is spent before target compilation begins, in Visual Studio toolchain discovery (`MsvcToolchainLocator::discover`).

On a cold build (fresh workspace or worktree without an existing `.mqb/cache/toolchain/` entry), `MsvcToolchainLocator::discover()` unconditionally invokes `cmd.exe /c vcvarsall.bat` even when the launching process environment already contains a complete Visual Studio developer environment. The optimization may skip that expensive recapture, but it must still validate the ambient toolset against a machine-registered Visual Studio installation.

## Goal

Enable `MsvcToolchainLocator` to recognize, validate, and adopt a trusted ambient Visual Studio environment for the requested host/target architecture. Retain one `vswhere.exe` installation validation, avoid redundant `vcvarsall.bat` process execution, and preserve all freshness, ownership, explicit-override, and fallback guarantees.

## Acceptance criteria

1. Retain a corrected 100-TU x 5 before report and a same-machine, same-tool-hash, same-environment after report.
2. Record raw samples and MQB phase timings for ordinary-cold and PCH-cold scenarios.
3. Identify the optimized MQB-owned phase with evidence; do not attribute MSVC compile time to MQB without proof.
4. Add deterministic semantic/regression coverage for ambient toolchain adoption, architecture mismatch fallback, untrusted environment fallback, and cache persistence.
5. The benchmark harness continues to enforce exact compile/link cache counts for all eight ordinary/PCH transitions.
6. Debug and Release MQB self-builds, complete native test shards, self-host/package validation, C++ layout, and bilingual documentation gates pass.
7. Document the achieved improvement and any remaining gap to direct Ninja without overstating the result.

## Non-goals

- Weakening dependency, toolchain, or include-freshness checks.
- Bypassing validation of ambient environment variables (untrusted or corrupted environments must fail open to discovery).
- Writing any user-global state outside project `.mqb/`.
- Claiming cross-machine performance guarantees from one workstation.
