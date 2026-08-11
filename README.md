# MQB — MSVC Quick Build

面向 Windows + MSVC 的轻量 C++ 构建工具。目标是让普通 C++ 项目在**不维护 `.sln` / `.vcxproj` / CMakeLists.txt** 的情况下，直接从源文件完成发现、增量编译、模块拓扑、链接和运行。

> **当前迁移状态**
>
> - `cpp/` 中的 **C++23 重构版 (`mqb.exe`)** 已具备普通 C++、project-local named modules 与 project-local header units 的端到端构建链，并由 Visual Studio 2026 真实工具链 E2E 持续验证。
> - `v5.0.0-rc.1` 是 C++ 重构版的首个可分发 Release Candidate；它是独立的 `mqb.exe`，**尚不代表 PowerShell → C++ 的最终 cutover**。
> - `build.ps1` 仍保留为 **PowerShell Golden Reference / 过渡期稳定入口**；现有 `install.bat` 和 PowerShell profile 不会被 RC 静默替换。
> - MQB 不宣称与 MSBuild “1:1 等价”。当前原则是：显式建模已支持的常用 MSVC 语义，并用真实编译器回归测试证明行为。

## v5.0.0-rc.1 C++ Release Candidate

RC 的 Windows x64 包由 GitHub Actions 在 **Release 配置**下构建；同一条 workflow 会先运行完整 VS2026 CTest，再生成 zip 和 SHA-256，最后才允许创建 prerelease。

发布资产：

```text
vscode-msvc-quick-build-v5.0.0-rc.1-windows-x64.zip
vscode-msvc-quick-build-v5.0.0-rc.1-windows-x64.zip.sha256
```

安装/试用：

```powershell
# 解压后，将 mqb.exe 所在目录加入 PATH
mqb --help
mqb main.cpp --run
```

`mqb --help` 第一行会携带二进制内嵌版本；RC 包应显示：

```text
MQB 5.0.0-rc.1 - MSVC Quick Build (C++ refactor)
```

发布二进制使用静态 MSVC runtime，避免要求用户额外安装 Visual C++ Redistributable。Release Candidate 的完整边界见 [`release/v5.0.0-rc.1.md`](release/v5.0.0-rc.1.md)。

---

## C++ 重构版现在能做什么

- **结构化 MSVC 调用**：直接执行 `cl.exe` / `link.exe`，不通过 shell 拼接命令字符串。
- **工具链发现**：支持 Visual Studio 与 portable MSVC 路径，工具链身份进入缓存判断。
- **单入口智能发现**：`mqb main.cpp` 会从项目内 include / named-import 连接关系选择相关 translation units。
- **Project-local named modules**：支持 `.ixx` / `.cppm` / `.mpp` interface providers、partitions 与 implementation units，使用 `/scanDependencies` + P1689 建立真实拓扑。
- **Project-local header units**：`import "header.hpp";` / `import <header>;` 会进入 P1689 模块管线；header 本身不会被伪装成普通 TU，IFC 由 target 动态分配、增量生成与修复。
- **增量编译**：使用 `/sourceDependencies` 跟踪实际头文件 freshness；编译参数、工具链、模块引用和计划输出参与 compile identity。
- **IFC 增量正确性**：provider IFC 缺失、provider/header-unit 重编或引用变化会可靠传导到 consumer。
- **增量链接**：对象、显式库、linker identity 与 link options 共同决定是否重新链接。
- **有界并行**：`-j/--jobs` 控制 TU scan/compile 并发；job count 是 execution policy，不污染 build cache identity。
- **`mqb.json`**：严格、带版本的项目配置，遵循 `explicit CLI > mqb.json > built-in defaults`。
- **结构化运行参数**：`--run -- arg1 "arg 2"` 保持 argv 边界。
- **隔离构建产物**：全部 C++ 中间产物放在项目 `.mqb/` 下，不在源码目录通配删除 `.obj/.ifc`。

### 当前明确不支持

以下能力目前**故意 fail closed**，不会为了“看起来能编译”而偷偷退回普通 TU 路径：

- external / prebuilt named-module providers；
- `import std;`；
- C (`.c`) translation units；
- 将 C++ RC 宣称为 PowerShell 版本的完整行为替代品。

Modules 剩余扩展策略跟踪在 Issue #16；最终 stable v5 还需要 PowerShell ↔ C++ parity / installer / default-entry cutover 的独立验收。

---

## 从源码构建 `mqb.exe`

要求：Windows、CMake 3.25+、Visual Studio 2026 / MSVC、C++23 编译能力。

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

