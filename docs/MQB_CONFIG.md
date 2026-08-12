# `mqb.json` project configuration v1

`mqb.json` is MQB's project configuration file. MQB searches upward from the invocation directory for the nearest `mqb.json`; the directory containing that file becomes the project root and the root of `.mqb/` artifacts.

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
    "standard": "23",
    "type": "exe",
    "runtime": "MT",
    "ltcg": true,
    "subsystem": "console",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include", "third_party/include"],
    "library_dirs": ["third_party/lib"],
    "libraries": ["math", "codec.lib"],
    "compiler_args": ["/W4"],
    "linker_args": ["/OPT:NOREF"]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests", "tools"],
    "extra_sources": ["src/manual_adapter.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  }
}
```

## Build fields

| Field | Type | Values / meaning |
|---|---|---|
| `configuration` | string | `debug` or `release` |
| `architecture` | string | `x86` or `x64` |
| `standard` | string | `14`, `17`, `20`, `23`, or `latest`; `c++...` spellings are also accepted |
| `type` | string | `exe`, `dll`, or `static`; `executable`, `dynamic`, and `lib` aliases are also accepted |
| `runtime` | string | `MD`, `MDd`, `MT`, or `MTd` |
| `ltcg` | boolean | coupled `/GL` compile + downstream `/LTCG` policy |
| `subsystem` | string | `console` or `windows`; invalid for static targets |
| `output` | string | target name under `.mqb/bin/`; MQB supplies the target suffix |
| `defines` | string array | preprocessor definitions without `/D` |
| `include_dirs` | string array | include search directories |
| `library_dirs` | string array | library search directories; invalid for static targets |
| `libraries` | string array | requested libraries; invalid for static targets |
| `compiler_args` | string array | ordered raw `cl.exe` argv elements |
| `linker_args` | string array | ordered raw `link.exe` argv elements; invalid for static targets |

All path-valued config entries are resolved relative to the directory containing `mqb.json`, not the shell's current working directory.

`type`, `runtime`, `ltcg`, and `subsystem` are typed policy. The MSVC backend owns their command-line spelling, and typed policy remains authoritative over conflicting raw arguments.

Typed LTCG is coupled: `build.ltcg: true` adds `/GL` to affected compilation and `/LTCG` to the downstream target recipe. Executable/DLL targets pass `/LTCG` to `link.exe`; static targets pass it to `lib.exe`.

DLL import libraries are placed deterministically beside the DLL under `.mqb/bin/`. Static targets use the dedicated `lib.exe` archive pipeline and separate archive cache metadata.

Raw compiler/linker arguments are literal argv elements. MQB does not split one JSON string containing spaces into several switches.

## Discovery fields

| Field | Type | Meaning |
|---|---|---|
| `enabled` | boolean | enable or disable single-entry smart discovery |
| `exclude_dirs` | string array | exact project directories to prune before graph construction |
| `extra_sources` | string array | exact supported translation units to add even when disconnected from the entry graph |
| `exclude_sources` | string array | exact supported translation units to exclude and treat as traversal barriers |

Supported translation-unit extensions are:

```text
C ordinary source:     .c
C++ ordinary source:   .cpp .cc .cxx
module interface:      .ixx .cppm .mpp
```

C sources participate in ordinary include/main discovery but are never parsed as C++ module syntax.

Version 1 uses exact paths rather than a glob language. An ordinary `extra_sources` entry may not define another `main()`. The entry TU itself may not be excluded. A source may not appear in both `extra_sources` and `exclude_sources`.

Built-in exclusions such as `.mqb`, `.git`, `.vs`, `build`, `out`, and `cmake-build-*` remain active in addition to configured exclusions.

### Named modules and header units

Project-local named modules and project-local header units do not require separate v1 config sections. Smart discovery may select reachable local module-provider candidates, but MSVC `/scanDependencies` P1689 metadata remains authoritative for provider selection, dependency ordering, ambiguity/cycle diagnostics, and unresolved requirements.

Project-local header-unit IFCs are allocated, built, freshness-tracked, and repaired automatically. Modules and header units require C++20 or newer.

External/prebuilt named-module providers and `import std` remain unsupported and fail closed; that feature boundary is tracked in Issue #16.

## Precedence

For scalar options:

```text
explicit CLI option > mqb.json > built-in default
```

This includes `configuration`, `architecture`, `standard`, `type`, `runtime`, `ltcg`, `subsystem`, and `output`.

List-like options are additive in deterministic order:

```text
mqb.json entries, then CLI entries
```

This applies to defines, include directories, library directories, libraries, `compiler_args`, and `linker_args`.

## Path bases

There are two intentional relative-path bases:

```text
CLI relative paths      -> invocation directory
mqb.json relative paths -> mqb.json directory
```

For example, with:

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

still loads `project/mqb.json`, places artifacts under `project/.mqb/`, and interprets `"include_dirs": ["include"]` as `project/include`.

## Cache behavior

The config file timestamp is not a global rebuild trigger. Effective typed options participate in compile/link/archive identity instead.

- changing `runtime` changes compile recipe identity;
- changing `ltcg` changes compile identity and downstream link/archive identity;
- changing `type` changes downstream target ownership without forcing unrelated recompilation when compile policy is unchanged;
- changing only `subsystem` changes link identity and relinks;
- changing compiler arguments changes compile identity;
- changing linker arguments or explicit libraries/search paths changes link identity;
- named-module/header-unit compile identity includes typed provider/reference and IFC output identity.

Missing recorded outputs invalidate freshness and are repaired by the appropriate compile, link, or archive action.

## Parallelism

`-j/--jobs` is execution policy only and is intentionally not a v1 config field:

```powershell
mqb main.cpp -j 8
```

Changing job count does not change compile, link, or archive signatures.

## Current boundaries

- `build.type` supports `exe`, `dll`, and `static`.
- `build.ltcg` is a coupled compile/downstream boolean policy.
- Static targets support ordinary C/C++ translation units; static targets requiring named Modules/Header Units fail closed until archive topology is explicitly validated.
- `--run` is executable-only.
- v1 config has no external/prebuilt module-provider or `import std` policy.
- v1 config does not store parallel job count.
- discovery correction fields use exact paths, not globs.
- explicit user libraries are freshness-tracked; indirect `/DEFAULTLIB` transitive dependencies are not claimed as fully tracked.

For the broader build/module/cache architecture, see [`ARCHITECTURE.md`](ARCHITECTURE.md). For stable self-hosting, see [`SELF_HOSTING.md`](SELF_HOSTING.md).
