# C++ V2 Architecture

This document defines the architecture for migrating MSVC Quick Build from the PowerShell implementation to a typed C++23 build tool.

## Goals

- Keep the PowerShell implementation as the behavioral reference until parity is proven.
- Separate source selection, build policy, dependency topology, compiler/linker execution, and process execution.
- Keep MSVC-specific spellings out of the core model.
- Make compile and link cache invalidation explainable and regression-testable.
- Treat paths and argv as structured data rather than shell command strings.
- Prefer a conservative rebuild/relink or an explicit unsupported error over reusing uncertain artifacts.

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
    |      reachable project-local named-module provider candidates
    |      module-pipeline routing requirement
    |
    v
Target routing
    +--> Ordinary target orchestration
    |      bounded parallel N x compile coordinator -> ordered results -> one link coordinator
    |
    +--> Named-module orchestration
           parallel /scanDependencies
               -> P1689R5 typed model
               -> resolved provider graph + compile levels
               -> level-barrier bounded compile waves
               -> one incremental link coordinator
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
8. **Source identity is not a basename.** Project artifacts preserve relative source identity; external sources receive a stable hashed namespace.
9. **Writable artifacts are exclusive.** Module target preflight rejects empty or colliding object, dependency, scan, IFC, compile-cache, executable, and link-cache paths before external processes start.
10. **Run-time argv is not build identity.** `--run` and arguments after `--` are execution state only; changing them must not invalidate compile or link caches.
11. **Discovery is not dependency freshness.** Smart discovery chooses candidate translation units; MSVC `/sourceDependencies` remains the authority for header freshness after compilation.
12. **Configuration is typed policy, not shell text.** `mqb.json` decodes into optional typed overrides before MSVC arguments are produced.
13. **Parallelism is execution policy, not build identity.** Job counts must not invalidate compile or link caches.
14. **Module topology and header freshness are different data.** `/scanDependencies` determines pre-compile module ordering; `/sourceDependencies` remains the post-compile freshness source for headers.
15. **Provider selection has one owner.** `ModuleDependencyGraphBuilder` resolves named-module providers once; orchestration consumes those exact resolved edges for `/reference` wiring and downstream rebuild propagation.
16. **Unsupported module requirements fail closed.** Header units and unresolved external/prebuilt named modules are never silently ignored.
17. **Discovery does not become a second module compiler.** Its lexical module pass selects project-local candidates and records whether module routing is required; P1689 remains authoritative for topology and provider validation.
18. **Module routing is explicit execution state, not cache identity.** A discovery result may force the named-module pipeline even when no local interface was selected, preventing import-only targets from silently falling back to ordinary compilation.

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

List-like build inputs are additive and deterministic: project-config entries appear first, then CLI entries. The v1 schema and examples are documented in `docs/MQB_CONFIG.md`. Unknown fields, duplicate JSON keys, wrong field types, malformed JSON, and unsupported schema versions are rejected instead of guessed.

## Smart source discovery

With one positional ordinary source, MQB smart-discovers the connected target by default. Multiple positional sources remain an explicit ordered source set. `--no-discover` disables discovery; `--discover` explicitly enables it and can override project configuration.

Ordinary discovery uses quoted `#include "..."` connectivity, configured include directories, same-basename ownership, deterministic traversal, built-in directory exclusions, and project-config corrections. A reachable non-entry ordinary source defining `main(...)` is a traversal barrier. Explicitly excluded sources and directories are also traversal barriers.

The discovery index also understands the supported translation-unit extension classes from `mqb_core`: ordinary `.cpp/.cc/.cxx` units and module-interface `.ixx/.cppm/.mpp` units. A lightweight lexical module pass extracts named module declarations/imports only for source selection. It ignores comments, literals, preprocessor directives, and header-unit imports. Reachable named imports connect to all matching project-local interface candidates; discovery deliberately does not choose a winner among duplicate providers.

When a selected TU contains named-module syntax, discovery reports that the target requires the module pipeline even if no project-local interface provider was found. The CLI propagates that execution-only state through `MsvcTargetRouter`, so `import std` or an unresolved external/prebuilt named module cannot silently fall back to the ordinary pipeline.

Discovery output selects candidate TUs only. Incremental header invalidation continues to use compiler-emitted `/sourceDependencies` metadata, while the real module target pipeline runs `/scanDependencies` and the P1689 graph builder to determine authoritative module topology and provider resolution.

## Compile identity and cache freshness

`BuildSignature::for_compile` models the versioned compiler recipe identity. Compile signature v3 includes:

