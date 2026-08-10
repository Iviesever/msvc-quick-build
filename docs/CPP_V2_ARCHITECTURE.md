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
  toolchain discovery
  cl.exe / link.exe argument builders
  /sourceDependencies
  /scanDependencies
```

## Architectural rules

1. **Core does not know `cl.exe`.** MSVC options such as `/O2`, `/MD`, `/scanDependencies`, and `/MACHINE:X64` belong to the backend.
2. **Planner does not execute.** `BuildPlanner` consumes resolved inputs and cache decisions and only creates typed actions.
3. **Correctness beats cache hit rate.** Uncertain cache state rebuilds.
4. **No internal shell command API.** Executable and argv remain separate until the final platform adapter.
5. **UTF-8 at the process abstraction boundary.** Windows converts textual argv/environment data to UTF-16 immediately before `CreateProcessW`.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the compiler recipe identity. Its versioned v1 fingerprint includes normalized source identity, translation-unit kind, toolchain identity, configuration, architecture, language standard, ordered defines, ordered include paths, and ordered extra compiler arguments.

Dependency membership/timestamps and artifact locations are intentionally outside that fingerprint:

```text
compiler recipe identity ----> BuildSignature
source/header freshness ----> CompileCacheValidator
artifact placement ----------> Artifact / cache storage
```

`CompileCacheValidator` is pure. It receives file snapshots and cached metadata and returns typed `BuildReason` values. It never touches the filesystem or launches a process.

## Dependency graph

`DependencyGraph` stores `node depends on dependency` edges. Nodes must exist before edges are added. Duplicate nodes and missing references are errors, duplicate edges are idempotent, topological levels are deterministic, and cycles fail explicitly with the unresolved node set.

These levels will later map to safe parallel module compilation.

## Planner boundary

For compile planning:

```text
CompilePlanItem
    +-- reusable cache ------> no action
    +-- stale cache ---------> CompileAction
                                  source
                                  one object artifact
                                  typed rebuild reasons
```

A stale translation unit with zero or multiple object artifacts is a planning error. The executor therefore receives actions rather than policy decisions.

## Process boundary

`mqb_process` defines a platform-neutral contract:

```text
ProcessSpec
  executable: filesystem::path
  arguments: vector<string>       // argv[1..]
  working_directory: optional path
  environment: name/value overrides
  inherit_environment: bool
  capture_stdout/stderr: bool

ProcessRunner::run(ProcessSpec)
  -> expected<ProcessResult, ProcessError>
```

On Windows, `CreateProcessW` forces argv back into one mutable command-line buffer. That conversion is isolated in `mqb_platform_windows` and tested separately: complex arguments are encoded using Microsoft-compatible backslash/quote rules and round-tripped through `CommandLineToArgvW`.

`WindowsProcessRunner` then provides the real launch layer. It currently handles:

- UTF-8 argv/environment conversion to UTF-16;
- inherited or explicitly isolated Unicode environment blocks;
- working directory;
- stdout and stderr capture through separate pipes;
- concurrent pipe draining so large compiler diagnostics cannot deadlock the child;
- child exit codes as data rather than launch failures;
- native Windows error codes for launch/wait/I/O failures;
- RAII ownership of process, thread, and pipe handles.

Before final cutover we should harden inherited-handle isolation with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`; the current runner limits handles it creates but still uses `bInheritHandles=TRUE` while captured streams are active.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest. ✅
2. **Pure core** — artifacts, signatures, plan model, dependency graph, cache validation, compile planner. ✅
3. **Process abstraction** — typed process spec, Windows quoting, real Win32 runner. **In progress / verification.**
4. **MSVC toolchain backend** — discover toolchain and capture the VS environment. **Next after Phase 3 is green.**
5. **Ordinary translation units** — compile plus `/sourceDependencies` cache refresh/persistence.
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

The GitHub Windows CI currently uses the Visual Studio 2026 CMake generator.
