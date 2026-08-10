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
Target orchestration
  N x compile coordinator -> collect objects -> one link coordinator
    |
    +--------------------+
    v                    v
Core                  MSVC backend
  BuildRequest           MsvcToolchainLocator
  Artifact / TU          MsvcCompiler
  ProjectArtifactLayout  MsvcCompileExecutor
  BuildSignature         MsvcLinker
  CompileCache           /sourceDependencies reader
  LinkCache                    |
  DependencyGraph              v
  BuildPlanner           Process abstraction
    |                     ProcessSpec { executable, argv, cwd, env }
    |                          |
    +--------------------------+
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
8. **Source identity is not a basename.** Project artifacts preserve relative source identity; external sources receive a stable hashed namespace, and duplicate object mappings are rejected before execution.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the versioned compiler recipe identity: source/unit identity, compiler identity, configuration, architecture, language standard, ordered defines, ordered include paths, and ordered extra arguments.

```text
compiler recipe identity ----> BuildSignature
source/header freshness ------> CompileCacheValidator
artifact placement -----------> ProjectArtifactLayout
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

When user-facing library support is added, resolved library-file freshness must join the link input state. Library names and search paths in the signature alone are not sufficient to detect an updated `.lib` file.

## Project artifact layout

The current explicit-target CLI uses the process working directory as the project root. Sources inside that root preserve their relative path; sources outside it are isolated under `.external/<stable-path-hash>/`.

For example:

```text
project/
  main.cpp
  src/foo.cpp
  tests/foo.cpp
  .mqb/
    obj/
      main.cpp.obj
      src/foo.cpp.obj
      tests/foo.cpp.obj
    deps/
      main.cpp.json
      src/foo.cpp.json
      tests/foo.cpp.json
    cache/
      compile/...
      link/main.linkcache
    bin/
      main.exe
```

This prevents `src/foo.cpp`, `tests/foo.cpp`, `foo.cpp`, and `foo.cxx` from aliasing one object/cache artifact.

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

### Incremental target

```text
ordered source requests
      -> compile each TU with IncrementalCompileCoordinator
      -> collect collision-free object artifacts
      -> any TU compiled? ---- yes ----> force_relink
      -> IncrementalLinkCoordinator
      -> one target executable
```

`MsvcIncrementalTargetCoordinator` currently schedules translation units sequentially. This is deliberate: correctness and stable target semantics are established before parallel scheduling is introduced. Parallel compilation can later replace the scheduling strategy without moving compiler/linker policy into the CLI.

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

The portable track resolves VC Tools and Windows Kit layout without launching a subprocess. The Visual Studio track uses `vswhere`/fallback discovery and captures `vcvarsall.bat` output through an isolated UTF-16 environment dump. The captured environment is returned as structured overrides rather than mutating MQB's own process environment.

GitHub CI enables installed-MSVC integration tests explicitly; local tests keep those tests opt-in so a portable-only development machine is not forced to have a registered Visual Studio installation.

## Current CLI milestone

`mqb.exe` now supports an **explicit ordered set of ordinary C++ translation units** (`.cpp`, `.cc`, `.cxx`):

```powershell
mqb main.cpp src/utils.cpp a/helper.cpp b/helper.cpp
```

The first source currently supplies the default target name (`main.cpp` -> `main.exe`). Every source is incrementally validated independently; only stale TUs compile, and all resulting objects feed one independently cached link action.

The real VS2026 E2E suite verifies:

- cold multi-TU compile + link + executable launch;
- warm build with zero compiler and linker actions;
- same-basename sources in different directories have distinct artifacts;
- a header private to one TU rebuilds only that TU;
- any rebuilt TU explicitly forces relink;
- the resulting executable behavior changes after the partial rebuild;
- Debug -> Release invalidates compile and link recipes without source edits.

Not yet exposed in the C++ CLI: automatic source discovery, `-o/--output`, `--run`, user libraries/library paths, project config files, and C++ Modules.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest. ✅
2. **Pure core** — artifacts, signatures, plan model, dependency graph, compile cache validation/planning. ✅
3. **Process abstraction** — typed process spec, Windows quoting, Win32 runner. ✅
4. **MSVC toolchain backend** — portable discovery plus `vswhere/vcvarsall` environment capture. ✅
5. **Ordinary single TU** — typed compiler arguments, `/sourceDependencies`, cache persistence, real incremental CLI. ✅
6. **Link state** — independent linker identity/signature/cache/planning/backend/orchestration and executable production. ✅
7. **Explicit multi-TU target** — project artifact layout, ordered source set, per-TU incremental compile, single incremental link. ✅
8. **Target UX and discovery** — `-o`, `--run`, libraries, project config, smart source discovery, then parallel compile scheduling.
9. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts, `import std`.
10. **Parity** — run PowerShell and C++ against the same E2E fixtures.
11. **Cutover** — make `mqb.exe` primary only after parity tests pass.

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
