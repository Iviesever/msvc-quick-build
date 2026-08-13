# C++ Source Layout Contract

**[简体中文](README.md) | English**

This file is authoritative for **file placement and dependency boundaries under `cpp/`**. See [`../docs/ARCHITECTURE_EN.md`](../docs/ARCHITECTURE_EN.md) for the higher-level design.

## 1. Top-level rule

MQB maintains one C++ product tree:

```text
cpp/
├─ include/                 # stable product interfaces shared across TUs
│  └─ mqb/
├─ src/                     # product implementation, physically layered by responsibility
├─ tests/                   # C++ tests mirroring implementation ownership
└─ mqb.json                 # self-build production manifest
```

Do not create component-local `include/src/tests` project trees. Choose the responsibility first, then use the unified `include/`, `src/`, and `tests/` roots.

## 2. Top-level responsibilities

| Directory | Responsibility |
|---|---|
| `core` | toolchain-independent build model, planner, artifact identity, caches |
| `config` | `mqb.json` model, parsing, policy resolution |
| `discovery` | source and module-candidate discovery |
| `json` | internal JSON parser |
| `modules` | typed P1689 model, provider graph, module dependency graph |
| `orchestration` | compile/link/archive/module pipeline coordination |
| `msvc` | MSVC compiler/linker/librarian/module-scan/toolchain primitives |
| `process` | platform-independent process model |
| `platform/windows` | Windows quoting, process launch, other platform boundaries |
| `app` | executable composition |

Broad responsibilities must continue into physical sublayers:

```text
cpp/src/
├─ app/
│  ├─ Application.cpp/.hpp
│  ├─ main.cpp
│  ├─ cli/                  # argument parsing + invocation normalization
│  ├─ diagnostics/          # CLI diagnostics / process-output formatting
│  ├─ project/              # project config + CLI/config policy composition
│  └─ targets/              # ordinary/module/static target adapters
├─ core/
│  ├─ cache/                # compile/link/archive cache model + persistence
│  ├─ model/                # typed build/signature/TU classification model
│  └─ planning/             # planner, dependency graph, artifact layout
├─ msvc/
│  ├─ compiler/             # cl.exe compile primitive + source dependencies
│  ├─ linker/               # link.exe + library resolution
│  ├─ librarian/            # lib.exe primitive
│  ├─ modules/              # MSVC module dependency scanning
│  └─ toolchain/            # VS / portable toolchain discovery
├─ orchestration/
│  ├─ scheduling/           # bounded work scheduling
│  ├─ incremental/          # ordinary incremental compile/link/archive flows
│  ├─ modules/              # named-module/header-unit coordination
│  └─ routing/              # ordinary vs module pipeline routing
├─ config/
├─ discovery/
├─ json/
├─ modules/
└─ platform/windows/
```

`cpp/tests/` mirrors the same secondary responsibilities for `app`, `core`, `msvc`, and `orchestration`. Cross-component/full-CLI scenarios belong in `tests/e2e/`.

## 3. Stable facades and private implementation

`include/mqb/...` is the only public include root. Interfaces genuinely shared across TUs/responsibilities remain in stable facades such as `include/mqb/core`, `include/mqb/msvc`, and `include/mqb/orchestration`.

**Implementation layering does not imply public include-path churn.** The `.cpp` files under `core/msvc/orchestration` can be physically grouped without moving public headers. Split a public facade only when the API itself develops a real subdomain boundary.

Headers under `src/app/...` are executable-private interfaces and do not enter public `include/mqb`. The `src/app/` root contains only `Application.cpp`, `Application.hpp`, and `main.cpp`; it must not drift back into a catch-all.

## 4. Dependency direction

Maintain these boundaries:

- `core` does not depend on `msvc`, `orchestration`, or `platform/windows`;
- `core/cache` owns cache identity/validation/persistence, `core/planning` owns decisions/graphs, and `core/model` remains typed domain state;
- `config` / `discovery` do not own MSVC process invocation;
- `modules` owns the provider graph; other directories must not independently guess providers;
- `msvc` constructs and executes MSVC primitives; `msvc/toolchain` only discovers/composes toolchains and does not own upper-layer build policy;
- `orchestration` composes flows; `scheduling` only schedules and `routing` only selects pipelines instead of reimplementing them;
- `process` remains platform-independent and Windows-specific behavior lives in `platform/windows`;
- `app` may compose lower layers, but lower layers do not depend back on `app`;
- `app/targets` must not duplicate formatting/error-expansion logic owned by `app/diagnostics`.

See [`../docs/ARCHITECTURE_EN.md`](../docs/ARCHITECTURE_EN.md) for the full logical graph and invariants.

## 5. Placing a new file

Decide in this order: responsibility owner → product/test → whether a public interface is required → toolchain/platform boundary → whether placement creates an upward dependency.

```text
new cache serializer          -> src/core/cache (+ include/mqb/core if shared)
new build-graph policy        -> src/core/planning
new typed build model         -> src/core/model
new cl.exe compile primitive  -> src/msvc/compiler
new link library resolver     -> src/msvc/linker
new VS discovery rule         -> src/msvc/toolchain
new bounded scheduler         -> src/orchestration/scheduling
new module coordinator        -> src/orchestration/modules
new CLI flag                  -> src/app/cli (+ tests/app/cli/e2e)
new CLI diagnostics           -> src/app/diagnostics
```

## 6. `cpp/mqb.json` and automated gate

`cpp/mqb.json` is the production manifest MQB uses to build itself:

- its source set must exactly match real `cpp/src/**/*.cpp` translation units;
- moves/splits must update the manifest;
- `include` is the only public include root; app-private include roots serve executable composition only;
- product code defaults to **C++23** with `/W4` and `/permissive-`.

Before self-build, `tests/native/build_mqb.ps1` runs `tests/native/assert_cpp_layout.ps1`. The gate rejects unregistered top-level responsibilities, drift back into flat `app/core/msvc/orchestration` roots, files assigned to the wrong owner, and tests that no longer mirror the implementation responsibility.

## 7. C++ language floor

MQB product code targets **C++23** by default. New code should prefer typed domain state, RAII, and standard-library ownership/error models, including:

- `std::expected` / `std::unexpected` for recoverable errors;
- `std::span` / `std::string_view` for non-owning views;
- `std::filesystem::path` for paths;
- scoped RAII wrappers for Win32 handles/resources;
- structured `executable + argv` process descriptions instead of shell-command strings;
- `std::optional`, scoped enums, and designated initializers instead of magic values.

Do not introduce raw `new/delete` ownership, C-style string ownership, unwrapped Win32 resource lifetimes, or shell-command pipelines merely for legacy familiarity.

Modern syntax is not a goal by itself. Adopt a facility when it reduces ownership ambiguity, duplicate implementation, untyped state, or error-propagation noise; do not sacrifice clarity merely to advertise C++23.

## 8. Refactoring floor

Any directory/code refactor must preserve:

- one `include` / `src` / `tests` root;
- established dependency direction and stable public facades;
- exact alignment between `cpp/mqb.json` and production sources;
- a passing `tests/native/assert_cpp_layout.ps1` gate;
- native Debug/Release, self-host, installer/package gates.

Revisit the top-level product structure only when the product shape actually changes; do not create artificial library targets merely to make the tree look tidy.
