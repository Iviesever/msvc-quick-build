# C++ V2 Architecture

This document defines the architecture for migrating MSVC Quick Build from the current PowerShell implementation to a typed C++23 core.

## Goals

- Preserve the current PowerShell implementation as the behavioral reference while C++ V2 is incomplete.
- Build a typed core before porting MSVC-specific process orchestration.
- Separate planning from execution so build decisions can be tested without launching compilers.
- Keep MSVC as a backend instead of allowing compiler-specific details to leak into the core model.
- Make cache invalidation explainable and regression-testable.

## Layering

```text
CLI (mqb.exe)
    |
    v
BuildRequest
    |
    v
Core --------------------------------------------------+
  Project model                                        |
  Dependency graph                                     |
  Build signatures                                     |
  Cache validation                                     |
  Build planner                                        |
  Build actions                                        |
    |                                                  |
    +-------------------- typed boundary --------------+
                           |
                           v
                    Process abstraction
                    executable + argv + cwd + env
                           |
                           v
                    Platform process runner
                           |
                           v
                    MSVC backend
                    Toolchain discovery
                    Dependency scanners
                    Compiler / linker invocation
```

### Rule 1: Core does not know `cl.exe`

Core may model compiler options and build actions, but MSVC spellings such as `/O2`, `/MD`, `/scanDependencies`, and `/MACHINE:X64` belong in the MSVC backend.

### Rule 2: Planner does not execute processes

`BuildPlanner` produces a `BuildPlan`. An executor consumes that plan. This enables `--dry-run`, deterministic unit tests, and rebuild explanations.

### Rule 3: build correctness beats cache hit rate

A false cache miss wastes time. A false cache hit can silently produce stale binaries. Any uncertain cache state must rebuild.

### Rule 4: paths and process arguments are structured values

Internal APIs use `std::filesystem::path`. The process model stores executable and argv as separate values; shell command strings are not accepted as an internal API.

## Typed core model

The C++ core currently defines:

- `BuildConfiguration`, `Architecture`, and `CppStandard`;
- `BuildRequest`;
- typed `BuildAction` variants and `BuildPlan`;
- `Artifact` and `TranslationUnit`;
- `CompilerOptions`;
- `ToolchainIdentity`;
- `BuildSignature`;
- `DependencyGraph`;
- `CompileCacheEntry`, `FileSnapshot`, and `CompileCacheValidator`;
- `CompilePlanItem` and `BuildPlanner`.

These types replace portions of the dynamic PowerShell context with compile-time checked values before any MSVC process orchestration is migrated.

## Compile signature boundary

`BuildSignature::for_compile` represents the **compiler recipe identity** for one translation unit. The current v1 schema includes:

- normalized source path;
- translation-unit kind;
- compiler path, version, and backend-provided binary stamp;
- build configuration and target architecture;
- C++ language standard;
- ordered preprocessor definitions;
- ordered include search paths;
- ordered additional compiler arguments.

It intentionally excludes dependency timestamps, dependency membership, and artifact output paths:

```text
compile recipe identity ----> BuildSignature
source/header freshness ----> CompileCacheValidator
where results are stored ----> Artifact mapping / cache storage
```

The signature digest is a deterministic 128-bit non-cryptographic cache fingerprint. It is not a security primitive. The schema string (`mqb.compile.signature.v1`) participates in the digest so field changes can deliberately invalidate old cache entries.

## Cache validation boundary

`CompileCacheValidator` is a pure decision component. It does not query the filesystem and does not launch a compiler. The caller supplies immutable `FileSnapshot` values plus optional cached metadata.

A cached object is reusable only when all of the following remain valid:

```text
cache metadata exists
        +
compiler recipe signature matches
        +
toolchain identity matches
        +
object artifact exists
        +
source is not newer than object
        +
every cached dependency still exists
        +
no cached dependency is newer than object
```

The validator reports typed `BuildReason` values. A later CLI can implement `mqb explain` without parsing log text.

## Dependency graph contract

`DependencyGraph` is compiler-agnostic. Scanner/backend code resolves compiler-specific concepts into stable string keys before adding them to the graph.

An edge means `node depends on dependency`, so topological results place prerequisites before consumers. Duplicate nodes and missing nodes are errors, duplicate edges are idempotent, topological levels are deterministic, and cycles fail explicitly rather than returning partial build orders.

## Planner boundary

`BuildPlanner` consumes already-resolved `TranslationUnit` values and already-computed cache validation results. It does **not** inspect timestamps, query the filesystem, calculate signatures, or execute processes.

```text
CompilePlanItem
    |
    +-- reusable cache ------> no action
    |
    +-- stale cache ---------> CompileAction
                                  source
                                  object artifact
                                  typed rebuild reasons
```

A stale translation unit must expose exactly one object artifact. Missing or duplicate object outputs are planning errors rather than implicit guesses.

## Process boundary

`mqb_process` introduces the platform-neutral process contract:

```text
ProcessSpec
  executable: filesystem::path
  arguments: vector<string>      // argv[1..], never one shell string
  working_directory: optional path
  environment: structured name/value overrides
  inherit_environment: bool
  capture_stdout/stderr: bool

ProcessRunner::run(ProcessSpec)
  -> expected<ProcessResult, ProcessError>
```

Text in `ProcessSpec` is UTF-8 by convention. Platform runners are responsible for converting it to native encodings.

On Windows, `CreateProcessW` is an awkward platform boundary because the API receives a single mutable command-line buffer. MQB therefore keeps argv structured until the final Windows adapter. `mqb_platform_windows` owns the Microsoft-compatible argument quoting algorithm and is tested by round-tripping complex arguments through `CommandLineToArgvW`.

This gives the project one explicit place for Windows quoting rules instead of allowing compiler commands such as `"cl ..."` to be assembled throughout the codebase.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest smoke test. ✅
2. **Pure core** — artifact model, signatures, build plan, dependency graph, cache validation, compile planner. ✅
3. **Process abstraction** — typed process spec plus platform argument encoding, then real Win32 runner. **In progress.**
4. **MSVC toolchain backend** — locate toolchain and capture environment.
5. **Ordinary translation units** — compile and `/sourceDependencies` cache persistence/refresh.
6. **Link state** — explicit linker signature and link planning.
7. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts.
8. **Parity** — run PowerShell and C++ implementations against the same E2E fixtures.
9. **Cutover** — make `mqb.exe` the primary entry point only after parity tests pass.

## Build locally

From the repository root:

```powershell
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

A Visual Studio CMake generator may be used instead of Ninja; the core and process model are generator-independent.
