# MQB — MSVC Quick Build

面向 Windows + MSVC 的轻量 C/C++ 构建工具。目标是让普通 C/C++ 项目在**不维护 `.sln` / `.vcxproj` / CMakeLists.txt** 的情况下，直接从源文件完成发现、增量编译、模块拓扑、链接和运行。

> **当前迁移状态**
>
> - `cpp/` 中的 **C++23 重构版 (`mqb.exe`)** 已具备普通 C/C++、project-local named modules 与 project-local header units 的端到端构建链，并由 Visual Studio 2026 真实工具链 E2E 持续验证。
> - `v5.0.0-rc.2` 是 C++ 重构版的第二个可分发 Release Candidate；它新增原生 `.c`、typed runtime/subsystem CLI 与 `mqb.json` policy，**尚不代表 PowerShell → C++ 的最终 cutover**。
> - **当前 `main` 已领先于 rc.2**：普通 C/C++ 目标已具备 typed `exe` / `dll` / `static` 输出，CLI 与 strict `mqb.json` 都可路由到对应 linker / librarian pipeline；这些能力尚未作为新的 prerelease 发布。
> - `build.ps1` 仍保留为 **PowerShell Golden Reference / 过渡期稳定入口**；现有 `install.bat` 和 PowerShell profile 不会被 RC 静默替换。
> - MQB 不宣称与 MSBuild “1:1 等价”。当前原则是：显式建模已支持的常用 MSVC 语义，并用真实编译器回归测试证明行为。

## v5.0.0-rc.2 C++ Release Candidate

RC 的 Windows x64 包由 GitHub Actions 在 **Release 配置**下构建；同一条 workflow 会先运行完整 VS2026 CTest，再生成 zip 和 SHA-256，最后才允许创建 prerelease。

发布资产：

```text
vscode-msvc-quick-build-v5.0.0-rc.2-windows-x64.zip
vscode-msvc-quick-build-v5.0.0-rc.2-windows-x64.zip.sha256
```

安装/试用：

```powershell
# 解压后，将 mqb.exe 所在目录加入 PATH
mqb --help
mqb main.cpp --run
mqb main.c --run
```

`mqb --help` 第一行会携带二进制内嵌版本；RC 包应显示：

```text
MQB 5.0.0-rc.2 - MSVC Quick Build (C++ refactor)
```

发布二进制使用静态 MSVC runtime，避免要求用户额外安装 Visual C++ Redistributable。Release Candidate 的完整边界见 [`release/v5.0.0-rc.2.md`](release/v5.0.0-rc.2.md)。

> 本节描述**已发布的 rc.2 包**。下面“现在能做什么”描述的是**当前仓库 mainline**，因此会包含 rc.2 之后已经合入但尚未重新发布的能力。

---

## C++ 重构版现在能做什么

- **结构化 MSVC 调用**：直接执行 `cl.exe` / `link.exe` / `lib.exe`，不通过 shell 拼接命令字符串。
- **C 与 C++ translation units**：原生支持 `.c`、`.cpp`、`.cc`、`.cxx`；C discovery 不会把合法的 `module/import/export` 标识符误识别成 C++ Modules 语法。
- **工具链发现**：支持 Visual Studio 与 portable MSVC 路径，工具链身份进入缓存判断。
- **单入口智能发现**：`mqb main.cpp` / `mqb main.c` 会从项目内 include / named-import 连接关系选择相关 translation units。
- **Project-local named modules**：支持 `.ixx` / `.cppm` / `.mpp` interface providers、partitions 与 implementation units，使用 `/scanDependencies` + P1689 建立真实拓扑。
- **Project-local header units**：`import "header.hpp";` / `import <header>;` 会进入 P1689 模块管线；header 本身不会被伪装成普通 TU，IFC 由 target 动态分配、增量生成与修复。
- **增量编译**：使用 `/sourceDependencies` 跟踪实际头文件 freshness；编译参数、工具链、模块引用和计划输出参与 compile identity。
- **Typed target kinds**：`--type exe|dll|static`（以及兼容 `-type`）统一进入 typed target policy。exe/DLL 由 linker pipeline 负责，普通 C/C++ static target 由独立 `MsvcLibrarian` / `lib.exe` pipeline 负责。
- **Typed MSVC runtime**：`--runtime MD|MDd|MT|MTd`（以及兼容 `-runtime`）由 backend 统一映射，参与 compile identity，并保持 typed policy 对冲突 raw flag 的最终权威。
- **Typed subsystem**：`--subsystem console|windows`（以及兼容 `-subsystem`）进入 link identity；仅 subsystem 改变时不会误重编 fresh TUs。
- **IFC 增量正确性**：provider IFC 缺失、provider/header-unit 重编或引用变化会可靠传导到 consumer。
- **增量 downstream target**：exe/DLL 使用 link identity/cache；static archive 使用独立 archive identity/cache。目标输出缺失会触发修复，fresh translation units 不会被无关重编。
- **Static archive correctness**：static archive 从干净临时库重建后替换目标，source/object set 缩小时不会残留已移除的旧 object member。
- **有界并行**：`-j/--jobs` 控制 TU scan/compile 并发；job count 是 execution policy，不污染 build cache identity。
- **`mqb.json`**：严格、带版本的项目配置，遵循 `explicit CLI > mqb.json > built-in defaults`，并支持 `build.type` / `build.runtime` / `build.subsystem`。
- **结构化运行参数**：`--run -- arg1 "arg 2"` 保持 argv 边界。
- **隔离构建产物**：全部 C++ 中间产物放在项目 `.mqb/` 下，不在源码目录通配删除 `.obj/.ifc`。

