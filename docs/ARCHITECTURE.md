# MQB 架构

**简体中文 | [English](ARCHITECTURE_EN.md)**

本文只描述 MQB 的**设计边界与数据流**。用户 CLI 与配置语义见 [`MQB_CONFIG.md`](MQB_CONFIG.md)，仓库目录强制规则见 [`../cpp/README.md`](../cpp/README.md)，自举发布契约见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。

## 1. 设计目标

MQB 是一个单一原生 C++23 产品：`mqb.exe`。核心设计原则只有几条：

1. **typed data 优先**：build request、artifact、module reference、process argv 都是结构化数据；
2. **correctness 优先于 cache hit rate**：身份不明确时重新构建，不能猜；
3. **依赖真值来自工具链**：header freshness 来自 `/sourceDependencies`，module topology 来自 `/scanDependencies` / P1689；
4. **平台边界集中**：Windows quoting / `CreateProcessW` 不泄漏到 core；
5. **writable state 集中**：所有 MQB-owned 中间产物与 cache 位于 project `.mqb/`。

## 2. 逻辑分层

```text
src/app
  CLI + project composition
          |
          v
 config + discovery
          |
          v
     orchestration  <------ modules
          |                   |
          +---------+---------+
                    v
                   msvc
          compiler/linker/lib/
          source-deps/P1689/
          toolchain discovery
                    |
                    v
                 process
                    |
                    v
             platform/windows
               CreateProcessW

core = shared typed build model, planner, artifact identity and caches
```

依赖不是任意双向调用。上层组合 policy，下层提供 primitive capability。

## 3. 职责边界

### `app`

Executable composition layer：

- CLI parsing；
- invocation / project setup；
- 合并 CLI、`mqb.json` 与 discovery 结果；
- 选择 ordinary / module / static target pipeline；
- 输出 diagnostics；
- `main()`。

`app` 不是公共 library API，app-private headers 留在 `src/app`。

### `core`

工具链无关的构建语义：

- build request / plan；
- translation-unit 与 artifact identity；
- compile/link/archive cache model；
- project artifact layout；
- dependency graph 与 typed options。

`core` 不应知道 `cl.exe`、`link.exe`、`lib.exe`、Windows quoting 或 `CreateProcessW`。

### `config`

拥有 versioned `mqb.json` model、严格解析与 policy resolution。未知字段、错误类型、重复 key、不支持 schema 必须 fail closed。

### `discovery`

只负责**候选源码选择**：include traversal、entry reachability、项目修正项与 module candidate detection。

Discovery 不决定最终 module provider，也不负责 header freshness。

### `modules`

拥有 P1689 typed model 与 module dependency graph：

- project-local named module provider；
- project-local header unit；
- external/prebuilt read-only IFC provider；
- toolchain-owned `std` / `std.compat` provider；
- provider ambiguity / conflict / cycle / unresolved requirement diagnostics。

Provider ownership 只能有一个权威实现；其他层不能靠文件名猜 IFC。

### `orchestration`

组合执行流程：

- bounded scan/compile scheduling；
- incremental compile/link/archive；
- ordinary target pipeline；
- module scan/compile waves；
- target routing；
- toolchain-owned standard-module provider 的按需注入。

它组织 primitive，但不拥有 CLI parsing，也不重新实现 MSVC 参数拼写。

### `msvc`

MSVC backend primitive layer：

- Visual Studio / portable toolchain discovery；
- compiler / linker / librarian invocation construction；
- `/sourceDependencies` reader；
- `/scanDependencies` scanner；
- library resolution；
- 当前 VC Tools `std.ixx` / `std.compat.ixx` capability discovery。

### `process` / `platform/windows`

`process` 定义平台无关的 executable、argv、cwd、environment、result/error model。

`platform/windows` 才负责 Windows command-line encoding 与 `CreateProcessW`。MQB 内部没有通用 shell-command-string API。

## 4. 普通构建流水线

```text
CLI/config
   ↓
source selection
   ↓
artifact preflight
   ↓
compile identity + freshness
   ↓
bounded incremental compile
   ↓
link/archive identity + freshness
   ↓
incremental link or archive
   ↓
.mqb/bin/<target>
```

Header freshness 不靠目录时间戳猜测，而是由编译器生成的 source-dependency metadata 驱动。

Compile、link、archive cache 独立；某一层需要重建不代表所有上游都必须无条件重做。

## 5. Modules / Header Units 流水线

```text
selected source candidates
        ↓
artifact preflight
        ↓
bounded /scanDependencies
        ↓
P1689 typed rules
        ↓
provider resolution
   ┌────┼─────────────────────┐
   │    │                     │
project external IFC     std/std.compat?
source   read-only            │
   │        │                 └─> selected VC Tools module source
   │        │                              ↓
   │        │                       /scanDependencies
   │        │                              ↓
   └────────┴──────────────> provider graph fixed point
                                   ↓
                         dependency-level compile waves
                                   ↓
                           incremental final link
```

### Project-local provider

项目源码中的 module interface / header unit 由 MQB 分配 IFC、OBJ、dependency metadata 与 cache artifact。Provider/consumer 关系由 P1689 决定。

### External/prebuilt provider

用户通过 `modules.external` 或 `--module-ifc name=path.ifc` 显式声明。它具有以下性质：

- 只读 dependency；
- 不参与 source discovery；
- 不进入 MQB compile levels；
- provider identity 进入 consumer compile/cache identity；
- 缺失或冲突时 fail closed。

### `std` / `std.compat`

标准库 named modules 属于 selected MSVC toolchain，而不是普通 external-provider registry。

