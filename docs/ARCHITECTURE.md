# MQB architecture

This document defines the stable native C++23 architecture of MSVC Quick Build.

MQB has one supported implementation: `mqb.exe`. Build inputs, process arguments, artifacts, caches, and toolchain state are modeled as structured data; there is no fallback build implementation.

## Goals

- Separate source selection, build policy, dependency topology, compiler/linker execution, and process execution.
- Keep MSVC-specific switches out of the Core model.
- Make compile/link/archive cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.
- Prefer conservative rebuild/relink or an explicit unsupported error over uncertain artifact reuse.
- Keep one authoritative native execution path.

## Layers

```text
CLI (mqb.exe)
    |
    +--> Project Config (mqb_config)
    |      mqb.json locator/parser
    |      CLI > config > defaults resolver
    |
    +--> Source Discovery (mqb_discovery)
    |      ordinary C/C++ candidate selection
    |      reachable project-local named-module candidates
    |      module/header-unit routing requirement
    |
    v
Target routing
    +--> Ordinary target orchestration
    |      bounded parallel compile -> deterministic results -> link/archive
    |
    +--> Module target orchestration
           parallel /scanDependencies
               -> P1689 typed model
               -> named-module + project-local header-unit graph
               -> deterministic compile levels
               -> bounded dependency waves
               -> incremental link
    |
    +--------------------+--------------------+
    v                    v                    v
Core                  Modules              MSVC backend
  BuildRequest           P1689 parser          MsvcToolchainLocator
  Artifact / TU          provider graph        MsvcCompiler
  ProjectArtifactLayout  compile levels        MsvcCompileExecutor
  BuildSignature                               MsvcModuleDependencyScanner
  CompileCache                                 MsvcLibraryResolver
  Link/Archive cache                            MsvcLinker / MsvcLibrarian
  DependencyGraph                                  |
  BuildPlanner                                     v
    +----------------------------------------> Process abstraction
                                              ProcessSpec { executable, argv, cwd, env }
                                                    |
                                                    v
                                             WindowsProcessRunner
                                             CreateProcessW
```

## Architectural rules

1. Core does not know `cl.exe`, `link.exe`, or `lib.exe` spelling.
2. Planner does not execute; it produces typed actions and outputs.
3. Correctness beats cache hit rate.
4. No internal shell-command API.
5. Compile, link, and archive state are independent.
6. A fresh compile is an explicit downstream rebuild signal.
7. Source identity is not a basename.
8. Windows physical aliases must converge to one artifact identity.
9. Writable artifacts are exclusive and preflighted.
10. Runtime argv and job count are execution policy, not build identity.
11. Discovery chooses candidate sources; `/sourceDependencies` owns header freshness.
12. `/scanDependencies`/P1689 owns module topology.
13. Provider selection has one owner: `ModuleDependencyGraphBuilder`.
14. Header units are typed separately from named modules.
15. Unsupported module requirements fail closed.
16. Stable v5 has one native parser and one native executor.

## Project configuration

`mqb_config` owns versioned `mqb.json`. MQB searches upward from the invocation directory for the nearest config.

```text
CLI relative path --------> invocation directory
mqb.json relative path ---> directory containing mqb.json
artifact project root ----> directory containing mqb.json (when present)
```

Scalar precedence is `explicit CLI > mqb.json > built-in defaults`. List-like inputs are additive and deterministic. Unknown fields, duplicate keys, wrong types, malformed JSON, and unsupported schema versions are rejected. See `docs/MQB_CONFIG.md`.

## Smart source discovery

With one positional ordinary source, MQB smart-discovers a target by default. Multiple positional sources remain an explicit ordered source set.

Ordinary discovery uses local includes, configured include directories, same-basename ownership, deterministic traversal, project corrections, and a secondary-`main()` traversal barrier.

The discovery index classifies `.c`, `.cpp/.cc/.cxx`, and `.ixx/.cppm/.mpp`. Its lexical module pass selects candidates and routing state only; MSVC P1689 scanning remains authoritative for module topology.

## Build identity and cache

`BuildSignature::for_compile` models a versioned compiler recipe. Identity includes source/TU kind, compiler/toolchain identity, configuration, architecture, language standard, ordered compiler options, typed module/header-unit references, and required outputs.

```text
compiler recipe identity ----> BuildSignature
source/header/IFC freshness --> CompileCacheValidator
artifact placement -----------> ProjectArtifactLayout
```

Link and archive targets use separate downstream identities and caches. Link-only changes do not force unrelated compilation. Typed target kind, runtime, subsystem, and LTCG remain authoritative over conflicting raw escape-hatch arguments.

## Named modules and header units

The module pipeline supports project-local named modules and project-local header units:

```text
selected TU requests
      -> writable-artifact preflight
      -> bounded parallel /scanDependencies
      -> P1689 typed rules
      -> ModuleDependencyGraphBuilder
      -> dynamic IFC artifact assignment
      -> dependency-level compile waves
      -> incremental final link
```

Supported behavior includes interfaces, partitions, implementation units, consumers, quote/angle project-local header units, source-identity IFC routing, bounded same-level parallelism, incremental caches, mutation propagation, and missing-IFC repair.

Deliberate fail-closed boundaries remain external/prebuilt named-module providers and `import std`.

## Project artifact layout

```text
project/
  .mqb/
    obj/
    deps/
    scan/
    ifc/
    cache/
      compile/
      link/
      archive/
    bin/
```

All writable build state lives under `.mqb/` rather than being scattered through source directories.

## Process and toolchain boundary

`mqb_process` carries executable, argv, working directory, environment overrides, and capture policy. `mqb_platform_windows` owns Windows quoting and `CreateProcessW` execution.

`MsvcToolchainLocator` supports automatic, forced Visual Studio, and forced portable toolchain tracks. Toolchain identity participates in build/cache correctness.

## CLI boundary

Stable v5 accepts the native option set documented by `mqb --help`. Normal short options such as `-h`, `-v`, `-j`, `-o`, `-I`, `-D`, `-L`, and `-l` are native UX.

Known obsolete single-dash spellings are rejected as unknown options. There is no second parser or fallback executor.

## Verification and release gate

A stable release requires all of the following:

1. full installed-MSVC Debug tests;
2. full installed-MSVC Release tests;
3. native installer install/reinstall/uninstall validation;
4. embedded version verification;
5. Stage 0 -> Stage 1 MQB self-build through `cpp/mqb.json`;
6. deletion of `cpp/.mqb`, followed by Stage 1 -> Stage 2 clean self-host closure;
7. packaging of Stage 1 only, with byte-identity verification;
8. exact package manifest and SHA-256 validation;
9. publication of the exact validated artifact from the tag workflow.

CMake remains the bootstrap/test harness for Stage 0 and the unit/integration test graph; it is not the producer of the `mqb.exe` shipped in stable releases. See `docs/SELF_HOSTING.md`.

## Stable v5 status

The native architecture, installer lifecycle, exact-artifact release workflow, and self-hosting gate are complete for the v5.0.0 scope. Remaining feature work such as external/prebuilt named-module providers and `import std` is tracked independently in Issue #16.
