# MQB — MSVC Quick Build

**English | [简体中文](README_ZH.md)**

[![Native C++](https://github.com/Iviesever/msvc-quick-build/actions/workflows/native-ci.yml/badge.svg)](https://github.com/Iviesever/msvc-quick-build/actions/workflows/native-ci.yml)
[![Latest release](https://img.shields.io/github/v/release/Iviesever/msvc-quick-build)](https://github.com/Iviesever/msvc-quick-build/releases/latest)
[![License](https://img.shields.io/github/license/Iviesever/msvc-quick-build)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C)](cpp/README_EN.md)

**A native C++23 build system focused on Windows + MSVC.**

MQB turns a source file or project entry into a native MSVC build without requiring a generator step. It owns source discovery, incremental compile/link/archive decisions, C++ Modules and Header Units ordering, first-class PCH, toolchain discovery, and optional execution after a successful build.

```powershell
# One source file
mqb run main.cpp

# A project with mqb.json or one conventional main under root/src
mqb build
mqb run

# Pass arguments to the built program
mqb run -- input.txt "hello world" 42
```

> MQB is intentionally specialized. It does **not** try to replace CMake for large cross-platform ecosystems; it focuses on making native MSVC development direct, predictable, and fast to iterate on.

**Latest stable release:** [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest)

## Why MQB?

MQB is for developers who want MSVC-native behavior without turning every small or medium Windows C++ project into a meta-build project first.

| | MQB | CMake |
|---|---|---|
| Primary scope | Windows + MSVC | Cross-platform |
| Single-source build | `mqb run main.cpp` | Usually requires project configuration |
| Build execution | Direct MSVC toolchain orchestration | Generates/configures another build system |
| Native MSVC arguments | First-class CLI/config input | Supported through CMake abstractions / generator-specific escape hatches |
| C++ Modules / Header Units | P1689-driven MSVC pipeline | Supported, broader ecosystem scope |
| Cross-platform portability | No | Yes |
| Ecosystem size | Small and focused | Large and mature |

The tradeoff is deliberate: MQB gives up cross-platform breadth to model one toolchain deeply, including MSVC dependency metadata, IFC providers, native linker/librarian boundaries, Windows path identity, and project-local build state.

## Performance and reproducible evidence

MQB's build-system comparisons use the same generated source tree, MSVC toolchain, compiler flags, and worker ceiling for MQB and CMake + Ninja. CMake configuration is excluded from timed samples, Ninja is invoked directly, and repeated measurements report medians across cold, no-op, single-TU, public-header, and PCH-header scenarios.

Published results are evidence for the recorded machine and toolchain, not a universal leaderboard claim. The complete methodology, reproducible harness, environment details, and measured results are in [`docs/BUILD_SYSTEM_BENCHMARK.md`](docs/BUILD_SYSTEM_BENCHMARK.md).

## What MQB handles

- Native MSVC builds for `.c`, `.cpp`, `.cc`, and `.cxx`.
- `mqb build` / `mqb run` project commands with fail-closed default-entry resolution.
- Versioned `mqb.json` configuration and explicit named profiles.
- Header freshness and incremental compilation from MSVC `/sourceDependencies`.
- Independent compile, link, and archive caches.
- Bounded parallel scan/compile with `-j / --jobs` and resource-aware automatic parallelism.
- `exe`, `dll`, and `static` target kinds.
- First-class MSVC PCH for ordinary C++ targets.
- Project-local named modules and header units.
- External/prebuilt named-module IFC providers.
- MSVC toolchain-owned `import std` / `import std.compat`.
- P1689 `/scanDependencies` topology and transitive IFC closure.
- Native MSVC compiler, linker, and librarian arguments with explicit `/link` and `/lib` boundaries.
- Visual Studio and portable MSVC toolchain discovery.
- Unicode-safe Windows path/artifact identity.
- All MQB-owned writable state under the project `.mqb/` directory.
- Self-hosting, native CI, installer/package gates, and release automation.

## Installation

Requirements:

- Windows x64
- Visual Studio or Visual Studio Build Tools with the MSVC C++ toolchain

Download the Windows x64 ZIP from [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest), extract it, then run:

```powershell
.\install.bat
```

The default install directory is `%USERPROFILE%\bin`. Open a new terminal and verify:

```powershell
mqb --help
```

See [`docs/INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) for installation, PATH, and uninstall behavior.

## Quick start

### Build and run a project

For a simple project with exactly one conventional `main.{c,cpp,cc,cxx}` under the project root or `src/`:

```powershell
mqb build
mqb run
```

For a stable project entry, add `mqb.json`:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp"
  }
}
```

An explicit source always wins over `build.entry`:

```powershell
mqb run tools/tool.cpp
```

The original source-first form is also supported:

```powershell
mqb main.cpp
mqb main.cpp --run
```

A single entry source enables smart discovery by default. Multiple positional sources form an exact source set:

```powershell
mqb build main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

### Named profiles

Keep reusable build policy in `mqb.json` instead of repeating a long CLI sequence:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "standard": "23"
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "runtime": "MDd",
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

```powershell
mqb build --profile release
mqb run --profile dev -- input.txt
```

Profiles are a **single explicit overlay**: one profile per invocation, no inheritance, no multi-profile stacking, and no implicit default profile. Precedence is:

```text
CLI > selected profile > base mqb.json > built-in defaults
```

List inputs append in base → profile → CLI order. Profiles cannot set `build.entry`, so switching build policy cannot silently switch the project entry.

### First-class PCH

Ordinary C++ targets can hand precompiled headers to MQB as owned build artifacts:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp"
  }
}
```

```powershell
mqb build --pch include/pch.hpp
mqb build --profile dev --no-pch
```

MQB owns the synthetic creator, `.pch`, paired creator `.obj`, `/FI`, `/Yc` / `/Yu`, and `/Fp`, and records the `.pch` in consumer cache dependency identity. See [`docs/PRECOMPILED_HEADERS_EN.md`](docs/PRECOMPILED_HEADERS_EN.md).

### Target kinds

```powershell
mqb build main.cpp -o app
mqb build api.cpp --type dll -o codec
mqb build math.cpp vector.cpp --type static -o math
```

Supported target kinds are `exe`, `dll`, and `static`. `mqb run` applies only to executable targets.

### Native MSVC arguments

Compiler arguments can be passed directly. Executable/DLL builds use `/link` as the explicit linker boundary:

```powershell
mqb build foo.cpp /O2 /link /DEBUG:FULL /STACK:8388608
```

Static targets use the dedicated `/lib` librarian boundary:

```powershell
mqb build foo.cpp --type static /lib /WX /EXPORT:foo
```

`/lib` (case-insensitive) is the only public static-librarian boundary. `-lib` / `-LIB` are rejected to avoid collision with MQB's `-l <name>` / `-l<name>` library shorthand.

Librarian policy can also live in config:

```json
{
  "version": 1,
  "build": {
    "entry": "src/math.cpp",
    "type": "static",
    "librarian_args": ["/WX"]
  }
}
```

## C++ Modules and Header Units

MQB does not infer module ordering from filenames. It asks MSVC for dependency truth and builds a typed provider graph:

```text
selected sources
      ↓
/scanDependencies
      ↓
P1689 rules
      ↓
provider resolution
      ↓
dependency graph
      ↓
parallel compile waves
      ↓
incremental final link
```

Provider kinds include:

- project-local named modules;
- project-local header units;
- explicit external/prebuilt IFC providers;
- MSVC toolchain-owned `std` / `std.compat` providers.

External IFCs can be declared in `mqb.json`:

```json
{
  "version": 1,
  "modules": {
    "external": {
      "vendor.math": "third_party/ifc/vendor.math.ifc"
    }
  }
}
```

Provider ambiguity, conflicts, cycles, and unresolved requirements fail closed rather than being guessed.

## Incremental-build model

MQB treats compile, link, and archive reuse as separate correctness decisions. Build identity includes the inputs that materially affect the artifact, such as source/TU identity, selected toolchain, typed build policy, ordered native arguments, module/IFC requirements, resolved libraries, and required outputs.

Two principles drive the cache design:

1. **correctness before cache-hit rate** — ambiguous identity rebuilds instead of guessing;
2. **toolchain metadata is dependency truth** — header freshness comes from `/sourceDependencies`, while module topology comes from `/scanDependencies` / P1689.

All writable state is project-local:

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

See [`docs/ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) for the complete data flow and ownership model.

## `mqb.json`

MQB searches upward from the invocation directory for the nearest `mqb.json`. The directory containing it becomes both the project root and the `.mqb/` root.

Minimal configuration:

```json
{
  "version": 1
}
```

A common configuration:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "configuration": "release",
    "standard": "latest",
    "type": "exe",
    "pch": "include/pch.hpp",
    "output": "app",
    "include_dirs": ["include"]
  }
}
```

Config/profile paths resolve relative to the directory containing `mqb.json`; CLI paths resolve relative to the invocation directory. Unknown fields, wrong types, duplicate keys, and unsupported schema versions fail closed.

See [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) for the complete schema and precedence rules.

## Common CLI

```text
mqb build [source...] [options]
mqb run [source...] [options] [-- program-args...]
mqb <source...> [options]
```

| Option | Purpose |
|---|---|
| `--profile <name>` | Select one named profile from `mqb.json` |
| `--pch <header>` / `--no-pch` | Enable/override first-class PCH or disable inherited PCH policy |
| `--debug` / `--release` | Build configuration |
| `--std <14|17|20|23|latest>` | C++ standard |
| `--type <exe|dll|static>` | Target kind |
| `--x86` / `--x64` | Target architecture |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG |
| `--subsystem <console|windows>` | PE subsystem |
| `-j, --jobs <N>` | Maximum concurrent scan/compile jobs |
| `-o, --output <name>` | Target name |
| `--discover` / `--no-discover` | Source discovery |
| `--module-ifc <name=path>` | External/prebuilt named-module IFC |
| `-I <dir>` | Include directory |
| `-D <value>` | Preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | Library search directory |
| `-l <name>` / `--lib <name>` | Library |
| `/option` / `-option` | Native MSVC compiler argument |
| `/link <...>` | Route following build arguments to `link.exe` |
| `/lib <...>` | Static target: route following arguments to `lib.exe` |
| `--compiler-arg <arg>` | Raw compiler argv element |
| `--linker-arg <arg>` | Raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `--run` | Source-first compatibility form: run executable after build |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Complete CLI help |
| `--` | Pass remaining arguments to the target program under `mqb run` / `--run` |

Use the current binary's `mqb --help` as the complete CLI reference.

## Current boundaries

MQB deliberately fails closed where ownership is not yet modeled safely:

- first-class PCH does not mix with C translation units;
- first-class PCH does not mix with targets requiring the Modules/Header Units pipeline;
- `static` targets requiring the Modules/Header Units pipeline are currently rejected;
- MQB is Windows + MSVC only.

Ordinary C++ PCH and ordinary static-library builds are unaffected by the module/static limitation.

## Documentation

| Topic | English | 简体中文 |
|---|---|---|
| Configuration, profiles, precedence | [`MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) | [`MQB_CONFIG.md`](docs/MQB_CONFIG.md) |
| Precompiled headers | [`PRECOMPILED_HEADERS_EN.md`](docs/PRECOMPILED_HEADERS_EN.md) | [`PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md) |
| Installation | [`INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) | [`INSTALLATION.md`](docs/INSTALLATION.md) |
| Architecture | [`ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) | [`ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| Developing MQB | [`DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) | [`DEVELOPMENT.md`](docs/DEVELOPMENT.md) |
| Self-hosting / release gates | [`SELF_HOSTING_EN.md`](docs/SELF_HOSTING_EN.md) | [`SELF_HOSTING.md`](docs/SELF_HOSTING.md) |
| C++ source-layout contract | [`cpp/README_EN.md`](cpp/README_EN.md) | [`cpp/README.md`](cpp/README.md) |
| Release history | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) |

Full bilingual documentation index: [`docs/README.md`](docs/README.md) ([简体中文](docs/README_ZH.md)).

## Development

MQB is itself a native C++23 product and has a self-hosted development path:

```powershell
.\tests\native\develop.ps1
```

See [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) for contributor workflow and repository gates.

Bug reports, reproducible compatibility cases, and focused pull requests are welcome. If MQB's MSVC / C++ Modules work is useful or interesting to you, starring the repository is an easy way to follow its development.

## License

Apache License 2.0 (SPDX: `Apache-2.0`). See [`LICENSE`](LICENSE) for the full terms.
