# MQB — MSVC Quick Build

**语言：简体中文（默认） | [English](#english)**

## 简体中文

面向 Windows + MSVC 的轻量 C/C++ 构建工具。无需维护 `.sln` / `.vcxproj` / `CMakeLists.txt`，即可直接从源文件完成发现、增量编译、模块拓扑、链接和运行。

> **Stable v5：仅原生 C++ 实现**
>
> - `mqb.exe` 是唯一受支持的构建实现。
> - `mqb` 是唯一受支持的安装命令入口。
> - `mqb.json` 是唯一受支持的项目配置格式。
> - 已淘汰的 PowerShell 构建入口、兼容 shim、profile 注入和旧配置格式不会被静默接管。
> - 稳定版发布必须完成自举（self-host）：最终 ZIP 中的 `mqb.exe` 必须由 MQB 自身构建，而不是由 CMake 直接产出。

### 主要能力

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

### 从源码构建

要求：Windows、Visual Studio 2026 / MSVC、C++23 编译能力。开发测试仍使用 CMake 3.25+ 作为 bootstrap/test harness；稳定版发布的最终 `mqb.exe` 不由 CMake 直接产出。

#### 开发 / 测试 bootstrap

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

开发构建默认版本为 `5.0.0-dev`。发布版本由 `release/VERSION` 统一定义。

#### 用 MQB 构建 MQB

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

#### 完整测试

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64 `
  -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

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

完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

旧 `msvc_list.json` 不会被读取或迁移。

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

### 架构原则

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

### 稳定版发布门禁

`v5.0.0` 的正式发布要求同一候选提交同时通过：

1. `Native C++`：完整 installed-MSVC Debug tests；
2. `Native Installer`：install / reinstall / uninstall lifecycle；
3. `Native Release`：完整 Release tests、Stage 0 → Stage 1 → clean Stage 1 → Stage 2 self-host closure、exact package manifest、SHA-256、Stage 1 byte identity 与 packaged-installer validation；
4. 匹配 `release/VERSION` 的 `vX.Y.Z` tag 才能触发 publication；publication job 只发布同一 workflow run 已验证的 artifact，不二次 rebuild。

三条稳定 workflow 的定义文件都属于 `Native C++` 的 PR 触发范围；release / installer workflow 的变更不能绕过 Debug installed-MSVC gate。

历史 `v5.0.0-rc.1` / `v5.0.0-rc.2` release notes 保留原样。Issue #16 独立跟踪 external/prebuilt named-module providers 与 `import std`。

### 许可证

MIT — 见 [`LICENSE`](LICENSE)。

---

<a id="english"></a>

## English

MQB is a lightweight C/C++ build tool for Windows + MSVC. It builds directly from source files without requiring you to maintain `.sln`, `.vcxproj`, or `CMakeLists.txt` files for normal use, covering source discovery, incremental compilation, module topology, linking, and execution.

> **Stable v5: native C++ only**
>
> - `mqb.exe` is the only supported build implementation.
> - `mqb` is the only supported installed command entry point.
> - `mqb.json` is the only supported project configuration format.
> - Retired PowerShell build entry points, compatibility shims, profile injection, and legacy configuration formats are not silently adopted.
> - Stable releases must self-host: the `mqb.exe` shipped in the final ZIP must be built by MQB itself, not directly by CMake.

### Highlights

- Structured direct invocation of `cl.exe`, `link.exe`, and `lib.exe` without shell-built command strings.
- Native `.c`, `.cpp`, `.cc`, and `.cxx` translation units.
- Visual Studio and portable MSVC toolchain discovery.
- Smart discovery from a single entry source and exact source sets for multi-file invocations.
- Project-local named modules and project-local header units based on MSVC P1689 `/scanDependencies`.
- Real header freshness and incremental compilation based on `/sourceDependencies`.
- Typed `exe`, `dll`, and `static` target kinds.
- Typed MSVC runtime, LTCG, and subsystem policies.
- Strict `mqb.json` project configuration.
- Structured program arguments with `--run -- arg1 "arg 2"`.
- All generated build state is isolated under the project's `.mqb/` directory.

Current fail-closed boundaries include external/prebuilt named-module providers, `import std;`, and static-library targets that would require the Modules/Header Unit pipeline.

### Building from source

Requirements: Windows, Visual Studio 2026 / MSVC, and C++23 compiler support. Development and testing still use CMake 3.25+ as the bootstrap/test harness; the final stable `mqb.exe` is not directly produced by CMake.

#### Development / test bootstrap

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

Development builds default to version `5.0.0-dev`. Release versions are defined by `release/VERSION`.

#### Build MQB with MQB

[`cpp/mqb.json`](cpp/mqb.json) is MQB's native self-build project description. Given a working MQB binary, MQB can build itself without using `CMakeLists.txt`:

```powershell
$version = (Get-Content .\release\VERSION -Raw).Trim()
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
& C:\path\to\mqb.exe apps\mqb\main.cpp --env vs -D $define
Pop-Location
```

Output:

```text
cpp\.mqb\bin\mqb.exe
```

The stable release workflow uses a two-generation self-host closure: bootstrap Stage 0 builds Stage 1 with MQB; after deleting `cpp/.mqb`, Stage 1 builds Stage 2 from clean MQB state. **Stage 1** is the artifact that is released, and both Stage 1 and Stage 2 must report the same stable version. See [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) for the complete contract.

#### Full test suite

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64 `
  -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

### Quickstart

#### Single file / automatic discovery

```powershell
mqb main.cpp --env vs --std latest --run
mqb main.c --env vs --run
```

A single positional source enables smart discovery by default.

Disable discovery with:

```powershell
mqb main.cpp --no-discover
```

#### Exact multi-file source set

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

Multiple positional sources form an exact source set; MQB does not automatically expand it.

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

#### Program arguments

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

Every argument after `--` is passed to the program as a separate argv element.

### `mqb.json`

MQB searches upward from the invocation directory for the nearest `mqb.json`. The configuration file's directory becomes the project root and the root of `.mqb/` state.

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

See [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md) for the full schema, path bases, precedence rules, and cache behavior.

Legacy `msvc_list.json` files are not read or migrated.

### Common CLI options

```text
mqb <entry.c|entry.cpp> [options] [-- program-args...]
mqb <source.c|source.cpp|module.ixx|module.cppm|module.mpp> <more-sources...> [options]
```

| Option | Purpose |
|---|---|
| `--debug` / `--release` | Select build configuration |
| `--config <debug|release>` | Explicitly select build configuration |
| `--std <14|17|20|23|latest>` | Select C++ language standard |
| `--type <exe|dll|static>` | Select target kind |
| `--x86` / `--x64` | Select target architecture |
| `--runtime <MD|MDd|MT|MTd>` | Select MSVC runtime |
| `--ltcg` / `--no-ltcg` | Enable/disable coupled LTCG |
| `--subsystem <console|windows>` | Select executable/DLL subsystem |
| `-j, --jobs <N>` | Maximum concurrent scan/compile jobs |
| `-o, --output <name>` | Set the target name under `.mqb/bin/` |
| `--run` | Run an executable after a successful build |
| `--discover` / `--no-discover` | Control smart discovery |
| `-I <dir>` | Add include directory |
| `-D <value>` | Add preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | Add library search directory |
| `-l <name>` / `--lib <name>` | Link a library explicitly |
| `--compiler-arg <arg>` | Append one literal `cl.exe` argv element |
| `--linker-arg <arg>` | Append one literal `link.exe` argv element |
| `--env <auto|vs|portable>` | Select toolchain preference |
| `--portable-root <dir>` | Add a portable toolchain root candidate |
| `-v, --verbose` | Show detailed output |
| `-h, --help` | Show help and embedded version |
| `--` | Pass all following argv elements to the target program |

Retired single-dash aliases are rejected as unknown options rather than entering a compatibility path.

### Installation

After extracting a release package, run:

```powershell
.\install.bat
```

The default install directory is:

```text
%USERPROFILE%\bin
```

The installer deploys `mqb.exe` plus installer-owned maintenance/state files. It does not create a `build` compatibility command and does not modify PowerShell profiles.

Uninstall with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

See [`docs/INSTALLATION.md`](docs/INSTALLATION.md) for the complete installation contract.

### Architecture

```text
CLI / mqb.json
      ↓
Source selection / Toolchain discovery
      ↓
P1689 module topology (when required)
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

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for more detail on modules, caching, and orchestration.

### Stable release gates

A formal `v5.0.0` release requires the same candidate commit to pass all of the following:

1. `Native C++`: full installed-MSVC Debug tests;
2. `Native Installer`: install / reinstall / uninstall lifecycle;
3. `Native Release`: full Release tests, Stage 0 → Stage 1 → clean Stage 1 → Stage 2 self-host closure, exact package manifest, SHA-256, Stage 1 byte identity, and packaged-installer validation;
4. only a `vX.Y.Z` tag matching `release/VERSION` can trigger publication, and the publication job releases the already-validated artifact from the same workflow run without rebuilding it.

All three stable workflow definition files are included in the `Native C++` pull-request trigger set, so release or installer workflow changes cannot bypass the Debug installed-MSVC gate.

Historical `v5.0.0-rc.1` and `v5.0.0-rc.2` release notes remain unchanged. Issue #16 independently tracks external/prebuilt named-module providers and `import std`.

### License

MIT — see [`LICENSE`](LICENSE).
