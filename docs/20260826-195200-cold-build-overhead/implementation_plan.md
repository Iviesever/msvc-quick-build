# Implementation Plan - Reduce Ordinary and PCH Cold-Build Overhead

## Decision

Implement ambient Visual Studio toolchain adoption in `MsvcToolchainLocator`. When a cold build runs in an environment where a valid Visual Studio developer toolset is already active, MQB validates it with one `vswhere.exe` query and adopts it without rerunning `vcvarsall.bat`. Adoption requires matching host and target architecture markers, the latest registered MSVC toolset, verified binaries, and machine-owned SDK/MSVC roots.

## Contract

- **Objective**: Eliminate ~1.8 s of redundant `vcvarsall.bat` startup overhead on cold builds in active MSVC developer environments without weakening cache identity or security validation.
- **Scope**:
  1. `cpp/src/msvc/toolchain/VisualStudioToolchainDiscovery.hpp` & `.cpp`: Expose registered-installation resolution and derive default discovery locations from the Windows installation rather than mutable `ProgramFiles` variables.
  2. `cpp/src/msvc/toolchain/VisualStudioToolchainCache.hpp` & `.cpp`: Validate and materialize ambient toolchain evidence using the same cache/freshness authority.
  3. `cpp/src/msvc/toolchain/MsvcToolchainLocator.cpp`: Query ambient adoption if project cache is absent/misses, before falling back to `discover_visual_studio_toolchain`.
  4. `cpp/tests/msvc/toolchain/visual_studio_tests.cpp`: Add deterministic tests for adoption, host/target mismatch, missing markers, explicit overrides, stale toolsets, untrusted VS/SDK roots, and cache emission.
  5. Retain corrected before/after 100-TU x 5 raw reports and run the complete release gates.

## Proposed Changes

### Toolchain Discovery Layer

#### [MODIFY] `cpp/src/msvc/toolchain/VisualStudioToolchainCache.hpp` and `.cpp`
- Implement `adopt_ambient_visual_studio_toolchain(process::ProcessRunner&, const DiscoveryOptions&)`:
  - Inspect `environment_path("VCToolsInstallDir")`.
  - Require exact `VSCMD_ARG_HOST_ARCH` and `VSCMD_ARG_TGT_ARCH` matches.
  - Reject ambient adoption when an explicit `vswhere_path` or `cmd_path` is supplied.
  - Validate `vc_tools_root` as the latest toolset beneath the installation returned by `vswhere`.
  - Verify compiler, linker, and librarian exist at `tool_paths(vc_tools_root, options)`.
  - Validate required environment variables (`INCLUDE`, `LIB`, `LIBPATH`, `PATH`) are present and non-empty.
  - Validate include/library paths against the registered Visual Studio root plus Windows and Windows Kits roots derived from the operating system, not from ambient trust declarations.
  - Compute `binary_stamp(compiler)`.
  - Return a Visual Studio `MsvcToolchain` with `reused = false`.

#### [MODIFY] `cpp/src/msvc/toolchain/VisualStudioToolchainDiscovery.hpp` and `.cpp`
- Expose `locate_visual_studio_installation` for the ambient validation path.
- Derive default `vswhere.exe` and Visual Studio fallback roots from `GetWindowsDirectoryW` rather than mutable process environment variables.

#### [MODIFY] `cpp/src/msvc/toolchain/MsvcToolchainLocator.cpp`
- In `MsvcToolchainLocator::discover`:
  - After checking cache file reuse (if absent/miss), check `adopt_ambient_visual_studio_toolchain(runner_, options)`.
  - If ambient toolchain is adopted:
    - Save project toolchain cache if `cache_file` is specified (so subsequent warm runs use the cached record).
    - Seal compiler environment identity and return.
  - If ambient adoption is not available/valid, fall back to `discover_visual_studio_toolchain(runner_, options)`.

### Test Layer

#### [MODIFY] `cpp/tests/msvc/toolchain/visual_studio_tests.cpp`
- Add tests verifying:
  1. Valid ambient Visual Studio environment is adopted after exactly one `vswhere` validation call and does not invoke `vcvarsall.bat`.
  2. Missing/mismatched host or target architecture markers fall back to ordinary discovery.
  3. Explicit `vswhere_path` and `cmd_path` remain authoritative.
  4. Unregistered VS roots, self-declared SDK roots, untrusted include roots, and stale MSVC toolsets are rejected.
  5. Cache persistence after ambient adoption matches the expected cache format and is reusable with zero subprocesses.

### Benchmark & Documentation Layer

#### [MODIFY] `docs/BUILD_SYSTEM_BENCHMARK.md` and `docs/BUILD_SYSTEM_BENCHMARK_ZH.md`
- Update cold-build performance evidence and analysis.

## Verification Plan

### Automated Tests
1. Native visual studio toolchain tests:
   ```powershell
   & tests/native/develop.ps1
   ```
2. Sharded native test suites (all 77/77 tests across 8 shards):
   ```powershell
   & tests/native/run_native_tests.ps1 -BuilderMqbPath ... -TestMqbPath ... -RepoRoot .
   ```
3. C++ layout and bilingual docs validation:
   ```powershell
   & tests/native/assert_cpp_layout.ps1 -CppRoot cpp
   & tests/docs/verify_bilingual_docs.ps1
   ```
4. Full 100-TU x 5 benchmark comparison:
   ```powershell
   pwsh -File tests/native/benchmark_build_systems.ps1 -MqbPath <built_mqb> -TranslationUnits 100 -Iterations 5 -OutputPath "$env:TEMP/bench_100_after.json"
   ```
