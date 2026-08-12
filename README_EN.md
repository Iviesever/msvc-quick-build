# MQB — MSVC Quick Build

**[简体中文](README.md) | English**

MQB is a native C/C++ build tool for **Windows + MSVC**. Give it source files and it handles source discovery, incremental compilation, Modules/Header Units dependency ordering, linking or archiving, while keeping all writable build state under the project `.mqb/` directory.

It does not require generating a Visual Studio solution or using CMake/CTest to build ordinary projects.

## Why MQB

- **Build directly from source files**: supports `.c`, `.cpp`, `.cc`, `.cxx`, and C++ module interface sources.
- **Real incremental builds**: tracks header freshness with MSVC `/sourceDependencies` and keeps separate compile, link, and archive caches.
- **Modern C++ Modules**: supports project-local named modules, header units, explicit external/prebuilt IFC providers, and MSVC `import std` / `std.compat`.
- **Three target kinds**: `exe`, `dll`, and `static`.
- **MSVC toolchain discovery**: works with Visual Studio installations and portable MSVC toolchains.
- **Strict project configuration**: `mqb.json` uses a versioned, fail-closed schema; explicit CLI options override configuration values.
- **Structured process invocation**: executable, argv, and paths stay structured instead of being flattened into shell command strings; Windows path identity is Unicode-safe.
- **Clean project layout**: OBJ, IFC, dependency metadata, caches, and final outputs live under `.mqb/`.

> Current boundary: `static` targets support ordinary C/C++, but static-library targets that require the Modules/Header Units pipeline are still rejected explicitly rather than silently degraded.

## Installation

Download the Windows x64 package from GitHub Releases, extract it, and run:

```powershell
.\install.bat
```

The default installation directory is `%USERPROFILE%\bin`. See [`docs/INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) for PATH ownership, reinstall, and uninstall behavior.

## Quick start

### Single file

```powershell
mqb main.cpp
```

Build and run:

```powershell
mqb main.cpp --run
```

Select the language standard and Release configuration:

```powershell
mqb main.cpp --std 23 --release
```

### Multiple files

Multiple positional sources form an exact source set:

```powershell
mqb main.cpp src/math.cpp src/io.cpp -j 8 -o app
```

Single-entry builds use smart discovery by default. Disable it when needed:

```powershell
mqb main.cpp --no-discover
```

### DLLs and static libraries

```powershell
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

### Program arguments

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

Arguments after `--` are passed to the target program as argv.

## C++ Modules

Project-local named modules and header units enter the MSVC `/scanDependencies` / P1689 pipeline, where MQB resolves provider relationships, compile ordering, and IFC dependencies.

Bind an external or prebuilt named module to a read-only IFC explicitly:

```powershell
mqb main.cpp --module-ifc math.core=C:\sdk\math.core.ifc
```

Or configure it in `mqb.json`:

```json
{
  "version": 1,
  "modules": {
    "external": {
      "math.core": "third_party/ifc/math.core.ifc"
    }
  }
}
```

MSVC standard-library named modules belong to the selected toolchain; a project cannot spoof or override the `std` / `std.compat` providers. When the selected MSVC toolchain and language mode support them, use for example:

```powershell
mqb main.cpp --std latest
```

where `main.cpp` may contain `import std;`. See [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) and [`docs/ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) for the complete rules.

## `mqb.json`

MQB searches upward from the invocation directory for the nearest `mqb.json`. Its directory becomes the project root and the root of `.mqb/`.

Minimal configuration:

```json
{
  "version": 1
}
```

Typical configuration:

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x64",
    "standard": "23",
    "type": "exe",
    "runtime": "MT",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include"],
    "libraries": ["user32"]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests"]
  }
}
```

See [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) for the full schema, path bases, precedence, external module providers, and cache behavior.

## Common CLI options

| Option | Purpose |
|---|---|
| `--debug` / `--release` | Build configuration |
| `--std <14|17|20|23|latest>` | C++ language standard |
| `--type <exe|dll|static>` | Target kind |
| `--x86` / `--x64` | Target architecture |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG policy |
| `--subsystem <console|windows>` | PE subsystem |
| `-j, --jobs <N>` | Maximum concurrent scans/compiles |
| `-o, --output <name>` | Output target name |
| `--discover` / `--no-discover` | Source discovery |
| `--module-ifc <name=path>` | Bind an external/prebuilt named-module IFC |
| `-I <dir>` / `-D <value>` | Include directory / preprocessor definition |
| `-L <dir>` / `-l <name>` | Library path / library |
| `--compiler-arg <arg>` | Pass one raw `cl.exe` argv element |
| `--linker-arg <arg>` | Pass one raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `--run` | Run an executable after a successful build |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Complete CLI help |

`mqb --help` is the authoritative CLI reference.

## Build outputs

All writable build state lives under the project root:

```text
.mqb/
├─ obj/
├─ deps/
├─ scan/
├─ ifc/
├─ cache/
└─ bin/
```

Source directories do not need to contain MQB intermediate artifacts.

## Documentation

| Document | Read it for |
|---|---|
| [`docs/INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) | Installation, PATH, reinstall, uninstall, release package |
| [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) | Full `mqb.json` schema and precedence |
| [`docs/ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) | Build model, Modules, caches, responsibility boundaries |
| [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) | Repository development and test entry points |
| [`docs/SELF_HOSTING_EN.md`](docs/SELF_HOSTING_EN.md) | Stable-release bootstrap and reproducibility gates |
| [`cpp/README_EN.md`](cpp/README_EN.md) | C++ source layout and dependency rules |

## Developing MQB

Repository development entry point:

```powershell
.\tests\native\develop.ps1
```

MQB builds and validates its current source using MQB itself. See [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) for contributor details.

## License

Apache License 2.0 (SPDX: `Apache-2.0`). See [`LICENSE`](LICENSE) for the full terms.
