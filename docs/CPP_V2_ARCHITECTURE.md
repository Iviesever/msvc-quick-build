# C++ V2 Architecture

This document defines the architecture for migrating MSVC Quick Build from the PowerShell implementation to a typed C++23 build tool.

## Goals

- Keep the PowerShell implementation as the behavioral reference until parity is proven.
- Separate build policy from compiler/process execution.
- Keep MSVC-specific spellings out of the core model.
- Make cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.

## Layers

```text
CLI (mqb.exe)
    |
    v
Core
  BuildRequest
  Artifact / TranslationUnit
  BuildSignature
  CompileCacheValidator
  DependencyGraph
  BuildPlanner -> BuildPlan / BuildAction
    |
    v
Process abstraction
  ProcessSpec { executable, argv, cwd, env }
  ProcessRunner -> expected<ProcessResult, ProcessError>
    |
    v
Platform implementation
  WindowsProcessRunner / CreateProcessW
    |
    v
MSVC backend
  MsvcToolchainLocator
  portable_msvc layout | vswhere -> vcvarsall -> environment
  cl.exe / link.exe argument builders (next)
  /sourceDependencies (later)
  /scanDependencies (later)
```

## Architectural rules

1. **Core does not know `cl.exe`.** MSVC flags belong to the backend.
2. **Planner does not execute.** `BuildPlanner` only creates typed actions.
3. **Correctness beats cache hit rate.** Uncertain cache state rebuilds.
4. **No internal shell command API.** Executable and argv remain separate until a platform adapter absolutely requires another representation.
5. **UTF-8 at the process abstraction boundary.** Windows converts textual argv/environment data to UTF-16 immediately before `CreateProcessW`.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the versioned compiler recipe identity: source/unit identity, compiler identity, configuration, architecture, language standard, ordered defines, ordered include paths, and ordered extra arguments.

```text
compiler recipe identity ----> BuildSignature
source/header freshness ----> CompileCacheValidator
artifact placement ----------> Artifact / cache storage
```

`CompileCacheValidator` is pure and returns typed `BuildReason` values; it does not touch the filesystem or launch a process.

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

The executor therefore receives actions rather than policy decisions.

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

GitHub CI enables an installed-MSVC integration test explicitly; local tests keep that test opt-in so a portable-only development machine is not forced to have a registered Visual Studio installation.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest. ✅
2. **Pure core** — artifacts, signatures, plan model, dependency graph, cache validation, compile planner. ✅
3. **Process abstraction** — typed process spec, Windows quoting, Win32 runner. ✅
4. **MSVC toolchain backend** — portable discovery plus `vswhere/vcvarsall` environment capture. **In progress / verification.**
5. **Ordinary translation units** — typed compiler arguments, compile execution, `/sourceDependencies`, cache persistence/refresh.
6. **Link state** — explicit linker signature and link planning.
7. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts.
8. **Parity** — run PowerShell and C++ against the same E2E fixtures.
9. **Cutover** — make `mqb.exe` primary only after parity tests pass.

## Local build

```powershell
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

To opt into the installed Visual Studio integration test:

```powershell
cmake -S cpp -B cpp/build -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
```

GitHub Windows CI currently uses the Visual Studio 2026 CMake generator and enables that integration test.