只有 P1689 requirement 实际引用 `std` / `std.compat` 时，MQB 才查找对应 VC Tools module source。Provider source 也经过 `/scanDependencies`，因此例如 `std.compat -> std` 的闭包来自工具链元数据，而不是硬编码边。

生成的 IFC/OBJ/cache 仍属于当前项目 `.mqb/`，不会写入 Visual Studio 安装目录；其 identity 同时包含 provider source、compiler recipe 与 toolchain identity。

## 6. Build identity 与 cache

`BuildSignature` 表示 versioned compile recipe。一个可复用 compile artifact 的身份至少需要覆盖：

- source identity 与 TU kind；
- selected toolchain identity；
- configuration / architecture / language standard；
- runtime / LTCG 等 typed compile policy；
- ordered compiler arguments；
- typed module/header-unit references；
- required outputs。

关键原则：

- source identity 不能退化成 basename；
- Windows physical aliases 必须收敛为同一 artifact identity；
- external/prebuilt IFC 变化必须使依赖它的 consumer cache 失效；
- toolchain 变化不能静默复用不兼容的 `std` IFC；
- missing recorded output 本身就是 stale signal；
- job count 与 runtime program argv 属于 execution policy，不进入 build identity。

## 7. Artifact layout

所有 MQB-owned writable state 位于 project root：

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

源码目录不承载 MQB 中间产物。Writable artifact 在执行前做 ownership / collision preflight，避免两个逻辑输出静默写到同一位置。

## 8. Project 与路径模型

存在两个独立相对路径基准：

```text
CLI path      -> invocation directory
config path   -> directory containing mqb.json
```

存在 `mqb.json` 时，它的目录是 project root 与 `.mqb/` root；否则 project root 由 invocation/source context 决定。

### Windows path identity authority

MQB 面向 Windows/MSVC，因此凡是判断**路径是否代表同一 Windows 路径**的逻辑，都只有一个 authority：

```cpp
mqb::platform::windows::path_identity_key(path)
```

它负责 lexical normalization、消除非根路径的冗余尾分隔符，并只折叠 ASCII `A-Z`；non-ASCII UTF-8 bytes 原样保留，不允许经过 locale-sensitive narrow `std::tolower`。Discovery、artifact layout、module provider identity、compile/link/archive cache、`BuildSignature`、library resolver、LINK freshness/observation、Visual Studio/portable toolchain identity 都必须共享这一规则。

以下操作不得自行重新实现 Windows path identity：path equality、dedup、set membership、case-insensitive key、root containment。`generic_string() + std::tolower`、自定义 `normalized_path_text()` / `same_windows_path()` 一类实现都不属于允许的 identity primitive。

存在一个刻意收窄的不同语义：当 MQB 需要验证**两个已经存在的路径是否指向同一个物理 filesystem object**时，例如 `/sourceDependencies` 返回的 source provenance，或 include-search freshness 需要把 junction/symlink/canonical alias 视为同一个已存在搜索目录，可以在先比较 `path_identity_key()` 后使用 `std::filesystem::equivalent()` 作为 physical-provenance probe。它不得生成 identity key，也不得用于 `BuildSignature`、cache/artifact ownership、普通 path set membership，代码必须用明确的 `physical/provenance` 命名与注释说明该语义。

字符串**展示/序列化**、环境变量名比较、MSVC option parsing，以及“选择 latest version”所需的确定性**排序**不是路径 identity；这些场景可以保留各自规则，但代码应避免把 ordering/formatting helper 当作 equality authority。

配置解析与 precedence 的完整行为见 [`MQB_CONFIG.md`](MQB_CONFIG.md)。

## 9. 源码物理结构

MQB 只有一套产品树：

```text
cpp/
├─ include/
├─ src/
├─ tests/
└─ mqb.json
```

内部再按 `core / config / discovery / modules / orchestration / msvc / process / platform` 职责组织。这里不重复目录细则；强制规则以 [`../cpp/README.md`](../cpp/README.md) 为准。

## 10. 架构不变量

1. Core 不依赖 MSVC executable spelling 或 Windows process API。
2. 内部 process invocation 始终保持 executable + argv 的结构化表示。
3. Discovery 只选 candidate；`/sourceDependencies` 与 P1689 分别拥有 header/module 真值。
4. Module provider selection 只有一个 owner。
5. Header unit 与 named module 保持不同 typed identity。
6. External IFC 永远是只读 dependency，不能变成 MQB-owned writable artifact。
7. `std` / `std.compat` 永远属于 selected toolchain，不能被项目覆盖。
8. Compile/link/archive cache state 相互独立。
9. Writable artifact 必须在执行前完成冲突检查。
10. Correctness 优先于缓存命中率；unsupported/ambiguous state fail closed。
11. `cpp/include`、`cpp/src`、`cpp/tests` 各自只有一个物理根。
12. MQB 自身的开发、测试与发布构建以 MQB 为构建系统。
13. Windows path equality / dedup / identity key 只能由 `path_identity_key()` 定义；`filesystem::equivalent()` 仅允许用于显式的已存在 physical-provenance/freshness probe，不能成为第二套 lexical/case-folding authority。

## 11. 当前边界

当前 `exe` / `dll` module pipeline 支持 project-local modules/header units、external/prebuilt IFC，以及 toolchain-owned `std` / `std.compat`。

`static` target 仍走独立 archive pipeline；**当目标需要 Modules/Header Units pipeline 时会显式拒绝**。这是当前产品边界，不应在其他层通过降级或猜测绕过。

开发入口见 [`DEVELOPMENT.md`](DEVELOPMENT.md)，stable self-host/release gate 见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。