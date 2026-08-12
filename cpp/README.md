# C++ 源码目录契约

**简体中文 | [English](README_EN.md)**

本文件是 `cpp/` 的**文件放置与依赖边界权威说明**。高层设计见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 1. 顶层规则

MQB 只维护一套 C++ 产品树：

```text
cpp/
├─ include/                 # 跨 translation unit 的头文件
│  └─ mqb/
├─ src/                     # 产品实现
├─ tests/                   # C++ tests
└─ mqb.json                 # MQB 自构建 production manifest
```

**不要**重新创建这种组件级子工程结构：

```text
cpp/core/include + cpp/core/src
cpp/config/include + cpp/config/src
cpp/<component>/tests
```

先判断代码职责，再把文件放进统一的 `include/`、`src/`、`tests/` 根。

## 2. 职责目录

主要职责：

| 目录 | 负责什么 |
|---|---|
| `core` | 工具链无关 build model、planner、artifact identity、cache |
| `config` | `mqb.json` model / parsing / policy resolution |
| `discovery` | source 与 module candidate discovery |
| `json` | 内部 JSON parser |
| `modules` | P1689 typed model、provider graph、module dependency graph |
| `orchestration` | compile/link/archive/module pipeline coordination |
| `msvc` | MSVC compiler/linker/librarian/toolchain primitives |
| `process` | 平台无关 process model |
| `platform/windows` | Windows quoting、process launch 等平台边界 |
| `app` | CLI、project composition、diagnostics、`main()`；仅存在于 `src/app` / tests 对应位置 |

物理布局大致为：

```text
cpp/
├─ include/mqb/
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  ├─ process/
│  └─ platform/windows/
├─ src/
│  ├─ app/
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
└─ tests/
   ├─ app/
   ├─ core/
   ├─ config/
   ├─ discovery/
   ├─ json/
   ├─ modules/
   ├─ orchestration/
   ├─ msvc/
   ├─ process/
   ├─ platform/windows/
   └─ e2e/
```

## 3. 头文件放在哪里

### `include/mqb/...`

放**跨 translation unit / 跨职责需要共享**的产品接口。

它是唯一公共 include root。不要新增 `cpp/<component>/include`。

### `src/app/...`

App-private header 与 executable composition implementation 放在一起。CLI/main 专用接口不需要进入公共 `include/mqb`。

### 其他 private implementation detail

如果只服务单个 `.cpp`，优先保持在 implementation 内部；不要为了“看起来模块化”而制造没有跨 TU 价值的公共 header。

## 4. 依赖方向

必须保持：

- `core` 不依赖 `msvc` 或 `platform/windows`；
- `config` / `discovery` 不拥有 MSVC process invocation；
- `modules` 拥有 provider graph，不允许其他目录各自猜 provider；
- `orchestration` 组合流程，`msvc` 构造并执行 MSVC primitive；
- `process` 保持平台无关，Windows-specific 实现进入 `platform/windows`；
- `app` 可以组合下层能力，但下层不应反向依赖 `app`。

更完整的逻辑图与不变量见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 5. 新文件放置判断

新增代码时按这个顺序判断：

1. 它属于哪个职责？
2. 是产品代码还是测试？
3. 产品接口是否真的需要跨 TU 共享？
4. 是否包含 toolchain/platform-specific 细节？
5. 是否会造成下层反向依赖上层？

例子：

```text
新的 cache identity model      -> include/mqb/core + src/core
新的 mqb.json parser 规则      -> config
新的 P1689 provider 逻辑       -> modules
新的编译批次调度               -> orchestration
新的 cl.exe argument builder   -> msvc
新的 Windows process quoting   -> platform/windows
新的 CLI flag                  -> src/app (+ 对应 tests/app/e2e)
```

## 6. `cpp/mqb.json`

`cpp/mqb.json` 是 MQB 构建自身的 production manifest。

要求：

- production source set 必须与真实 `cpp/src/**/*.cpp` 一致；
- production TU 数量不是稳定契约；
- 文件移动/拆分后同步更新 manifest；
- include roots 保持少量、稳定，不用新增目录层级解决职责问题。

开发 driver 会校验 manifest 与真实 source set 的一致性。

## 7. Tests

所有 C++ tests 都进入 `cpp/tests/`，按被测职责镜像；跨组件/完整 CLI 场景放入 `e2e`。

不要把测试重新放进产品目录，也不要在文档中硬编码测试数量。权威测试集合由 `tests/native/run_native_tests.ps1` 与 CI 发现和验证。

日常验证流程见 [`../docs/DEVELOPMENT.md`](../docs/DEVELOPMENT.md)。

## 8. 重构底线

目录重构必须保持：

- 单一 `include` / `src` / `tests` 根；
- 现有逻辑依赖方向；
- `cpp/mqb.json` 与 production source set 一致；
- native test suite 与 self-host gates 不退化。

不要为了目录“更整齐”而重新制造独立 library target 的假象；只有产品形态真的改变时，才应重新讨论顶层结构。
