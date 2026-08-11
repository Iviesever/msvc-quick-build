# C++ Refactor Architecture

This document defines the current typed C++23 architecture of MSVC Quick Build and the boundary of the v5 release-candidate line.

## Goals

- Keep the PowerShell implementation as the behavioral reference until parity and cutover are explicitly proven.
- Separate source selection, build policy, dependency topology, compiler/linker execution, and process execution.
- Keep MSVC-specific switches out of the Core model.
- Make compile/link cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.
- Prefer conservative rebuild/relink or an explicit unsupported error over uncertain artifact reuse.

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
    |      reachable project-local named-module candidates
    |      module/header-unit routing requirement
    |
    v
Target routing
    +--> Ordinary target orchestration
    |      bounded parallel compile -> deterministic ordered results -> incremental link
    |
    +--> Module target orchestration
           parallel /scanDependencies
               -> P1689R5 typed model
               -> named-module + project-local header-unit graph
               -> deterministic compile levels
               -> bounded level-barrier compile waves
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
  LinkCache                                    MsvcLinker
  DependencyGraph                              /sourceDependencies reader
  BuildPlanner                                       |
    |                                                v
    +----------------------------------------> Process abstraction
                                              ProcessSpec { executable, argv, cwd, env }
                                                    |
                                                    v
                                             WindowsProcessRunner
                                             CreateProcessW
```

## Architectural rules

1. **Core does not know `cl.exe` or `link.exe`.** MSVC spelling belongs to the backend.
2. **Planner does not execute.** `BuildPlanner` produces typed planned actions and outputs.
3. **Correctness beats cache hit rate.** Uncertain cache state rebuilds or relinks.
4. **No internal shell-command API.** Executable, argv, cwd, and environment remain structured until the Windows adapter.
5. **Compile state and link state are independent.** Link-only changes do not force unrelated compilation.
6. **A fresh compile is an explicit relink signal.** Correctness must not depend only on filesystem timestamp granularity.
7. **Source identity is not a basename.** Project artifacts preserve source identity and external sources use a stable namespace.
8. **Windows physical aliases must converge.** Existing sources that are physically equivalent must receive the same artifact identity even when Windows/MSVC spells their paths differently.
9. **Writable artifacts are exclusive.** Target preflight rejects empty/colliding writable outputs before external processes start.
10. **Run-time argv and job count are execution policy, not build identity.**
11. **Discovery is not dependency freshness.** Discovery chooses candidate TUs; `/sourceDependencies` remains the post-compile header-freshness authority.
12. **Module topology and header freshness are separate.** `/scanDependencies`/P1689 orders module work; `/sourceDependencies` validates real compile dependencies.
13. **Provider selection has one owner.** `ModuleDependencyGraphBuilder` resolves graph edges; orchestration consumes those resolved edges rather than guessing providers again.
14. **Header units are typed separately from named modules.** Named-module consumers use `/reference`; header-unit consumers use `/headerUnit:*`; producers use `/exportHeader` + `/headerName:*` + `/ifcOutput`.
15. **Discovery does not become a second module compiler.** Its lexical pass only selects candidates and records routing state; P1689 remains authoritative.
16. **Unsupported module requirements fail closed.** External/prebuilt named-module providers and `import std` are never silently ignored or downgraded to the ordinary pipeline.

## Project configuration

`mqb_config` owns versioned `mqb.json`. The CLI searches upward from the invocation directory for the nearest config.

```text
CLI relative path --------> invocation directory
mqb.json relative path ---> directory containing mqb.json
artifact project root ----> directory containing mqb.json (when present)
```

Scalar precedence is `explicit CLI > mqb.json > built-in defaults`. List-like inputs are additive and deterministic. Unknown fields, duplicate keys, wrong types, malformed JSON, and unsupported schema versions are rejected rather than guessed. See `docs/MQB_CONFIG.md`.

## Smart source discovery

With one positional ordinary source, MQB smart-discovers a target by default. Multiple positional sources remain an explicit ordered source set.

Ordinary discovery uses quoted local includes, configured include directories, same-basename ownership, deterministic traversal, project corrections, and a secondary-`main()` traversal barrier.

The discovery index also classifies `.cpp/.cc/.cxx` ordinary TUs and `.ixx/.cppm/.mpp` module interfaces. Its lexical module pass:

- extracts named module declarations/imports for candidate selection;
- follows reachable named imports to all matching project-local interface candidates;
- does not guess a winner between duplicate providers;
- recognizes header-unit import syntax only as a **module-pipeline routing signal**;
- never promotes a header into the translation-unit source set.

If discovery observes named-module or header-unit syntax but has no local named interface to add, it still sets execution-only module routing state. Therefore unresolved external modules, `import std`, and header-unit-only entries cannot silently fall back to ordinary compilation.

## Build identity and compile cache

`BuildSignature::for_compile` models a versioned compiler recipe. Relevant identity includes source/TU kind, compiler/toolchain identity, configuration, architecture, language standard, ordered compiler options, typed module/header-unit references, and required planned interface outputs.

Artifact placement and freshness are separate concepts:

```text
compiler recipe identity ----> BuildSignature
source/header/IFC freshness --> CompileCacheValidator
artifact placement -----------> ProjectArtifactLayout
```

`CompileCacheValidator` validates the complete planned output set. Compile actions are not object-only: a header-unit producer is a valid **IFC-only** action. This is why deleting only an IFC makes a provider stale even when no object is expected.

Consumer cache metadata records relevant compiler-discovered dependencies and interface artifacts. A provider that actually recompiles in the current dependency wave also sends an explicit downstream rebuild signal, avoiding false hits caused by timestamp granularity.

## Named modules and header units

The implemented module target supports project-local named modules and project-local header units:

```text
selected TU requests
      -> target-wide writable-artifact preflight
      -> bounded parallel /scanDependencies
      -> P1689 typed rules
      -> ModuleDependencyGraphBuilder
             named-provider resolution
             project-local header-unit nodes
             deterministic compile levels
      -> dynamic header-unit artifact assignment from P1689 source-path
      -> dependency-level compile waves
             named provider: /interface /ifcOutput
             named consumer: /reference logical-name=ifc
             header producer: /exportHeader /headerName:* /ifcOutput
             header consumer: /headerUnit:* header=ifc
             provider rebuilt -> downstream explicit rebuild
      -> incremental final link using ordinary/module TU objects only
