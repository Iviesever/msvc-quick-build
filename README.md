# MQB — MSVC Quick Build

**简体中文 | [English](README_EN.md)**

MQB 是面向 **Windows + MSVC** 的原生 C/C++ 构建工具。给它一个或多个源文件，它负责源码发现、增量编译、Modules/Header Units 依赖排序、链接/归档，以及可选的构建后运行。

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

### 单文件

```powershell
mqb main.cpp --run
```

单入口默认启用 smart discovery；可写构建状态统一放在项目 `.mqb/` 下。

### 精确多源文件

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示精确 source set，不再自动扩展源码集合。

### Target kinds

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

支持 `exe`、`dll`、`static`。

### 把参数传给程序

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

`--` 后的内容只属于目标程序，不参与构建参数解析。

## 核心能力

- `.c` / `.cpp` / `.cc` / `.cxx` 原生 MSVC 构建。
- Visual Studio 与 portable MSVC toolchain discovery。
- 基于 `/sourceDependencies` 的 header freshness 与增量编译。
- compile / link / archive 独立缓存。
- `-j / --jobs` 有界并行 scan/compile。
- `exe` / `dll` / `static` typed targets。
- typed runtime、LTCG、subsystem policy。
- project-local named modules 与 header units。
- external/prebuilt named-module IFC providers。
- MSVC toolchain-owned `import std` / `import std.compat`。
- P1689 `/scanDependencies` 驱动的 module topology 与 transitive IFC closure。
- Windows Unicode-safe artifact/path identity。
- 所有 writable build state 收敛到项目 `.mqb/`。

> 当前边界：需要 Modules/Header Units pipeline 的 `static` target 仍会显式拒绝；普通静态库构建不受影响。

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
    "configuration": "release",
    "standard": "latest",
    "type": "exe",
    "output": "app",
    "include_dirs": ["include"]
  }
}
```

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

完整字段、路径基准、CLI/config precedence 和 module provider 规则见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

## 常用 CLI

```text
mqb <source...> [options]
```

| 选项 | 作用 |
|---|---|
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
| `--compiler-arg <arg>` | 原样 compiler argv element |
| `--linker-arg <arg>` | 原样 linker argv element |
| `--env <auto|vs|portable>` | toolchain selection |
| `--run` | 构建后运行 executable |
| `-v, --verbose` | 详细输出 |
| `-h, --help` | 完整 CLI 帮助 |
| `--` | 后续参数传给目标程序 |

完整参数列表以当前 binary 的 `mqb --help` 为准。

## 文档

| 主题 | 文档 |
|---|---|
| `mqb.json` 配置与 precedence | [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) |
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
