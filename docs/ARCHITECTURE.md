# MQB 架构 / Architecture

**语言：简体中文 | [English](ARCHITECTURE_EN.md)**

## 1. 总原则

MQB 是一个单一原生 C++23 产品：`mqb.exe`。仓库不再把内部职责伪装成一组独立安装库，也不维护第二套 CMake 工程结构。

架构必须同时在两处清晰：

1. **逻辑依赖清晰**：上层只依赖下层能力，平台/工具链细节不能泄漏进核心模型；
2. **物理目录清晰**：目录树直接表达文件角色和代码职责，不依赖构建系统才能理解项目。

## 2. 物理目录是架构的一部分

`cpp/` 的顶层结构是固定契约：

```text
cpp/
├─ include/                 # 唯一跨组件头文件根
│  └─ mqb/
│     ├─ core/             # 工具链无关模型、规划、缓存、artifact identity
│     ├─ config/           # mqb.json model / policy resolution
│     ├─ discovery/        # source / module candidate discovery
│     ├─ json/             # 内部 JSON parser
│     ├─ modules/          # P1689 与 module dependency graph
│     ├─ orchestration/    # incremental / module / target pipeline coordination
│     ├─ msvc/             # MSVC primitive invocation 与 toolchain discovery
│     ├─ process/          # 平台无关 process model
│     └─ platform/windows/ # Windows process / quoting boundary
│
├─ src/                    # 唯一产品实现根
│  ├─ app/                 # CLI、target composition、main；可含 app-private headers
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
│
├─ tests/                  # 唯一 C++ 测试根，按职责镜像
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
├─ README.md               # 强制目录契约
└─ mqb.json                # MQB 自构建的唯一 production manifest
```

因此以下结构明确禁止重新出现：

```text
cpp/core/include + cpp/core/src
cpp/config/include + cpp/config/src
cpp/<任何组件>/tests
```

这类布局适合独立 library target；MQB 当前不是这种产品形态。完整强制规则见 [`cpp/README.md`](../cpp/README.md)。

## 3. 逻辑分层

```text
src/app
  CLI / mqb.json composition / executable entry
        |
        +---------------------+
        |                     |
        v                     v
   config + discovery      target routing
                                |
                  +-------------+-------------+
                  |                           |
                  v                           v
            orchestration                modules
      incremental compile/link        P1689 / provider graph
      archive / module waves               |
                  |                         |
                  +------------+------------+
                               v
                              msvc
               compiler / linker / librarian /
               source-deps / scan / toolchain
                               |
                               v
                         process model
                               |
                               v
                      platform/windows
                         CreateProcessW

core  <---- shared typed build model, planner, cache and artifact rules
```

这不是“目录之间互相平级随便调用”。依赖方向必须保持明确。

## 4. 各职责边界

### `app`

`src/app` 是 executable composition layer。它负责 CLI parsing、把 CLI/config/discovery 结果组装成 target request、选择高层执行流程以及 `main()`。

它不是公共 library API，因此 app-private headers 与实现共同放在 `src/app`，不进入 `cpp/include`。

### `core`

`core` 只描述工具链无关的构建语义：

- `BuildRequest` / `BuildPlan` / `BuildPlanner`；
- translation-unit 与 artifact identity；
- compiler/link policy data；
- compile/link/archive cache；
- dependency graph；
- project artifact layout。

**Core 不得知道 `cl.exe`、`link.exe`、`lib.exe`、Windows quoting 或 `CreateProcessW`。**

### `config`

`config` 拥有 versioned `mqb.json` model、解析和 CLI > config > defaults 的 policy resolution。未知字段、错误类型、重复 key 和不支持 schema 必须 fail closed。

### `discovery`

`discovery` 负责候选 source selection，不负责最终 MSVC module topology。普通 include traversal、project corrections、secondary-main barrier 以及 module syntax candidate detection 属于这里。

### `modules`

`modules` 负责 P1689 typed model 与 module dependency graph。provider ownership 只有一个权威实现；named modules 与 header units 必须保持类型区别。项目内 provider、显式 external/prebuilt provider 和 toolchain-owned 标准库 provider 的选择、冲突和歧义也只在这里裁决；discovery/orchestration 不允许通过猜测 IFC 路径来补 provider。

