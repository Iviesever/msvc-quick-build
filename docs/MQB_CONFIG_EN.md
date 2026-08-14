# `mqb.json` Configuration Reference

**[简体中文](MQB_CONFIG.md) | English**

`mqb.json` is MQB's versioned project configuration file. MQB searches upward from the invocation directory for the nearest `mqb.json`. Once found, its directory becomes:

- the project root;
- the base for configuration- and profile-relative paths;
- the root of writable `.mqb/` build state.

## 1. Minimal configuration

```json
{
  "version": 1
}
```

`version` is required. Version 1 uses strict JSON: duplicate keys, unknown fields, wrong types, empty string entries, comments, and trailing commas are rejected rather than ignored.

The root object accepts only:

```text
version
build
discovery
modules
profiles
```

## 2. Complete example

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
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
    "libraries": ["user32", "codec.lib"],
    "compiler_args": ["/W4"],
    "linker_args": ["/OPT:NOREF"]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests", "tools"],
    "extra_sources": ["src/manual_adapter.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  },
  "modules": {
    "external": {
      "vendor.math": "third_party/ifc/vendor.math.ifc"
    }
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "runtime": "MDd",
        "compiler_args": ["/W4"]
      },
      "discovery": {
        "enabled": true
      }
    },
    "release": {
      "build": {
        "configuration": "release",
        "ltcg": true,
        "compiler_args": ["/O2"]
      }
    }
  }
}
```

## 3. `build`

| Field | Type | Meaning |
|---|---|---|
| `entry` | string | default entry used by `mqb build` / `mqb run` when no positional source is provided; relative to `mqb.json` |
| `configuration` | string | `debug` / `release` |
| `architecture` | string | `x86` / `x64` |
| `standard` | string | `14`, `17`, `20`, `23`, `latest`; `c++...` spellings are also accepted |
| `type` | string | `exe`, `dll`, `static`; aliases: `executable`, `dynamic`, `lib` |
| `runtime` | string | `MD`, `MDd`, `MT`, `MTd` |
| `ltcg` | boolean | couples compile `/GL` with downstream `/LTCG` |
| `subsystem` | string | `console` / `windows`; not accepted for static targets |
| `output` | string | target name under `.mqb/bin/`; extension follows target kind |
| `defines` | string[] | preprocessor definitions without `/D` |
| `include_dirs` | string[] | include search paths |
| `library_dirs` | string[] | library search paths; not accepted for static targets |
| `libraries` | string[] | link libraries; not accepted for static targets |
| `compiler_args` | string[] | ordered raw argv elements passed to `cl.exe` |
| `linker_args` | string[] | ordered raw linker argv elements; not accepted for static targets |

### Default entry

`build.entry` is a project-level default entry, not a source list. It participates only when these command forms omit every positional source:

```powershell
mqb build
mqb run
```

Entry selection is fixed as:

```text
explicit positional source(s)
    > build.entry
    > conventional fallback
