# C++ 源码目录契约

**简体中文 | [English](README_EN.md)**

本文件是 `cpp/` 的**文件放置与依赖边界权威说明**。高层设计见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 1. 顶层规则

MQB 只维护一套 C++ 产品树：

```text
cpp/
├─ include/                 # 跨 translation unit 的稳定产品接口
│  └─ mqb/
├─ src/                     # 产品实现，按职责物理分层
├─ tests/                   # C++ tests，镜像实现职责
└─ mqb.json                 # MQB 自构建 production manifest
```

不要创建组件自己的 `include/src/tests` 小项目树。先判断职责，再进入统一的 `include/`、`src/`、`tests/` 根。

## 2. 顶层职责

| 目录 | 负责什么 |
|---|---|
| `core` | 工具链无关 build model、planner、artifact identity、cache |
| `config` | `mqb.json` model、document loading、schema decode、policy resolution |
| `discovery` | source 与 module candidate discovery |
| `json` | 唯一内部 JSON grammar parser |
| `modules` | P1689 typed model、provider graph、module dependency graph |
| `orchestration` | compile/link/archive/module pipeline coordination |
| `msvc` | MSVC compiler/linker/librarian/module scan/toolchain primitives |
| `process` | 平台无关 process model |
| `platform/windows` | Windows quoting、process launch 等平台边界 |
| `app` | executable composition |

宽职责必须继续物理下沉：

```text
cpp/src/
├─ app/
│  ├─ Application.cpp/.hpp
│  ├─ main.cpp
│  ├─ cli/                  # argument parsing + invocation normalization
│  ├─ diagnostics/          # CLI diagnostics / process output formatting
│  ├─ project/              # project config + CLI/config policy composition
│  └─ targets/              # ordinary/module/static target adapters
├─ config/
│  ├─ loading/              # mqb.json locate/read + json::parse error mapping
│  ├─ schema/               # strict config schema / enum / path decoding
│  └─ resolution/           # project config + CLI override resolution
├─ core/
│  ├─ cache/                # compile/link/archive cache model + persistence
│  ├─ model/                # typed build/signature/TU classification model
│  └─ planning/             # planner、dependency graph、artifact layout
├─ msvc/
│  ├─ compiler/             # cl.exe compile primitive + source deps
│  ├─ linker/               # link.exe + library resolution
│  ├─ librarian/            # lib.exe primitive
│  ├─ modules/              # MSVC module dependency scanning
│  └─ toolchain/            # VS / portable toolchain discovery
├─ orchestration/
│  ├─ scheduling/           # bounded work scheduling
│  ├─ incremental/          # ordinary incremental compile/link/archive flows
│  ├─ modules/              # named-module/header-unit coordination
│  └─ routing/              # ordinary vs module pipeline routing
├─ discovery/
├─ json/
├─ modules/
└─ platform/windows/
```

`cpp/tests/` 对 `app`、`core`、`msvc`、`orchestration` 使用同样的二级职责镜像。`tests/config/` 通过公共 config facade 验证 loading/schema/resolution 的组合行为；跨组件/完整 CLI 场景放 `tests/e2e/`。

## 3. 稳定 facade 与 private implementation

`include/mqb/...` 是唯一公共 include root。跨 TU / 跨职责需要共享的接口留在对应 facade，例如 `include/mqb/config`、`include/mqb/core`、`include/mqb/msvc`、`include/mqb/orchestration`。

**implementation 分层不等于公共 include-path churn。** `config/core/msvc/orchestration` 的 `.cpp` 可以按职责移动，而公共 header 路径保持稳定。只有 API 自身出现真实子领域边界时，才讨论进一步拆 public facade。

`src/app/...` 的 header 是 executable-private 接口，不进入公共 `include/mqb`。`src/app/` 根只允许 `Application.cpp`、`Application.hpp`、`main.cpp`，不能重新退化成 catch-all。

## 4. 依赖方向

必须保持：

