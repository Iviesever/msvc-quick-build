# MQB — MSVC Quick Build

**简体中文 | [English](README_EN.md)**

MQB 是面向 **Windows + MSVC** 的原生 C/C++ 构建工具。给它一个项目入口或源文件，它负责源码发现、增量编译、Modules/Header Units 依赖排序、链接/归档，以及可选的构建后运行。

最新稳定版与下载：[GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases/latest)

## 安装

要求：Windows x64，以及 Visual Studio / Visual Studio Build Tools 中的 MSVC C++ toolchain。

从 GitHub Releases 下载 Windows x64 ZIP，解压后运行：

```powershell
.\install.bat
```

默认安装到 `%USERPROFILE%\bin`。重新打开终端后验证：

```powershell
mqb --help
```

安装、PATH 与卸载行为见 [`docs/INSTALLATION.md`](docs/INSTALLATION.md)。

## 快速开始

### 构建 / 运行项目

对于根目录或 `src/` 下只有一个 conventional `main.{c,cpp,cc,cxx}` 的简单项目：

```powershell
mqb build
mqb run
mqb run -- input.txt "hello world" 42
```

正式项目可在 `mqb.json` 中设置稳定的默认入口：

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

### Named profiles

常用构建策略可以声明成显式命名 profile，而不用重复一长串 CLI 参数：

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

第一版 profile 是**单个显式 overlay**：一次只能选择一个，没有继承、没有多 profile 叠加，也没有隐式默认 profile。优先级为 `CLI > selected profile > base mqb.json > built-in`；list 输入按 base → profile → CLI 追加。Profile 不能设置 `build.entry`，因此切换构建策略不会悄悄切换项目入口。

### First-class PCH

普通 C++ target 可以把 MSVC 预编译头交给 MQB 作为一等构建产物管理：

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp"
  }
}
```

也可以直接从 CLI 启用或覆盖：

```powershell
mqb build --pch include/pch.hpp
mqb build --profile dev --no-pch
```

PCH 是 scalar policy，遵循 `CLI > selected profile > base mqb.json > disabled default`。MQB 自己拥有 synthetic creator、`.pch`、配对 creator `.obj`、`/FI`、`/Yc` / `/Yu` 与 `/Fp`，并把 `.pch` 纳入 consumer cache dependency；raw PCH structural switches 不与这套模型竞争。当前 first-class PCH 支持普通 C++ 的 `exe` / `dll` / `static` target，C TU 和需要 Modules/Header Units pipeline 的 target 会 fail closed。详见 [`docs/PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md)。

### Source-first 兼容形式

原有直接构建形式继续支持：

```powershell
mqb main.cpp
mqb main.cpp --run
```

单入口默认启用 smart discovery；可写构建状态统一放在项目 `.mqb/` 下。

### 精确多源文件

