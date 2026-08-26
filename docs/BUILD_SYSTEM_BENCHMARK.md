# Build-system comparison methodology

**English | [简体中文](BUILD_SYSTEM_BENCHMARK_ZH.md)**

MQB includes a reproducible Windows/MSVC harness for comparing MQB with a CMake + Ninja build of the same generated source tree. The purpose is to produce inspectable engineering evidence, not a universal claim that one build system is always faster.

The harness is [`../tests/native/benchmark_build_systems.ps1`](../tests/native/benchmark_build_systems.ps1).

## Scope

The initial comparison covers two ordinary MSVC build shapes:

- a multi-translation-unit executable;
- the same style of executable with a precompiled header managed by each build system's first-class PCH mechanism.

Each fixture exercises these transitions:

| Fixture | Scenario | Change |
|---|---|---|
| ordinary | `ordinary-cold` | no prior object/link state |
| ordinary | `ordinary-no-op` | immediate unchanged rebuild |
| ordinary | `ordinary-single-tu` | mutate one `.cpp` only |
| ordinary | `ordinary-public-header` | mutate one header included by every TU |
| PCH | `pch-cold` | no prior PCH/object/link state |
| PCH | `pch-no-op` | immediate unchanged rebuild |
| PCH | `pch-single-tu` | mutate one `.cpp` only |
| PCH | `pch-header` | mutate the PCH header |

C++ Modules and Header Units are deliberately outside the first comparison matrix. They have different support boundaries and deserve a separate methodology instead of being mixed into an ordinary/PCH headline number.

## Fairness rules

The harness is intentionally conservative about what it calls comparable:

1. **Same physical source tree per iteration.** MQB state lives in `.mqb/`; CMake/Ninja state lives in `cmake-build/`.
2. **Same clean MSVC environment.** The harness first resolves the requested benchmark tools, snapshots the caller's process environment, removes inherited Visual Studio/Windows SDK `PATH` entries plus `CL`/`LINK` option-injection variables, imports the latest x64 Visual Studio developer environment with `vswhere` + `VsDevCmd.bat`, and restores the caller's environment in `finally`. It also verifies that `cl.exe` and `link.exe` resolve from the selected Visual Studio installation.
3. **Same explicit parallel ceiling.** `-Jobs N` becomes MQB `-j N` and Ninja `-j N`.
4. **Aligned release compiler/linker policy.** The generated CMake target uses the same core MSVC release flags used by MQB for this fixture: `/utf-8`, `/W3`, `/EHsc`, `/permissive-`, `/Zc:__cplusplus`, `/Zc:preprocessor`, `/diagnostics:column`, `/O2`, `/Oi`, `/MD`, `/Z7`, `/DNDEBUG`, and `/std:c++23preview`; release link policy uses `/INCREMENTAL:NO`, `/OPT:REF`, `/OPT:ICF`, x64, and console subsystem.
5. **Build-system dependency work remains visible.** CMake/Ninja may add its own dependency-tracking arguments and MQB may use its own metadata paths. Those are part of the systems being compared and are not manually removed.
6. **CMake configure time is excluded from build timings.** Configuration is measured and reported separately. `ordinary-cold` and `pch-cold` start after `build.ninja` already exists.
7. **Ninja is invoked directly.** Timed CMake/Ninja samples run `ninja -C ...`, not `cmake --build`, so the CMake command-wrapper overhead is not charged to the comparison.
8. **Execution order alternates.** MQB and Ninja run first on alternating iteration/scenario combinations to reduce systematic first/second-run bias.
9. **Every successful timed build is executed.** The resulting executable must return zero before the sample is accepted.
10. **No hosted-runner threshold.** Results are evidence. The harness never turns a percentage into a pass/fail product-quality claim.

These rules make the comparison intentionally favorable to CMake/Ninja in one important respect: its one-time configure cost is visible in the JSON but excluded from the paired build medians.

## Run locally

Use a Release MQB executable and choose an explicit worker count appropriate for the machine:

```powershell
.\tests\native\benchmark_build_systems.ps1 `
  -MqbPath .\path\to\mqb.exe `
  -Iterations 5 `
  -TranslationUnits 100 `
  -Jobs 8 `
  -OutputPath .\build-system-comparison.json
