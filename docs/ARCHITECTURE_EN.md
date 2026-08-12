# MQB Architecture

**Language: [简体中文](ARCHITECTURE.md) | English**

MQB is one native C++23 executable, not a collection of independently shipped libraries. Its architecture is intentionally visible in the filesystem:

- `cpp/include` is the single cross-component header root;
- `cpp/src` is the single product implementation root;
- `cpp/tests` is the single C++ test root;
- responsibilities are mirrored underneath those roots;
- component-local `include/src/tests` trees are forbidden.

Logical dependency boundaries remain equally strict: `core` is toolchain-independent, `config` owns project policy, `discovery` selects source candidates, `modules` owns P1689 topology and provider selection, `orchestration` composes build pipelines, `msvc` owns MSVC primitives and selected-toolchain capabilities, `process` is platform-neutral, and `platform/windows` owns the Windows execution boundary.

External/prebuilt named-module providers are explicit typed policy, never filesystem guesses. `mqb.json` uses `modules.external` as a logical-name-to-IFC map; the CLI uses repeatable `--module-ifc name=path.ifc`, with CLI entries overriding config by logical name. The P1689 module graph remains the sole provider owner: project-local and configured external providers for the same logical name are an ambiguity error. External IFCs are read-only dependencies, never MQB-owned compile nodes or writable artifacts, and resolved consumers reuse the existing typed `ModuleReference` signature/cache machinery.

`std` and `std.compat` are toolchain-owned rather than generic external providers. The selected MSVC toolchain explicitly reports whether its current `VCToolsInstallDir/modules` contains `std.ixx` and `std.compat.ixx`. MQB never source-parses `import std` as a special case: it scans the selected project TUs first, and only a P1689 by-name requirement for `std` or `std.compat` causes the matching toolchain source to be injected. Toolchain module sources are scanned through P1689 as well, so dependencies such as `std.compat -> std` close naturally before the complete graph is passed to the single `ModuleDependencyGraphBuilder` provider owner.

Injected standard-library module interfaces are ordinary generated module providers from that point onward. MQB assigns their writable IFC, object, source-dependency metadata, scan metadata, and compile-cache paths under the current project's `.mqb` tree, compiles them with the same typed compiler recipe as the consumer, feeds their IFC through normal `ModuleReference` routing, and explicitly links their objects. Their compile identity includes the provider source, compiler recipe, and selected `ToolchainIdentity`, so switching VC Tools/compiler identity cannot silently reuse an incompatible standard-library IFC.

MSVC standard-library named modules currently require `--std latest`. If the selected toolchain does not expose the required standard-library module source, or if the language mode is unsupported, the module-target layer emits a capability-specific diagnostic and fails closed. Project-local sources and generic `modules.external` / `--module-ifc` policy cannot impersonate `std` or `std.compat` providers.

See [`cpp/README_EN.md`](../cpp/README_EN.md) for the enforced physical layout contract and [`ARCHITECTURE.md`](ARCHITECTURE.md) for the detailed invariants and module pipeline.

MQB is also its own development, test, and release build system. CMake/CTest are not part of the current authoritative chain. The stable gate is pinned seed → MQB-built Stage 0 → 67 Release tests → Stage 1 → clean Stage 2, with exact artifact and installer validation. See [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) for the full contract.