### 当前明确不支持

以下能力目前**故意 fail closed**，不会为了“看起来能编译”而偷偷退回错误管线：

- external / prebuilt named-module providers；
- `import std;`；
- 需要 named-module / header-unit pipeline 的 static-library target（普通 C/C++ static target 已支持）；
- 将当前 C++ mainline 宣称为 PowerShell 版本的完整行为替代品。

Modules 剩余扩展策略跟踪在 Issue #16；最终 stable v5 还需要 PowerShell ↔ C++ parity / installer / default-entry cutover 的独立验收。

---

## 从源码构建 `mqb.exe`

要求：Windows、CMake 3.25+、Visual Studio 2026 / MSVC、C++23 编译能力。

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
cmake --build cpp/build --config Release --target mqb
.\cpp\build\apps\mqb\Release\mqb.exe --help
```

默认开发构建版本为 `5.0.0-dev`。发布流水线会显式传入 `-DMQB_VERSION=5.0.0-rc.2`，因此开发二进制不会冒充已发布版本。

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
mqb main.c --env vs --run
```

单个 positional source 默认启用 smart discovery。若入口通过本地 include / named import 连接到其他 translation units，MQB 会选择相关源文件再构建。C translation units 参与普通 include/main discovery，但不会进入 C++ module lexical parser。

显式关闭：

```powershell
mqb main.cpp --no-discover
```

### 多文件精确 source set

```powershell
mqb main.cpp src/math.cpp src/io.cpp --release -j 8 -o app
```

多个 positional sources 表示**精确 source set**，不再自动扩大集合。

### Target kind：exe / DLL / static

```powershell
# 默认 executable
mqb main.cpp -o app

# DLL；导出符号时 import library 会放在 .mqb/bin/ 下
mqb api.cpp --type dll -o codec

# 普通 C/C++ static library；由 lib.exe pipeline 生成 .lib
mqb math.cpp vector.cpp --type static -o math
```

`--type` 也接受 `executable` / `dynamic` / `lib` alias。static target 不接受 subsystem、library search path、linked libraries 或 raw linker args；这些 linker-only policy 会 fail closed，而不是被静默忽略。需要 named modules/header units 的 static target 目前也会 fail closed。

### Runtime / subsystem

```powershell
# 静态 CRT Release 风格 runtime policy
mqb main.cpp --runtime MT

# Windows GUI subsystem；只改变 link policy，不应让 fresh TU 重编
mqb winmain.cpp --subsystem windows
```

同样的 policy 可写入 `mqb.json`：

```json
{
  "version": 1,
  "build": {
    "runtime": "MT",
    "subsystem": "console"
  }
}
```

显式 CLI scalar 始终覆盖 config scalar。

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
    "type": "exe",
    "runtime": "MT",
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

MQB 从 invocation directory 向上查找最近的 `mqb.json`；配置文件所在目录成为 project root 和 `.mqb/` 根。`build.type` 支持 `exe` / `dll` / `static`（以及 `executable` / `dynamic` / `lib` aliases）。完整 schema、路径基准、precedence 与 cache 行为见 [`docs/MQB_CONFIG.md`](docs/MQB_CONFIG.md)。

---

## 常用 CLI

```text
mqb <entry.c|entry.cpp> [options] [-- program-args...]
mqb <source.c|source.cpp|module.ixx|module.cppm|module.mpp> <more-sources...> [options]
```