```

The conventional fallback **does not recursively scan the project**. It checks only:

```text
<project-root>/main.c
<project-root>/main.cpp
<project-root>/main.cc
<project-root>/main.cxx
<project-root>/src/main.c
<project-root>/src/main.cpp
<project-root>/src/main.cc
<project-root>/src/main.cxx
```

Exactly one candidate is required. Zero candidates require an explicit source or `build.entry`; multiple candidates require explicit disambiguation. If `build.entry` is configured but missing or has an unsupported translation-unit extension, MQB fails on that configured entry instead of silently falling back to a conventional main.

Explicit sources always win, for example:

```powershell
mqb run tools/tool.cpp
```

Even when `build.entry` exists, this builds and runs `tools/tool.cpp`.

`build.entry` may appear **only in the root `build` layer**. Profiles are build-policy overlays and cannot change project identity through `profiles.<name>.build.entry`; strict schema validation rejects that field.

### Typed policy

`type`, `runtime`, `ltcg`, and `subsystem` are typed MQB policies, not string aliases for MSVC flags. The backend owns the final command-line spelling; raw compiler/linker arguments should not be used to bypass conflicting typed policy.

`ltcg: true` affects both compile and the downstream target:

- compile: `/GL`;
- executable / DLL: linker `/LTCG`;
- static library: librarian `/LTCG`.

A DLL import library is placed beside the DLL under `.mqb/bin/`. Static targets use a dedicated `lib.exe` archive pipeline and archive cache.

## 4. `discovery`

| Field | Type | Meaning |
|---|---|---|
| `enabled` | boolean | enable single-entry smart discovery |
| `exclude_dirs` | string[] | project directories pruned before graph construction |
| `extra_sources` | string[] | exact translation-unit paths that are always added |
| `exclude_sources` | string[] | exact paths that are excluded and act as traversal barriers |

Supported translation-unit extensions:

```text
C:                  .c
C++:                .cpp .cc .cxx
module interface:   .ixx .cppm .mpp
```

Rules:

- an entry selected through `build.entry` or the unique conventional fallback enters the same smart-discovery path as an explicit single entry;
- multiple positional sources already form an exact source set and do not depend on smart discovery;
- v1 uses exact paths, not globs;
- the entry TU cannot be excluded;
- one file cannot appear in both `extra_sources` and `exclude_sources`;
- built-in excluded directories such as `.mqb`, `.git`, `.vs`, `build`, `out`, and `cmake-build-*` remain active.

Discovery selects candidate sources only. Header freshness is authoritative from `/sourceDependencies`; module topology is authoritative from `/scanDependencies` / P1689.

## 5. `modules`

### Project-local modules / header units

Project-local named modules and header units need no extra configuration. MQB sends candidate sources through the `/scanDependencies` / P1689 pipeline and uses the resulting provider graph to determine dependencies and compile ordering.

Modules/Header Units require C++20 or later.

### External / prebuilt named modules

`modules.external` maps a logical module name to an explicit **read-only IFC**:

```json
{
  "version": 1,
  "modules": {
    "external": {
      "vendor.math": "third_party/ifc/vendor.math.ifc",
      "vendor.io": "C:/sdk/vendor.io.ifc"
    }
  }
}
```

Relative IFC paths in configuration are resolved relative to the directory containing `mqb.json`.

CLI equivalent:

```powershell
mqb main.cpp --module-ifc vendor.math=C:\sdk\vendor.math.ifc
```

Behavior:

- an external IFC is a dependency, not an MQB-owned writable artifact;
- it is not added to source discovery or compile levels;
- a missing provider fails closed before the real consumer compile begins;
- provider identity participates in consumer compile/cache identity, so replacing the IFC invalidates affected consumers;
- base config, selected profile, and CLI merge providers by logical name; a later layer replaces the matching earlier provider in place;
- declaring the same logical name more than once on the CLI is an error.

### `std` / `std.compat`

`std` and `std.compat` are **not** members of the `modules.external` registry. They belong to the selected MSVC toolchain and cannot be spoofed or overridden with configuration or `--module-ifc`.

Only when a P1689 requirement actually references `std` or `std.compat` does MQB query the current VC Tools standard-library module source and inject it as a toolchain-owned provider into the same module graph. Current MSVC standard-library named modules require a supporting toolchain and `--std latest`.

## 6. `profiles`

`profiles` maps profile names to configuration overlays. Each profile accepts only:

```text
build
discovery
modules
```

Those sections reuse the same typed-policy, list, path, and module-provider decoding rules as the root layers, except that profile-local `build.entry` is prohibited.

Example:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "standard": "23",
    "defines": ["PROJECT=1"]
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "runtime": "MDd",
        "defines": ["DEV=1"],
        "compiler_args": ["/W4"]
      }
    },
    "release": {
      "build": {
        "configuration": "release",
        "ltcg": true,
        "compiler_args": ["/O2"]
      }
    }
  }
}
```

Select a profile explicitly:

```powershell
mqb build --profile release
mqb run --profile dev -- input.txt
```

The equals form is also accepted:

```powershell
mqb build --profile=release
```

The current contract is deliberately small and deterministic:

