# C++ Source Layout Contract

**[简体中文](README.md) | English**

This file is authoritative for **file placement and dependency boundaries under `cpp/`**. See [`../docs/ARCHITECTURE_EN.md`](../docs/ARCHITECTURE_EN.md) for the higher-level design.

## 1. Top-level rule

MQB maintains one C++ product tree:

```text
cpp/
├─ include/                 # headers shared across translation units
│  └─ mqb/
├─ src/                     # product implementation
├─ tests/                   # C++ tests
└─ mqb.json                 # self-build production manifest
```

Do **not** recreate component-local project trees such as:

```text
cpp/core/include + cpp/core/src
cpp/config/include + cpp/config/src
cpp/<component>/tests
```

Choose the responsibility first, then place the file under the unified `include/`, `src/`, or `tests/` root.

## 2. Responsibility directories

| Directory | Responsibility |
|---|---|
| `core` | toolchain-independent build model, planner, artifact identity, caches |
| `config` | `mqb.json` model, parsing, policy resolution |
| `discovery` | source and module-candidate discovery |
| `json` | internal JSON parser |
| `modules` | typed P1689 model, provider graph, module dependency graph |
| `orchestration` | compile/link/archive/module pipeline coordination |
| `msvc` | MSVC compiler/linker/librarian/toolchain primitives |
| `process` | platform-independent process model |
| `platform/windows` | Windows quoting, process launch, and other platform boundaries |
| `app` | executable composition, physically subdivided into CLI, diagnostics, project policy, and target adapters |

The physical layout is approximately:

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
│  │  ├─ Application.cpp/.hpp
│  │  ├─ main.cpp
│  │  ├─ cli/                # argument parsing + invocation normalization
│  │  ├─ diagnostics/        # CLI-facing diagnostics / process output formatting
│  │  ├─ project/            # config discovery + CLI/config policy composition
│  │  └─ targets/            # ordinary/module/static target adapters
│  ├─ core/
│  ├─ config/
│  ├─ discovery/
│  ├─ json/
│  ├─ modules/
│  ├─ orchestration/
│  ├─ msvc/
│  └─ platform/windows/
└─ tests/
   ├─ app/                    # mirrors app sub-responsibilities, e.g. app/cli
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

## 3. Where headers go

### `include/mqb/...`

Use it for product interfaces that genuinely need to be shared **across translation units or responsibilities**.

It is the only public include root. Do not add `cpp/<component>/include`.

### `src/app/...`

Keep app-private headers beside the corresponding executable-composition responsibility. CLI/main-only interfaces do not need to enter public `include/mqb`.

The `src/app/` root contains only the executable shell: `Application.cpp`, `Application.hpp`, and `main.cpp`. CLI parsing, diagnostics, project setup, and target adapters must not drift back into that root as another catch-all.

### Other private implementation details

If something serves one `.cpp` only, prefer keeping it inside the implementation rather than creating a public header simply to look modular.

## 4. Dependency direction

Maintain these boundaries:

- `core` does not depend on `msvc` or `platform/windows`;
- `config` / `discovery` do not own MSVC process invocation;
- `modules` owns the provider graph; other directories must not independently guess providers;
- `orchestration` composes flows while `msvc` constructs and executes MSVC primitives;
- `process` remains platform-independent and Windows-specific implementation goes under `platform/windows`;
- `app` may compose lower-level capabilities, but lower layers should not depend back on `app`;
- `app/targets` adapts pipelines and must not duplicate formatting/error-expansion logic owned by `app/diagnostics`.

See [`../docs/ARCHITECTURE_EN.md`](../docs/ARCHITECTURE_EN.md) for the full logical graph and invariants.

## 5. Placing a new file

Use this order:

1. Which responsibility owns it?
2. Is it product code or a test?
3. Does the product interface really need to be shared across TUs?
4. Does it contain toolchain- or platform-specific behavior?
5. Would the proposed location create an upward dependency from a lower layer?

Examples:

```text
new cache identity model      -> include/mqb/core + src/core
new mqb.json parser rule      -> config
new P1689 provider logic      -> modules
new compile-batch scheduling  -> orchestration
new cl.exe argument builder   -> msvc
new Windows process quoting   -> platform/windows
new CLI flag                  -> src/app/cli (+ tests/app/cli/e2e)
new CLI diagnostics           -> src/app/diagnostics
new target CLI adapter        -> src/app/targets
```

## 6. `cpp/mqb.json`

`cpp/mqb.json` is MQB's production manifest for building itself.

Requirements:

- its production source set must match the real `cpp/src/**/*.cpp` set;
- the production TU count is not a stable contract;
- moving or splitting files requires updating the manifest;
- `include` is the only public include root; app-private include roots are executable-composition details and do not create public API surfaces.

Development drivers validate the manifest against the actual production source set and run `tests/native/assert_cpp_layout.ps1` before self-building to enforce the responsibility-directory contract.

## 7. Tests

All C++ tests live under `cpp/tests/`, mirrored by the responsibility under test. Cross-component and full CLI scenarios belong in `e2e`. For example, CLI parser tests live under `tests/app/cli/`, not directly under `tests/app/`.

Do not move tests back into product directories and do not hard-code the test count in documentation. The authoritative test set is discovered and validated by `tests/native/run_native_tests.ps1` and CI.

See [`../docs/DEVELOPMENT_EN.md`](../docs/DEVELOPMENT_EN.md) for the normal validation workflow.

## 8. C++ language floor

MQB product code targets **C++23** by default. New code should prefer typed data, RAII, standard-library ownership and error models such as `std::expected`, `std::span`, `std::string_view`, `std::filesystem`, scoped resource wrappers, and structured `executable + argv` process descriptions instead of raw ownership, C-style strings, or shell-command strings.

Modern syntax is not a goal by itself. Adopt a newer facility when it reduces ownership ambiguity, duplicate implementation, or untyped state; do not trade readability or compiler stability merely to advertise C++23.

## 9. Refactoring floor

Directory refactors must preserve:

- one `include` / `src` / `tests` root;
- the existing logical dependency direction;
- alignment between `cpp/mqb.json` and the production source set;
- a passing `tests/native/assert_cpp_layout.ps1` contract;
- native test and self-host gates.

Do not recreate the appearance of independent library targets merely to make the tree look tidy. Revisit the top-level structure only if the product shape itself actually changes.
