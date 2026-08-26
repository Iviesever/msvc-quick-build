# MSVC 参数覆盖契约

**[English](MSVC_PARAMETER_COVERAGE.md) | 简体中文**

MQB 把参数覆盖拆成三个不同的 correctness layer。某一层变绿，不能被当作后续层已经完整的证据。

## Layer 1 — 官方 canonical option 覆盖

`tests/native/verify_msvc_parameter_inventory.ps1` 是 `MSVC_PARAMETER_INVENTORY.md` 中记录的 canonical inventory gate。

它把固定的 MicrosoftDocs denominator 保持在 **444 个 canonical entries**（compiler 309、linker 114、librarian 21），并只回答一个窄问题：

> 官方文档中的每一个 canonical row，是否都有确定的 MQB ownership 结果，而不是落入未知黑洞？

这一层有意**不证明**每一种 syntax variant、documented mode、file operand、secondary output 或 cross-stage interaction 都是安全的。

## Layer 2 — Semantic variant inventory

`tests/native/verify_msvc_semantic_variant_inventory.ps1` 会把 `tests/native/msvc_semantic_variant_inventory.cpp` 与候选 MQB product library 一起构建，并生成 `msvc-semantic-variant-inventory.tsv`。

这份契约使用具体的高风险拼写，而不是 canonical family row。它要求覆盖以下 risk class：

- `artifact-producing`
- `file-input-bearing`
- `response-file-bearing`
- `cross-stage`
- `mode-switching`
- `version-dependent`
- `pipeline-changing`

每一行都记录 tool、semantic family、concrete variant、risk class、expected ownership、actual ownership，以及存在时的 Layer-3 behavioral trace。Verifier 还会拒绝重复的 `(tool, variant)` 行；如果任何必需的 risk class 从 inventory 中消失，也会失败。

这一层是有意维护的 curated inventory，而不是假装可以枚举一个没有上界的 grammar。即使某个新发现的高风险 mode 所属 canonical family 已经包含在 444/444 gate 中，也应把具体 mode 加到这里。

### Dynamic Debugging policy

Dynamic Debugging 是推动这一 cross-stage 模型的典型反例。Microsoft 把它记录为 compiler、librarian、linker 协同工作的功能，并会产生替代的 object/import/export/executable/PDB artifacts。MQB 当前还没有建模这套替代 artifact graph，也没有建模该功能的 cross-stage constraints。

因此，MQB 能识别的所有 `/dynamicdeopt*` 形式，在 compiler、linker、librarian 上都属于 **Class D / unsupported**。Semantic inventory 还会针对 Dynamic Debugging case 调用 `route_compiler()`、`route_linker()` 与 `route_librarian()`，并要求返回 `unsupported_option`，其中包含以下诊断：

> Dynamic Debugging introduces cross-stage policy and secondary artifacts that are not yet represented in MQB's build/cache graph.

未来如果实现 Dynamic Debugging，必须用 first-class build/cache graph 替代当前 fail-closed 边界；仅仅把这些 variant 改回 passthrough 并不充分。

## Layer 3 — Behavioral graph tests

聚焦的 native 与 C++ tests 用于证明单个 ownership label 无法建立的行为：semantic normalization、conflict handling、file-input resolution 与 freshness、secondary-output repair、runtime-library freshness、version admission、cache identity，以及 pipeline boundaries。

示例包括 AddressSanitizer、LibFuzzer、OpenMP、external-include、transitive-default-library、include-search 和 linker side-output native gates。Layer-2 的行可以把这些测试作为 traceability evidence，但 semantic inventory 不能替代它们。

## 如何解释 CI

因此，CI 应按下面的方式理解：

```text
Layer 1: canonical documentation coverage
    +
Layer 2: concrete high-risk semantic variants
    +
Layer 3: focused behavioral graph evidence
    =
parameter-engine closure evidence
```

`444 / 444` 仍然有价值且是强制要求，但它表示的是 **canonical ownership coverage**，并不表示“所有 MSVC 参数语义都已经安全”。
