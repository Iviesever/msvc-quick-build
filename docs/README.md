# MQB Documentation

**English | [简体中文](README_ZH.md)**

This is the authoritative index for MQB's maintained documentation.

> Historical document paths are kept stable for external links. Some older English-only technical references therefore use an unsuffixed filename and pair with a newer `_ZH.md` translation, while the original bilingual document set uses an unsuffixed Chinese file plus `_EN.md`. Do not infer language from the suffix; use this index.

## User and project documentation

| Topic | English | 简体中文 |
|---|---|---|
| Project overview / quick start | [`../README.md`](../README.md) | [`../README_ZH.md`](../README_ZH.md) |
| Installation | [`INSTALLATION_EN.md`](INSTALLATION_EN.md) | [`INSTALLATION.md`](INSTALLATION.md) |
| `mqb.json`, profiles, precedence | [`MQB_CONFIG_EN.md`](MQB_CONFIG_EN.md) | [`MQB_CONFIG.md`](MQB_CONFIG.md) |
| First-class PCH | [`PRECOMPILED_HEADERS_EN.md`](PRECOMPILED_HEADERS_EN.md) | [`PRECOMPILED_HEADERS.md`](PRECOMPILED_HEADERS.md) |
| Parallelism and P1689 warm scan reuse | [`PARALLELISM_EN.md`](PARALLELISM_EN.md) | [`PARALLELISM.md`](PARALLELISM.md) |

## Architecture and implementation contracts

| Topic | English | 简体中文 |
|---|---|---|
| Architecture / build data flow | [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) | [`ARCHITECTURE.md`](ARCHITECTURE.md) |
| C++ source-layout contract | [`../cpp/README_EN.md`](../cpp/README_EN.md) | [`../cpp/README.md`](../cpp/README.md) |
| MSVC Parameter Engine | [`MSVC_PARAMETER_ENGINE.md`](MSVC_PARAMETER_ENGINE.md) | [`MSVC_PARAMETER_ENGINE_ZH.md`](MSVC_PARAMETER_ENGINE_ZH.md) |
| Exact MSVC parameter inventory | [`MSVC_PARAMETER_INVENTORY.md`](MSVC_PARAMETER_INVENTORY.md) | [`MSVC_PARAMETER_INVENTORY_ZH.md`](MSVC_PARAMETER_INVENTORY_ZH.md) |
| MSVC parameter coverage contract | [`MSVC_PARAMETER_COVERAGE.md`](MSVC_PARAMETER_COVERAGE.md) | [`MSVC_PARAMETER_COVERAGE_ZH.md`](MSVC_PARAMETER_COVERAGE_ZH.md) |
| Persistent warm source-discovery fast path | [`WARM_FAST_PATH.md`](WARM_FAST_PATH.md) | [`WARM_FAST_PATH_ZH.md`](WARM_FAST_PATH_ZH.md) |

## Development, validation, and release

| Topic | English | 简体中文 |
|---|---|---|
| Developing MQB | [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) | [`DEVELOPMENT.md`](DEVELOPMENT.md) |
| Self-hosting and release contract | [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) | [`SELF_HOSTING.md`](SELF_HOSTING.md) |
| Performance governance | [`PERFORMANCE_GOVERNANCE.md`](PERFORMANCE_GOVERNANCE.md) | [`PERFORMANCE_GOVERNANCE_ZH.md`](PERFORMANCE_GOVERNANCE_ZH.md) |
| Build-system comparison methodology | [`BUILD_SYSTEM_BENCHMARK.md`](BUILD_SYSTEM_BENCHMARK.md) | [`BUILD_SYSTEM_BENCHMARK_ZH.md`](BUILD_SYSTEM_BENCHMARK_ZH.md) |
| Contributing | [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | [`../CONTRIBUTING_ZH.md`](../CONTRIBUTING_ZH.md) |

## Language parity rule

For a maintained bilingual topic, both language versions should describe the same product boundary, commands, supported behavior, failure modes, and validation contract. A language update that changes product semantics should update its paired document in the same pull request.

The English root [`README.md`](../README.md) is the canonical GitHub landing page. [`../README_EN.md`](../README_EN.md) is retained as a compatibility alias for old external links. The Chinese landing page is [`../README_ZH.md`](../README_ZH.md).