```

Header-unit producers are IFC-only and never become link object inputs. P1689 lookup method (`include-quote` / `include-angle`) and header spelling remain typed identity used by the MSVC backend.

Warm module targets still repeat topology scanning. The current cache guarantee is **compile 0 / link 0**, not scan 0; scan caching is a future optimization.

### Supported module behavior

- strict P1689 parsing and schema validation;
- project-local named-module interfaces, partitions, implementation units, and consumers;
- single-entry discovery of reachable project-local named providers;
- project-local quote/angle header units discovered by the real scanner;
- source-identity IFC routing and Windows physical-path alias convergence;
- bounded same-level parallelism with dependency barriers;
- incremental compile/link caches;
- provider/header-unit mutation propagation;
- missing named-provider IFC and header-unit IFC repair;
- public `mqb main.cpp --run` VS2026 E2E for named modules and header units.

### Deliberate fail-closed boundary

- external/prebuilt named-module provider execution;
- `import std`.

The P1689 type model may recognize requirements that execution policy does not yet support. Parsing a requirement is not permission to compile it incorrectly.

## Project artifact layout

Without `mqb.json`, the invocation directory is the project root. With `mqb.json`, its directory becomes the project root.

```text
project/
  .mqb/
    obj/     <source-key>.obj
    deps/    <source-key>.json
    scan/    <source-key>.json
    ifc/     <source-key>.ifc
    cache/
      compile/<source-key>.mqbcache
      link/<target>.linkcache
    bin/
      <target>.exe
```

IFCs are keyed by physical/source identity rather than logical module spelling. For existing Windows sources, artifact identity uses physical ancestry/equivalence and case-insensitive keys so scanner path aliases converge. Non-existing planned paths retain a conservative lexical fallback.

## Ordinary incremental target

```text
ordered source requests
      -> preflight unique writable artifacts
      -> BoundedWorkScheduler(max_parallel_compiles)
      -> parallel IncrementalCompileCoordinator
      -> deterministic result/failure ordering
      -> collect objects in source order
      -> any TU compiled? ---- yes ----> force_relink
      -> IncrementalLinkCoordinator
      -> target executable
```

Link identity tracks ordered objects, resolved explicit libraries, linker identity, link options, and output identity. Explicit libraries are resolved to exact `.lib` inputs before link execution.

## Process and toolchain boundary

`mqb_process` carries executable, argv, working directory, environment overrides, and capture policy. `mqb_platform_windows` owns Windows quoting and `CreateProcessW` execution.

`MsvcToolchainLocator` supports automatic, forced Visual Studio, and forced portable toolchain tracks. Toolchain identity participates in build/cache correctness. GitHub CI opts into real installed-MSVC tests explicitly.

## Verification and release-candidate gate

The main C++ workflow runs the installed Visual Studio 2026 suite in Debug. The `v5.0.0-rc.1` release pipeline additionally creates a fresh x64 **Release** build, runs the complete installed-MSVC CTest suite, verifies the embedded user-visible version, stages a standalone package, calculates SHA-256, and uploads the exact validated package as an Actions artifact.

Only the push-to-`main` publish job has release write permission. It downloads the artifact produced by the successful Release test job and creates the GitHub prerelease; pull-request runs cannot publish.

The release candidate uses static MSVC runtime linkage. It intentionally does **not** change the legacy `build.ps1` installer/profile entry.

## Migration sequence

1. Scaffold / typed Core / process / MSVC backend — complete.
2. Incremental compile/link and project artifact layout — complete.
3. Public CLI, project config, smart discovery, bounded parallelism — complete for the C++ target scope.
4. Project-local named-module pipeline and source discovery — complete.
5. Project-local header units through public CLI — complete (#19–#24).
6. **C++ standalone Release Candidate** — `v5.0.0-rc.1` release gate.
7. External/prebuilt module providers and `import std` — tracked in Issue #16.
8. PowerShell ↔ C++ behavioral parity campaign.
9. Installer/profile/default-entry cutover.
10. Stable v5 and retirement of superseded PowerShell code only after parity is proven.

The RC is therefore a real distributable C++ build, but not a declaration that migration is finished.
