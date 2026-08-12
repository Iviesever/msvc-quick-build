# MQB — MSVC Quick Build

**语言：简体中文（默认） | [English](#english)**

## 简体中文

MQB 是面向 Windows + MSVC 的原生 C/C++ 构建工具。它直接从源文件完成 source discovery、增量编译、Modules/Header Units 拓扑、链接、静态库归档与可执行程序运行。

> **Stable v5：native-only / MQB 自构建**
>
> - `mqb.exe` 是唯一受支持的构建实现。
> - `mqb` 是唯一安装命令入口。
> - `mqb.json` 是唯一项目配置格式。
> - MQB 的开发构建、Debug/Release 测试、自举和发布构建全部由 MQB 完成。
> - 已淘汰的 PowerShell 构建入口、compatibility shim、profile 注入与旧配置格式不会被静默接管。

### 主要能力

- 结构化调用 `cl.exe` / `link.exe` / `lib.exe`，内部不以 shell command string 作为通用执行 API。
- 原生支持 `.c` / `.cpp` / `.cc` / `.cxx`。
- Visual Studio 与 portable MSVC toolchain discovery。
- 单入口 smart discovery 与多文件精确 source set。
- Project-local named modules 与 project-local header units，使用 MSVC P1689 `/scanDependencies`。
- 基于 `/sourceDependencies` 的真实 header freshness 与增量编译。
- Typed `exe` / `dll` / `static` target kinds。
- Typed runtime、LTCG、subsystem policy。
- Strict `mqb.json` configuration。
- `--run -- ...` 结构化 program argv。
- 所有 writable build state 收敛在项目 `.mqb/`。

当前明确 fail closed：external/prebuilt named-module providers、`import std;`，以及需要 Modules/Header Unit pipeline 的 static-library target。相关后续工作独立跟踪于 Issue #16。

## 源码结构：物理目录就是架构

MQB 只维护一套 C++ 产品树。**禁止每个组件再复制自己的 `include/`、`src/`、`tests/`。**

```text
cpp/
├─ include/                 # 唯一跨组件头文件根
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
├─ src/                     # 唯一产品实现根
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
├─ tests/                   # 唯一 C++ 测试根，按职责镜像
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
├─ README.md                # 强制目录与依赖契约
└─ mqb.json                 # MQB 自构建 production manifest
```

职责规则见 [`cpp/README.md`](cpp/README.md)，完整逻辑架构见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。核心约束是：`cpp/include`、`cpp/src`、`cpp/tests` 各自只有一个物理根；新代码必须先选择职责，再选择文件位置。

## 开发：用 MQB 构建 MQB

要求：Windows、Visual Studio/MSVC C++23，以及一个已经可运行的 MQB 作为 seed。

推荐入口：

```powershell
.\tests\native\develop.ps1
```

显式指定 seed / configuration / development version：

```powershell
.\tests\native\develop.ps1 `
  -SeedMqbPath C:\path\to\mqb.exe `
  -Configuration Debug `
  -Version 5.0.0-dev
```

开发链：

```text
已安装/指定 seed MQB
        ↓
MQB 构建当前 Debug MQB
        ↓
当前 MQB 构建 67 个测试可执行文件
        ↓
直接执行 67/67 tests
```

开发产物复制到：

```text
native-dev\debug\mqb.exe
```

### 只构建 MQB

[`cpp/mqb.json`](cpp/mqb.json) 是 MQB 自身的 production manifest。当前 manifest 精确描述 42 个 production translation units，并且只需要两个 include roots：

```text
include
src/app
```

手工构建：

```powershell
$version = '5.0.0-dev'
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb src\app\main.cpp --env vs --debug --runtime MTd -D $define
Pop-Location
```

输出：

```text
cpp\.mqb\bin\mqb.exe
```

### 完整测试

`tests/native/run_native_tests.ps1` 是权威 native test driver：

1. 从 `cpp/mqb.json` 读取 41 个 non-main production translation units；
2. 只从统一的 `cpp/tests/` 树枚举并要求恰好 67 个 `*_tests.cpp`；
3. 使用当前 MQB 构建每个测试可执行文件；
4. 对 CLI E2E tests 传入当前 MQB 本身；
5. 直接执行全部测试并要求 67/67 通过。

```powershell
.\tests\native\run_native_tests.ps1 `
  -BuilderMqbPath .\native-dev\debug\mqb.exe `
  -TestMqbPath .\native-dev\debug\mqb.exe `
  -RepoRoot . `
  -Configuration Debug
```

开发与测试链不调用 CMake/CTest。MQB 自身就是 MQB 的开发构建系统。

## 稳定版自举链

首个 stable v5 使用历史 `v5.0.0-rc.2` `mqb.exe` 作为 pinned seed，并校验其 Release ZIP SHA-256 与 executable identity。seed 只负责构建当前源码 Stage 0，永不进入 stable package。

```text
pinned historical MQB seed
        ↓  MQB 构建当前 42-TU 源码
Stage 0
        ↓  MQB 构建并运行 67/67 Release tests
Stage 0 → Stage 1
        ↓  清空 MQB build state
Stage 1 → Stage 2
```

最终只发布 **Stage 1**。Stage 1/Stage 2 closure、exact package、SHA-256、byte identity 与 installer lifecycle 都是 release-blocking gate。完整契约见 [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md)。

## 快速开始

单文件 / smart discovery：

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
```

关闭 discovery：

```powershell
mqb main.cpp --no-discover
```

多文件精确 source set：

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

Target kind：

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

Runtime / LTCG / subsystem：

```powershell
mqb main.cpp --runtime MT
mqb main.cpp --ltcg
mqb math.cpp vector.cpp --type static --ltcg -o math
mqb winmain.cpp --subsystem windows
```

Program argv：

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

## `mqb.json`

MQB 从 invocation directory 向上查找最近的 `mqb.json`。该文件所在目录成为 project root 和 `.mqb/` root。

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

完整 schema、路径规则、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。旧 `msvc_list.json` 不会被读取或迁移。

## 常用 CLI

| 选项 | 作用 |
|---|---|
| `--debug` / `--release` | 构建配置 |
| `--config <debug|release>` | 显式构建配置 |
| `--std <14|17|20|23|latest>` | C++ 标准 |
| `--type <exe|dll|static>` | target kind |
| `--x86` / `--x64` | 目标架构 |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG policy |
| `--subsystem <console|windows>` | subsystem |
| `-j, --jobs <N>` | 最大并发 scan/compile 数 |
| `-o, --output <name>` | `.mqb/bin/` 下的目标名 |
| `--run` | 构建后运行 executable |
| `--discover` / `--no-discover` | source discovery |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | library |
| `--compiler-arg <arg>` | 原样 `cl.exe` argv element |
| `--linker-arg <arg>` | 原样 linker argv element |
| `--env <auto|vs|portable>` | toolchain selection |
| `--portable-root <dir>` | portable toolchain root candidate |
| `-v, --verbose` | verbose output |
| `-h, --help` | help + embedded version |
| `--` | 后续 argv 传给目标程序 |

PowerShell-era 单横线别名会 fail closed 为 unknown option。

## 安装

稳定版包名：

```text
msvc-quick-build-v5.0.0-windows-x64.zip
```

解压后：

```powershell
.\install.bat
```

默认安装到 `%USERPROFILE%\bin`。安装器不会创建旧 `build` compatibility command，也不修改 PowerShell profile。详见 [`docs/INSTALLATION.md`](docs/INSTALLATION.md)。

## 稳定版发布门禁

正式 `v5.0.0` 必须在同一候选提交上通过：

1. `Native C++`：pinned seed → current Debug MQB → 67/67 Debug tests；
2. `Native Installer`：pinned seed → current Release MQB → install/reinstall/uninstall；
3. `Native Release`：pinned seed → Stage 0 → 67/67 Release tests → Stage 1 → clean Stage 2 → exact package / checksum / Stage 1 byte identity / packaged installer；
4. 只有匹配 `release/VERSION` 的 `vX.Y.Z` tag 才允许 publication，而且 publication 只消费同一 workflow run 已验证 artifact，不 rebuild。

历史 `v5.0.0-rc.1` / `v5.0.0-rc.2` 保持不重写。

## 许可证

MIT — 见 [`LICENSE`](LICENSE)。

---

<a id="english"></a>

## English

MQB is a native C/C++ build tool for Windows + MSVC. It performs source discovery, incremental compilation, module topology, linking, archiving, and execution directly from source files.

> **Stable v5: native-only and MQB-built end to end.**
>
> `mqb.exe` is the only supported implementation, `mqb` is the only installed command, and `mqb.json` is the only project configuration format. Development, tests, self-hosting, and release generations are all built by MQB.

### Source layout: physical structure is architecture

MQB has one C++ product tree. Components must **not** grow private copies of `include/`, `src/`, or `tests/`.

```text
cpp/
├─ include/                 # single cross-component header root
│  └─ mqb/{core,config,discovery,json,modules,orchestration,msvc,process,platform/...}
├─ src/                     # single product implementation root
│  ├─ app/
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
├─ tests/                   # single C++ test root, mirrored by responsibility
├─ README.md                # enforced layout contract
└─ mqb.json                 # self-build production manifest
```

See [`cpp/README.md`](cpp/README.md) for the strict filesystem contract and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for dependency boundaries.

### Development

Run:

```powershell
.\tests\native\develop.ps1
```

or provide a seed explicitly:

```powershell
.\tests\native\develop.ps1 `
  -SeedMqbPath C:\path\to\mqb.exe `
  -Configuration Debug `
  -Version 5.0.0-dev
```

The chain is seed MQB → current Debug MQB → 67 MQB-built test executables → 67/67 direct test execution. CMake/CTest are not part of this path.

[`cpp/mqb.json`](cpp/mqb.json) is the exact production manifest. It describes 42 production translation units and only two include roots: `include` and `src/app`.

Manual self-build:

```powershell
$version = '5.0.0-dev'
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb src\app\main.cpp --env vs --debug --runtime MTd -D $define
Pop-Location
```

Output: `cpp\.mqb\bin\mqb.exe`.

### Stable self-host chain

```text
pinned historical MQB seed
        ↓
MQB-built Stage 0
        ↓  67/67 MQB-built Release tests
Stage 1 release candidate
        ↓  clean MQB state
Stage 2 closure proof
```

Only Stage 1 may be packaged. See [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md).

### Quickstart

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
mqb main.cpp --run -- input.txt "hello world" 42
```

A single positional source enables smart discovery by default. Multiple positional sources form an exact source set.

### Configuration and CLI

MQB searches upward for the nearest `mqb.json`; that directory becomes the project and `.mqb/` root. See [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) for the full schema and path/precedence/cache rules.

Common native options include `--debug`, `--release`, `--std`, `--type`, `--runtime`, `--ltcg`, `--subsystem`, `-j`, `-o`, `-I`, `-D`, `-L`, `-l`, `--compiler-arg`, `--linker-arg`, `--env`, `--run`, `-v`, and `-h`. Retired PowerShell-era aliases fail closed.

### Installation

Stable package:

```text
msvc-quick-build-v5.0.0-windows-x64.zip
```

Extract and run `install.bat`. The default install root is `%USERPROFILE%\bin`; no legacy `build` command or PowerShell profile modification is installed. See [`docs/INSTALLATION.md`](docs/INSTALLATION.md).

### Release gates

Stable publication requires the same commit to pass MQB-built Debug 67/67, installer lifecycle, MQB-built Release 67/67, Stage 0 → Stage 1 → clean Stage 2 self-host closure, exact package/checksum/byte identity, and packaged installer validation. Publication is tag-only and consumes the already-validated artifact without rebuilding.

Historical `v5.0.0-rc.1` and `v5.0.0-rc.2` remain unchanged.

### License

MIT — see [`LICENSE`](LICENSE).
