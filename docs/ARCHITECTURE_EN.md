# MQB Architecture

**Language: [简体中文](ARCHITECTURE.md) | English**

MQB is one native C++23 executable, not a collection of independently shipped libraries. Its architecture is intentionally visible in the filesystem:

- `cpp/include` is the single cross-component header root;
- `cpp/src` is the single product implementation root;
- `cpp/tests` is the single C++ test root;
- responsibilities are mirrored underneath those roots;
- component-local `include/src/tests` trees are forbidden.

Logical dependency boundaries remain equally strict: `core` is toolchain-independent, `config` owns project policy, `discovery` selects source candidates, `modules` owns P1689 topology, `orchestration` composes build pipelines, `msvc` owns MSVC primitives, `process` is platform-neutral, and `platform/windows` owns the Windows execution boundary.

See [`cpp/README_EN.md`](../cpp/README_EN.md) for the enforced physical layout contract.

MQB is also its own development, test, and release build system. CMake/CTest are not part of the current authoritative chain. The stable gate is pinned seed → MQB-built Stage 0 → 67 Release tests → Stage 1 → clean Stage 2, with exact artifact and installer validation. See [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) for the full contract.
