# MQB — MSVC Quick Build

**[简体中文](README.md) | English**

MQB is a native C/C++ build tool for **Windows + MSVC**. Give it one or more source files and it handles source discovery, incremental compilation, Modules/Header Units ordering, linking/archiving, and optional execution after a successful build.

Latest stable release and downloads: [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest)

## Installation

Requirements: Windows x64 and an MSVC C++ toolchain from Visual Studio / Visual Studio Build Tools.

Download the Windows x64 ZIP from GitHub Releases, extract it, then run:

```powershell
.\install.bat
```

The default install directory is `%USERPROFILE%\bin`. Open a new terminal and verify:

```powershell
mqb --help
```

See [`docs/INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) for install, PATH, and uninstall behavior.

## Quick start

### Single file

```powershell
mqb main.cpp --run
```

A single entry source enables smart discovery by default; writable build state stays under the project `.mqb/` directory.

### Exact multi-source build

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

Multiple positional sources form an exact source set; MQB does not expand it through automatic source discovery.

### Target kinds

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

Supported target kinds are `exe`, `dll`, and `static`.

### Pass arguments to the program

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

Everything after `--` belongs to the target program and is not parsed as a build option.

## Core capabilities

- Native MSVC builds for `.c` / `.cpp` / `.cc` / `.cxx`.
- Visual Studio and portable MSVC toolchain discovery.
- Header freshness and incremental compilation from `/sourceDependencies`.
- Independent compile / link / archive caches.
- Bounded parallel scan/compile with `-j / --jobs`.
- Typed `exe` / `dll` / `static` targets.
- Typed runtime, LTCG, and subsystem policy.
- Project-local named modules and header units.
- External/prebuilt named-module IFC providers.
- MSVC toolchain-owned `import std` / `import std.compat`.
- P1689 `/scanDependencies` module topology and transitive IFC closure.
- Windows Unicode-safe artifact/path identity.
- All writable build state kept under project `.mqb/`.

> Current boundary: `static` targets that require the Modules/Header Units pipeline are still rejected explicitly. Ordinary static-library builds are unaffected.

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
    "configuration": "release",
    "standard": "latest",
    "type": "exe",
    "output": "app",
    "include_dirs": ["include"]
  }
}
```

External/prebuilt module IFCs can also be declared in configuration:

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

See [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) for the complete schema, path bases, CLI/config precedence, and module-provider rules.

## Common CLI

```text
mqb <source...> [options]
```

| Option | Purpose |
|---|---|
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
| `--compiler-arg <arg>` | Raw compiler argv element |
| `--linker-arg <arg>` | Raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `--run` | Run executable after build |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Complete CLI help |
| `--` | Pass remaining arguments to the target program |

Use the current binary's `mqb --help` as the complete CLI reference.

## Documentation

| Topic | Document |
|---|---|
| `mqb.json` configuration and precedence | [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) |
| Installation, PATH, and uninstall | [`docs/INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) |
| Architecture and Modules/cache model | [`docs/ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) |
| Developing MQB | [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) |
| Self-hosting and release gates | [`docs/SELF_HOSTING_EN.md`](docs/SELF_HOSTING_EN.md) |
| C++ source-layout contract | [`cpp/README_EN.md`](cpp/README_EN.md) |
| Release history and notes | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) |

## Development

```powershell
.\tests\native\develop.ps1
```

See [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) for contributor details.

## License

Apache License 2.0 (SPDX: `Apache-2.0`). See [`LICENSE`](LICENSE) for the full terms.
