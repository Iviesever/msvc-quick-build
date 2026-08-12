# C++ Layout Contract

**Language: [简体中文](README.md) | English**

`cpp/` maintains a single native MQB source tree. The directory structure is organized strictly by **file role + code responsibility**, and components are not allowed to duplicate private copies of `include/`, `src/`, or `tests/`.

```text
cpp/
├─ include/                 # single cross-component header root; only public include root
│  └─ mqb/
│     ├─ core/             # toolchain-agnostic build model, planning, and cache
│     ├─ config/           # mqb.json and CLI/config policy model
│     ├─ discovery/        # source / module candidate discovery
│     ├─ json/             # internal JSON parser
│     ├─ modules/          # P1689 and module dependency graph
│     ├─ orchestration/    # target pipeline / incremental coordination
│     ├─ msvc/             # MSVC compiler/linker/librarian/toolchain primitives
│     ├─ process/          # platform-agnostic process model
│     └─ platform/windows/ # Windows process / command-line boundary
│
├─ src/                    # single implementation root
│  ├─ app/                 # mqb.exe thin entry, invocation/project setup, diagnostics, and app orchestration
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
│
├─ tests/                  # single C++ test root; mirrored by target responsibility
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
└─ mqb.json                # single production manifest for MQB self-hosting
```

### Enforced Rules

1. `cpp/include` is the single cross-component include root; adding `cpp/<component>/include` is forbidden.
2. `cpp/src` is the single product implementation root; adding `cpp/<component>/src` is forbidden.
3. `cpp/tests` is the single C++ test root; pushing tests back into component directories is forbidden.
4. `src/app` contains only CLI / executable composition; `main.cpp` must remain a thin entry point, and app-private headers must not enter the public `include/` root.
5. `core` must not depend on `msvc` or `platform/windows`.
6. `msvc` encapsulates MSVC primitive invocations; `orchestration` coordinates compile/link/archive/module pipelines without directly parsing CLI arguments.
7. `platform/windows` implements only Windows boundaries; platform-agnostic process data models reside in `process`.
8. New code must select a responsibility directory before choosing a file; creating arbitrary flat directories for convenience is forbidden.
9. `cpp/mqb.json` must precisely list production translation units and maintain a stable, minimal set of include roots.
10. Directory refactoring must not lower the `67/67 Debug + 67/67 Release + self-host` gates.
