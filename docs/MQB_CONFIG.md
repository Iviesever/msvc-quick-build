# `mqb.json` Project Configuration v1

`mqb.json` is the C++ V2 project configuration file for MSVC Quick Build.

MQB searches upward from the directory where it is invoked and uses the nearest `mqb.json`. The directory containing that file becomes the project root and the root of `.mqb/` artifacts.

## Minimal file

```json
{
  "version": 1
}
```

`version` is required. Version 1 uses strict JSON: comments, trailing commas, duplicate keys, unknown schema fields, incorrect field types, and empty entries in string arrays are rejected.

## Full example

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x64",
    "standard": "17",
    "type": "exe",
    "runtime": "MT",
    "subsystem": "console",
    "output": "game",
    "defines": [
      "GAME_BUILD=1"
    ],
    "include_dirs": [
      "include",
      "third_party/include"
    ],
    "library_dirs": [
      "third_party/lib"
    ],
    "libraries": [
      "math",
      "codec.lib"
    ],
    "compiler_args": [
      "/W4"
    ],
    "linker_args": [
      "/OPT:NOREF"
    ]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": [
      "tests",
      "tools"
    ],
    "extra_sources": [
      "src/manual_adapter.cpp"
    ],
    "exclude_sources": [
      "src/legacy.cpp"
    ]
  }
}
```

## Build fields

| Field | Type | Values / meaning |
|---|---|---|
| `configuration` | string | `debug` or `release` |
| `architecture` | string | `x86` or `x64` |
| `standard` | string | `14`, `17`, `20`, `23`, or `latest` (`c++14`, `c++17`, `c++20`, `c++23`, `c++latest` are also accepted) |
| `type` | string | `exe`, `dll`, or `static` (`executable`, `dynamic`, and `lib` aliases are also accepted) |
| `runtime` | string | MSVC CRT runtime: `MD`, `MDd`, `MT`, or `MTd` |
| `subsystem` | string | PE subsystem: `console` or `windows`; not valid for a static target |
| `output` | string | target name under `.mqb/bin/`; MQB supplies the `.exe`, `.dll`, or `.lib` suffix from `type` |
| `defines` | string array | preprocessor definitions, without `/D` |
| `include_dirs` | string array | include search directories |
| `library_dirs` | string array | library search directories; linker-only and therefore invalid for a static target |
| `libraries` | string array | requested libraries; `.lib` is optional for ordinary names; linker-only and invalid for a static target |
| `compiler_args` | string array | ordered raw `cl.exe` argv elements |
| `linker_args` | string array | ordered raw `link.exe` argv elements; invalid for a static target |

All path-valued config entries are resolved relative to the directory containing `mqb.json`, not the shell's current working directory.

`type`, `runtime`, and `subsystem` are typed policy, not shorthand for raw flags. The MSVC backend remains the sole owner of their command-line spelling. An explicit typed runtime is emitted after raw compiler arguments; typed DLL/output/subsystem routing is emitted after raw linker arguments, so conflicting raw `/MD*`, `/DLL`, `/IMPLIB`, `/SUBSYSTEM:*`, or `/OUT:*` switches cannot silently override the typed project policy.

For a DLL target that exports symbols, MQB supplies a deterministic import-library location beside the DLL (`.mqb/bin/<target>.lib`). Linker side outputs that were actually produced are recorded in the link cache; deleting a recorded import library or export file invalidates link freshness and causes a repair relink without recompiling otherwise-fresh translation units.

For a static target, MQB routes the compiled ordinary C/C++ object set to the dedicated `lib.exe` librarian pipeline. Static archive metadata lives under `.mqb/cache/archive/<target>.archivecache`, separate from executable/DLL link cache metadata. Linker-only policy (`subsystem`, `library_dirs`, `libraries`, or `linker_args`) is rejected for static targets rather than silently ignored. Static targets that require the named-module/header-unit pipeline remain fail-closed until archive topology is explicitly validated.

Raw compiler/linker arguments are literal argv elements. MQB does not split one JSON string containing spaces into several switches. For example:

```json
"compiler_args": ["/W4", "/WX"]
```

represents two compiler arguments, while `"/W4 /WX"` is one argument and is not rewritten by MQB.

Backend-owned structured routing remains authoritative. Raw arguments are emitted before MQB's planned output/topology switches such as `/Fo`, `/ifcOutput`, `/scanDependencies`, `/DLL`, `/IMPLIB`, `/MACHINE`, `/SUBSYSTEM`, and `/OUT`.

## Discovery fields

| Field | Type | Meaning |
|---|---|---|
| `enabled` | boolean | enable or disable single-entry smart source discovery |
| `exclude_dirs` | string array | exact project directories to prune before discovery graph construction |
| `extra_sources` | string array | exact supported C/C++ translation units to add even when disconnected from the entry graph |
| `exclude_sources` | string array | exact supported C/C++ translation units to exclude and treat as traversal barriers |

Supported translation-unit extensions in the current V2 source classifier are:

```text
C ordinary source:     .c
C++ ordinary source:   .cpp .cc .cxx
module interface:      .ixx .cppm .mpp
```

C sources participate in ordinary include/main smart discovery but are never parsed as C++ module syntax. This keeps legal C identifiers such as `module`, `import`, or `export` from routing a C target into the P1689 module pipeline.

Version 1 intentionally uses exact paths rather than a glob language. This keeps correction semantics deterministic while the discovery model is still being stabilized.

An ordinary `extra_sources` entry may not define another `main()`. The entry TU itself may not be excluded. A source may not appear in both `extra_sources` and `exclude_sources`.

Built-in directory exclusions such as `.mqb`, `.git`, `.vs`, `build`, `out`, and `cmake-build-*` remain active in addition to configured exclusions.

### Named modules and header units

Project-local named modules and project-local header units do **not** require separate v1 config sections.

For a single ordinary entry such as:

```cpp
import math;
int main() { return answer(); }
```

smart discovery may select a reachable local `.ixx/.cppm/.mpp` provider candidate. Discovery does not decide which candidate is authoritative; MSVC `/scanDependencies` P1689 metadata remains responsible for provider selection, dependency ordering, ambiguity/cycle diagnostics, and unresolved requirements.

Header-unit lexical syntax stays out of ordinary TU discovery edges, but it routes the target through the same authoritative P1689 module pipeline. Project-local header-unit IFCs are allocated, built, freshness-tracked, and repaired automatically.

Modules and header units require C++20 or newer. Selecting `standard: "14"` or `"17"` for an ordinary target is supported; if that target requires module/header-unit scanning, MQB fails closed before launching the unsupported module operation.

If a selected source contains named-module syntax but no supported local provider is found, the target fails closed. External/prebuilt named-module providers and `import std` remain tracked separately in issue #16.

## Precedence

For scalar options:

```text
explicit CLI option > mqb.json > built-in default
```

This includes `configuration`, `architecture`, `standard`, `type`, `runtime`, `subsystem`, and `output`.

Examples:

```powershell
# mqb.json says release; this build is debug.
mqb main.cpp --debug