- source and translation-unit kind;
- compiler identity;
- configuration, architecture, and language standard;
- ordered defines, include paths, and extra compiler arguments;
- ordered typed module references (`logical-name -> IFC path`);
- for a module interface unit, the planned IFC output path.

Ordinary object placement remains outside recipe identity. Module IFC placement is different: a provider's planned IFC path is part of the recipe because consumers reference that exact interface artifact.

```text
compiler recipe identity ----> BuildSignature
source/header/IFC freshness --> CompileCacheValidator
artifact placement -----------> ProjectArtifactLayout
```

`CompileCacheValidator` is pure and returns typed `BuildReason` values; it does not touch the filesystem or launch a process. It validates all planned compile outputs, not only the object. Therefore a provider with an existing object but a missing IFC is stale.

Consumer cache metadata records imported IFC files alongside compiler-discovered header dependencies. A provider that actually recompiles in the current module wave also propagates an explicit downstream rebuild signal, preventing timestamp-granularity races from producing a false consumer hit.

`CompileCacheFile` persists compile metadata in a versioned binary format. Missing metadata is a normal cold-cache condition; corrupt/truncated/unsupported metadata is rejected and conservatively rebuilt.

Ordinary Debug and Release compilation use MSVC `/Z7`, keeping compiler debug information inside each TU's object instead of sharing a compiler PDB between concurrent `cl.exe` processes.

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

`LinkerIdentity` stamps `link.exe` independently from `cl.exe`. User-requested libraries are resolved deterministically to exact `.lib` files before invoking `link.exe`; link-cache metadata persists those resolved inputs.

Current boundary: explicitly requested libraries are tracked precisely. Indirect `/DEFAULTLIB` dependencies embedded in objects or libraries are still resolved by `link.exe` and are not claimed as fully tracked transitive link inputs.

## Project artifact layout

Without `mqb.json`, the invocation directory is the project root. With `mqb.json`, its directory becomes the project root. Sources inside the root preserve relative source identity; sources outside it are isolated under `.external/<stable-path-hash>/`.

For a source such as `src/foo.cpp` or `modules/math.ixx`, the layout owns separate namespaces:

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

IFCs are keyed by source identity rather than logical module spelling. This avoids Windows filename problems such as partition `:` characters and avoids same-basename collisions. The module target coordinator additionally validates global writable-artifact exclusivity before scanning.

## Dependency graphs and planning

`DependencyGraph` stores `node depends on dependency` edges. It rejects duplicate/missing nodes, makes duplicate edges idempotent, returns deterministic topological levels, and fails cycles explicitly.

`ModuleDependencyGraphBuilder` consumes typed P1689 rules and produces one `ModuleDependencyPlan` containing:

```text
compile_levels
resolved_dependencies {
    consumer_source,
    provider_source,
    logical_name
}
unresolved_requirements
```

Named-provider selection prefers the unique `is-interface=true` provider when same-name module units exist. Multiple interface providers fail explicitly. Header-unit identity remains typed but unresolved until the dedicated header-unit milestone.

The exact resolved dependency edges are reused for both compile ordering and MSVC `/reference logical-name=ifc` wiring. Orchestration does not run a second provider-selection algorithm.

## Named-module MSVC pipeline

The implemented named-module pipeline supports project-local named-module interfaces and consumers through both explicit CLI target sets and single-entry module-aware discovery:

```text
selected source requests
      -> preflight all writable artifacts
      -> bounded parallel MsvcModuleDependencyScanner
             cl.exe /scanDependencies <per-source P1689 JSON>
      -> exactly one P1689 rule per one-source scan
      -> ModuleDependencyGraphBuilder
             provider resolution
             deterministic topological compile levels
      -> MsvcModuleCompileCoordinator
             for each level:
                 bounded parallel incremental compile
                 module interface: /interface /TP /ifcOutput
                 consumer: /reference logical-name=<planned-ifc>
             barrier between levels
             provider compiled this run -> downstream explicit_rebuild
      -> MsvcIncrementalLinkCoordinator
             any TU compiled -> force_relink
      -> target executable
```

`/scanDependencies` is topology-only; it does not produce `.obj` or `.ifc`. The real compile remains responsible for `/sourceDependencies`, object output, and IFC output.

Warm module target builds currently repeat topology scanning. The proven cache guarantee is **compile 0 / link 0**, not scan 0. Scan-result caching is a future optimization and must not change build identity semantics.

### Current module capability boundary

Supported and regression-tested:

