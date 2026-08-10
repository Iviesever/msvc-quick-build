# C++ V2 Architecture

This document defines the initial architecture for migrating MSVC Quick Build from the current PowerShell implementation to a typed C++23 core.

## Goals

- Preserve the current PowerShell implementation as the behavioral reference while C++ V2 is incomplete.
- Build a typed core before porting MSVC-specific process orchestration.
- Separate planning from execution so build decisions can be tested without launching compilers.
- Keep MSVC as a backend instead of allowing compiler-specific details to leak into the core model.
- Make cache invalidation explainable and regression-testable.

## Non-goals for the first milestone

The first C++ milestone does **not** implement:

- `cl.exe` process execution;
- Visual Studio/MSVC discovery;
- `/sourceDependencies`;
- `/scanDependencies` or C++ Modules;
- incremental cache reuse;
- linking;
- parallel compilation.

Those features remain in the PowerShell reference implementation until the corresponding C++ subsystem has tests and a stable interface.

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

The future `BuildPlanner` produces a `BuildPlan`. The executor consumes that plan. This enables `--dry-run`, deterministic unit tests, and rebuild explanations.

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
- `BuildSignature`.

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
source/header freshness ----> dependency validation
where results are stored ----> Artifact mapping / cache storage
```

This separation prevents the compile signature from becoming a second dependency scanner and allows the same cached compiler result to be placed at a different artifact location.

The current signature digest is a deterministic 128-bit non-cryptographic cache fingerprint. It is not a security primitive. The schema string (`mqb.compile.signature.v1`) is part of the digest so future field changes can invalidate old cache entries deliberately.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest smoke test. ✅
2. **Pure core** — artifact model, build signatures, build plan, then dependency graph and planner. **In progress.**
3. **Process abstraction** — typed executable + argv + exit result without shell command concatenation.
4. **MSVC toolchain backend** — locate toolchain and capture environment.
5. **Ordinary translation units** — compile and `/sourceDependencies` cache validation.
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
