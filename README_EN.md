# MQB — MSVC Quick Build

**Language: [简体中文](README.md) | English**

MQB is a native C/C++ build tool for Windows + MSVC. It performs source discovery, incremental compilation, module topology, linking, archiving, and execution directly from source files.

> **Stable v5: native-only and MQB-built end to end.**
>
> - `mqb.exe` is the only supported implementation.
> - `mqb` is the only installed command entry point.
> - `mqb.json` is the only project configuration format.
> - Development, tests, self-hosting, and release generations are all built by MQB.
> - Retired PowerShell build entries, compatibility shims, profile injections, and legacy config formats are not silently taken over.

### Key Features

- Structured invocation of `cl.exe` / `link.exe` / `lib.exe` without using shell command strings as the general execution API.
- Native support for `.c` / `.cpp` / `.cc` / `.cxx`.
- Visual Studio and portable MSVC toolchain discovery.
- Single-entry smart discovery and multi-file exact source set.
- Project-local named modules and project-local header units using MSVC P1689 `/scanDependencies`.
- Real header freshness and incremental compilation based on `/sourceDependencies`.
- Typed `exe` / `dll` / `static` target kinds.
- Typed runtime, LTCG, and subsystem policy.
- Strict `mqb.json` configuration.
- Structured program argv via `--run -- ...`.
- All writable build state converged under project `.mqb/`.

Currently explicit fail-closed: external/prebuilt named-module providers, `import std;`, and static-library targets requiring Modules/Header Unit pipeline. Follow-up work is tracked independently in Issue #16.

## Source Layout: Physical Structure is Architecture

MQB maintains a single C++ product tree. Components must **not** grow private copies of `include/`, `src/`, or `tests/`.

```text
cpp/
├─ include/                 # single cross-component header root
│  └─ mqb/
│     ├─ core/
│     ├─ config/
│     ├─ discovery/
│     ├─ json/
│     ├─ modules/
│     ├─ orchestration/
│     ├─ msvc/
│     ├─ process/
│     └─ platform/windows/
│
├─ src/                     # single product implementation root
│  ├─ app/                  # CLI / main / app-private headers
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
│
├─ tests/                   # single C++ test root, mirrored by responsibility
│  ├─ app/
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  ├─ process/
│  ├─ platform/windows/
│  └─ e2e/
│
├─ README.md                # enforced layout contract
└─ mqb.json                 # self-build production manifest
```

See [`cpp/README_EN.md`](cpp/README_EN.md) for the strict filesystem contract and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for dependency boundaries. The core constraint is: `cpp/include`, `cpp/src`, and `cpp/tests` each have exactly one physical root. New code must select a responsibility before choosing a file location.

## Development: Building MQB with MQB

Requirements: Windows, Visual Studio/MSVC C++23, and a working MQB as seed.

Recommended entry point:

```powershell
.\tests\native\develop.ps1
```

Or provide seed / configuration / version explicitly:

```powershell
.\tests\native\develop.ps1 `
  -SeedMqbPath C:\path\to\mqb.exe `
  -Configuration Debug `
  -Version 5.0.0-dev
```

Development chain:

```text
installed/specified seed MQB
        ↓
MQB builds current Debug MQB
        ↓
Current MQB builds 67 test executables
        ↓
67/67 tests executed directly
```

Development artifacts output to:

```text
native-dev\debug\mqb.exe
```

### Building MQB Only

[`cpp/mqb.json`](cpp/mqb.json) is MQB's self-hosting production manifest. The current manifest precisely describes 42 production translation units and requires only two include roots:

```text
include
src/app
```

Manual build:

```powershell
$version = '5.0.0-dev'
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb src\app\main.cpp --env vs --debug --runtime MTd -D $define
Pop-Location
```

Output:

```text
cpp\.mqb\bin\mqb.exe
```

### Full Test Suite

`tests/native/run_native_tests.ps1` is the authoritative native test driver:

1. Reads 41 non-main production translation units from `cpp/mqb.json`;
2. Enumerates and requires exactly 67 `*_tests.cpp` files from the unified `cpp/tests/` tree;
3. Builds each test executable using the current MQB;
4. Passes the current MQB itself to CLI E2E tests;
5. Executes all tests directly and requires 67/67 to pass.

```powershell
.\tests\native\run_native_tests.ps1 `
  -BuilderMqbPath .\native-dev\debug\mqb.exe `
  -TestMqbPath .\native-dev\debug\mqb.exe `
  -RepoRoot . `
  -Configuration Debug
