# MQB Architecture

**[简体中文](ARCHITECTURE.md) | English**

This document describes MQB's **design boundaries and data flow** only. User-facing CLI/configuration semantics live in [`MQB_CONFIG_EN.md`](MQB_CONFIG_EN.md), repository layout rules in [`../cpp/README_EN.md`](../cpp/README_EN.md), and stable self-hosting rules in [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md).

## 1. Design goals

MQB is one native C++23 product: `mqb.exe`. Its architecture follows a small set of principles:

1. **Typed data first**: build requests, artifacts, module references, and process argv remain structured.
2. **Correctness over cache hit rate**: rebuild when identity is uncertain; never guess.
3. **Toolchain metadata is authoritative**: header freshness comes from `/sourceDependencies`; module topology comes from `/scanDependencies` / P1689.
4. **Platform boundaries stay concentrated**: Windows quoting and `CreateProcessW` do not leak into core models.
5. **Writable state stays concentrated**: all MQB-owned intermediates and caches live under project `.mqb/`.

## 2. Logical layers

```text
src/app
  CLI + project composition
          |
          v
 config + discovery
          |
          v
     orchestration  <------ modules
          |                   |
          +---------+---------+
                    v
                   msvc
          compiler/linker/lib/
          source-deps/P1689/
          toolchain discovery
                    |
                    v
                 process
                    |
                    v
             platform/windows
               CreateProcessW

core = shared typed build model, planner, artifact identity, and caches
```

Dependencies are not arbitrary two-way calls. Upper layers compose policy; lower layers provide primitive capability.

## 3. Responsibility boundaries

### `app`

Executable composition layer:

- CLI parsing;
- invocation and project setup;
- merge CLI, `mqb.json`, and discovery results;
- choose ordinary, module, or static-target pipelines;
- diagnostics;
- `main()`.

`app` is not a public library API, so app-private headers stay in `src/app`.

### `core`

Toolchain-independent build semantics:

- build requests and plans;
- translation-unit and artifact identity;
- compile/link/archive cache models;
- project artifact layout;
- dependency graphs and typed options.

`core` must not know `cl.exe`, `link.exe`, `lib.exe`, Windows quoting, or `CreateProcessW`.

### `config`

Owns the versioned `mqb.json` model, strict parsing, and policy resolution. Unknown fields, wrong types, duplicate keys, and unsupported schema must fail closed.

### `discovery`

Selects **candidate sources** only: include traversal, entry reachability, project corrections, and module-candidate detection.

Discovery does not decide final module providers and does not own header freshness.

### `modules`

Owns the typed P1689 model and module dependency graph:

- project-local named-module providers;
- project-local header units;
- external/prebuilt read-only IFC providers;
- toolchain-owned `std` / `std.compat` providers;
- provider ambiguity, conflicts, cycles, and unresolved requirements.

Provider ownership has one authoritative implementation. Other layers may not guess IFCs from file names.

### `orchestration`

Composes execution flows:

- bounded scan/compile scheduling;
- incremental compile/link/archive;
- ordinary-target pipeline;
- module scan/compile waves;
- target routing;
- demand-driven injection of toolchain-owned standard modules.

It coordinates primitives but does not own CLI parsing or reimplement MSVC argument spelling.

### `msvc`

MSVC backend primitive layer:

- Visual Studio / portable toolchain discovery;
- compiler / linker / librarian invocation construction;
- `/sourceDependencies` reading;
- `/scanDependencies` scanning;
- library resolution;
- discovery of the current VC Tools `std.ixx` / `std.compat.ixx` capability.

### `process` / `platform/windows`

`process` defines platform-independent executable, argv, cwd, environment, result, and error models.

`platform/windows` owns Windows command-line encoding and `CreateProcessW`. MQB has no general-purpose shell-command-string API internally.

## 4. Ordinary build pipeline

```text
CLI/config
   ↓
source selection
   ↓
artifact preflight
   ↓
compile identity + freshness
   ↓
bounded incremental compile
   ↓
link/archive identity + freshness
   ↓
incremental link or archive
   ↓
.mqb/bin/<target>
```

Header freshness is driven by compiler-produced source-dependency metadata rather than directory timestamp guesses.

Compile, link, and archive caches are independent. Rebuilding one layer does not imply unconditional work at every other layer.

## 5. Modules / Header Units pipeline

