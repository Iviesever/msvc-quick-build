# C++ V2 Architecture

This document defines the architecture for migrating MSVC Quick Build from the PowerShell implementation to a typed C++23 build tool.

## Goals

- Keep the PowerShell implementation as the behavioral reference until parity is proven.
- Separate source selection, build policy, compiler/linker execution, and process execution.
- Keep MSVC-specific spellings out of the core model.
- Make compile and link cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.
- Prefer a conservative rebuild/relink over reusing uncertain artifacts.

## Layers

```text
CLI (mqb.exe)
    |
    +--> Project Config (mqb_config)
    |      mqb.json locator/parser
    |      CLI > config > defaults resolver
    |
    +--> Source Discovery (mqb_discovery)
    |      ordinary C++ candidate-TU selection
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
  BuildSignature         MsvcLibraryResolver
  CompileCache           MsvcLinker
  LinkCache              /sourceDependencies reader
  DependencyGraph              |
  BuildPlanner                 v
    |                    Process abstraction
    |                    ProcessSpec { executable, argv, cwd, env }
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
9. **Run-time argv is not build identity.** `--run` and arguments after `--` are execution state only; changing them must not invalidate compile or link caches.
10. **Discovery is not dependency freshness.** Smart discovery chooses candidate translation units; MSVC `/sourceDependencies` remains the authority for header freshness.
11. **Configuration is typed policy, not shell text.** `mqb.json` decodes into optional typed overrides before MSVC arguments are produced.

## Project configuration

`mqb_config` owns the versioned `mqb.json` contract. The CLI searches upward from the invocation directory for the nearest `mqb.json`.

Path semantics are intentionally split:

```text
CLI relative path --------> invocation directory
mqb.json relative path ---> directory containing mqb.json
artifact project root ----> directory containing mqb.json (when present)
```

Scalar option precedence is:

```text
built-in defaults
    <- mqb.json fields that are present
        <- explicitly supplied CLI scalar options
```

The CLI parser therefore tracks whether `--debug`, `--release`, `--x86`, `--x64`, `--std`, `--discover`, and `--no-discover` were actually supplied. Parser defaults must never accidentally override project configuration.

List-like build inputs are additive and deterministic: project-config entries appear first, then CLI entries. This currently applies to defines, include directories, library directories, and libraries.

The v1 schema and examples are documented in `docs/MQB_CONFIG.md`. Unknown fields, duplicate JSON keys, wrong field types, malformed JSON, and unsupported schema versions are rejected instead of guessed.

## Smart ordinary-C++ discovery

With one positional source, MQB smart-discovers the connected ordinary-C++ target by default. Multiple positional sources remain an explicit ordered source set. `--no-discover` disables discovery; `--discover` explicitly enables it and can override project configuration.

Discovery uses:

- quoted `#include "..."` connectivity;
- configured include directories;
- same-basename ownership such as `foo.hpp <-> foo.cpp/.cc/.cxx`;
- deterministic traversal from the entry TU;
- built-in directory exclusions (`.mqb`, `.git`, `.vs`, `build`, `out`, `cmake-build-*`);
- project-config `exclude_dirs`, `extra_sources`, and `exclude_sources` exact-path corrections.

A reachable non-entry source defining `main(...)` is a traversal barrier, not merely a final-result filter. An explicitly excluded source is also a traversal barrier, so a test/tool TU cannot bridge the graph into a private subgraph. Configured excluded directories are pruned before graph construction. Explicit extra sources may add disconnected ordinary TUs but may not define another `main()`.

Discovery output only selects TUs. Incremental header invalidation continues to use compiler-emitted `/sourceDependencies` metadata.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the versioned compiler recipe identity: source/unit identity, compiler identity, configuration, architecture, language standard, ordered defines, ordered include paths, and ordered extra arguments.

```text
compiler recipe identity ----> BuildSignature
source/header freshness ------> CompileCacheValidator
artifact placement -----------> ProjectArtifactLayout
```

`CompileCacheValidator` is pure and returns typed `BuildReason` values; it does not touch the filesystem or launch a process.

`CompileCacheFile` persists compile metadata in a versioned binary format. Missing metadata is a normal cold-cache condition; corrupt/truncated/unsupported metadata is rejected and conservatively rebuilt.

A config-only change that alters effective compiler options therefore changes compile identity even when no source timestamp changes.

## Link identity and cache freshness

Linking has its own state machine rather than piggybacking on object timestamps.

```text
ordered object inputs --------+
resolved library identities --+
link.exe identity ------------+
link options -----------------+--> BuildSignature::for_link
output identity --------------+

output/object/library freshness ---> LinkCacheValidator
fresh compile ---------------------> force_relink
```

`LinkerIdentity` stamps `link.exe` independently from `cl.exe`, so a linker update can invalidate only link state.

User-requested libraries are resolved deterministically to exact `.lib` files before invoking `link.exe`. Link cache v2 persists the resolved library inputs, so updating a `.lib` can trigger compile-0/link-1 invalidation. Library names/search paths are recipe state; the resolved files themselves are freshness inputs.

Current boundary: explicitly requested libraries are tracked precisely. Indirect `/DEFAULTLIB` dependencies embedded in objects or libraries are still resolved by `link.exe` and are not claimed as fully tracked transitive link inputs.

