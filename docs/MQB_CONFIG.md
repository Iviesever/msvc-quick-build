# `mqb.json` Project Configuration v1

`mqb.json` is the C++ V2 project configuration file for MSVC Quick Build.

MQB searches upward from the directory where it is invoked and uses the nearest `mqb.json`. The directory containing that file becomes the project root and the root of `.mqb/` artifacts.

## Minimal file

```json
{
  "version": 1
}
```

`version` is required. Version 1 uses strict JSON: comments, trailing commas, duplicate keys, unknown schema fields, and incorrect field types are rejected.

## Full example

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x64",
    "standard": "23",
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
| `standard` | string | `20`, `23`, or `latest` (`c++20`, `c++23`, `c++latest` are also accepted) |
| `output` | string | target name under `.mqb/bin/` |
| `defines` | string array | preprocessor definitions, without `/D` |
| `include_dirs` | string array | include search directories |
| `library_dirs` | string array | library search directories |
| `libraries` | string array | requested libraries; `.lib` is optional for ordinary names |

All path-valued config entries are resolved relative to the directory containing `mqb.json`, not the shell's current working directory.

## Discovery fields

| Field | Type | Meaning |
|---|---|---|
| `enabled` | boolean | enable or disable single-entry smart source discovery |
| `exclude_dirs` | string array | exact project directories to prune before discovery graph construction |
| `extra_sources` | string array | exact supported C++ translation units to add even when disconnected from the entry graph |
| `exclude_sources` | string array | exact supported C++ translation units to exclude and treat as traversal barriers |

Supported C++ translation-unit extensions in the current V2 source classifier are:

```text
ordinary source:   .cpp .cc .cxx
module interface:  .ixx .cppm .mpp
```

Version 1 intentionally uses exact paths rather than a glob language. This keeps correction semantics deterministic while the discovery model is still being stabilized.

An ordinary `extra_sources` entry may not define another `main()`. The entry TU itself may not be excluded. A source may not appear in both `extra_sources` and `exclude_sources`.

Built-in directory exclusions such as `.mqb`, `.git`, `.vs`, `build`, `out`, and `cmake-build-*` remain active in addition to configured exclusions.

### Named modules and discovery

Project-local named modules do **not** require a separate v1 config section.

For a single ordinary entry such as:

```cpp
import math;
int main() { return answer(); }
```

smart discovery may select a reachable local `.ixx/.cppm/.mpp` provider candidate. Discovery does not decide which candidate is authoritative; MSVC `/scanDependencies` P1689 metadata remains responsible for provider selection, dependency ordering, ambiguity/cycle diagnostics, and unresolved requirements.

If a selected source contains named-module syntax but no supported local provider is found, the target still routes through the module pipeline and fails closed. It does not silently fall back to an ordinary compile.

Header units, external/prebuilt providers, and `import std` do not yet have execution/artifact policy and remain unsupported.

## Precedence

For scalar options:

```text
explicit CLI option > mqb.json > built-in default
```

Examples:

```powershell
# mqb.json says release; this build is debug.
mqb main.cpp --debug

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

This applies to defines, include directories, library directories, and libraries. For example, `-I local` adds a CLI include directory after the project's `include_dirs` entries.

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

The config file timestamp itself is not a special global rebuild trigger. Instead, the effective typed build options produced from the file participate in the existing compile/link signatures.

For example, changing only:

```json
"defines": ["VALUE=1"]
```

to:

```json
"defines": ["VALUE=2"]
```

changes compiler recipe identity and recompiles affected TUs even if no source file timestamp changed.

Likewise, library names/search paths affect link recipe identity, while exact resolved `.lib` files are separately tracked as link freshness inputs.

For named modules, compile identity also includes typed module references and planned module-interface output identity. Imported IFC files participate in consumer freshness validation, and a missing provider IFC invalidates the provider's cached compile outputs.

## Parallelism

`-j/--jobs` is intentionally **not** a v1 config field. It is execution policy only:

```powershell
mqb main.cpp -j 8
```

Changing the job count does not alter compile or link signatures and therefore must not invalidate an otherwise reusable build cache.

## Current boundaries

- v1 config has no header-unit, external/prebuilt module-provider, or `import std` policy.
- v1 config does not store parallel job count.
- `exclude_dirs`, `extra_sources`, and `exclude_sources` are exact paths, not globs.
- Explicit user libraries are freshness-tracked; indirect `/DEFAULTLIB` transitive dependencies are not claimed as fully tracked.
- C++ V2 currently classifies C++ translation units only; `.c` is not supported by the V2 source classifier.

For the broader build/module/cache architecture, see [`CPP_V2_ARCHITECTURE.md`](CPP_V2_ARCHITECTURE.md).
