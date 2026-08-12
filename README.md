# MQB — MSVC Quick Build

面向 Windows + MSVC 的轻量 C/C++ 构建工具。无需维护 `.sln` / `.vcxproj` / CMakeLists.txt，即可直接从源文件完成发现、增量编译、模块拓扑、链接和运行。

> **Stable v5：native only**
>
> - `mqb.exe` 是唯一受支持的构建实现。
> - `mqb` 是唯一受支持的安装命令入口。
> - `mqb.json` 是唯一受支持的项目配置格式。
> - 已淘汰的 PowerShell 构建入口、兼容 shim、profile 注入和旧配置格式不会被静默接管。
> - stable release 必须 self-host：最终 ZIP 中的 `mqb.exe` 必须由 MQB 自身构建，而不是由 CMake 直接产出。

## 主要能力

- 直接结构化调用 `cl.exe` / `link.exe` / `lib.exe`，不通过 shell 拼接编译命令。
- 原生支持 `.c` / `.cpp` / `.cc` / `.cxx` translation units。
- 支持 Visual Studio 与 portable MSVC 工具链发现。
- 单入口 smart discovery 与多文件精确 source set。
- Project-local named modules 与 project-local header units，基于 MSVC P1689 `/scanDependencies`。
- 基于 `/sourceDependencies` 的真实 header freshness 与增量编译。
- Typed `exe` / `dll` / `static` target kinds。
- Typed MSVC runtime、LTCG、subsystem policy。
- `mqb.json` strict project configuration。
- `--run -- arg1 "arg 2"` 结构化运行参数。
- 所有 build state 隔离在项目 `.mqb/` 目录。

当前明确 fail closed 的范围包括 external/prebuilt named-module providers、`import std;`，以及需要 Modules/Header Unit pipeline 的 static-library target。

## 从源码构建

要求：Windows、Visual Studio 2026 / MSVC、C++23 编译能力。开发测试仍使用 CMake 3.25+ 作为 bootstrap/test harness；稳定版发布的最终 `mqb.exe` 不由 CMake 直接产出。

### 开发 / 测试 bootstrap

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

开发构建默认版本为 `5.0.0-dev`。发布版本由 `release/VERSION` 统一定义。

### 用 MQB 构建 MQB

仓库中的 [`cpp/mqb.json`](cpp/mqb.json) 是 MQB 自身的 native project description。给定一个可运行的 MQB binary，可完全绕过 `CMakeLists.txt` 构建 MQB：

```powershell
$version = (Get-Content .\release\VERSION -Raw).Trim()
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
& C:\path\to\mqb.exe apps\mqb\main.cpp --env vs -D $define
Pop-Location
```

输出为：

```text
cpp\.mqb\bin\mqb.exe
```

Stable release workflow 使用两代自举闭包：bootstrap Stage 0 用 MQB 构建 Stage 1；清空 `cpp/.mqb` 后，再由 Stage 1 构建 Stage 2。最终发布 **Stage 1**，并要求 Stage 1/Stage 2 都报告相同 stable version。完整契约见 [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md)。

### 完整测试

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64 `
  -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

## Quickstart

### 单文件 / 自动发现

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
```

单个 positional source 默认启用 smart discovery。

关闭 discovery：

```powershell
mqb main.cpp --no-discover
```

### 多文件精确 source set

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示精确 source set，不再自动扩大集合。

### Target kind

```powershell
mqb main.cpp -o app
mqb api.cpp --type dll -o codec
mqb math.cpp vector.cpp --type static -o math
```

### Runtime / LTCG / subsystem

```powershell
mqb main.cpp --runtime MT
mqb main.cpp --ltcg
mqb math.cpp vector.cpp --type static --ltcg -o math
mqb winmain.cpp --subsystem windows
```

### 运行参数

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

`--` 后的每个参数都会按独立 argv 元素传给程序。

## `mqb.json`

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

完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

旧 `msvc_list.json` 不会被读取或迁移。

## 常用 CLI

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

## 安装

发布包解压后运行：

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

## 架构原则

```text
CLI / mqb.json
      ↓
Source selection / Toolchain discovery
      ↓
P1689 module topology (需要时)
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

## Stable release gate

`v5.0.0` 的正式发布要求同一候选提交同时通过：

1. `Native C++`：完整 installed-MSVC Debug tests；
2. `Native Installer`：install / reinstall / uninstall lifecycle；
3. `Native Release`：完整 Release tests、Stage 0 → Stage 1 → clean Stage 1 → Stage 2 self-host closure、exact package manifest、SHA-256、Stage 1 byte identity 与 packaged-installer validation；
4. 匹配 `release/VERSION` 的 `vX.Y.Z` tag 才能触发 publication；publication job 只发布同一 workflow run 已验证的 artifact，不二次 rebuild。

三条稳定 workflow 的定义文件都属于 `Native C++` 的 PR 触发范围；release / installer workflow 的变更不能绕过 Debug installed-MSVC gate。

历史 `v5.0.0-rc.1` / `v5.0.0-rc.2` release notes 保留原样。Issue #16 独立跟踪 external/prebuilt named-module providers 与 `import std`。

## License

MIT — 见 [`LICENSE`](LICENSE)。
