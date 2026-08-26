# MQB — MSVC Quick Build

**[English](README.md) | 简体中文**

[![Native C++](https://github.com/Iviesever/msvc-quick-build/actions/workflows/native-ci.yml/badge.svg)](https://github.com/Iviesever/msvc-quick-build/actions/workflows/native-ci.yml)
[![Latest release](https://img.shields.io/github/v/release/Iviesever/msvc-quick-build)](https://github.com/Iviesever/msvc-quick-build/releases/latest)
[![License](https://img.shields.io/github/license/Iviesever/msvc-quick-build)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C)](cpp/README.md)

**专注 Windows + MSVC 的原生 C++23 构建系统。**

MQB 可以直接把一个源文件或项目入口变成原生 MSVC 构建，不需要先经过 generator。它负责源码发现、增量编译/链接/归档决策、C++ Modules 与 Header Units 排序、一等 PCH、工具链发现，以及构建成功后的可选运行。

```powershell
# 单个源文件
mqb run main.cpp

# 带 mqb.json，或根目录/src 下恰好有一个 conventional main 的项目
mqb build
mqb run

# 向构建后的程序传参
mqb run -- input.txt "hello world" 42
```

> MQB 有意保持专用化。它**不试图替代**大型跨平台生态中的 CMake；它专注于让原生 MSVC 开发更直接、更可预测，并缩短迭代路径。

**最新稳定版：** [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest)

## 为什么是 MQB？

MQB 面向希望保留 MSVC 原生行为、又不想先把每个中小型 Windows C++ 项目变成 meta-build 项目的开发者。

| | MQB | CMake |
|---|---|---|
| 主要范围 | Windows + MSVC | 跨平台 |
| 单源文件构建 | `mqb run main.cpp` | 通常需要项目配置 |
| 构建执行 | 直接编排 MSVC 工具链 | 生成/配置另一个构建系统 |
| 原生 MSVC 参数 | 一等 CLI/config 输入 | 通过 CMake 抽象或 generator-specific escape hatch 支持 |
| C++ Modules / Header Units | P1689 驱动的 MSVC pipeline | 支持，覆盖更广生态 |
| 跨平台可移植性 | 否 | 是 |
| 生态规模 | 小而聚焦 | 大而成熟 |

这是有意的取舍：MQB 放弃跨平台广度，换取对单一工具链的深度建模，包括 MSVC dependency metadata、IFC provider、原生 linker/librarian 边界、Windows path identity，以及 project-local build state。

## MQB 负责什么

- `.c`、`.cpp`、`.cc`、`.cxx` 的原生 MSVC 构建。
- `mqb build` / `mqb run` 项目命令与 fail-closed 默认入口解析。
- versioned `mqb.json` 配置与显式 named profiles。
- 基于 MSVC `/sourceDependencies` 的 header freshness 与增量编译。
- 相互独立的 compile、link、archive cache。
- `-j / --jobs` 有界并行 scan/compile 与资源感知自动并行。
- `exe`、`dll`、`static` target kinds。
- 普通 C++ target 的 first-class MSVC PCH。
- project-local named modules 与 header units。
- external/prebuilt named-module IFC providers。
- MSVC toolchain-owned `import std` / `import std.compat`。
- P1689 `/scanDependencies` topology 与 transitive IFC closure。
- 原生 MSVC compiler、linker、librarian 参数，以及显式 `/link` / `/lib` 边界。
- Visual Studio 与 portable MSVC toolchain discovery。
- Unicode-safe Windows path/artifact identity。
- 所有 MQB-owned writable state 都位于项目 `.mqb/` 下。
- self-hosting、native CI、installer/package gates 与 release automation。

## 安装

要求：

- Windows x64
- Visual Studio 或 Visual Studio Build Tools，并安装 MSVC C++ toolchain

从 [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest) 下载 Windows x64 ZIP，解压后运行：

```powershell
.\install.bat
```

默认安装目录为 `%USERPROFILE%\bin`。重新打开终端后验证：

```powershell
mqb --help
```

安装、PATH 与卸载行为见 [`docs/INSTALLATION.md`](docs/INSTALLATION.md)。

## 快速开始

### 构建并运行项目

对于 project root 或 `src/` 下恰好只有一个 conventional `main.{c,cpp,cc,cxx}` 的简单项目：

```powershell
mqb build
mqb run
```

正式项目可以用 `mqb.json` 固定入口：

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp"
  }
}
```

显式 source 永远优先于 `build.entry`：

```powershell
mqb run tools/tool.cpp
```

原来的 source-first 形式继续支持：

```powershell
mqb main.cpp
mqb main.cpp --run
```

单个入口 source 默认启用 smart discovery；多个 positional sources 则形成精确 source set：

```powershell
mqb build main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

### Named profiles

可复用的构建策略可以放进 `mqb.json`，不用重复一长串 CLI 参数：

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

Profile 是**单个显式 overlay**：一次 invocation 只选一个，没有继承、没有多 profile 叠加，也没有隐式默认 profile。优先级为：

```text
CLI > selected profile > base mqb.json > built-in defaults
```

List 输入按 base → profile → CLI 追加。Profile 不能设置 `build.entry`，所以切换构建策略不会悄悄切换项目入口。

### First-class PCH

普通 C++ target 可以把预编译头交给 MQB 作为其拥有的 build artifact：

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

MQB 自己拥有 synthetic creator、`.pch`、配对 creator `.obj`、`/FI`、`/Yc` / `/Yu` 与 `/Fp`，并把 `.pch` 纳入 consumer cache dependency identity。详见 [`docs/PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md)。

### Target kinds

```powershell
mqb build main.cpp -o app
mqb build api.cpp --type dll -o codec
mqb build math.cpp vector.cpp --type static -o math
```

支持 `exe`、`dll`、`static`。`mqb run` 仅适用于 executable target。

### 原生 MSVC 参数

Compiler 参数可以直接传入。Executable/DLL 构建用 `/link` 作为显式 linker boundary：

```powershell
mqb build foo.cpp /O2 /link /DEBUG:FULL /STACK:8388608
```

Static target 使用专门的 `/lib` librarian boundary：

```powershell
mqb build foo.cpp --type static /lib /WX /EXPORT:foo
```

`/lib`（大小写不敏感）是唯一公开的 static-librarian boundary。`-lib` / `-LIB` 会被拒绝，以避免与 MQB 的 `-l <name>` / `-l<name>` library shorthand 冲突。

Librarian policy 也可以写进 config：

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

## C++ Modules 与 Header Units

MQB 不根据文件名猜 module 编译顺序，而是向 MSVC 获取 dependency truth，并建立 typed provider graph：

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

Provider kinds 包括：

- project-local named modules；
- project-local header units；
- 显式 external/prebuilt IFC providers；
- MSVC toolchain-owned `std` / `std.compat` providers。

External IFC 可以在 `mqb.json` 中声明：

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

Provider ambiguity、conflict、cycle 和 unresolved requirement 都会 fail closed，而不是靠猜测继续构建。

## 增量构建模型

MQB 把 compile、link、archive reuse 视为三个独立的 correctness decision。Build identity 覆盖会实质影响 artifact 的输入，例如 source/TU identity、selected toolchain、typed build policy、ordered native arguments、module/IFC requirements、resolved libraries 与 required outputs。

Cache 设计由两条原则驱动：

1. **correctness before cache-hit rate** —— identity 不明确时重建，不猜；
2. **toolchain metadata is dependency truth** —— header freshness 来自 `/sourceDependencies`，module topology 来自 `/scanDependencies` / P1689。

所有 writable state 都是 project-local：

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

完整数据流和 ownership model 见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## `mqb.json`

MQB 从 invocation directory 向上查找最近的 `mqb.json`。包含它的目录同时成为 project root 与 `.mqb/` root。

最小配置：

```json
{
  "version": 1
}
```

常见配置：

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

Config/profile 路径相对包含 `mqb.json` 的目录解析；CLI 路径相对 invocation directory 解析。未知字段、错误类型、重复 key 与不支持的 schema version 都会 fail closed。

完整 schema 与 precedence rules 见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

## 常用 CLI

```text
mqb build [source...] [options]
mqb run [source...] [options] [-- program-args...]
mqb <source...> [options]
```

| 选项 | 作用 |
|---|---|
| `--profile <name>` | 选择 `mqb.json` 中一个 named profile |
| `--pch <header>` / `--no-pch` | 启用/覆盖 first-class PCH，或关闭继承的 PCH policy |
| `--debug` / `--release` | 构建配置 |
| `--std <14|17|20|23|latest>` | C++ 标准 |
| `--type <exe|dll|static>` | target kind |
| `--x86` / `--x64` | target architecture |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG |
| `--subsystem <console|windows>` | PE subsystem |
| `-j, --jobs <N>` | 最大并发 scan/compile jobs |
| `-o, --output <name>` | target 名称 |
| `--discover` / `--no-discover` | source discovery |
| `--module-ifc <name=path>` | external/prebuilt named-module IFC |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | library |
| `/option` / `-option` | 原生 MSVC compiler argument |
| `/link <...>` | 将后续 build 参数路由到 `link.exe` |
| `/lib <...>` | static target：将后续参数路由到 `lib.exe` |
| `--compiler-arg <arg>` | raw compiler argv element |
| `--linker-arg <arg>` | raw linker argv element |
| `--env <auto|vs|portable>` | toolchain selection |
| `--run` | source-first 兼容形式：构建后运行 executable |
| `-v, --verbose` | 详细输出 |
| `-h, --help` | 完整 CLI 帮助 |
| `--` | `mqb run` / `--run` 后续参数传给目标程序 |

完整 CLI reference 以当前 binary 的 `mqb --help` 为准。

## 当前边界

MQB 在尚未安全建模 ownership 的位置会有意 fail closed：

- first-class PCH 暂不与 C translation unit 混用；
- first-class PCH 暂不与需要 Modules/Header Units pipeline 的 target 混用；
- 当前仍拒绝需要 Modules/Header Units pipeline 的 `static` target；
- MQB 仅支持 Windows + MSVC。

普通 C++ PCH 与普通 static-library build 不受 module/static 限制影响。

## 文档

| 主题 | English | 简体中文 |
|---|---|---|
| 配置、profiles、precedence | [`MQB_CONFIG_EN.md`](docs/MQB_CONFIG_EN.md) | [`MQB_CONFIG.md`](docs/MQB_CONFIG.md) |
| 预编译头 | [`PRECOMPILED_HEADERS_EN.md`](docs/PRECOMPILED_HEADERS_EN.md) | [`PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md) |
| 安装 | [`INSTALLATION_EN.md`](docs/INSTALLATION_EN.md) | [`INSTALLATION.md`](docs/INSTALLATION.md) |
| 架构 | [`ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md) | [`ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| 开发 MQB | [`DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) | [`DEVELOPMENT.md`](docs/DEVELOPMENT.md) |
| 自举 / release gates | [`SELF_HOSTING_EN.md`](docs/SELF_HOSTING_EN.md) | [`SELF_HOSTING.md`](docs/SELF_HOSTING.md) |
| C++ 源码目录契约 | [`cpp/README_EN.md`](cpp/README_EN.md) | [`cpp/README.md`](cpp/README.md) |
| Release 历史 | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) |

## 开发

MQB 自身也是一个原生 C++23 产品，并具有 self-hosted development path：

```powershell
.\tests\native\develop.ps1
```

Contributor workflow 与 repository gates 见 [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)。

欢迎 bug report、可复现 compatibility case 与聚焦的 pull request。如果 MQB 的 MSVC / C++ Modules 工作对你有用或有意思，Star 仓库是一种很轻量的关注开发进展方式。

## License

Apache License 2.0（SPDX: `Apache-2.0`）。完整条款见 [`LICENSE`](LICENSE)。
