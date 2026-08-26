# MSVC parameter coverage contract

**English | [简体中文](MSVC_PARAMETER_COVERAGE_ZH.md)**

MQB treats parameter coverage as three different correctness layers. A green result at one layer must not be used as evidence that the later layers are complete.

## Layer 1 — Official canonical option coverage

`tests/native/verify_msvc_parameter_inventory.ps1` is the canonical inventory gate documented in `MSVC_PARAMETER_INVENTORY.md`.

It keeps the pinned MicrosoftDocs denominator at **444 canonical entries** (compiler 309, linker 114, librarian 21) and asks one narrow question:

> Does every canonical documentation row have a deterministic MQB ownership result instead of falling into an unknown black hole?

This layer intentionally does **not** prove that every syntax variant, documented mode, file operand, secondary output, or cross-stage interaction is safe.

## Layer 2 — Semantic variant inventory

`tests/native/verify_msvc_semantic_variant_inventory.ps1` builds `tests/native/msvc_semantic_variant_inventory.cpp` against the candidate MQB product library and emits `msvc-semantic-variant-inventory.tsv`.

This contract uses concrete high-risk spellings rather than canonical family rows. It requires coverage for these risk classes:

- `artifact-producing`
- `file-input-bearing`
- `response-file-bearing`
- `cross-stage`
- `mode-switching`
- `version-dependent`
- `pipeline-changing`

Each row records the tool, semantic family, concrete variant, risk class, expected ownership, actual ownership, and a Layer-3 behavioral trace where one exists. The verifier also rejects duplicate `(tool, variant)` rows and fails if any required risk class disappears from the inventory.

This layer is deliberately curated instead of pretending to enumerate an unbounded grammar. A newly discovered high-risk mode should be added here even when its canonical family is already part of the 444/444 gate.

### Dynamic Debugging policy

Dynamic Debugging is the motivating cross-stage counterexample. Microsoft documents it as a coordinated compiler, librarian, and linker feature with alternate object/import/export/executable/PDB artifacts. MQB does not currently model that alternate artifact graph or the feature's cross-stage constraints.

Therefore all MQB-recognized `/dynamicdeopt*` forms are **Class D / unsupported** for compiler, linker, and librarian. The semantic inventory additionally calls `route_compiler()`, `route_linker()`, and `route_librarian()` for the Dynamic Debugging cases and requires an `unsupported_option` result containing this diagnostic:

> Dynamic Debugging introduces cross-stage policy and secondary artifacts that are not yet represented in MQB's build/cache graph.

A future Dynamic Debugging implementation must replace this fail-closed boundary with a first-class build/cache graph; simply changing the variants back to passthrough is not sufficient.

## Layer 3 — Behavioral graph tests

Focused native and C++ tests prove the behavior that an ownership label alone cannot establish: semantic normalization, conflict handling, file-input resolution and freshness, secondary-output repair, runtime-library freshness, version admission, cache identity, and pipeline boundaries.

Examples include the AddressSanitizer, LibFuzzer, OpenMP, external-include, transitive-default-library, include-search, and linker side-output native gates. Layer-2 rows may name these tests as traceability evidence, but the semantic inventory does not replace them.

## Interpretation

The intended reading of CI is therefore:

```text
Layer 1: canonical documentation coverage
    +
Layer 2: concrete high-risk semantic variants
    +
Layer 3: focused behavioral graph evidence
    =
parameter-engine closure evidence
```

`444 / 444` remains valuable and mandatory, but it means **canonical ownership coverage**, not "all MSVC parameter semantics are safe".