```

Development and test chains do not invoke CMake/CTest. MQB is its own development build system.

## Stable Self-Hosting Chain

The first stable v5 uses historical `v5.0.0-rc.2` `mqb.exe` as pinned seed, validating its Release ZIP SHA-256 and executable identity. The seed is responsible only for building Stage 0 of the current source code and never enters the stable package.

```text
pinned historical MQB seed
        ↓  MQB builds current 42-TU source
Stage 0
        ↓  MQB builds and runs 67/67 Release tests
Stage 0 → Stage 1
        ↓  clear MQB build state
Stage 1 → Stage 2
```

Only **Stage 1** is published. Stage 1/Stage 2 closure, exact package, SHA-256, byte identity, and installer lifecycle are all release-blocking gates. See [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) for full contract details.

## Quickstart

Single file / smart discovery:

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
```

Disable discovery:

```powershell
mqb main.cpp --no-discover
```

Multi-file exact source set:

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

Target kind:

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

Runtime / LTCG / subsystem:

```powershell
mqb main.cpp --runtime MT
mqb main.cpp --ltcg
mqb math.cpp vector.cpp --type static --ltcg -o math
mqb winmain.cpp --subsystem windows
```

Program argv:

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

## `mqb.json`

MQB searches upward from the invocation directory for the nearest `mqb.json`. That directory becomes the project root and `.mqb/` root.

Minimal configuration:

```json
{
  "version": 1
}
```

Example:

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x64",
    "standard": "latest",
    "type": "exe",
    "runtime": "MT",
    "ltcg": true,
    "subsystem": "console",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include"],
    "library_dirs": ["third_party/lib"],
    "libraries": ["codec"]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests"],
    "extra_sources": ["src/manual_adapter.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  }
}
```

See [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) for full schema, path rules, precedence, and cache behavior. Legacy `msvc_list.json` will not be read or migrated.

## Common CLI Options

| Option | Description |
|---|---|
| `--debug` / `--release` | Build configuration |
| `--config <debug|release>` | Explicit build configuration |
| `--std <14|17|20|23|latest>` | C++ standard |
| `--type <exe|dll|static>` | Target kind |
| `--x86` / `--x64` | Target architecture |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG policy |
| `--subsystem <console|windows>` | Subsystem |
| `-j, --jobs <N>` | Max concurrent scan/compile jobs |
| `-o, --output <name>` | Target output name under `.mqb/bin/` |
| `--run` | Run executable after building |
| `--discover` / `--no-discover` | Source discovery |
| `-I <dir>` | Include directory |
| `-D <value>` | Preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | Library search directory |
| `-l <name>` / `--lib <name>` | Library |
| `--compiler-arg <arg>` | Raw `cl.exe` argv element |
| `--linker-arg <arg>` | Raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `--portable-root <dir>` | Portable toolchain root candidate |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Help + embedded version |
| `--` | Pass remaining argv to target program |

PowerShell-era single-dash aliases fail closed as unknown options.

## Installation

Stable package name:

```text
msvc-quick-build-v5.0.0-windows-x64.zip
```

Extract and run:

```powershell
.\install.bat
```

Default install destination is `%USERPROFILE%\bin`. The installer does not create legacy `build` compatibility commands or modify PowerShell profiles. See [`docs/INSTALLATION.md`](docs/INSTALLATION.md) for details.

## Release Gates

Official `v5.0.0` must pass on the same candidate commit:

1. `Native C++`: pinned seed → current Debug MQB → 67/67 Debug tests;
2. `Native Installer`: pinned seed → current Release MQB → install/reinstall/uninstall;
3. `Native Release`: pinned seed → Stage 0 → 67/67 Release tests → Stage 1 → clean Stage 2 → exact package / checksum / Stage 1 byte identity / packaged installer;
4. Publication is allowed only for `vX.Y.Z` tags matching `release/VERSION`, consuming the already-validated artifact from the same workflow run without rebuilding.

Historical `v5.0.0-rc.1` / `v5.0.0-rc.2` remain unchanged.

## License

MIT — see [`LICENSE`](LICENSE).
