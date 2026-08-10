# MQB — MSVC Quick Build

面向 Windows + MSVC 的轻量 C++ 构建工具。目标是让普通 C++ 项目在**不维护 `.sln` / `.vcxproj` / CMakeLists.txt** 的情况下，直接从源文件完成发现、增量编译、模块拓扑、链接和运行。

> **当前迁移状态**
>
> - `cpp/` 中的 **C++23 V2 (`mqb.exe`)** 已具备可执行的普通 C++ / project-local named modules 构建链，并由 Visual Studio 2026 的真实工具链 E2E 持续验证。
> - `build.ps1` 仍保留为 **PowerShell Golden Reference / 过渡期稳定入口**。最终安装、发布与默认入口切换尚未完成。
> - MQB 不再宣称与 MSBuild “1:1 等价”。当前原则是：对已实现的常用 MSVC 编译/链接语义进行显式建模，并用回归测试证明行为。

## C++ V2 现在能做什么

- **结构化 MSVC 调用**：直接执行 `cl.exe` / `link.exe`，不通过 shell 拼接命令字符串。
- **工具链发现**：支持 Visual Studio 与 portable MSVC 路径，工具链身份进入缓存判断。
- **单入口智能发现**：`mqb main.cpp` 会从项目内 `#include` 和 named `import` 连接关系选择相关 translation units。
- **Project-local named modules**：支持 `.ixx` / `.cppm` / `.mpp` interface providers，使用 MSVC `/scanDependencies` + P1689 建立真实拓扑。
- **增量编译**：使用 `/sourceDependencies` 跟踪实际头文件 freshness；编译参数、工具链、模块引用和计划输出参与 compile identity。
- **IFC 增量正确性**：provider IFC 缺失、provider 重编或引用变化会可靠传导到 consumer。
- **增量链接**：对象、显式库、linker identity 与 link options 共同决定是否重新链接。
- **有界并行**：`-j/--jobs` 控制 TU scan/compile 并发；job count 是 execution policy，不污染 build cache identity。
- **`mqb.json`**：严格、带版本的项目配置，遵循 `explicit CLI > mqb.json > built-in defaults`。
- **结构化运行参数**：`--run -- arg1 "arg 2"` 保持 argv 边界。
- **隔离构建产物**：全部 C++ V2 中间产物放在项目 `.mqb/` 下，不在源码目录通配删除 `.obj/.ifc`。

### 当前明确不支持

以下能力目前**故意 fail closed**，不会为了“看起来能编译”而退回普通 TU 路径：

- C++ header units；
- external / prebuilt named-module providers；
- `import std;`；
- C (`.c`) translation units；
- 将 C++ V2 宣称为 PowerShell 版本的完整行为替代品。

这些 Modules 扩展策略跟踪在 Issue #16；每一种能力都会先定义 artifact / ownership / freshness / cache policy，再接真实 MSVC E2E。

---

## 构建 `mqb.exe`

要求：

- Windows；
- CMake 3.25+；
- Visual Studio 2026 / MSVC（仓库 CI 当前使用 `Visual Studio 18 2026` generator）；
- C++23 编译能力。

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
```

生成的可执行文件位于：

```text
cpp/build/apps/mqb/Release/mqb.exe
```

查看当前 CLI 契约：

```powershell
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

### 运行完整测试

普通开发测试：

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

