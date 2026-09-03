# Compilation database export

**English | [简体中文](COMPILATION_DATABASE.md)**

`mqb compdb` writes a deterministic `compile_commands.json` from the same typed MSVC compile recipes used by real MQB builds.

```powershell
mqb compdb
mqb compdb src/main.cpp src/helper.cpp --no-discover
mqb compdb src/main.cpp modules/math.ixx --no-discover --std latest
mqb compdb --output out/compile_commands.json
```

The database uses the standard `directory`, `file`, `arguments`, and `output` fields. `arguments` is an argv array whose first element is the selected `cl.exe`; MQB does not flatten Windows command lines into an ambiguous shell string.

## Side-effect contract

The command performs project/config resolution, source discovery, artifact-layout modeling, and MSVC toolchain discovery. It does not run:

- `cl.exe` compilation;
- `cl.exe /scanDependencies`;
- `link.exe`;
- `lib.exe`;
- the target program.

It does not create or repair OBJ, IFC, PCH, dependency metadata, executable, DLL, or static-library artifacts. Publishing the requested compilation database is its only intended project-file write. Toolchain discovery may maintain MQB's existing project-local discovery cache according to normal policy.

The output is path-sorted using MQB's Windows path-identity rules and is byte-deterministic for the same effective inputs and toolchain.

## Ordinary C and C++

Ordinary source entries use the exact typed compile recipe that a real target build would use:

- selected compiler executable;
- ordered native compiler argv;
- source-parent working directory;
- typed standard, architecture, runtime, defines, and include directories;
- `/sourceDependencies` metadata path;
- MQB-owned object output.

First-class PCH projects export the consumer recipes, including `/Yu`, `/Fp`, and `/FI`. The synthetic PCH creator is not exported as a user translation unit, and `mqb compdb` does not materialize it.

## C++ Modules and Header Units

Module-aware export is graph-aware rather than filename-inferred. Exact `/reference` and `/headerUnit` arguments can only be modeled after all required MSVC P1689 documents are reusable from a prior successful build.

### Cold or stale topology

For a new module project, or after a source change invalidates required P1689 evidence, `mqb compdb` fails closed before publishing output:

```text
error: mqb compdb requires reusable P1689 topology for Modules/Header Units
```

The diagnostic identifies pending scans and their typed reasons. MQB does not run those scans and does not publish a partial database containing guessed providers or incomplete references.

Use:

```powershell
mqb plan src/main.cpp modules/math.ixx --no-discover --std latest
mqb build src/main.cpp modules/math.ixx --no-discover --std latest
mqb compdb src/main.cpp modules/math.ixx --no-discover --std latest
```

`mqb plan` explains the pending scan work; one successful build materializes and validates the P1689 topology; the following `mqb compdb` can then export exact recipes.

If an older compilation database already exists, a cold/stale failure leaves it unchanged rather than replacing it with partial JSON.

### Warm trustworthy topology

When the complete topology is reusable, the database includes compile entries for:

- project translation units;
- project named-module interfaces and partitions;
- project Header Unit producers;
- selected MSVC toolchain-owned `std` / `std.compat` providers when required.

Explicit external/prebuilt IFC providers remain read-only graph inputs and therefore do not receive a compile entry.

Named-module consumers carry exact `/reference name=<IFC>` mappings. Header Unit consumers carry exact `/headerUnit:quote` or `/headerUnit:angle` mappings. Provider and consumer ordering in the JSON is deterministic path order; dependency execution order remains represented by the arguments and MQB's P1689 graph, not by array position.

### Primary `output` field

Each entry exposes the primary product of that compile recipe:

| Entry kind | `output` |
|---|---|
| ordinary translation unit | OBJ |
| named-module interface or partition | OBJ |
| named-module consumer | OBJ |
| Header Unit producer | IFC |
| toolchain-owned `std` / `std.compat` provider | OBJ |

A named-module interface also produces an IFC through `/ifcOutput`; the standard compilation-database schema has one `output` field, so MQB uses its linkable object as the primary output while retaining the exact IFC path in `arguments`. Header Unit producers are IFC-only and therefore use the IFC as `output`.

## Missing compile artifacts versus stale topology

Missing OBJ or IFC files do not by themselves invalidate a reusable provider graph. In that case `mqb compdb` still exports the exact recipes and references, but it does not repair the missing artifact.

A source or dependency change that makes P1689 stale is different: provider topology may have changed, so export fails closed until a successful build refreshes and seals the scan evidence.

## Link independence

Compilation-database export uses the module target's scan/graph/compile inspection authority without inspecting final link freshness. Unresolved libraries or unrelated linker outputs cannot prevent a valid compile database from being generated. Static-library targets that require the Modules/Header Units pipeline remain unsupported because that product combination is not yet implemented by MQB itself.

## Tool integration

Point an editor, language server, or analysis tool at the generated file, commonly at the project root:

```text
<project>/compile_commands.json
```

Consumers must support MSVC-style argv to understand options such as `/std:`, `/reference`, `/headerUnit:*`, `/Yu`, and `/Fp`. The database records MQB's exact MSVC contract; it does not translate that contract into Clang- or GCC-specific flags.