### `orchestration`

`orchestration` 负责**组合执行流程**，而不是直接实现编译器参数：

- bounded work scheduling；
- incremental compile / link / archive；
- ordinary target pipeline；
- module scan/compile waves；
- target routing；
- 在 P1689 明确要求 `std` / `std.compat` 时，把当前 toolchain 对应的 module source 注入同一 provider graph。

当前 orchestration 面向 MSVC，因此类名中可以出现 `Msvc*`；但 primitive command construction 仍属于 `msvc`。

### `msvc`

`msvc` 是 MSVC 后端 primitive layer：

- toolchain discovery；
- 当前 VC Tools 版本的 `modules/std.ixx` / `modules/std.compat.ixx` capability discovery；
- compiler / linker / librarian argument construction；
- process invocation adapters；
- `/sourceDependencies` reader；
- `/scanDependencies` scanner；
- library resolution。

它可以依赖 core/process/modules 所需的 typed data，但不拥有 CLI policy。

### `process` 与 `platform/windows`

`process` 只定义结构化 process data，例如 executable、argv、cwd、environment 与结果/error model。

`platform/windows` 才拥有 Windows command-line encoding 与 `CreateProcessW`。内部禁止以 shell command string 作为通用执行 API。

## 5. 架构不变量

1. Core 不知道 MSVC executable spelling。
2. Planner 只产生 typed plan，不直接执行。
3. Correctness 优先于 cache hit rate。
4. 内部没有 shell-command API；路径和 argv 始终是结构化数据。
5. Compile、link、archive cache state 独立。
6. fresh compile 是明确 downstream rebuild signal。
7. source identity 不能退化成 basename。
8. Windows physical aliases 必须收敛为同一 artifact identity。
9. writable artifacts 必须独占并 preflight。
10. runtime argv 与 job count 是 execution policy，不进入 build identity。
11. discovery 只选择 candidate；header freshness 由 `/sourceDependencies` 拥有。
12. module topology 由 `/scanDependencies` / P1689 拥有。
13. module provider selection 只有一个 owner。
14. header units 与 named modules 类型分离。
15. unsupported module requirements fail closed。
16. external/prebuilt named-module IFC 只能通过 typed project/CLI policy 显式声明；它是只读 dependency，不能变成 MQB-owned writable artifact。
17. `std` / `std.compat` provider 属于当前 MSVC toolchain；project source 与 generic external-provider 配置都不能伪造它们。
18. toolchain-owned 标准库 module 只有在 P1689 requirement 实际出现时才注入；provider source 也必须经过 P1689，再进入同一 graph/compile/cache/link 流程。
19. 标准库 module 的生成 IFC/OBJ/cache 属于当前项目 `.mqb`，其 identity 同时受 provider source、完整 compiler recipe 和 selected toolchain identity 约束。
20. stable v5 只有一个 native parser、一个 native executor、一个 C++ 源码树。
21. `cpp/include`、`cpp/src`、`cpp/tests` 分别只有一个物理根。
22. 不允许为了“方便”重新制造组件级 `include/src/tests`。

## 6. Project configuration

MQB 从 invocation directory 向上查找最近的 `mqb.json`。

```text
CLI relative path --------> invocation directory
mqb.json relative path ---> directory containing mqb.json
artifact project root ----> directory containing mqb.json (when present)
```

Scalar precedence：`explicit CLI > mqb.json > built-in defaults`。List-like inputs additive 且 deterministic。External module provider registry 以 logical module name 为 key：`mqb.json` 可在 `modules.external` 中声明 `name -> IFC path`，CLI 可重复使用 `--module-ifc name=path.ifc`；同名 CLI 项定点覆盖 config，CLI 自身重复同名项则 fail closed。

`std` / `std.compat` 不属于这个 registry。它们由 selected MSVC toolchain 的 `VCToolsInstallDir/modules` capability 决定，用户不能通过 `modules.external` 或 `--module-ifc` 覆盖。

MQB 自身的 `cpp/mqb.json` 也是自构建 production manifest。当前物理结构只需要两个 include roots：

