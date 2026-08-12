# MQB — MSVC Quick Build

**简体中文 | [English](README_EN.md)**

MQB 是面向 **Windows + MSVC** 的原生 C/C++ 构建工具。给它源文件，它直接完成源码发现、增量编译、Modules/Header Units 依赖排序、链接或归档，并把所有构建状态收敛到项目 `.mqb/`。

它不要求生成 Visual Studio solution，也不依赖 CMake/CTest 才能构建普通项目。

## 为什么用 MQB

- **一条命令直接构建**：支持 `.c`、`.cpp`、`.cc`、`.cxx`，以及 C++ module interface 文件。
- **真实增量构建**：基于 MSVC `/sourceDependencies` 跟踪头文件新鲜度，并为 compile/link/archive 分别维护缓存。
- **现代 C++ Modules**：支持 project-local named modules、header units、显式 external/prebuilt IFC provider，以及 MSVC `import std` / `std.compat`。
- **三种目标类型**：`exe`、`dll`、`static`。
- **MSVC toolchain discovery**：可使用 Visual Studio 安装或 portable MSVC toolchain。
- **严格项目配置**：`mqb.json` 使用版本化、fail-closed schema；CLI 显式选项优先于配置文件。
- **结构化进程调用**：内部传递 executable/argv/path，而不是把命令拼成 shell string；Windows 路径按 Unicode-safe identity 处理。
- **项目目录保持干净**：OBJ、IFC、dependency metadata、cache 与最终产物统一写入 `.mqb/`。

> 当前边界：`static` 目标支持普通 C/C++，但需要 Modules/Header Units pipeline 的静态库目标仍会显式拒绝；不会静默降级。

## 安装

从 GitHub Releases 下载 Windows x64 发布包，解压后运行：

```powershell
.\install.bat
```

默认安装到 `%USERPROFILE%\bin`。安装、PATH 所有权、重装与卸载规则见 [`docs/INSTALLATION.md`](docs/INSTALLATION.md)。

## 快速开始

### 单文件

```powershell
mqb main.cpp
```

构建并运行：

```powershell
mqb main.cpp --run
```

指定标准与 Release：

```powershell
mqb main.cpp --std 23 --release
```

### 多文件

多个 positional source 表示精确 source set：

```powershell
mqb main.cpp src/math.cpp src/io.cpp -j 8 -o app
```

单入口默认启用 smart discovery；需要关闭时：

```powershell
mqb main.cpp --no-discover
```

### DLL 与静态库

```powershell
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

### 给目标程序传参数

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

`--` 之后的参数原样作为目标程序 argv。

## C++ Modules

Project-local named modules 与 header units 会进入 MSVC `/scanDependencies` / P1689 pipeline，由 MQB 解析 provider graph、编译顺序与 IFC 依赖。

外部或预编译 named module 可以显式绑定只读 IFC：

```powershell
mqb main.cpp --module-ifc math.core=C:\sdk\math.core.ifc
```

也可以写入 `mqb.json`：

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

MSVC 标准库 named modules 由当前选中的 toolchain 提供；项目不能伪造或覆盖 `std` / `std.compat` provider。使用时需要满足当前 MSVC toolchain 的能力与语言模式要求，例如：

```powershell
mqb main.cpp --std latest
```

其中 `main.cpp` 可直接包含 `import std;`。完整规则见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) 与 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## `mqb.json`

MQB 从当前执行目录向上查找最近的 `mqb.json`。该文件所在目录成为 project root，同时也是 `.mqb/` 的根。

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

完整 schema、路径基准、优先级、external module provider 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

## 常用 CLI

| 选项 | 作用 |
|---|---|
| `--debug` / `--release` | 构建配置 |
| `--std <14|17|20|23|latest>` | C++ 标准 |
| `--type <exe|dll|static>` | 目标类型 |
| `--x86` / `--x64` | 目标架构 |
| `--runtime <MD|MDd|MT|MTd>` | MSVC runtime |
| `--ltcg` / `--no-ltcg` | LTCG policy |
| `--subsystem <console|windows>` | PE subsystem |
| `-j, --jobs <N>` | 最大并发 scan/compile 数 |
| `-o, --output <name>` | 输出目标名 |
| `--discover` / `--no-discover` | source discovery |
| `--module-ifc <name=path>` | 绑定 external/prebuilt named-module IFC |
| `-I <dir>` / `-D <value>` | include / define |
| `-L <dir>` / `-l <name>` | library path / library |
| `--compiler-arg <arg>` | 原样传递一个 `cl.exe` argv element |
| `--linker-arg <arg>` | 原样传递一个 linker argv element |
| `--env <auto|vs|portable>` | toolchain selection |
| `--run` | 成功构建后运行 executable |
| `-v, --verbose` | 详细输出 |
| `-h, --help` | 完整 CLI 帮助 |

`mqb --help` 是 CLI 的权威清单。

## 构建产物

所有 writable build state 位于 project root 下：

```text
.mqb/
├─ obj/
├─ deps/
├─ scan/
├─ ifc/
├─ cache/
└─ bin/
```

源码目录不需要承载 MQB 的中间产物。

## 文档

| 文档 | 什么时候看 |
|---|---|
| [`docs/INSTALLATION.md`](docs/INSTALLATION.md) | 安装、PATH、重装、卸载、发布包 |
| [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) | `mqb.json` 完整 schema 与 precedence |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 构建模型、Modules、cache、职责边界 |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | 本仓库开发与测试入口 |
| [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) | stable release 的自举与复现性门禁 |
| [`cpp/README.md`](cpp/README.md) | C++ 源码目录与依赖规则 |

## 开发 MQB

仓库开发入口：

```powershell
.\tests\native\develop.ps1
```

MQB 使用 MQB 自身构建和验证当前源码。开发者细节见 [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)。

## License

Apache License 2.0（SPDX: `Apache-2.0`）。完整条款见 [`LICENSE`](LICENSE)。
