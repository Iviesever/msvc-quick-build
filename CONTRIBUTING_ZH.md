# 为 MQB 贡献

**[English](CONTRIBUTING.md) | 简体中文**

感谢你愿意花时间改进 MQB。

MQB 有意专注于 **Windows + MSVC**。最有价值的贡献包括：可复现的 correctness case、兼容性修复、聚焦的性能改进、文档修正，以及在不削弱 ownership 边界的前提下深化现有 MSVC-native 模型的改动。

## 提交 Issue 之前

如果是 bug report，请尽量提供：

- 你测试的 MQB release/tag 或 commit；
- Windows 版本，以及 MSVC / Visual Studio toolset 信息；
- 能复现问题的精确 `mqb` 命令；
- 相关 `mqb.json`（如果有）；
- 尽可能小的源码树或 reduced reproduction；
- expected behavior 与 actual behavior；
- 在确实有助于解释故障时提供 verbose diagnostics。

构建系统中的 bug 往往是 identity 或 dependency bug。一个能明确展示“哪个输入发生了变化、哪个 artifact 被错误复用或错误重建”的小型 reproduction 尤其有价值。

## 开发环境

仓库正常的开发入口是：

```powershell
.\tests\native\develop.ps1
```

Contributor workflow 与 native gates 见 [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)。C++ 源码目录和依赖契约见 [`cpp/README.md`](cpp/README.md)，高层架构见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## Pull Requests

请保持 pull request 聚焦。改动应当：

- 保持仓库 responsibility-first 的 C++ 布局；
- 保持产品代码的 C++23 baseline；
- 尊重 typed ownership 边界，而不是引入平行的 ad-hoc 实现；
- 行为变化时同步更新测试；
- CLI/config 语义变化时同步更新用户文档；
- 保证 MQB-owned writable build state 仍位于项目 `.mqb/` 下；
- identity 或 ownership 不明确时继续保持 fail-closed。

Module 相关工作中，provider truth 属于 typed P1689/provider model。MSVC invocation 相关工作中，原生参数 ownership 应继续留在既有 MSVC layer。Windows-specific 行为应继续封装在 Windows boundary 后面。

## 范围

MQB 当前不是跨平台构建系统。改善 Windows/MSVC 体验的请求，比会稀释核心产品模型的大范围 portability layer 更符合项目方向。

英文和中文 issue 都欢迎。如果一份报告希望服务尽可能广的 contributor audience，优先使用英文。