```text
include
src/app
```

前者是唯一跨组件头文件根，后者仅服务 app-private headers。

## 7. Source discovery 与 module topology

普通单入口 source 默认启用 smart discovery；多个 positional sources 表示显式有序 source set。

Module pipeline：

```text
selected source candidates
       ↓
writable-artifact preflight
       ↓
bounded /scanDependencies
       ↓
P1689 typed rules
       ↓
std/std.compat requirement?
       ├─ no  ───────────────────────────────┐
       └─ yes -> selected VC Tools module source
                    ↓
              /scanDependencies
                    ↓
          repeat to fixed point              │
       ┌─────────────────────────────────────┘
       ↓
ModuleDependencyGraphBuilder
       ├─ project-local provider -> graph compile dependency
       ├─ explicit external IFC  -> read-only typed ModuleReference
       └─ toolchain std provider -> graph compile dependency
       ↓
dynamic artifact assignment (MQB-owned/generated providers)
       ↓
dependency-level compile waves
       ↓
incremental final link
```

稳定版支持 project-local named modules、project-local header units，以及显式配置的 external/prebuilt named-module IFC provider。External IFC 不参与 source discovery，不加入 compile levels，也不由 MQB 写入；实际 consumer 通过现有 `ModuleReference` 进入 compile signature 和 cache dependency，因此 provider 替换会失效对应 consumer cache，provider 缺失会在进入编译器前 fail closed。

MSVC `import std` / `import std.compat` 是 toolchain-owned capability，而不是 source-parser 特判。MQB 先扫描用户 TU；只有 P1689 by-name requirement 出现 `std` 或 `std.compat` 时，才从当前 `VCToolsInstallDir/modules` 选择对应 `.ixx`。这些 provider source 同样经过 `/scanDependencies`，因此 `std.compat -> std` 依赖由 P1689 自然闭包，而不是硬编码边。随后它们作为 module-interface TU 进入唯一的 `ModuleDependencyGraphBuilder`，并在项目 `.mqb` 下生成/复用 IFC、OBJ、source-dependency metadata 和 compile cache；provider OBJ 明确进入最终 link。

当前 MSVC 标准库 named modules 要求 `--std latest`。如果 selected toolchain 没有相应 `std.ixx` / `std.compat.ixx` capability，或语言模式不满足要求，MQB 会在 module target 层给出明确 unsupported diagnostic 并 fail closed。因为 generated provider compile cache 使用正常的 source + compiler recipe + `ToolchainIdentity`，切换 VC Tools/compiler identity 不会静默复用不兼容的标准库 IFC。

## 8. Build identity 与 artifact layout

`BuildSignature` 表示 versioned compiler recipe。Identity 覆盖 source/TU kind、toolchain identity、configuration、architecture、language standard、ordered compiler options、typed module/header-unit references 与 required outputs。

所有 writable build state 统一位于项目 `.mqb/`：

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

源码目录不承载编译中间产物。toolchain-owned `std.ixx` / `std.compat.ixx` 虽位于 VC Tools 安装目录，但其 MQB writable artifact 仍通过 external-source identity 映射回当前项目 `.mqb`，不会写入 Visual Studio 安装目录。

## 9. 开发、测试与发布门禁

MQB 自身就是 MQB 的构建系统。CMake/CTest **不属于**当前开发、测试、自举或发布链。

```text
pinned historical MQB seed
        ↓
MQB builds current Stage 0
        ↓
67/67 MQB-built Release tests
        ↓
Stage 0 builds Stage 1 with MQB
        ↓
clean MQB state
        ↓
Stage 1 builds Stage 2 with MQB
```

稳定候选必须同时通过：

- current Debug MQB 由 MQB 构建；
- 67/67 Debug tests 由 MQB 构建并直接执行；
- current Release/Stage 0 由 MQB 构建；
- 67/67 Release tests；
- Stage 0 → Stage 1 → clean Stage 2 self-host closure；
- installer lifecycle；
- exact package manifest / SHA-256 / Stage 1 byte identity。

只有 Stage 1 可以进入 stable package。完整契约见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。