```powershell
mqb build main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示精确 source set，不再自动扩展源码集合。

### Target kinds

```powershell
mqb build main.cpp -o app
mqb build api.cpp --type dll -o codec
mqb build math.cpp vector.cpp --type static -o math
```

支持 `exe`、`dll`、`static`。`mqb run` 只适用于 executable target。

### 原生 linker / librarian 参数

Executable / DLL 构建使用 `/link` 把其后的 build argv 交给 `link.exe`：

```powershell
mqb build foo.cpp /O2 /link /DEBUG:FULL /STACK:8388608
```

Static target 使用独立的 `/lib` boundary，把其后的参数交给 `lib.exe`：

```powershell
mqb build foo.cpp --type static /lib /WX /EXPORT:foo
```

`/lib` 是 **static librarian boundary 的唯一公开拼写**（`/LIB` 同样接受）；`-lib` / `-LIB` 明确拒绝，以免与 MQB 的 `-l <name>` / `-l<name>` library shorthand 冲突。Librarian policy 也可以配置：

```json
{
  "version": 1,
  "build": {
    "type": "static",
    "librarian_args": ["/WX", "/EXPORT:foo"]
  }
}
```

`librarian_args` 与其他 list policy 一样按 base config → selected profile → CLI `/lib` 顺序追加；它只允许用于 static target，并进入 archive recipe/cache identity。

## 核心能力

- `mqb build` / `mqb run` 项目命令与 fail-closed 默认入口解析。
- `mqb.json` named profiles 与 `base < profile < CLI` 分层解析。
- 普通 C++ target 的 first-class MSVC PCH、独立 PCH cache 与自动 consumer invalidation。
- `.c` / `.cpp` / `.cc` / `.cxx` 原生 MSVC 构建。
- Visual Studio 与 portable MSVC toolchain discovery。
- 基于 `/sourceDependencies` 的 header freshness 与增量编译。
- compile / link / archive 独立缓存。
- `-j / --jobs` 有界并行 scan/compile。
- `exe` / `dll` / `static` typed targets。
- typed runtime、LTCG、subsystem policy。
- 原生 MSVC compiler/linker/librarian 参数，以及 `/link` / `/lib` 明确分界。
- project-local named modules 与 header units。
- external/prebuilt named-module IFC providers。
- MSVC toolchain-owned `import std` / `import std.compat`。
- P1689 `/scanDependencies` 驱动的 module topology 与 transitive IFC closure。
- Windows Unicode-safe artifact/path identity。
- 所有 writable build state 收敛到项目 `.mqb/`。

> 当前边界：first-class PCH 暂不与 C TU 或 Modules/Header Units pipeline 混用；需要 Modules/Header Units pipeline 的 `static` target 仍会显式拒绝。普通 C++ PCH 与普通静态库构建不受影响。

## `mqb.json`

MQB 从执行目录向上查找最近的 `mqb.json`。该文件所在目录成为 project root，也成为 `.mqb/` 根目录。

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

静态库可以声明 librarian policy：

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

`build.entry` 相对 `mqb.json` 解析，仅在 `mqb build` / `mqb run` 没有显式 positional source 时使用。若未设置，MQB 只在 project root 与 `src/` 中寻找 conventional `main.{c,cpp,cc,cxx}`；必须恰好命中一个，否则明确报错。

`build.pch` 可为非空 header 路径或 `false`。配置/profile 中的 PCH 路径相对 `mqb.json`；CLI `--pch` 路径相对 invocation directory。Profile 可覆盖或用 `pch: false` 关闭 base PCH，CLI `--pch` / `--no-pch` 再覆盖 profile。

Named profile 也声明在同一配置文件中，并用 `--profile <name>` 显式选择。Profile 内路径同样相对 `mqb.json`，native compiler/linker/librarian arguments 仍经过统一的 MSVC Parameter Engine；profile 名本身不是额外 cache 维度，最终生效的构建语义才决定 cache identity。

External/prebuilt module IFC 也可在配置中声明：

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

完整字段、profiles、路径基准、CLI/config precedence 和 module provider 规则见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。PCH artifact、ownership 与 invalidation 模型见 [`docs/PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md)。

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
| `--x86` / `--x64` | 目标架构 |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG |
| `--subsystem <console|windows>` | PE subsystem |
| `-j, --jobs <N>` | 最大并发 scan/compile 数 |
| `-o, --output <name>` | 目标名 |
| `--discover` / `--no-discover` | source discovery |
| `--module-ifc <name=path>` | external/prebuilt named-module IFC |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | library |
| `/option` / `-option` | 原生 MSVC compiler 参数 |
| `/link <...>` | 后续 build 参数路由到 linker |
| `/lib <...>` | static target：后续 build 参数路由到 librarian；`-lib` 不支持 |
| `--compiler-arg <arg>` | 原样 compiler argv element |
| `--linker-arg <arg>` | 原样 linker argv element |
| `--env <auto|vs|portable>` | toolchain selection |
| `--run` | source-first 兼容形式：构建后运行 executable |
| `-v, --verbose` | 详细输出 |
| `-h, --help` | 完整 CLI 帮助 |
| `--` | `mqb run` / `--run` 后续参数传给目标程序 |

完整参数列表以当前 binary 的 `mqb --help` 为准。

## 文档

| 主题 | 文档 |
|---|---|
| `mqb.json` 配置、profiles 与 precedence | [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) |
| First-class PCH ownership、cache 与 invalidation | [`docs/PRECOMPILED_HEADERS.md`](docs/PRECOMPILED_HEADERS.md) |
| 安装、PATH、卸载 | [`docs/INSTALLATION.md`](docs/INSTALLATION.md) |
| 架构与 Modules/cache 模型 | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| 开发 MQB | [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) |
| 自举与发布门禁 | [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) |
| C++ 源码目录契约 | [`cpp/README.md`](cpp/README.md) |
| 历史版本与发布说明 | [GitHub Releases](https://github.com/Iviesever/msvc-quick-build/releases) |

## 开发

```powershell
.\tests\native\develop.ps1
```

贡献者流程见 [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)。

## License

Apache License 2.0（SPDX: `Apache-2.0`）。完整条款见 [`LICENSE`](LICENSE)。