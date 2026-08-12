# MQB Architecture

**Language: [简体中文](ARCHITECTURE.md) | English**

MQB is one native C++23 executable, not a collection of independently shipped libraries. Its architecture is intentionally visible in the filesystem:

- `cpp/include` is the single cross-component header root;
- `cpp/src` is the single product implementation root;
- `cpp/tests` is the single C++ test root;
- responsibilities are mirrored underneath those roots;
- component-local `include/src/tests` trees are forbidden.

Logical dependency boundaries remain equally strict: `core` is toolchain-independent, `config` owns project policy, `discovery` selects source candidates, `modules` owns P1689 topology and provider selection, `orchestration` composes build pipelines, `msvc` owns MSVC primitives, `process` is platform-neutral, and `platform/windows` owns the Windows execution boundary.

External/prebuilt named-module providers are explicit typed policy, never filesystem guesses. `mqb.json` uses `modules.external` as a logical-name-to-IFC map; the CLI uses repeatable `--module-ifc name=path.ifc`, with CLI entries overriding config by logical name. The P1689 module graph remains the sole provider owner: project-local and configured external providers for the same logical name are an ambiguity error. External IFCs are read-only dependencies, never MQB-owned compile nodes or writable artifacts, and resolved consumers reuse the existing typed `ModuleReference` signature/cache machinery.

`std` and `std.compat` are deliberately excluded from generic external-provider policy. Standard-library modules remain toolchain-owned and fail closed until the separate Issue #16 `import std` slice adds supported MSVC discovery, version identity, and cold/warm coverage.

See [`cpp/README_EN.md`](../cpp/README_EN.md) for the enforced physical layout contract and [`ARCHITECTURE.md`](ARCHITECTURE.md) for the detailed invariants and module pipeline.

MQB is also its own development, test, and release build system. CMake/CTest are not part of the current authoritative chain. The stable gate is pinned seed → MQB-built Stage 0 → 67 Release tests → Stage 1 → clean Stage 2, with exact artifact and installer validation. See [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) for the full contract.