```text
selected source candidates
        ↓
artifact preflight
        ↓
bounded /scanDependencies
        ↓
P1689 typed rules
        ↓
provider resolution
   ┌────┼─────────────────────┐
   │    │                     │
project external IFC     std/std.compat?
source   read-only            │
   │        │                 └─> selected VC Tools module source
   │        │                              ↓
   │        │                       /scanDependencies
   │        │                              ↓
   └────────┴──────────────> provider graph fixed point
                                   ↓
                         dependency-level compile waves
                                   ↓
                           incremental final link
```

### Project-local providers

Module interfaces and header units from project sources receive MQB-owned IFC, OBJ, dependency metadata, and cache artifacts. P1689 determines provider/consumer relationships.

### External/prebuilt providers

Users declare these explicitly with `modules.external` or `--module-ifc name=path.ifc`. They are:

- read-only dependencies;
- excluded from source discovery;
- excluded from MQB compile levels;
- included in consumer compile/cache identity;
- fail-closed when missing or conflicting.

### `std` / `std.compat`

Standard-library named modules belong to the selected MSVC toolchain, not the ordinary external-provider registry.

Only when P1689 actually requires `std` or `std.compat` does MQB locate the corresponding VC Tools module source. That provider source also passes through `/scanDependencies`, so closure such as `std.compat -> std` comes from toolchain metadata rather than a hard-coded edge.

Generated IFC/OBJ/cache artifacts still belong to the current project `.mqb/`; MQB never writes into the Visual Studio installation. Their identity includes provider source, compiler recipe, and toolchain identity.

## 6. Build identity and caches

`BuildSignature` represents a versioned compile recipe. Reusing a compile artifact requires identity that covers at least:

- source identity and TU kind;
- selected toolchain identity;
- configuration, architecture, and language standard;
- runtime/LTCG and other typed compile policy;
- ordered compiler arguments;
- typed module/header-unit references;
- required outputs.

Key rules:

- source identity must not collapse to a basename;
- Windows physical aliases must converge to one artifact identity;
- changing an external/prebuilt IFC must invalidate dependent consumer caches;
- changing toolchain identity must not silently reuse incompatible `std` IFCs;
- a missing recorded output is itself a stale signal;
- job count and runtime program argv are execution policy, not build identity.

## 7. Artifact layout

All MQB-owned writable state lives under the project root:

```text
.mqb/
├─ obj/
├─ deps/
├─ scan/
├─ ifc/
├─ cache/
│  ├─ compile/
│  ├─ link/
│  └─ archive/
└─ bin/
```

Source directories do not contain MQB intermediates. Writable artifacts are ownership/collision-preflighted before execution so two logical outputs cannot silently target the same physical path.

## 8. Project and path model

MQB has two independent relative-path bases:

```text
CLI path      -> invocation directory
config path   -> directory containing mqb.json
```

When `mqb.json` exists, its directory is the project root and `.mqb/` root. Without it, the project root is derived from invocation/source context.

See [`MQB_CONFIG_EN.md`](MQB_CONFIG_EN.md) for full parsing and precedence behavior.

## 9. Physical source layout

MQB has one product tree:

```text
cpp/
├─ include/
├─ src/
├─ tests/
└─ mqb.json
```

Those roots are organized by responsibility such as `core / config / discovery / modules / orchestration / msvc / process / platform`. This document does not duplicate the filesystem contract; [`../cpp/README_EN.md`](../cpp/README_EN.md) is authoritative.

## 10. Architecture invariants

1. Core does not depend on MSVC executable spelling or Windows process APIs.
2. Internal process invocation remains structured as executable + argv.
3. Discovery selects candidates; `/sourceDependencies` and P1689 own header/module truth respectively.
4. Module provider selection has one owner.
5. Header units and named modules retain distinct typed identities.
6. External IFCs are always read-only dependencies, never MQB-owned writable artifacts.
7. `std` / `std.compat` always belong to the selected toolchain and cannot be overridden by the project.
8. Compile/link/archive cache state is independent.
9. Writable artifacts are checked for conflicts before execution.
10. Correctness beats cache hit rate; unsupported or ambiguous states fail closed.
11. `cpp/include`, `cpp/src`, and `cpp/tests` each have one physical root.
12. MQB uses MQB as the build system for its own development, tests, and release builds.

## 11. Current boundary

The current `exe` / `dll` module pipeline supports project-local modules/header units, external/prebuilt IFCs, and toolchain-owned `std` / `std.compat`.

`static` targets use a separate archive pipeline; **a static-library target that requires the Modules/Header Units pipeline is rejected explicitly**. This is a current product boundary and must not be bypassed by guessing or silently degrading behavior in another layer.

See [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) for contributor entry points and [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) for stable self-hosting/release gates.