- at most one profile may be selected per invocation; duplicate `--profile` is an error;
- there is no profile inheritance;
- multi-profile stacking is not supported;
- there is no implicit default profile; omitting `--profile` means base config + CLI only;
- `--profile` requires an `mqb.json` project configuration;
- an unknown profile fails closed and reports the available profile names;
- relative paths inside a profile use the `mqb.json` directory as their base, exactly like root config paths;
- profile `compiler_args` / `linker_args` still flow through the same MSVC Parameter Engine. Semantic native options are normalized within the profile layer before higher-precedence CLI policy is applied;
- a profile may select typed policy such as `type`, so the final target still passes through the same executable/static/DLL validity gates.

## 7. Precedence

Scalar policy:

```text
explicit CLI > selected profile > base mqb.json > built-in default
```

This covers:

```text
configuration
architecture
standard
type
runtime
ltcg
subsystem
output
```

Within one layer, typed policy and equivalent native MSVC semantic options must agree or MQB fails closed before toolchain execution. For example, a profile that declares `runtime: "MT"` and `/MD` is rejected.

Entry selection is a separate source-selection policy:

```text
explicit positional source(s) > root build.entry > unique conventional main
```

Ordinary list-like inputs append deterministically:

```text
base mqb.json entries -> selected profile entries -> CLI entries
```

This applies to defines, include dirs, library dirs, libraries, compiler args, linker args, and discovery list corrections.

The external module provider registry merges by logical module name. A matching entry in a later layer replaces the earlier provider rather than creating a duplicate.

## 8. Path bases

MQB distinguishes two relative-path bases:

```text
CLI relative path                 -> invocation directory
mqb.json / profile relative path  -> directory containing mqb.json
```

`build.entry` uses the second base but is valid only in the root build layer.

Example:

```text
project/
├─ mqb.json
├─ src/main.cpp
├─ include/
└─ nested/work/
```

Configuration:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "include_dirs": ["include"]
  },
  "profiles": {
    "dev": {
      "build": {
        "include_dirs": ["third_party/dev/include"]
      }
    }
  }
}
```

From `project/nested/work`:

```powershell
mqb run --profile dev
```

MQB still loads `project/mqb.json`, resolves the entry as `project/src/main.cpp`, the base include directory as `project/include`, the profile include directory as `project/third_party/dev/include`, and places writable artifacts under `project/.mqb/`.

Explicit CLI sources and paths remain invocation-relative:

```powershell
mqb ../../src/main.cpp
```

## 9. Cache semantics

MQB does not treat "the config file timestamp changed" or "the profile name changed" as a global rebuild signal. Identity is derived from the **final effective build semantics**.

`build.entry` does not add a separate compile/link identity field; it selects the source-discovery entry for this invocation. The profile name is likewise not a separate cache dimension. Two profiles that resolve to identical effective build policy/source graphs should reuse the same existing cache identity.

Typical effects:

- `runtime`, compiler args, and typed module references change compile identity;
- `ltcg` changes compile and link/archive identity;
- `subsystem` changes link identity only;
- libraries, library dirs, and linker args change link identity;
- `type` changes downstream target ownership;
- discovery/profile corrections affect participating TUs through the final source set;
- changing an external/prebuilt IFC invalidates dependent consumer compile caches;
- changing the selected MSVC toolchain identity does not silently reuse incompatible toolchain-owned standard-library IFCs.

Missing recorded outputs also invalidate the corresponding compile/link/archive cache and trigger repair.

`-j/--jobs` is execution policy. It is not part of build identity and is not a v1 configuration field.

## 10. Explicit current boundaries

- `exe`, `dll`, and `static` are supported;
- `mqb run` and source-first `--run` apply only to executables;
- static targets do not accept subsystem, library paths/libraries, or linker args;
- **static-library targets that require the Modules/Header Units pipeline still fail closed**;
- discovery corrections use exact paths and do not support globs;
- conventional default-entry fallback is non-recursive and does not use globs;
- profiles currently support one explicit selection only: no inheritance, multi-profile stacking, or implicit default profile;
- profiles cannot override `build.entry`;
- freshness through indirect `/DEFAULTLIB`-style library propagation is not guaranteed to be complete.

See [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) for deeper provider-graph, artifact-identity, and responsibility boundaries.