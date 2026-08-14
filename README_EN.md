# MQB — MSVC Quick Build

**[简体中文](README.md) | English**

MQB is a native C/C++ build tool for **Windows + MSVC**. Give it a project entry or source files and it handles source discovery, incremental compilation, Modules/Header Units ordering, linking/archiving, and optional execution after a successful build.

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

### Build / run a project

For a simple project with exactly one conventional `main.{c,cpp,cc,cxx}` under the project root or `src/`:

```powershell
mqb build
mqb run
mqb run -- input.txt "hello world" 42
```

A real project can declare a stable default entry in `mqb.json`:

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

### Named profiles

Frequently repeated build policy can be declared as an explicit named profile instead of repeating a long CLI sequence:

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

The first profile version is a **single explicit overlay**: one profile per invocation, no inheritance, no multi-profile stacking, and no implicit default profile. Precedence is `CLI > selected profile > base mqb.json > built-in`; list inputs append in base → profile → CLI order. Profiles cannot set `build.entry`, so selecting build policy never silently changes project entry identity.

### Source-first compatibility form

The existing direct form remains supported:

```powershell
mqb main.cpp
mqb main.cpp --run
```

A single entry source enables smart discovery by default; writable build state stays under the project `.mqb/` directory.

### Exact multi-source build

```powershell
mqb build main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

Multiple positional sources form an exact source set; MQB does not expand it through automatic source discovery.

### Target kinds

```powershell
mqb build main.cpp -o app
mqb build api.cpp --type dll -o codec
mqb build math.cpp vector.cpp --type static -o math
```

Supported target kinds are `exe`, `dll`, and `static`. `mqb run` applies only to executable targets.

## Core capabilities

- `mqb build` / `mqb run` project commands with fail-closed default-entry resolution.
- Named `mqb.json` profiles with layered base < profile < CLI resolution.
- Native MSVC builds for `.c` / `.cpp` / `.cc` / `.cxx`.
- Visual Studio and portable MSVC toolchain discovery.
- Header freshness and incremental compilation from `/sourceDependencies`.
- Independent compile / link / archive caches.
- Bounded parallel scan/compile with `-j / --jobs`.
- Typed `exe` / `dll` / `static` targets.
- Typed runtime, LTCG, and subsystem policy.
- Native MSVC compiler/linker arguments with a `/link` boundary.
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
    "entry": "src/main.cpp",
    "configuration": "release",
    "standard": "latest",
    "type": "exe",
    "output": "app",
    "include_dirs": ["include"]
  }
}
```

`build.entry` is resolved relative to `mqb.json` and is used only when `mqb build` / `mqb run` has no explicit positional source. Without it, MQB checks only conventional `main.{c,cpp,cc,cxx}` files in the project root and `src/`; exactly one candidate must exist or MQB fails with a diagnostic.

Named profiles live in the same configuration file and are selected explicitly with `--profile <name>`. Profile paths are also resolved relative to `mqb.json`, and native compiler/linker arguments still pass through the shared MSVC Parameter Engine. The profile name itself is not an extra cache dimension; final effective build semantics determine cache identity.

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

See [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) for the complete schema, profiles, path bases, CLI/config precedence, and module-provider rules.

## Common CLI

```text
mqb build [source...] [options]
mqb run [source...] [options] [-- program-args...]
mqb <source...> [options]
```

| Option | Purpose |
|---|---|
| `--profile <name>` | Select one named profile from `mqb.json` |
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
| `/link <...>` | Route following build arguments to the linker |
| `--compiler-arg <arg>` | Raw compiler argv element |
| `--linker-arg <arg>` | Raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `--run` | Source-first compatibility form: run executable after build |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Complete CLI help |
| `--` | Pass remaining arguments to the target program under `mqb run` / `--run` |

Use the current binary's `mqb --help` as the complete CLI reference.

## Documentation

| Topic | Document |
|---|---|
| `mqb.json` configuration, profiles, and precedence | [`docs/MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) |
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