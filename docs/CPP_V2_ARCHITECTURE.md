# C++ V2 Architecture

This document defines the architecture for migrating MSVC Quick Build from the PowerShell implementation to a typed C++23 build tool.

## Goals

- Keep the PowerShell implementation as the behavioral reference until parity is proven.
- Separate build policy from compiler/linker/process execution.
- Keep MSVC-specific spellings out of the core model.
- Make compile and link cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.
- Prefer a conservative rebuild/relink over reusing uncertain artifacts.

## Layers

```text
CLI (mqb.exe)
    |
    v
Orchestration
  load -> snapshot -> validate -> plan -> execute -> persist
    |
    +--------------------+
    v                    v
Core                  MSVC backend
  BuildRequest           MsvcToolchainLocator
  Artifact / TU          MsvcCompiler
  BuildSignature         MsvcCompileExecutor
  CompileCache           MsvcLinker
  LinkCache              /sourceDependencies reader
  DependencyGraph             |
  BuildPlanner                v
    |                    Process abstraction
    |                      ProcessSpec { executable, argv, cwd, env }
    |                           |
    +---------------------------+
                                v
                         Windows platform
                         WindowsProcessRunner
                         CreateProcessW
```

## Architectural rules

1. **Core does not know `cl.exe` or `link.exe`.** MSVC switches and schemas belong to the backend.
2. **Planner does not execute.** `BuildPlanner` only creates typed actions.
3. **Correctness beats cache hit rate.** Uncertain cache state rebuilds or relinks.
4. **No internal shell command API.** Executable and argv remain separate until the Windows adapter builds the `CreateProcessW` command line.
5. **UTF-8 at the process abstraction boundary.** Windows converts textual argv/environment data to UTF-16 immediately before `CreateProcessW`.
6. **Compile state and link state are independent.** A compile cache hit does not imply a link cache hit, and link-only changes must never force unnecessary compilation.
7. **A fresh compile is an explicit relink signal.** Linking must not depend solely on filesystem timestamp granularity.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the versioned compiler recipe identity: source/unit identity, compiler identity, configuration, architecture, language standard, ordered defines, ordered include paths, and ordered extra arguments.

```text
compiler recipe identity ----> BuildSignature
source/header freshness ------> CompileCacheValidator
artifact placement -----------> Artifact / cache storage
```

`CompileCacheValidator` is pure and returns typed `BuildReason` values; it does not touch the filesystem or launch a process.

`CompileCacheFile` persists compile metadata in a versioned binary format. Missing metadata is a normal cold-cache condition; corrupt/truncated/unsupported metadata is rejected and conservatively rebuilt.

## Link identity and cache freshness

Linking has its own state machine rather than piggybacking on object timestamps.

```text
ordered object inputs --------+
link.exe identity ------------+
link options -----------------+--> BuildSignature::for_link
output identity --------------+

output/object freshness ----------> LinkCacheValidator
fresh compile --------------------> force_relink / explicit rebuild
```

`LinkerIdentity` stamps `link.exe` independently from `cl.exe`, so a linker update can invalidate only link state. `LinkCacheValidator` distinguishes link-input changes, linker-option changes, toolchain changes, missing output, and explicit relink.

`LinkCacheFile` uses a separate versioned cache format. Broken link metadata can only cause a safe relink; it must never permit reuse of a stale executable.

## Dependency graph and planner

`DependencyGraph` stores `node depends on dependency` edges. It rejects duplicate/missing nodes, makes duplicate edges idempotent, returns deterministic topological levels, and fails cycles explicitly.

For compile planning:

```text
CompilePlanItem
    +-- reusable cache ------> no action
    +-- stale cache ---------> CompileAction
                                  source
                                  exactly one object artifact
                                  typed rebuild reasons
```

For link planning:

```text
LinkPlanItem
    +-- reusable cache ------> no action
    +-- stale cache ---------> LinkAction
                                  ordered object inputs
                                  executable output
                                  typed relink reasons
```

Executors therefore receive actions rather than policy decisions.

## Orchestration

The application-facing coordinators compose existing policy/backend pieces without owning their algorithms.

### Incremental compile

