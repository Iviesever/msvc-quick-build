# Exact MSVC parameter inventory

MQB's parameter-engine coverage gate is derived from immutable MicrosoftDocs Git blob snapshots rather than a hand-maintained representative list.

`tests/native/verify_msvc_parameter_inventory.ps1` parses the official compiler, LINK, and LIB option tables, expands option variants that are explicitly enumerated in the reference (for example `[-]`, `/favor:<...>`, `/vd{...}`, and `/ZH:[...]`), and adds independently documented command-line forms such as LINK `/DEBUG:FULL`, `/DEBUG:NONE`, `/DEBUG:FASTLINK`, LIB response files, and `/WX:NO`.

The current denominator is:

- compiler: **309** canonical entries, MicrosoftDocs snapshot dated 2026-05-25;
- linker: **114** canonical entries, plus the `/DEBUG` syntax snapshot dated 2025-09-08;
- librarian: **21** canonical entries;
- total: **444** canonical entries.

The snapshot Git blob SHA and expected count are part of the verifier contract. Updating an official reference therefore requires an intentional code review instead of silently changing the denominator.

For each canonical entry, the verifier builds and runs a C++ probe against the candidate MQB product library and calls the real `MsvcParameterEngine::classify()`. A bare family name is tried first. If the documented family requires a colon payload and the bare name is unregistered, one neutral `:mqb_probe` payload is tried. Any entry that still resolves to the registry's `unregistered` result fails the Native C++ gate.

The emitted `msvc-parameter-inventory.tsv` records the tool, canonical spelling, concrete classification probe, and resulting ownership class for every entry. It is uploaded with the Native C++ build artifact so the exact A/B/C/D routing matrix is auditable from CI.

Exact inventory coverage is deliberately separate from toolchain lifecycle admission. For example, `/DEBUG:FASTLINK` remains a registered passthrough ownership result so pre-VS-2026 toolsets can admit it, while `MsvcParameterCapabilities` rejects it on the VS 2026 toolset boundary. Likewise, the inventory locks `/DEBUG:NONE` as passthrough and the obsolete compiler `/Zc:trigraphs` spelling as unsupported.

Focused behavioral tests remain responsible for semantic normalization, conflict handling, token shape, graph-aware file inputs, cache identity, and lifecycle boundaries. The exact inventory gate answers a narrower but stronger question: **does every canonical option in the pinned official references have a deterministic MQB ownership result, with no unknown black hole?**
