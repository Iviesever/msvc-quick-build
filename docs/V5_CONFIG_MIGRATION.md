# Stable v5 project-config migration: `msvc_list.json` -> `mqb.json`

Stable v5 does not treat `mqb.json` as a renamed `msvc_list.json`. The native schema is strict, typed, and intentionally different. This document records the supported semantic mapping and the boundaries that require user review.

## Direct semantic mapping

| Legacy `msvc_list.json` | Native `mqb.json` | Migration status |
| --- | --- | --- |
| `config` | `build.configuration` | direct: `debug` / `release` |
| `std` | `build.standard` | direct: `14` / `17` / `20` / `23` / `latest` |
| `output` | `build.output` | direct target-name mapping; artifact directory changes |
| `type` | `build.type` | direct for `exe` / `dll` / `static` |
| `runtime` | `build.runtime` | direct for `MD` / `MDd` / `MT` / `MTd` |
| `ltcg` | `build.ltcg` | direct boolean coupled LTCG policy |
| `subsystem` | `build.subsystem` | direct for executable/DLL targets |
| `defines` | `build.defines` | direct values; ordering caveat below |
| `include` | `build.include_dirs` | direct paths; config-relative paths remain config-root-relative |
| `libpath` | `build.library_dirs` | direct paths; config-relative paths remain config-root-relative |
| `libs` | `build.libraries` | direct library names; ordering caveat below |
| `flags` | `build.compiler_args` | direct raw argv values after reviewing tokenization |
| `link_flags` | `build.linker_args` | direct raw argv values after reviewing tokenization |

A minimal migrated file therefore looks like:

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "standard": "17",
    "type": "exe",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include"]
  }
}
```

## Precedence contract

Both implementations share the important scalar rule:

```text
explicit CLI scalar > project config scalar > built-in/preset default
```

The shared parity fixture validates this rather than only checking parser fields. A project file deliberately requests `release + static + output=should_not_win`; explicit CLI selects `debug + exe + output=config_cli`, and the resulting executable must run with Debug behavior while config-provided standard/define/include settings remain active.

For native MQB, list-like values have one consistent rule:

```text
mqb.json entries first, then CLI entries
```

The legacy script is not uniform: `defines`, `flags`, and `link_flags` are config-first, while include paths, library paths, and libraries are assembled CLI-first before config entries. Migrating projects that rely on conflicting/order-sensitive list entries therefore requires manual review; stable v5 does not emulate inconsistent historical ordering.

## Upward lookup and relative paths

Legacy `build.ps1` searches for `msvc_list.json` starting at the invocation directory and walking upward for at most five levels. Native MQB searches upward for the nearest `mqb.json` until the filesystem root.

Both treat relative paths stored in the discovered project file as relative to that file's directory. The shared fixture launches both tools from `nested/work`, two levels below the project file, and requires a header reachable only through the project-config include directory. This proves the common migration contract without pretending the search-depth limits are identical.

## Intentional non-equivalences

### Discovery excludes

Legacy `exclude` is a filename-glob filter in the PowerShell smart-discovery path. Native `discovery.exclude_dirs`, `discovery.extra_sources`, and `discovery.exclude_sources` are strict path-oriented corrections. Do not mechanically rename `exclude`; translate the intended source topology explicitly.

### Long-tail compiler/linker policy

The following legacy fields do not have a one-for-one typed `mqb.json` v1 field:

- `optimize`
- `warnings`
- `WX`
- `debug_info`
- `exceptions`
- `rtc1`
- `jmc`
- `sdl`
- `permissive`
- `fp`
- `charset`
- `incremental`

Some are already represented by native Debug/Release defaults; others can be expressed through `build.compiler_args` or `build.linker_args`. They should be migrated intentionally rather than silently copied. Typed runtime, LTCG, subsystem, target kind, and architecture should use their native typed fields where available.

### Architecture

Legacy project config has no equivalent typed architecture field in the established schema: the script defaults to x64 and exposes `-x86` as a command switch. Native `mqb.json` supports `build.architecture: "x64" | "x86"`. Choose the native field explicitly if architecture belongs to project policy.

### Output layout

Legacy project output is placed in the invocation working directory. Native project output belongs to the project-root `.mqb/bin/` layout. Stable-v5 parity requires the same target behavior, not byte-for-byte artifact paths.

### Environment preference

Legacy `-env` / `.msvc_build_env` is a persistent preference mechanism and is not a project-config field equivalent to native invocation-scoped `--env`. The stable migration keeps that distinction explicit.

## Shared parity fixture

`cpp/tests/parity/shared_config_migration.ps1` owns two paired scenarios, each containing both schemas while each implementation reads only its own file:

1. **config-only** — upward lookup plus config-relative include path; configuration selects Release, C++17, executable target, output name, define, and include directory.
2. **CLI override** — project files request Release/static/a losing output name; CLI overrides those scalars to Debug/executable/a new output while config standard/define/include and an additive CLI define remain observable in the running program.

The fixture deliberately does not compare artifact directories, log wording, cache formats, legacy list-order quirks, or discovery-exclude syntax.

## Migration checklist

1. Create `mqb.json` with `"version": 1`.
2. Move direct scalar/list mappings using the table above.
3. Convert discovery intent to native path-oriented fields instead of renaming `exclude`.
4. Review order-sensitive include/library/flag lists.
5. Review long-tail legacy policy and keep only policy still required, using typed native fields first and raw argv only where necessary.
6. Validate from the same subdirectory users normally invoke the build from.
7. Confirm the resulting target behavior and the new `.mqb/bin` artifact location before removing `msvc_list.json`.