- `core` 不依赖 `msvc`、`orchestration` 或 `platform/windows`；
- `core/cache` 负责 cache identity/validation/persistence，`core/planning` 负责决策与图，`core/model` 保持 typed domain model；
- `config/loading` 只负责文件定位/读取并调用 `json::parse`，不得拥有 JSON grammar；
- `config/schema` 只负责 `mqb.json` version、字段、类型、枚举与路径 decode；`config/resolution` 只负责 config/CLI policy merge，不做文件 IO 或 JSON parse；
- `config` / `discovery` 不拥有 MSVC process invocation；
- `modules` 拥有 provider graph，其他目录不得各自猜 provider；
- `msvc` 构造并执行 MSVC primitive；`msvc/toolchain` 只负责发现/组装工具链，不拥有上层 build policy；
- `orchestration` 组合流程；`scheduling` 只调度，`routing` 只选择 pipeline，不重做 pipeline；
- `process` 保持平台无关，Windows-specific 实现进入 `platform/windows`；
- `app` 可以组合下层能力，但下层不反向依赖 `app`；
- `app/targets` 不复制 `app/diagnostics` 的格式化与错误展开逻辑。

更完整的逻辑图与不变量见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 5. 新文件放置判断

新增文件时依次判断：职责 owner → 产品/测试 → 是否需要 public interface → toolchain/platform 边界 → 是否造成反向依赖。

```text
新 config 文件查找/IO       -> src/config/loading
新 mqb.json schema 字段     -> src/config/schema
新 CLI/config merge policy  -> src/config/resolution
新 JSON grammar rule        -> src/json
新 cache serializer         -> src/core/cache (+ include/mqb/core 如需共享)
新 build graph policy       -> src/core/planning
新 typed build enum/model   -> src/core/model
新 cl.exe compile primitive -> src/msvc/compiler
新 link library resolver    -> src/msvc/linker
新 VS discovery rule        -> src/msvc/toolchain
新 bounded scheduler        -> src/orchestration/scheduling
新 module coordinator       -> src/orchestration/modules
新 CLI flag                 -> src/app/cli (+ tests/app/cli/e2e)
新 CLI diagnostics          -> src/app/diagnostics
```

## 6. `cpp/mqb.json` 与自动 gate

`cpp/mqb.json` 是 MQB 构建自身的 production manifest：

- production source set 必须与真实 `cpp/src/**/*.cpp` 完全一致；
- 文件移动/拆分必须同步 manifest；
- `include` 是唯一公共 include root；app-private include roots 仅服务 executable composition；
- 默认产品标准保持 **C++23**，并使用 `/W4` 与 `/permissive-`。

`tests/native/build_mqb.ps1` 在自构建前执行 `tests/native/assert_cpp_layout.ps1`。该 gate 会拒绝未登记的一级职责目录、`app/config/core/msvc/orchestration` 根目录漂移、文件放错 owner、以及已登记 tests 职责的镜像漂移。

## 7. C++ 语言底线

MQB 产品代码默认 **C++23**。新代码优先使用 typed domain model、RAII 和标准库所有权/错误模型，例如：

- `std::expected` / `std::unexpected` 表达可恢复错误；
- `std::span` / `std::string_view` 表达非拥有视图；
- `std::filesystem::path` 表达路径；
- scoped RAII wrapper 管理 Win32 handle/资源；
- structured `executable + argv` 代替 shell command string；
- `std::optional`、enum class、designated initializer 等表达真实状态而非 magic value。

禁止新写裸 `new/delete` 所有权、C 风格字符串所有权、无封装 Win32 resource lifetime、或为了兼容旧习惯而退回 command-string pipeline。

现代语法不是目标本身：只有能减少所有权歧义、重复实现、无类型状态或错误传播噪声时才引入；不为了“显得 C++23”牺牲清晰度。

## 8. 重构底线

任何目录或代码重构必须保持：

- 单一 `include` / `src` / `tests` 根；
- 既定依赖方向与稳定 public facade；
- `cpp/mqb.json` 与 production sources 完全一致；
- `tests/native/assert_cpp_layout.ps1` 通过；
- native Debug/Release、self-host、installer/package gates 不退化。

只有产品形态真的改变时才重谈顶层结构；不要为了目录漂亮而制造虚假的独立 library target。
