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

## Initial typed model

The first commit introduces:

- `BuildConfiguration`;
- `Architecture`;
- `CppStandard`;
- `BuildRequest`.

These replace the first small portion of the dynamic PowerShell context with compile-time checked C++ types.

## Migration sequence

1. **Scaffold** — CMake, `mqb_core`, CLI executable, CTest smoke test.
2. **Pure core** — build signatures, dependency graph, artifact model, build plan.
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
