# MQB 文档

**[English](README.md) | 简体中文**

这是 MQB 维护文档的权威索引。

> 为了不破坏已有外链，历史文档路径保持稳定。因此，一部分较早的英文-only 技术参考使用无后缀文件名，并与后来补充的 `_ZH.md` 中文版配对；而原本就有双语的文档则通常是无后缀中文文件 + `_EN.md` 英文文件。不要根据文件名后缀猜语言，以本索引为准。

## 用户与项目文档

| 主题 | English | 简体中文 |
|---|---|---|
| 项目概览 / 快速开始 | [`../README.md`](../README.md) | [`../README_ZH.md`](../README_ZH.md) |
| 安装 | [`INSTALLATION_EN.md`](INSTALLATION_EN.md) | [`INSTALLATION.md`](INSTALLATION.md) |
| `mqb.json`、profiles、precedence | [`MQB_CONFIG_EN.md`](MQB_CONFIG_EN.md) | [`MQB_CONFIG.md`](MQB_CONFIG.md) |
| First-class PCH | [`PRECOMPILED_HEADERS_EN.md`](PRECOMPILED_HEADERS_EN.md) | [`PRECOMPILED_HEADERS.md`](PRECOMPILED_HEADERS.md) |
| 并行调度与 P1689 warm scan reuse | [`PARALLELISM_EN.md`](PARALLELISM_EN.md) | [`PARALLELISM.md`](PARALLELISM.md) |

## 架构与实现契约

| 主题 | English | 简体中文 |
|---|---|---|
| 架构 / build data flow | [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) | [`ARCHITECTURE.md`](ARCHITECTURE.md) |
| C++ 源码目录契约 | [`../cpp/README_EN.md`](../cpp/README_EN.md) | [`../cpp/README.md`](../cpp/README.md) |
| MSVC Parameter Engine | [`MSVC_PARAMETER_ENGINE.md`](MSVC_PARAMETER_ENGINE.md) | [`MSVC_PARAMETER_ENGINE_ZH.md`](MSVC_PARAMETER_ENGINE_ZH.md) |
| 精确 MSVC 参数清单 | [`MSVC_PARAMETER_INVENTORY.md`](MSVC_PARAMETER_INVENTORY.md) | [`MSVC_PARAMETER_INVENTORY_ZH.md`](MSVC_PARAMETER_INVENTORY_ZH.md) |
| MSVC 参数覆盖契约 | [`MSVC_PARAMETER_COVERAGE.md`](MSVC_PARAMETER_COVERAGE.md) | [`MSVC_PARAMETER_COVERAGE_ZH.md`](MSVC_PARAMETER_COVERAGE_ZH.md) |
| Persistent warm source-discovery fast path | [`WARM_FAST_PATH.md`](WARM_FAST_PATH.md) | [`WARM_FAST_PATH_ZH.md`](WARM_FAST_PATH_ZH.md) |

## 开发、验证与发布

| 主题 | English | 简体中文 |
|---|---|---|
| 开发 MQB | [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) | [`DEVELOPMENT.md`](DEVELOPMENT.md) |
| 自举与发布契约 | [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) | [`SELF_HOSTING.md`](SELF_HOSTING.md) |
| 性能治理 | [`PERFORMANCE_GOVERNANCE.md`](PERFORMANCE_GOVERNANCE.md) | [`PERFORMANCE_GOVERNANCE_ZH.md`](PERFORMANCE_GOVERNANCE_ZH.md) |
| 贡献指南 | [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | [`../CONTRIBUTING_ZH.md`](../CONTRIBUTING_ZH.md) |

## 语言一致性规则

对于维护中的双语主题，中英文版本应描述同样的产品边界、命令、支持行为、失败模式与验证契约。任何会改变产品语义的文档更新，都应在同一个 pull request 中同步更新对应语言版本。

英文根 [`README.md`](../README.md) 是 GitHub canonical landing page。[`../README_EN.md`](../README_EN.md) 只为兼容历史外链而保留。中文首页为 [`../README_ZH.md`](../README_ZH.md)。
