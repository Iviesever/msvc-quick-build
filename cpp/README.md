# C++ 目录契约

**语言：简体中文 | [English](README_EN.md)**

`cpp/` 只维护一套原生 MQB 源码树。目录按**文件角色 + 代码职责**分层，不允许每个组件再复制一套 `include/` / `src/` / `tests/`。

```text
cpp/
├─ include/                 # 跨 translation unit 的头文件；唯一公共 include root
│  └─ mqb/
│     ├─ core/             # 工具链无关的构建模型、规划与缓存
│     ├─ config/           # mqb.json 与 CLI/config policy model
│     ├─ discovery/        # source / module candidate discovery
│     ├─ json/             # 内部 JSON parser
│     ├─ modules/          # P1689 与 module dependency graph
│     ├─ orchestration/    # target pipeline / incremental coordination
│     ├─ msvc/             # MSVC compiler/linker/librarian/toolchain primitives
│     ├─ process/          # 平台无关 process model
│     └─ platform/windows/ # Windows process / command-line boundary
│
├─ src/                    # 唯一 implementation root
│  ├─ app/                 # mqb.exe 薄入口、invocation/project setup、diagnostics 与 app orchestration
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
│
├─ tests/                  # 唯一 C++ test root；按被测职责镜像
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
└─ mqb.json                # MQB 自构建的唯一 production manifest
```

### 强制规则

1. `cpp/include` 是唯一跨组件 include root；禁止新增 `cpp/<component>/include`。
2. `cpp/src` 是唯一产品实现根；禁止新增 `cpp/<component>/src`。
3. `cpp/tests` 是唯一 C++ 测试根；禁止把测试重新塞回产品组件目录。
4. `src/app` 只放 CLI / executable composition；`main.cpp` 必须保持薄入口，app-private headers 不得进入公共 `include/`。
5. `core` 不得依赖 `msvc` 或 `platform/windows`。
6. `msvc` 封装 MSVC primitive invocation；`orchestration` 负责组合 compile/link/archive/module pipeline，不直接承担 CLI 解析。
7. `platform/windows` 只实现 Windows 边界；平台无关 process 数据模型放在 `process`。
8. 新代码必须先选择职责目录，再选择文件；不得以“方便”为理由创建新的平级杂项目录。
9. `cpp/mqb.json` 必须精确列出 production translation units，并保持一个稳定、少量的 include-root 集合。
10. 目录重构不得降低 `67/67 Debug + 67/67 Release + self-host` 门禁。
