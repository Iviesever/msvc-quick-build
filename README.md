# MQB — MSVC Quick Build

**语言：简体中文（默认） | [English](#english)**

## 简体中文

面向 Windows + MSVC 的原生 C/C++ 构建工具。MQB 直接从源文件完成发现、增量编译、模块拓扑、链接、归档与运行；项目不需要为了使用 MQB 维护额外的工程生成层。

> **Stable v5：仅原生 C++ / 全程 MQB 自举**
>
> - `mqb.exe` 是唯一受支持的构建实现。
> - `mqb` 是唯一受支持的安装命令入口。
> - `mqb.json` 是唯一受支持的项目配置格式。
> - 开发构建、Debug/Release 测试构建、稳定版自举构建都由 MQB 完成。
> - 稳定版 ZIP 中的 `mqb.exe` 必须由 MQB 自身构建，并且该代 MQB 必须能从干净状态再次构建 MQB。
> - 已淘汰的 PowerShell 构建入口、兼容 shim、profile 注入和旧配置格式不会被静默接管。

### 主要能力

- 直接结构化调用 `cl.exe` / `link.exe` / `lib.exe`，不通过 shell 拼接编译命令。
- 原生支持 `.c` / `.cpp` / `.cc` / `.cxx` translation units。
- 支持 Visual Studio 与 portable MSVC 工具链发现。
- 单入口 smart discovery 与多文件精确 source set。
- Project-local named modules 与 project-local header units，基于 MSVC P1689 `/scanDependencies`。
- 基于 `/sourceDependencies` 的真实 header freshness 与增量编译。
- Typed `exe` / `dll` / `static` target kinds。
- Typed MSVC runtime、LTCG、subsystem policy。
- strict `mqb.json` project configuration。
- `--run -- arg1 "arg 2"` 结构化运行参数。
- 所有 build state 隔离在项目 `.mqb/` 目录。

当前明确 fail closed 的范围包括 external/prebuilt named-module providers、`import std;`，以及需要 Modules/Header Unit pipeline 的 static-library target。

### 开发：用 MQB 构建 MQB

要求：Windows、Visual Studio / MSVC、C++23 编译能力，以及一个**已经可运行的 MQB** 作为 seed。

日常开发建议先安装最近的稳定版 MQB，然后在仓库根目录执行：

```powershell
.\tests\native\develop.ps1
```

默认流程是：

```text
已安装/指定的 seed MQB
        ↓
当前源码 Debug MQB
        ↓
当前 Debug MQB 构建全部 67 个测试程序
        ↓
直接执行 67 个测试
```

指定 seed、配置或开发版本：

```powershell
.\tests\native\develop.ps1 `
  -SeedMqbPath C:\path\to\mqb.exe `
  -Configuration Debug `
  -Version 5.0.0-dev
```

开发产物会复制到：

```text
native-dev\debug\mqb.exe
```

#### 只构建 MQB

仓库中的 [`cpp/mqb.json`](cpp/mqb.json) 是 MQB 自身的 native project description，精确描述当前生产源码集合、include roots、C++23、runtime 与编译参数。

```powershell
$version = '5.0.0-dev'
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb apps\mqb\main.cpp --env vs --debug --runtime MTd -D $define
Pop-Location
```

输出：

```text
cpp\.mqb\bin\mqb.exe
```

#### 完整测试

`tests/native/run_native_tests.ps1` 是权威 native test driver。它会：

1. 从 `cpp/mqb.json` 读取 41 个非 `main` 生产 translation units；
2. 枚举并要求恰好存在 67 个 `*_tests.cpp`；
3. 用当前 MQB 构建每个测试可执行文件；
4. 对 CLI E2E 测试传入当前 MQB 本身；
5. 直接运行全部测试并要求 67/67 通过。

```powershell
.\tests\native\run_native_tests.ps1 `
  -BuilderMqbPath .\native-dev\debug\mqb.exe `
  -TestMqbPath .\native-dev\debug\mqb.exe `
  -RepoRoot . `
  -Configuration Debug
```

开发与测试构建不需要第二套工程生成器；MQB 本身就是 MQB 的开发构建系统。

### 稳定版自举链

首次稳定版发布前，CI 需要一个已有二进制解决编译器/构建工具的 bootstrap 问题。当前 workflow 使用历史 `v5.0.0-rc.2` `mqb.exe` 作为**固定 seed**，并强制校验其 SHA-256。该 seed 只负责构建当前源码的 Stage 0，不会被打包。

```text
固定历史 seed MQB（校验 SHA-256）
        ↓  MQB 构建当前 42-TU 源码
Stage 0（当前源码，测试代）
        ↓  67 个 Release tests 也全部由 Stage 0 / MQB 构建
Stage 0 → Stage 1（MQB 构建 MQB）
        ↓  清空 cpp/.mqb
Stage 1 → Stage 2（再次用 MQB 构建 MQB）
```

最终只发布 **Stage 1**。Stage 0、Stage 1、Stage 2 都必须报告相同稳定版版本；Stage 1/Stage 2 自举闭包与精确源码 manifest 是 release-blocking gate。完整契约见 [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md)。

### 快速开始

#### 单文件 / 自动发现

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
```

单个 positional source 默认启用 smart discovery。

关闭 discovery：

```powershell
mqb main.cpp --no-discover
```

#### 多文件精确 source set

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示精确 source set，不再自动扩大集合。

#### Target kind

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

#### Runtime / LTCG / subsystem

```powershell
mqb main.cpp --runtime MT
mqb main.cpp --ltcg
mqb math.cpp vector.cpp --type static --ltcg -o math
mqb winmain.cpp --subsystem windows
```

#### 运行参数

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

`--` 后的每个参数都会按独立 argv 元素传给程序。

### `mqb.json`

MQB 从 invocation directory 向上查找最近的 `mqb.json`；配置文件所在目录成为 project root 和 `.mqb/` 根。

最小配置：

```json
{
  "version": 1
}
```

示例：

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

完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。旧 `msvc_list.json` 不会被读取或迁移。

### 常用 CLI

```text
mqb <entry.c|entry.cpp> [options] [-- program-args...]
mqb <source.c|source.cpp|module.ixx|module.cppm|module.mpp> <more-sources...> [options]
```

| 选项 | 作用 |
|---|---|
| `--debug` / `--release` | 选择构建配置 |
| `--config <debug|release>` | 显式选择构建配置 |
| `--std <14|17|20|23|latest>` | 选择 C++ 标准 |
| `--type <exe|dll|static>` | 选择 target kind |
| `--x86` / `--x64` | 选择目标架构 |
| `--runtime <MD|MDd|MT|MTd>` | 选择 MSVC runtime |
| `--ltcg` / `--no-ltcg` | 开启/关闭 coupled LTCG |
| `--subsystem <console|windows>` | 选择 executable/DLL subsystem |
| `-j, --jobs <N>` | 最大并发 scan/compile 数 |
| `-o, --output <name>` | 设置 `.mqb/bin/` 下的目标名 |
| `--run` | 构建成功后运行 executable |
| `--discover` / `--no-discover` | 控制 smart discovery |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | 显式链接库 |
| `--compiler-arg <arg>` | 追加一个原样 `cl.exe` argv 元素 |
| `--linker-arg <arg>` | 追加一个原样 `link.exe` argv 元素 |
| `--env <auto|vs|portable>` | 工具链选择 |
| `--portable-root <dir>` | 增加 portable toolchain root candidate |
| `-v, --verbose` | 输出详细信息 |
| `-h, --help` | 帮助与内嵌版本 |
| `--` | 后续 argv 传给目标程序 |

已淘汰的单横线别名会被当作 unknown option 拒绝，不会进入兼容执行路径。

### 安装

稳定版发布包：

```text
msvc-quick-build-v5.0.0-windows-x64.zip
```

解压后运行：

```powershell
.\install.bat
```

默认安装到：

```text
%USERPROFILE%\bin
```

安装器部署 `mqb.exe` 与 installer-owned maintenance/state files，不创建 `build` 兼容命令，也不修改 PowerShell profile。

卸载：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

完整安装契约见 [`docs/INSTALLATION.md`](docs/INSTALLATION.md)。

### 架构原则

```text
CLI / mqb.json
      ↓
Source selection / Toolchain discovery
      ↓
P1689 module topology（需要时）
      ↓
Build identity + incremental validation
      ↓
Build plan
      ↓
Bounded compile waves
      ↓
Incremental link / archive
      ↓
Optional executable run
```

更详细的模块、缓存和 orchestration 设计见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

### 稳定版发布门禁

`v5.0.0` 的正式发布要求同一候选提交同时通过：

1. `Native C++`：固定 seed → 当前 Debug MQB → 由当前 MQB 构建并运行 67/67 Debug tests；
2. `Native Installer`：固定 seed → 当前 Release MQB → install / reinstall / uninstall lifecycle；
3. `Native Release`：固定 seed → Stage 0 → 67/67 Release tests → Stage 1 → clean Stage 2 self-host closure → exact package manifest → SHA-256 → Stage 1 byte identity → packaged-installer validation；
4. 匹配 `release/VERSION` 的 `vX.Y.Z` tag 才能触发 publication；publication job 只发布同一 workflow run 已验证的 artifact，不二次 rebuild。

历史 `v5.0.0-rc.1` / `v5.0.0-rc.2` release notes 保留原样。Issue #16 独立跟踪 external/prebuilt named-module providers 与 `import std`。

### 许可证

MIT — 见 [`LICENSE`](LICENSE)。

---

<a id="english"></a>

## English

MQB is a native C/C++ build tool for Windows + MSVC. It performs source discovery, incremental compilation, module topology, linking, archiving, and execution directly from source files.

> **Stable v5: native C++ only, MQB-built end to end**
>
> - `mqb.exe` is the only supported build implementation.
> - `mqb` is the only supported installed command entry point.
> - `mqb.json` is the only supported project configuration format.
> - Development builds, Debug/Release test builds, and stable self-host generations are all built by MQB.
> - The `mqb.exe` shipped in the stable ZIP must be built by MQB itself and must be able to build another MQB generation from clean state.
> - Retired PowerShell entry points, compatibility shims, profile injection, and legacy configuration formats are not silently adopted.

### Highlights

- Structured direct invocation of `cl.exe`, `link.exe`, and `lib.exe`.
- Native `.c`, `.cpp`, `.cc`, and `.cxx` translation units.
- Visual Studio and portable MSVC discovery.
- Smart single-entry discovery and exact multi-source sets.
- Project-local named modules and header units using MSVC P1689 `/scanDependencies`.
- Real header freshness with `/sourceDependencies`.
- Typed `exe`, `dll`, and `static` targets.
- Typed MSVC runtime, LTCG, and subsystem policies.
- Strict `mqb.json` configuration.
- Structured program argv through `--run -- ...`.
- All build state under the project `.mqb/` directory.

Current fail-closed boundaries include external/prebuilt named-module providers, `import std;`, and static-library targets that would require the Modules/Header Unit pipeline.

### Development: build MQB with MQB

Requirements: Windows, Visual Studio / MSVC with C++23 support, and one already-working MQB binary as a seed.

For day-to-day development, install the latest stable MQB and run from the repository root:

```powershell
.\tests\native\develop.ps1
```

The default cycle is:

```text
installed/specified seed MQB
        ↓
current-source Debug MQB
        ↓
current Debug MQB builds all 67 test executables
        ↓
run all 67 tests directly
```

Choose the seed, configuration, or development version explicitly:

```powershell
.\tests\native\develop.ps1 `
  -SeedMqbPath C:\path\to\mqb.exe `
  -Configuration Debug `
  -Version 5.0.0-dev
```

The development binary is copied to `native-dev\debug\mqb.exe`.

#### Build MQB only

[`cpp/mqb.json`](cpp/mqb.json) is MQB's native self-build project description. It describes the exact production source set, include roots, C++23 mode, runtime, and compiler policy.

```powershell
$version = '5.0.0-dev'
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb apps\mqb\main.cpp --env vs --debug --runtime MTd -D $define
Pop-Location
```

Output: `cpp\.mqb\bin\mqb.exe`.

#### Full test suite

`tests/native/run_native_tests.ps1` is the authoritative native test driver. It requires exactly 41 non-main production translation units and exactly 67 `*_tests.cpp` files, builds every test with MQB, passes the current MQB binary to CLI E2E tests, and directly executes the full graph.

```powershell
.\tests\native\run_native_tests.ps1 `
  -BuilderMqbPath .\native-dev\debug\mqb.exe `
  -TestMqbPath .\native-dev\debug\mqb.exe `
  -RepoRoot . `
  -Configuration Debug
```

MQB itself is the development build system for MQB.

### Stable self-host chain

Before the first stable release, CI needs an already-existing binary to solve the bootstrap problem. The workflow uses the historical `v5.0.0-rc.2` `mqb.exe` as a **pinned seed** and verifies the release ZIP SHA-256. The seed only builds current-source Stage 0 and is never packaged.

```text
pinned historical seed MQB (SHA-256 verified)
        ↓  MQB builds current 42-TU source set
Stage 0 (current source, test generation)
        ↓  Stage 0 / MQB builds and runs all 67 Release tests
Stage 0 → Stage 1 (MQB builds MQB)
        ↓  delete cpp/.mqb
Stage 1 → Stage 2 (MQB builds MQB again)
```

Only **Stage 1** is released. Stage 0, Stage 1, and Stage 2 must report the same stable version. See [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md).

### Quickstart

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
mqb main.cpp --run -- input.txt "hello world" 42
```

A single positional source enables smart discovery by default. Multiple positional sources are an exact source set.

### `mqb.json`

MQB searches upward from the invocation directory for the nearest `mqb.json`. The directory containing that file is the project root and `.mqb/` root.

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
  }
}
```

See [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) for the complete schema, path rules, precedence, and cache behavior. Legacy `msvc_list.json` is not read or migrated.

### Common CLI

| Option | Purpose |
|---|---|
| `--debug` / `--release` | Build configuration |
| `--std <14|17|20|23|latest>` | C++ standard |
| `--type <exe|dll|static>` | Target kind |
| `--x86` / `--x64` | Target architecture |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | Coupled LTCG policy |
| `--subsystem <console|windows>` | Subsystem |
| `-j, --jobs <N>` | Parallel scan/compile limit |
| `-o, --output <name>` | Output name under `.mqb/bin/` |
| `--run` | Run executable after build |
| `-I <dir>` | Include directory |
| `-D <value>` | Preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | Library search directory |
| `-l <name>` / `--lib <name>` | Library |
| `--compiler-arg <arg>` | Raw `cl.exe` argv element |
| `--linker-arg <arg>` | Raw linker argv element |
| `--env <auto|vs|portable>` | Toolchain selection |
| `-v, --verbose` | Verbose output |
| `-h, --help` | Help and embedded version |
| `--` | Remaining argv for the program |

Retired PowerShell-era aliases fail as unknown options.

### Installation

Stable package:

```text
msvc-quick-build-v5.0.0-windows-x64.zip
```

Extract it and run:

```powershell
.\install.bat
```

The default install root is `%USERPROFILE%\bin`. The installer does not create a `build` compatibility command and does not modify PowerShell profiles. See [`docs/INSTALLATION.md`](docs/INSTALLATION.md).

### Architecture

```text
CLI / mqb.json
      ↓
Source selection / Toolchain discovery
      ↓
P1689 module topology when needed
      ↓
Build identity + incremental validation
      ↓
Build plan
      ↓
Bounded compile waves
      ↓
Incremental link / archive
      ↓
Optional executable run
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

### Stable release gates

Stable `v5.0.0` requires the same candidate commit to pass:

1. `Native C++`: pinned seed → current Debug MQB → 67/67 Debug tests built and run by MQB;
2. `Native Installer`: pinned seed → current Release MQB → install/reinstall/uninstall lifecycle;
3. `Native Release`: pinned seed → Stage 0 → 67/67 Release tests → Stage 1 → clean Stage 2 closure → exact package/checksum/Stage 1 byte identity → packaged installer lifecycle;
4. publication only from a `vX.Y.Z` tag matching `release/VERSION`, using the already-validated artifact without rebuilding.

Historical `v5.0.0-rc.1` and `v5.0.0-rc.2` release notes remain unchanged. Issue #16 tracks post-v5 module policies independently.

### License

MIT — see [`LICENSE`](LICENSE).