- P1689R5 parsing and strict schema validation;
- project-local named-module interface providers;
- named consumers resolved to exact provider sources;
- source-identity IFC routing;
- bounded same-level parallelism with dependency-level barriers;
- module-aware compile signatures and cache freshness;
- downstream rebuild propagation when a provider recompiles;
- incremental final linking;
- explicit public CLI module-interface targets;
- single-entry public CLI discovery of reachable project-local named-module providers;
- real VS2026 provider/consumer executable E2E, including warm builds, provider-only mutation, and missing-IFC repair.

Not yet supported by the execution policy:

- header units;
- unresolved external/prebuilt named-module providers;
- `import std` (currently appears as an unresolved external named module and fails closed).

The P1689 model can represent more cases than the current execution policy accepts. That is intentional: parsing a requirement is not permission to compile it incorrectly.

## Ordinary orchestration

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

`force_rebuild` is a request-local execution signal represented by `BuildReason::explicit_rebuild`. It does not enter the compile signature and is not persisted into the cache.

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

### Ordinary incremental target

```text
ordered source requests
      -> preflight unique source/object/deps/cache artifacts
      -> BoundedWorkScheduler(max_parallel_compiles)
      -> parallel IncrementalCompileCoordinator per ordinary TU
      -> join every in-flight compile
      -> deterministic failure selection
      -> collect collision-free objects in original source order
      -> any TU compiled? -------- yes ----> force_relink
      -> IncrementalLinkCoordinator
      -> one target executable
```

The scheduler assigns indices monotonically and at most once, joins all work that was already assigned, and quenches dispatch after stop publication. Target results are reassembled in source order.

## Process boundary

`mqb_process` defines a platform-neutral `ProcessSpec` with executable, argv, working directory, structured environment overrides, inheritance choice, and stdout/stderr capture controls.

`mqb_platform_windows` isolates Windows command-line quoting and the real `CreateProcessW` runner. Captured child processes use `STARTUPINFOEXW` with `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, so unrelated pipe handles from concurrent compiler launches are not inherited by sibling children.

`--run` uses the same structured process boundary. Arguments after `--` remain distinct argv elements and do not participate in build signatures.

## MSVC toolchain boundary

`MsvcToolchainLocator` preserves automatic, forced Visual Studio, and forced portable tracks. The Visual Studio track uses `vswhere`/fallback discovery and isolated `vcvarsall.bat` environment capture. The portable track resolves VC Tools and Windows Kit layout without mutating MQB's own process environment.

GitHub CI enables installed-MSVC integration tests explicitly; local tests keep them opt-in.

## Current verification milestone

The VS2026 PR suite verifies ordinary CLI behavior plus the named-module backend, routing, and source-discovery path.

Among the real installed-MSVC cases are:

- ordinary single/multi-TU compile and incremental link behavior;
- same-directory four-TU cold `-j4` and warm `-j1`, proving job count is not build identity;
- real `/scanDependencies` P1689 output before IFCs exist;
- real module provider IFC creation and consumer `/reference` compilation;
- full named-module target cold build and executable launch;
- warm named-module target with compile 0 / link 0;
- single-entry `mqb main.cpp` discovery of a reachable project-local module provider;
- provider-source mutation causing provider + consumer + link rebuild;
- provider IFC deletion with source/object otherwise warm, causing provider + consumer + link repair;
- artifact collision/empty-artifact validation before any external process starts.

The named-module target E2E uses the installed Visual Studio 2026 compiler and linker, not a fake process runner.

## Current CLI milestone

The public `mqb.exe` CLI now owns both the ordinary-C++ path and the supported project-local named-module path: project config, smart source discovery, bounded `-j/--jobs` execution, independent link caching, library resolution, structured `--run` argv, explicit module-interface target routing, and single-entry discovery of reachable project-local named-module providers.

This is not full C++ Modules parity. Header units, external/prebuilt provider execution, and `import std` remain explicit fail-closed gaps, and PowerShell/C++ parity testing still precedes cutover.

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
12. **Parallel ordinary-TU scheduling** — bounded concurrency with deterministic reporting/failure semantics. ✅
13. **Named Modules core/orchestration** — P1689 scan, provider graph, IFC artifacts, module-aware cache, dependency waves, incremental link, real VS2026 E2E. ✅
14. **Modules expansion/UX** — public CLI routing and project-local module-aware source selection ✅; header units, external/prebuilt provider policy, and `import std` remain.
15. **Parity** — run PowerShell and C++ against the same E2E fixtures.
16. **Cutover** — make `mqb.exe` primary only after parity tests pass.

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