| 选项 | 作用 |
|---|---|
| `--debug` / `--release` | 选择构建配置 |
| `--std <14|17|20|23|latest>` | 选择 C++ 标准（C TU 不发 C++ `/std`） |
| `--type <exe|dll|static>` | 选择 executable / DLL / static-library target kind |
| `--x86` / `--x64` | 选择目标架构 |
| `--runtime <MD|MDd|MT|MTd>` | 选择 MSVC runtime library |
| `--subsystem <console|windows>` | 选择 executable/DLL subsystem；static target 不适用 |
| `-j, --jobs <N>` | 最大并发 scan/compile 数量 |
| `-o, --output <name>` | `.mqb/bin/` 下的目标名 |
| `--run` | 构建成功后运行 executable target |
| `--discover` / `--no-discover` | 显式打开/关闭单入口 smart discovery |
| `-I <dir>` | include directory |
| `-D <value>` | preprocessor definition |
| `-L <dir>` / `--lib-path <dir>` | library search directory |
| `-l <name>` / `--lib <name>` | 显式链接库 |
| `--compiler-arg <arg>` | 追加一个原样 `cl.exe` argv 元素 |
| `--linker-arg <arg>` | 追加一个原样 `link.exe` argv 元素 |
| `--env <auto|vs|portable>` | 工具链选择 |
| `--portable-root <dir>` | 增加 portable toolchain root 候选 |
| `-v, --verbose` | 输出 project/config/toolchain/artifact/pipeline 信息 |
| `--` | 后续参数原样作为 executable program argv |

`mqb --help` 是 CLI 的权威即时说明，并显示当前二进制内嵌版本。

---

## `.mqb/` 产物布局

```text
.mqb/
├── obj/     # collision-free object files
├── deps/    # /sourceDependencies metadata
├── scan/    # /scanDependencies / P1689 metadata
├── ifc/     # named-module / header-unit IFC artifacts
├── cache/   # compile/link/archive cache metadata
└── bin/     # executable / DLL / static-library target artifacts
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
Incremental link / archive
      ↓
Optional executable run
```

关键边界：Source discovery 只选候选；`/scanDependencies` 管模块拓扑，`/sourceDependencies` 管实际 header freshness；Planner 与 Executor 分离；shell text 不是构建模型；缓存命中必须由 identity + outputs + dependencies 一起证明；并行度只是 execution policy；未定义 artifact/freshness policy 的能力必须 fail closed。

更详细的模块、缓存和 orchestration 设计见 [`docs/CPP_V2_ARCHITECTURE.md`](docs/CPP_V2_ARCHITECTURE.md)。

---

## PowerShell Golden Reference（过渡期）

根目录的 `build.ps1`、`install.bat`、`Microsoft.PowerShell_profile.ps1` 仍属于旧 PowerShell 实现。它们继续保持已有用户的稳定入口、作为 C++ 迁移 Golden Reference，并支撑后续 parity campaign。

**`v5.0.0-rc.2` 不修改这套安装入口。** RC 用户应直接解压 `mqb.exe` 并自行加入 PATH；在 stable v5 的 parity/cutover 通过之前，不应把 `install.bat` 理解为 C++ `mqb.exe` 的安装器。

旧 `msvc_list.json` / PowerShell 参数体系也不等于 C++ 的 `mqb.json` schema；不要混用两套配置契约。

---

## 当前路线

- **已发布 `v5.0.0-rc.2`**：交付原生 C translation units 与 typed runtime/subsystem policy 的 C++ standalone candidate；
- **当前 mainline**：普通 C/C++ `exe` / `dll` / `static` typed target-kind parity 已完成，CLI + strict `mqb.json` + artifact/cache routing + 真实 MSVC E2E 已合入，但尚未重新发布；
- **下一主线**：typed LTCG，将 `/GL` compile policy 与 exe/DLL 的 `/LTCG` link policy、static archive 的下游消费约束显式建模，而不是要求用户手动同步 raw args；
- **随后**：shared PowerShell ↔ C++ parity fixtures/harness → installer/profile/default-entry cutover → stable release workflow generalization；
- **Issue #16**：独立继续 external/prebuilt named-module providers 与 `import std`；
- 满足 stable gate 后，从**同一份已验证 artifact** 发布正式 `v5.0.0`，再逐步收缩 PowerShell Golden Reference。

## License

MIT — 见 [`LICENSE`](LICENSE)。