## Project artifact layout

Without `mqb.json`, the invocation directory is the project root. With `mqb.json`, its directory becomes the project root even when MQB is launched from a nested directory.

Sources inside the root preserve relative identity; sources outside it are isolated under `.external/<stable-path-hash>/`.

```text
project/
  mqb.json
  main.cpp
  src/foo.cpp
  .mqb/
    obj/
      main.cpp.obj
      src/foo.cpp.obj
    deps/
      main.cpp.json
      src/foo.cpp.json
    cache/
      compile/...
      link/main.linkcache
    bin/
      main.exe
```

This prevents same-basename sources in different directories from aliasing one object/cache artifact.

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
                                  ordered resolved libraries
                                  executable output
                                  typed relink reasons
```

Executors therefore receive actions rather than policy decisions.

## Orchestration

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
requested libraries -> MsvcLibraryResolver -> exact .lib inputs
LinkCacheFile
      -> output/object/library snapshots
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

`MsvcIncrementalTargetCoordinator` still schedules translation units sequentially. Parallel compilation is the next execution-policy milestone; it must not change cache identity, artifact routing, diagnostics ordering, or failure semantics.

Ordinary cache-load/save failures are surfaced as warnings and conservatively rebuild/relink. Compiler/linker execution failures are build errors.

## Process boundary

`mqb_process` defines a platform-neutral `ProcessSpec` with executable, argv, working directory, structured environment overrides, inheritance choice, and stdout/stderr capture controls.

`mqb_platform_windows` isolates Windows command-line quoting and the real `CreateProcessW` runner. Tests cover complex argv round-trips, Unicode environment blocks, separate stdout/stderr capture, non-zero child exit codes, native launch errors, and concurrent draining of large output streams.

`--run` uses the same structured process boundary. Arguments after `--` remain distinct argv elements, including whitespace-containing strings, option-looking strings, and empty arguments. Run mode and child argv do not participate in build signatures.

Captured CRLF is normalized before forwarding through Windows text streams so nested output does not become `\r\r\n`; lone carriage returns are preserved.

Before final cutover, handle inheritance should be hardened further with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`.

## MSVC toolchain boundary

`MsvcToolchainLocator` preserves automatic, forced Visual Studio, and forced portable tracks. The Visual Studio track uses `vswhere`/fallback discovery and isolated `vcvarsall.bat` environment capture. The portable track resolves VC Tools and Windows Kit layout without mutating MQB's own process environment.

GitHub CI enables installed-MSVC integration tests explicitly; local tests keep them opt-in.

## Current CLI milestone

Examples:

```powershell
# One entry: smart discovery
mqb main.cpp

# Explicit source set
mqb main.cpp src/utils.cpp a/helper.cpp b/helper.cpp -o product

# Exact static library request
mqb main.cpp -L "vendor libs" -l math

# Structured run argv
mqb main.cpp --run -- "hello world" --child-option ""
```

When an `mqb.json` is found, effective build/discovery options are resolved before toolchain discovery and artifact planning.

The real VS2026 suite now verifies, among other cases:

- single-entry smart discovery plus same-basename artifact isolation;
- second-entry traversal barriers and project discovery corrections;
- warm compile/link reuse;
- private-header partial rebuild through `/sourceDependencies`;
- Debug/Release recipe invalidation without source edits;
- exact static-library resolution and library-only relink;
- custom output names and independent link-cache identities;
- structured `--run` argv and child exit propagation;
- upward `mqb.json` lookup from a nested working directory;
- config-relative include/library paths and config-root artifact placement;
- config-only define changes invalidating compile recipes;
- explicit CLI scalar overrides winning over project config while config list inputs remain available.

The current VS2026 PR suite contains 32 registered CTest cases, including real target and project-config E2E tests.

Not yet implemented in the C++ V2 path: parallel TU scheduling and C++ Modules/P1689/IFC handling. PowerShell/C++ behavioral parity and final cutover also remain future gates.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest. ✅
2. **Pure core** — artifacts, signatures, plan model, dependency graph, compile cache validation/planning. ✅
3. **Process abstraction** — typed process spec, Windows quoting, Win32 runner. ✅
4. **MSVC toolchain backend** — portable discovery plus `vswhere/vcvarsall` environment capture. ✅
5. **Ordinary single TU** — typed compiler arguments, `/sourceDependencies`, cache persistence, real incremental CLI. ✅
6. **Link state** — independent linker identity/signature/cache/planning/backend/orchestration. ✅
7. **Explicit multi-TU target** — collision-free layout, per-TU incremental compile, one independently cached link. ✅
8. **Target UX** — `-o/--output`, `--run`, structured `--` argv passthrough, child exit propagation. ✅
9. **Explicit libraries** — exact resolution, link-cache v2, `.lib` freshness, `-L/-l`. ✅
10. **Smart source discovery** — single-entry graph selection, secondary-entry barriers, corrections. ✅
11. **Project config v1** — upward `mqb.json`, typed schema, path semantics, CLI precedence, config E2E. ✅
12. **Parallel ordinary-TU scheduling** — bounded concurrency with deterministic reporting/failure semantics.
13. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts, `import std`.
14. **Parity** — run PowerShell and C++ against the same E2E fixtures.
15. **Cutover** — make `mqb.exe` primary only after parity tests pass.

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
