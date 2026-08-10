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
    +-------------------- interfaces ------------------+
                           |
                           v
                    MSVC backend
                    Toolchain discovery
                    Dependency scanners
                    Compiler / linker process execution
```

### Rule 1: Core does not know `cl.exe`

Core may model compiler options and build actions, but MSVC spellings such as `/O2`, `/MD`, `/scanDependencies`, and `/MACHINE:X64` belong in the MSVC backend.

### Rule 2: Planner does not execute processes

`BuildPlanner` produces a `BuildPlan`. A later executor consumes that plan. This enables `--dry-run`, deterministic unit tests, and rebuild explanations.

### Rule 3: build correctness beats cache hit rate

A false cache miss wastes time. A false cache hit can silently produce stale binaries. Any uncertain cache state must rebuild.

### Rule 4: paths are structured values

Internal APIs use `std::filesystem::path`. Shell command strings are not used as the process model; executable path and argument vectors remain separate values.

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

It intentionally excludes dependency timestamps, dependency membership, and artifact output paths. Those belong to separate concerns:

```text
compile recipe identity ----> BuildSignature
source/header freshness ----> CompileCacheValidator
where results are stored ----> Artifact mapping / cache storage
```

The current signature digest is a deterministic 128-bit non-cryptographic cache fingerprint. It is not a security primitive. The schema string (`mqb.compile.signature.v1`) is part of the digest so future field changes can invalidate old cache entries deliberately.

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

The dependency list comes from the last successful compiler dependency scan. Missing dependency snapshots are treated conservatively as cache misses. Multiple stale dependencies collapse to one `dependency_changed` reason so diagnostics stay stable.

The validator reports typed `BuildReason` values rather than human-formatted strings. A later planner/CLI layer can therefore implement `mqb explain` without parsing log text.

## Dependency graph contract

`DependencyGraph` is compiler-agnostic. Scanner/backend code resolves compiler-specific concepts into stable string keys before adding them to the graph.

An edge is expressed as:

```text
node depends on dependency
```

so topological results always place prerequisites before consumers. The graph follows these rules:

- nodes must be registered before edges are added;
- duplicate nodes are errors because they indicate an upstream identity bug;
- references to missing nodes are errors rather than silently creating placeholders;
- duplicate edges are idempotent;
- `topological_levels()` returns deterministic, lexicographically ordered parallel levels;
- cycles fail explicitly and report the unresolved node set instead of returning a partial build order.

The level representation will later map directly to safe parallel module compilation, while `topological_order()` provides a flattened deterministic order for simpler planning and diagnostics.

## Planner boundary

`BuildPlanner` consumes already-resolved `TranslationUnit` values and already-computed cache validation results. It does **not** inspect timestamps, query the filesystem, calculate compiler signatures, or execute processes.

For compile planning it follows this contract:

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

A stale translation unit must expose exactly one object artifact. Missing or duplicate object outputs are planning errors rather than implicit guesses. Module translation units may additionally expose an interface artifact; the compile planner still selects the single object artifact for the current `CompileAction` model.

This keeps the planner deterministic and makes the eventual executor intentionally boring: it receives actions, not policy decisions.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest smoke test. ✅
2. **Pure core** — artifact model, build signatures, build plan, dependency graph, cache validation, compile planner. ✅
3. **Process abstraction** — typed executable + argv + exit result without shell command concatenation. **Next.**
4. **MSVC toolchain backend** — locate toolchain and capture environment.
5. **Ordinary translation units** — compile and `/sourceDependencies` cache persistence/refresh.
6. **Link state** — explicit linker signature and link planning.
7. **Modules** — P1689 `/scanDependencies`, module graph, IFC/object artifacts.
8. **Parity** — run PowerShell and C++ implementations against the same E2E fixtures.
9. **Cutover** — make `mqb.exe` the primary entry point only after parity tests pass.

## Build the scaffold

From the repository root:

```powershell
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

A Visual Studio CMake generator may be used instead of Ninja; the core is intentionally generator-independent.