启用需要本机 Visual Studio/MSVC 的真实工具链测试：

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64 `
  -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

主线 CI 会运行后一种配置。

---

## C++ V2 Quickstart

下面假设 `mqb.exe` 已在 `PATH` 中；开发阶段也可以直接使用上面的构建输出路径。

### 单文件 / 自动发现

```powershell
mqb main.cpp --env vs --std latest --run
```

单个 positional source 默认启用 smart discovery。若 `main.cpp` 通过本地 include/import 连接到其他 C++ TU，MQB 会选择相关源文件再构建。

可以显式关闭：

```powershell
mqb main.cpp --no-discover
```

### 多文件精确 source set

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示**精确 source set**，不再自动扩大集合。

### 运行时参数

```powershell
mqb main.cpp --run -- input.txt "hello world" 42
```

`--` 后的每个参数都按独立 argv 元素传给程序。

### Project-local named modules

例如：

```cpp
// math.ixx
export module math;
export int answer() { return 42; }
```

```cpp
// main.cpp
import math;
int main() { return answer() == 42 ? 0 : 1; }
```

只需要：

```powershell
mqb main.cpp --env vs --std latest --run
```

smart discovery 会把项目内可达的 `math.ixx` 作为**候选 provider** 加入 source set；真正的 provider 选择、依赖顺序和冲突诊断仍由 `/scanDependencies` 的 P1689 结果决定。

也可以显式指定：

```powershell
mqb main.cpp math.ixx --env vs --std latest -j 2 -o app --run
```

未引用的其他 module interface 不会因为存在于项目目录就自动进入目标。

---

## `mqb.json`

C++ V2 使用根目录 `mqb.json`，不是 PowerShell 版本的 `msvc_list.json`。

最小文件：

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

MQB 从 invocation directory 向上查找最近的 `mqb.json`；配置文件所在目录成为 project root 和 `.mqb/` 根。

完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

---

## 常用 C++ V2 CLI

```text
mqb <entry.cpp> [options] [-- program-args...]
mqb <source.cpp|module.ixx|module.cppm|module.mpp> <more-sources...> [options]
```

常用选项：

| 选项 | 作用 |
|---|---|
| `--debug` / `--release` | 选择构建配置 |
| `--std <20|23|latest>` | 选择 C++ 标准 |
| `--x86` / `--x64` | 选择目标架构 |
| `-j, --jobs <N>` | 最大并发 scan/compile 数量 |
| `-o, --output <name>` | `.mqb/bin/` 下的目标名 |
| `--run` | 构建成功后运行 |
| `--discover` / `--no-discover` | 显式打开/关闭单入口 smart discovery |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | 显式链接库 |
| `--env <auto|vs|portable>` | 工具链选择 |
| `--portable-root <dir>` | 增加 portable toolchain root 候选 |
| `-v, --verbose` | 输出 project/config/toolchain/artifact/pipeline 信息 |
| `--` | 后续参数原样作为 program argv |

`mqb --help` 是 CLI 的权威即时说明。

---

## `.mqb/` 产物布局

```text
.mqb/
├── obj/     # collision-free object files
├── deps/    # /sourceDependencies metadata
├── scan/    # /scanDependencies / P1689 metadata
├── ifc/     # module interface artifacts
├── cache/   # compile/link cache metadata
└── bin/     # executable
```

源文件身份参与 artifact routing，因此跨目录同 basename TU 不会共用同一个 object/cache 路径。

---

## 架构原则

C++ V2 的核心链路是：

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
Incremental link
      ↓
Optional run
```

几个重要边界：

1. **Source discovery 只选候选**，不替代编译器依赖信息。
2. **`/scanDependencies` 只负责模块拓扑**，不替代 `/sourceDependencies` 的实际头文件 freshness。
3. **Planner 与 Executor 分离**；Core 不直接知道 `cl.exe` 命令行细节。
4. **shell text 不是构建模型**；进程使用 executable + argv + environment 的结构化表示。
5. **缓存命中必须由 build identity + outputs + dependencies 一起证明**，不能只看源码/EXE mtime。
6. **并行度是执行策略**，改变 `-j` 不应导致 rebuild。
7. 未定义 artifact/freshness policy 的能力必须 **fail closed**。

更详细的模块边界、缓存和 orchestration 设计见 [`docs/CPP_V2_ARCHITECTURE.md`](docs/CPP_V2_ARCHITECTURE.md)。

---

## PowerShell Golden Reference（过渡期）

根目录以下文件仍保留：

```text
build.ps1
install.bat
Microsoft.PowerShell_profile.ps1
```

它们属于旧 PowerShell 实现，目前的作用是：

- 保持已有用户的稳定入口；
- 为 C++ V2 行为迁移提供 Golden Reference；
- 支撑后续 PowerShell/C++ parity campaign 与最终 cutover。

因此**现在不应把 `install.bat` 理解为 C++ V2 `mqb.exe` 的正式安装器**。C++ V2 的发布、安装与默认入口切换仍是后续迁移里程碑。

旧 `msvc_list.json` / PowerShell 参数体系也不等于 C++ V2 的 `mqb.json` schema；不要混用两套配置契约。

---

## 当前路线

接下来的主要工程方向：

- 完成 Modules 扩展策略：header units、external/prebuilt providers、`import std`；
- PowerShell ↔ C++ V2 行为 parity campaign；
- `mqb.exe` 发布/安装/默认入口 cutover；
- 再逐步删除不再需要的 PowerShell Golden Reference。

当前 Modules 扩展跟踪：Issue #16。

## License

MIT — 见 [`LICENSE`](LICENSE)。
