# 开发 MQB

**简体中文 | [English](DEVELOPMENT_EN.md)**

本文只说明**如何在仓库里开发和验证 MQB**。用户安装与使用见根目录 [`README.md`](../README.md)；架构边界见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

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