```

Requirements:

- Windows x64;
- MSVC x64 tools;
- CMake 3.20 or newer;
- Ninja;
- the MQB executable under test.

The script deliberately filters inherited Visual Studio/Windows SDK paths and MSVC option-injection variables before importing one fresh x64 MSVC environment. This prevents removed or mixed installations, or unequal `CL`/`LINK` flags, from contaminating either build path.

Use `-KeepWorktree` when debugging the generated fixtures. Otherwise the temporary fixture tree is removed after the run.

## Output contract

The JSON report records:

- exact MQB, CMake, Ninja, and `cl.exe` paths and SHA-256 identities;
- tool version strings where available;
- Windows / PowerShell / CPU-width / MSVC environment metadata;
- translation-unit count, iterations, jobs, architecture, and configuration;
- every raw build sample with execution order, plus MQB phase timings and compile/link cache counters;
- CMake configure samples and medians, separately from build time;
- per-scenario MQB and CMake/Ninja medians;
- MQB absolute and percentage deltas relative to CMake/Ninja;
- the `cmake+ninja / mqb` median ratio.

For `mqb_delta_pct_vs_cmake_ninja`, a negative value means the MQB sample had lower wall-clock time. The ratio is provided for convenience, but neither field should be quoted without the machine/tool/methodology metadata from the same report.

## Issue #129 measured evidence

Issue #129 used this contract for a same-machine 100-TU, 5-iteration, 32-worker before/after study. CMake 4.1.2, Ninja 1.13.1, MSVC 19.51.36248, the selected Visual Studio installation, Windows SDK, operating system, CPU width, and harness policy were unchanged. The MQB binary hash necessarily changed because the candidate contains the optimization.

| Scenario | Before MQB median | After MQB median | After Ninja median | After MQB delta vs Ninja |
|---|---:|---:|---:|---:|
| `ordinary-cold` | 2503.64 ms | 686.00 ms | 792.07 ms | -13.39% |
| `pch-cold` | 2853.43 ms | 1018.69 ms | 996.74 ms | +2.20% |

The selected product change removes redundant `vcvarsall.bat` environment capture when the caller already has a complete developer environment. It still runs one `vswhere.exe` query to bind ambient paths to a registered Visual Studio installation. On this machine, ordinary cold time fell by 1817.64 ms (72.60%) and PCH cold time fell by 1834.73 ms (64.30%). PCH cold remained 21.96 ms slower than direct Ninja; this is retained as an honest remaining gap, not treated as a threshold failure or a cross-machine claim.

The complete raw reports, including all 80 samples per report, MQB phase timings, tool hashes, and exact cache counters, are retained as [`benchmark-before.json`](20260826-195200-cold-build-overhead/benchmark-before.json) and [`benchmark-after.json`](20260826-195200-cold-build-overhead/benchmark-after.json).

## Issue #130 measured evidence

Issue #130 repeated the same 100-TU, 5-iteration, 32-worker contract for ordinary public-header and configured PCH-header rebuilds. The before/after reports have identical Windows, PowerShell, CPU, Visual Studio, MSVC 19.51.36248, Windows SDK, CMake 4.1.2, Ninja 1.13.1, and harness policy metadata; only the expected MQB candidate hash differs.

| Scenario | Before MQB median | After MQB median | After Ninja median | After MQB delta vs Ninja |
|---|---:|---:|---:|---:|
| `ordinary-public-header` | 560.50 ms | 516.70 ms | 697.13 ms | -25.88% |
| `pch-header` | 874.50 ms | 875.38 ms | 895.90 ms | -2.29% |

The selected product change is semantic and narrow: once the owned PCH creator has successfully rebuilt and the `.pch` exists, every consumer compile is already mandatory. MQB now propagates that authoritative decision to every consumer and skips loading and snapshotting stale consumer cache state, while retaining the normal compiler, dependency-reader, cache-write, and single-link path. Deterministic tests prove creator-before-consumer ordering, all-consumer invalidation, exactly one link/archive, and a following no-op.

The representative median PCH compile phase moved from 815.975 ms to 812.467 ms, but PCH wall-clock moved by only +0.88 ms (+0.10%). That is not a measurable end-to-end speedup on this run and must not be presented as one: required parallel MSVC compilation dominates the scenario and normal workstation variance is larger than the removed MQB bookkeeping. The ordinary public-header path remained correct and materially faster than direct Ninja in the after report. No-op and single-TU samples also retained their exact cache-transition contracts; their small timing movements are treated as measurement variance rather than product claims.

The complete 80-sample reports, raw execution order, MQB phase timings, tool hashes, environment identity, and exact cache counters are retained as [`benchmark-before.json`](20260826-220833-header-rebuild-paths/benchmark-before.json) and [`benchmark-after.json`](20260826-220833-header-rebuild-paths/benchmark-after.json).

## CI contract

`.github/workflows/build-system-evidence.yml` runs a small correctness-sized fixture on relevant pull requests. It builds the current MQB product from the repository, runs one comparison iteration with a small TU count, and uploads the JSON report.

That workflow proves that the comparison harness remains executable, both build paths produce correct programs, and every MQB scenario performs the expected cold/no-op/incremental cache transition. It is **not** a stable performance leaderboard: hosted runner placement, system load, antivirus state, filesystem caches, and VM noise can materially move wall-clock numbers.

For publishable measurements, run multiple iterations on a named physical machine, keep the full JSON, and report the machine and toolchain alongside the medians.
