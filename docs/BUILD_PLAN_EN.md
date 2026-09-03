# Build-plan inspection

**English | [简体中文](BUILD_PLAN.md)**

`mqb plan` explains the incremental work MQB would perform for the current source tree, configuration, selected MSVC toolchain, and existing project-local cache evidence.

```powershell
mqb plan
mqb plan src/main.cpp --format text
mqb plan src/main.cpp modules/math.ixx --no-discover --std latest --format json
```

The command uses the same typed scan, compile, link, archive, PCH, and module-graph authorities as a real build. It does not execute a compile, `/scanDependencies`, link, or archive action, and it does not create or rewrite project `.mqb/` state. Toolchain discovery may still inspect the installed Visual Studio environment before a plan can be constructed.

## Output formats

`--format text` is the default human-readable form. `--format json` emits deterministic UTF-8 JSON suitable for tools and tests.

Every step reports:

- `kind`, such as `module_scan`, `pch`, `compile`, `link`, or `archive`;
- `status`: `planned` or `up_to_date`;
- typed rebuild reasons;
- owned output paths;
- an exact structured process recipe only when the step is planned.

A process recipe contains the executable, ordered argv, working directory, environment policy, and environment removals. An up-to-date step deliberately has no process field because no process would run.

Module-aware output can additionally report:

- `pipeline: "modules"`;
- `module_graph.status`: `pending` or `ready`;
- dependency-level `compile_levels` when the graph is ready;
- step `owner`: `project` or `toolchain`;
- step `role`: `translation_unit`, `module_interface`, or `header_unit`;
- the compile `level` for graph nodes.

The JSON document remains version 1. These fields are additive to the existing plan format.

## Ordinary targets, PCH, and static libraries

Ordinary C/C++ executable and DLL plans inspect compile and final link decisions. First-class PCH plans include the PCH creator and propagate planned PCH work to its consumers and final link. Static-library plans expose the exact deterministic transactional `lib.exe` recipe while retaining the final `.lib` as the public output.

## Modules and Header Units

MQB does not infer module topology from filenames or source spelling. The dependency graph is authoritative only after reusable MSVC P1689 output exists for every required source.

### Cold or stale topology

For a new module target, or whenever any required P1689 evidence is stale, `mqb plan` emits the exact planned `/scanDependencies` steps and reports:

```text
pipeline: modules
graph:   pending
```

At this boundary MQB does **not** invent providers, compile waves, Header Unit producers, `std` / `std.compat` providers, or a link decision. Those stages depend on the scan output that has not been produced yet.

Run a real build to create and validate the missing evidence:

```powershell
mqb build src/main.cpp modules/math.ixx --no-discover --std latest
```

### Warm trustworthy topology

When all required P1689 evidence is reusable, the plan continues through the real provider graph and reports:

- project-local named-module providers;
- project-local Header Unit producers;
- explicit external/prebuilt IFC providers as read-only dependencies;
- selected MSVC toolchain-owned `std` / `std.compat` providers;
- dependency-level compile ordering;
- exact `/reference` and `/headerUnit` compiler recipes for planned nodes;
- the final incremental link decision.

If a provider IFC is missing while topology remains reusable, the provider is planned, downstream consumers receive the typed `explicit rebuild` reason, and final link work is planned. Inspection itself does not repair the missing artifact.

If a source change makes P1689 stale, the graph returns to `pending`; stale compile levels and link decisions are suppressed rather than displayed as though they were still trustworthy.

## Side-effect contract

For project build state, `mqb plan` is read-only:

- no compile or module-scan process;
- no `link.exe` or `lib.exe` process;
- no OBJ, IFC, PCH, executable, DLL, or static-library output;
- no dependency-metadata write;
- no compile, link, archive, PCH, or scan-cache write;
- no repair of missing or malformed artifacts.

The result describes the current evidence. A subsequent real build revalidates filesystem and cache state at execution time; it does not blindly trust an earlier plan snapshot.

## Current boundaries

- Executable and DLL targets support module-aware planning.
- Static-library targets that require the Modules/Header Units pipeline still fail closed because that product combination is not implemented.
- First-class PCH still does not mix with the Modules/Header Units pipeline.
- `mqb compdb` does not yet export graph-aware Modules/Header Units entries.
