# `mqb.json` Configuration Reference

**[简体中文](MQB_CONFIG.md) | English**

`mqb.json` is MQB's versioned project configuration file. MQB searches upward from the invocation directory for the nearest `mqb.json`. Once found, its directory becomes:

- the project root;
- the base for configuration-relative paths;
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
```

## 2. Complete example

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
  }
}
```

## 3. `build`

| Field | Type | Meaning |
|---|---|---|
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
- a CLI provider overrides a config provider with the same logical name;
- declaring the same logical name more than once on the CLI is an error.

### `std` / `std.compat`

`std` and `std.compat` are **not** members of the `modules.external` registry. They belong to the selected MSVC toolchain and cannot be spoofed or overridden with configuration or `--module-ifc`.

Only when a P1689 requirement actually references `std` or `std.compat` does MQB query the current VC Tools standard-library module source and inject it as a toolchain-owned provider into the same module graph. Current MSVC standard-library named modules require a supporting toolchain and `--std latest`.

## 6. Precedence

Scalar policy:

```text
explicit CLI > mqb.json > built-in default
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

Ordinary list-like inputs append deterministically:

```text
mqb.json entries -> CLI entries
```

This applies to defines, include dirs, library dirs, libraries, compiler args, and linker args.

The external module provider registry merges by logical module name. A CLI entry replaces the config entry with the same name rather than merely appending another provider.

## 7. Path bases

MQB distinguishes two relative-path bases:

```text
CLI relative path      -> invocation directory
mqb.json relative path -> directory containing mqb.json
```

Example:

```text
project/
├─ mqb.json
├─ main.cpp
├─ include/
└─ nested/work/
```

From `project/nested/work`:

```powershell
mqb ../../main.cpp
```

MQB still loads `project/mqb.json`, resolves `"include_dirs": ["include"]` as `project/include`, and places writable artifacts under `project/.mqb/`.

## 8. Cache semantics

MQB does not treat "the config file timestamp changed" as a global rebuild signal. Identity is derived from the **effective build semantics**.

Typical effects:

- `runtime`, compiler args, and typed module references change compile identity;
- `ltcg` changes compile and link/archive identity;
- `subsystem` changes link identity only;
- libraries, library dirs, and linker args change link identity;
- `type` changes downstream target ownership;
- changing an external/prebuilt IFC invalidates dependent consumer compile caches;
- changing the selected MSVC toolchain identity does not silently reuse incompatible toolchain-owned standard-library IFCs.

Missing recorded outputs also invalidate the corresponding compile/link/archive cache and trigger repair.

`-j/--jobs` is execution policy. It is not part of build identity and is not a v1 configuration field.

## 9. Explicit current boundaries

- `exe`, `dll`, and `static` are supported;
- `--run` applies only to executables;
- static targets do not accept subsystem, library paths/libraries, or linker args;
- **static-library targets that require the Modules/Header Units pipeline currently fail closed**;
- discovery corrections use exact paths, not globs;
- transitive `/DEFAULTLIB` dependencies are not promised to have complete freshness tracking.

See [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) for the lower-level provider graph, artifact identity, and responsibility boundaries.
