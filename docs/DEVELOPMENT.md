# 开发 MQB

**简体中文 | [English](DEVELOPMENT_EN.md)**

本文只说明**如何在仓库里开发和验证 MQB**。用户安装与使用见根目录 [`README_ZH.md`](../README_ZH.md)；架构边界见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 要求

- Windows；
- 可用的 Visual Studio / MSVC C++ toolchain；
- 一个已经能运行的 `mqb.exe` 作为 seed。

MQB 使用 MQB 自身构建当前源码。日常开发链不依赖 CMake/CTest。

## 推荐入口

在仓库根目录运行：

```powershell
.\tests\native\develop.ps1
```

如果 `mqb` 不在 PATH，显式指定 seed：

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

需要指定构建配置或开发版本时，可使用脚本提供的 `-Configuration` / `-Version` 参数。

该入口完成两件事：

```text
seed MQB
   ↓
构建当前源码的 MQB
   ↓
用当前 MQB 构建并执行完整 native test suite
```

因此正常开发优先使用 `develop.ps1`，不要手工维护另一套“开发专用”构建系统。

## 自身项目描述

[`../cpp/mqb.json`](../cpp/mqb.json) 是 MQB 构建自身的 production manifest。

它必须与真实 `cpp/src/**/*.cpp` production source set 保持一致。Production TU 数量不是文档契约；新增、移动或拆分源码时，保持 manifest 与真实源码集合一致即可。

源码目录规则以 [`../cpp/README.md`](../cpp/README.md) 为准。

## Native test driver

底层测试入口是：

```text
tests/native/run_native_tests.ps1
```

它会：

1. 校验 `cpp/mqb.json` 的 production source identity；
2. 从统一的 `cpp/tests/` 树发现 native tests；
3. 使用被验证的当前 MQB 构建测试程序；
4. 直接执行测试；
5. 对 CLI E2E 场景把当前 MQB 本身作为被测程序。

测试数量会随仓库演进，不在本文硬编码；权威结果以脚本和 CI 为准。

## 目录约束

C++ 产品代码只有三个物理根：

```text
cpp/
├─ include/
├─ src/
└─ tests/
```

三个根内部按职责组织。不要新增 `cpp/<component>/include`、`src` 或 `tests` 子工程。

详细依赖与文件放置规则见 [`../cpp/README.md`](../cpp/README.md)。

## 提交前检查

对 C++ 产品代码的改动，至少应满足：

- 当前 MQB 能由 seed MQB 构建；
- native test suite 通过；
- `cpp/mqb.json` 与 production source set 一致；
- 新代码遵守 `cpp/README.md` 的职责边界；
- 用户可见行为变化同步更新 README / 配置文档；
- 自举或发布链变化同步更新 [`SELF_HOSTING.md`](SELF_HOSTING.md)。

CI 的 stable release 约束比日常开发更严格，详见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。

## 性能证据记录与历史实验

`tests/native/compare_reporting.py` 的 `Recorder` 每次将完整调用追加到带连续序号的 `calls.jsonl`。使用上下文管理器，或在正常和异常出口显式调用 `finalize()`；它会校验日志并生成兼容格式的 `calls.json`。运行中查看增量应读 journal，而不是期待每次调用后都有完整 JSON。非零退出、超时和写入失败必须保留原始诊断；合法日志前缀不能证明实验完成。此机制是单写者记录，不承诺断电耐久性。

Reporting journal correctness 在相关 PR 中运行两平台 Python 契约与历史记录的纯数据回放，不启动旧记录里的 MQB 命令。原生、自举、打包及独立 ABBA 仍是独立门槛。新 ABBA 产物同时保留当次实际 A/B 程序、种子、源码归档及前后哈希；不可拿新产物补称旧实验缺失的身份。

Release Cumulative Evidence 中固定 v5.4.0 的历史比较只保留显式手动入口，不因当前记录器改动自动重跑。Fixed Binary Warm/Cold 的冻结 base 限制在分配运行器前判断，原检查仍保留；不符合其历史身份的 PR 不启动旧诊断或 ETW。跳过这些历史实验不等于累计性能通过；5.6 的原累计数据、失败样本和发布门槛须单独处置。