默认开发构建版本为 `5.0.0-dev`。发布流水线会显式传入 `-DMQB_VERSION=5.0.0-rc.1`，因此开发二进制不会冒充已发布版本。

### 运行完整测试

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64 `
  -DMQB_ENABLE_INSTALLED_MSVC_TESTS=ON
cmake --build cpp/build --config Debug --parallel
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Release Candidate 还会在独立 build tree 中以 `Release` 配置重复完整 installed-MSVC 测试，并对最终包做版本与 SHA-256 校验。

---

## Quickstart

### 单文件 / 自动发现

```powershell
mqb main.cpp --env vs --std latest --run
```

单个 positional source 默认启用 smart discovery。若 `main.cpp` 通过本地 include / named import 连接到其他 C++ TU，MQB 会选择相关源文件再构建。

显式关闭：

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

smart discovery 会把项目内可达的 `math.ixx` 作为**候选 provider** 加入 source set；真正的 provider 选择、依赖顺序和冲突诊断仍由 `/scanDependencies` 的 P1689 结果决定。未引用的 module interface 不会因为存在于目录里就自动进入目标。

### Project-local header units

```cpp
// util.hpp
inline int answer() { return 42; }
```

```cpp
// main.cpp
import "util.hpp";
int main() { return answer() == 42 ? 0 : 1; }
```

同样只需要：

```powershell
mqb main.cpp --env vs --std latest --run
```

lexical discovery 只负责识别“这个 entry 必须进入 module pipeline”，不会把 `util.hpp` 放进 translation-unit source set。MSVC `/scanDependencies` 的 P1689 `source-path` / lookup method 才是 header-unit provider 拓扑的权威来源；MQB 随后为该物理 header 动态分配 `.mqb/ifc`、deps 和 cache artifact。

---

## `mqb.json`

C++ 重构版使用根目录 `mqb.json`，不是 PowerShell 版本的 `msvc_list.json`。

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

MQB 从 invocation directory 向上查找最近的 `mqb.json`；配置文件所在目录成为 project root 和 `.mqb/` 根。完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

---

## 常用 CLI

```text
mqb <entry.cpp> [options] [-- program-args...]
mqb <source.cpp|module.ixx|module.cppm|module.mpp> <more-sources...> [options]
```

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

`mqb --help` 是 CLI 的权威即时说明，并显示当前二进制内嵌版本。

---

## `.mqb/` 产物布局

```text
.mqb/
├── obj/     # collision-free object files
├── deps/    # /sourceDependencies metadata
├── scan/    # /scanDependencies / P1689 metadata
├── ifc/     # named-module / header-unit IFC artifacts
├── cache/   # compile/link cache metadata
└── bin/     # executable
```

Windows 上同一物理源文件即使被用户或 MSVC scanner 以不同大小写/路径别名表示，也会归一到稳定 artifact identity，避免一份 source 分裂出两套 cache/IFC。

---

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
Incremental link
      ↓
Optional run
```

关键边界：Source discovery 只选候选；`/scanDependencies` 管模块拓扑，`/sourceDependencies` 管实际 header freshness；Planner 与 Executor 分离；shell text 不是构建模型；缓存命中必须由 identity + outputs + dependencies 一起证明；并行度只是 execution policy；未定义 artifact/freshness policy 的能力必须 fail closed。

更详细的模块、缓存和 orchestration 设计见 [`docs/CPP_V2_ARCHITECTURE.md`](docs/CPP_V2_ARCHITECTURE.md)。

---

## PowerShell Golden Reference（过渡期）

根目录的 `build.ps1`、`install.bat`、`Microsoft.PowerShell_profile.ps1` 仍属于旧 PowerShell 实现。它们继续保持已有用户的稳定入口、作为 C++ 迁移 Golden Reference，并支撑后续 parity campaign。

**`v5.0.0-rc.1` 不修改这套安装入口。** RC 用户应直接解压 `mqb.exe` 并自行加入 PATH；在 stable v5 的 parity/cutover 通过之前，不应把 `install.bat` 理解为 C++ `mqb.exe` 的安装器。

旧 `msvc_list.json` / PowerShell 参数体系也不等于 C++ 的 `mqb.json` schema；不要混用两套配置契约。

---

## 当前路线

- `v5.0.0-rc.1`：交付可验证的 C++ standalone candidate；
- Issue #16：继续 external/prebuilt named-module providers 与 `import std`；
- PowerShell ↔ C++ 行为 parity campaign；
- installer/profile/default-entry cutover；
- 满足上述 stable gate 后发布正式 v5，并逐步删除不再需要的 PowerShell Golden Reference。

## License

MIT — 见 [`LICENSE`](LICENSE)。