# mqb.json says DLL or static; this invocation explicitly builds an executable.
mqb main.cpp --type exe

# mqb.json says MT; this invocation uses the dynamic CRT.
mqb main.cpp --runtime MD

# mqb.json says windows; this invocation uses the console subsystem.
mqb main.cpp --subsystem console

# mqb.json disables discovery; this explicitly turns it back on.
mqb main.cpp --discover

# mqb.json output is ignored for this invocation.
mqb main.cpp -o scratch
```

MQB tracks whether scalar CLI options were explicitly supplied, so parser defaults do not overwrite project configuration.

List-like options are additive rather than replacing the config list. The effective order is:

```text
mqb.json entries, then CLI entries
```

This applies to defines, include directories, library directories, libraries, `compiler_args`, and `linker_args`. For example, a CLI `--compiler-arg /WX` is appended after project `compiler_args` entries. If the effective target kind is `static`, any effective linker-only list remains invalid and fails closed.

## Path bases

There are two intentional relative-path bases:

```text
CLI relative paths      -> invocation directory
mqb.json relative paths -> mqb.json directory
```

This makes nested-directory invocation predictable. For example, with:

```text
project/
  mqb.json
  main.cpp
  include/
  nested/work/
```

running from `project/nested/work`:

```powershell
mqb ../../main.cpp
```

still loads `project/mqb.json`, places artifacts under `project/.mqb/`, and interprets config `"include_dirs": ["include"]` as `project/include`.

## Cache behavior

The config file timestamp itself is not a special global rebuild trigger. Instead, the effective typed build options produced from the file participate in the existing compile/link/archive identities.

Changing `runtime` changes compiler recipe identity, recompiles affected TUs, and therefore rebuilds the downstream target. Leaving `runtime` unset preserves the historical Debug `/MDd` and Release `/MD` recipes and their existing signature identity.

Changing `type` changes target output/recipe ownership. The historical executable signature byte stream is retained for executable targets; DLL targets add typed link target-kind identity. Static targets use a separate archive identity/cache and the `lib.exe` pipeline. Switching among executable/DLL/static does not recompile otherwise-fresh translation units when the compile recipe is unchanged, but it does build the appropriate downstream target artifact.

Changing only `subsystem` changes link recipe identity and relinks without recompiling otherwise-fresh TUs. `subsystem` is not valid when the effective target kind is static.

Changing a compiler argument changes compiler recipe identity and recompiles affected TUs even if no source timestamp changed. The resulting fresh objects force the downstream target action. Changing only a linker argument changes link recipe identity and relinks without recompiling otherwise-fresh TUs; linker arguments are invalid for static targets.

Likewise, library names/search paths affect link recipe identity, while exact resolved `.lib` files are separately tracked as link freshness inputs. Recorded DLL linker side outputs are separately checked for existence and repaired by relinking if missing. Static archive freshness separately checks its current object inputs, archive output, and librarian identity.

For named modules and header units, compile identity also includes typed provider/reference and interface-output identity. Imported IFC files participate in consumer freshness validation, and a missing provider IFC invalidates the provider's cached outputs.

## Parallelism

`-j/--jobs` is intentionally **not** a v1 config field. It is execution policy only:

```powershell
mqb main.cpp -j 8
```

Changing the job count does not alter compile, link, or archive signatures and therefore must not invalidate an otherwise reusable build cache.

## Current boundaries

- v1 `build.type` supports `exe`, `dll`, and `static`; `lib` is accepted as a static-library alias.
- Static targets currently support ordinary C/C++ translation units; static targets requiring named Modules/Header Units fail closed until archive topology is explicitly validated.
- `--run` is executable-only; DLL/static targets are built artifacts and are not launched as programs.
- v1 config has no external/prebuilt module-provider or `import std` policy.
- v1 config does not store parallel job count.
- `exclude_dirs`, `extra_sources`, and `exclude_sources` are exact paths, not globs.
- Explicit user libraries are freshness-tracked; indirect `/DEFAULTLIB` transitive dependencies are not claimed as fully tracked.

For stable-v5 migration decisions, see [`V5_PARITY.md`](V5_PARITY.md). For the broader build/module/cache architecture, see [`CPP_V2_ARCHITECTURE.md`](CPP_V2_ARCHITECTURE.md).