```text
CompileCacheFile
      -> file snapshots
      -> CompileCacheValidator
      -> BuildPlanner::plan_compile
      -> MsvcCompileExecutor
      -> /sourceDependencies reader
      -> CompileCacheFile::save
```

### Incremental link

```text
LinkCacheFile
      -> output/object snapshots
      -> LinkCacheValidator
      -> BuildPlanner::plan_link
      -> MsvcLinker
      -> LinkCacheFile::save
```

Ordinary cache-load/save failures are surfaced as warnings and conservatively rebuild/relink. Compiler/linker execution failures are build errors.

## Process boundary

`mqb_process` defines a platform-neutral `ProcessSpec` with executable, argv, working directory, structured environment overrides, inheritance choice, and stdout/stderr capture controls.

`mqb_platform_windows` isolates Windows command-line quoting and the real `CreateProcessW` runner. Tests cover complex argv round-trips, explicit/inherited Unicode environment blocks, separate stdout/stderr capture, non-zero child exit codes, native launch errors, and concurrent draining of large output streams.

The current runner uses RAII for process/thread/pipe handles. Before final cutover, handle inheritance should be hardened further with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`.

## MSVC toolchain boundary

`MsvcToolchainLocator` preserves the two environment tracks from the PowerShell reference:

```text
automatic
   |
   +-- first existing portable_msvc root --> portable layout discovery
   |
   +-- otherwise --------------------------> Visual Studio discovery

forced portable --> portable only
forced VS -------> Visual Studio only
```

### Portable track

The locator selects the latest VC Tools and Windows Kit version directories, resolves `cl.exe`, `link.exe`, and `lib.exe` for the requested host/target architecture, creates PATH/INCLUDE/LIB overrides, and stamps the compiler binary for cache identity. Discovery itself launches no subprocess.

### Visual Studio track

The locator first tries the standard `vswhere.exe` location, then known VS 2026/2022 edition directories as a fallback. After finding `vcvarsall.bat`, it captures the initialized environment through an isolated temporary batch wrapper and `cmd.exe /u`, so the environment dump is parsed as UTF-16 rather than depending on the active console code page.

The captured environment is returned as structured name/value overrides; the locator does not mutate MQB's own process environment. `VCToolsInstallDir` is used to resolve the selected compiler/linker/librarian binaries.

GitHub CI enables installed-MSVC integration tests explicitly; local tests keep those tests opt-in so a portable-only development machine is not forced to have a registered Visual Studio installation.

## Current CLI milestone

`mqb.exe` currently supports one ordinary C++ translation unit (`.cpp`, `.cc`, `.cxx`) and produces collision-free internal artifacts under a source-local `.mqb/` directory:

```text
.mqb/
  obj/    <source filename>.obj
  deps/   compiler dependency JSON
  cache/  compile cache + link cache
  bin/    <source filename>.exe
```

The CLI composes the incremental compile coordinator followed by the incremental link coordinator. Warm builds can therefore skip both compiler and linker independently, while a fresh compile explicitly forces relink even when filesystem timestamps are ambiguous.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest. ✅
2. **Pure core** — artifacts, signatures, plan model, dependency graph, compile cache validation/planning. ✅
3. **Process abstraction** — typed process spec, Windows quoting, Win32 runner. ✅
4. **MSVC toolchain backend** — portable discovery plus `vswhere/vcvarsall` environment capture. ✅
5. **Ordinary single TU** — typed compiler arguments, `/sourceDependencies`, cache persistence, real incremental CLI. ✅
6. **Link state** — independent linker identity/signature/cache/planning/backend/orchestration and executable production. ✅
7. **Multi-TU and target CLI** — source discovery, multiple objects, output naming, libraries, run mode, project-level artifact layout.
8. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts, `import std`.
9. **Parity** — run PowerShell and C++ against the same E2E fixtures.
10. **Cutover** — make `mqb.exe` primary only after parity tests pass.

## Local build

```powershell
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

To opt into tests that require a registered Visual Studio installation:

```powershell
cmake -S cpp -B cpp/build -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
```

GitHub Windows CI currently uses the Visual Studio 2026 CMake generator and enables those integration tests.
