# MQB 自举契约 / Self-hosting contract

**语言：简体中文 | [English](SELF_HOSTING_EN.md)**

MQB 的稳定版、日常开发构建和测试构建都以 MQB 自身作为构建系统。CMake/CTest 不属于 stable-v5 的开发、测试或发布链。

这是一条 release-blocking 契约，不是可选演示。

### Bootstrap 问题

任何自举编译器/构建工具都需要一个已经存在的可执行版本作为第一颗 seed。首个 stable v5 发布使用历史 `v5.0.0-rc.2` 的 `mqb.exe` 作为**固定 seed**：

- CI 从 GitHub Release 取得历史 ZIP；
- 强制校验固定 SHA-256；
- 强制校验 `MQB 5.0.0-rc.2` 身份；
- seed 只用于构建当前源码的 Stage 0；
- seed 永远不会进入 stable package。

稳定版发布后，本地开发应优先使用已安装的 stable MQB 作为 seed。

### 四代关系

```text
固定历史 seed MQB
        |
        | MQB + cpp/mqb.json
        v
Stage 0（当前源码）
        |
        | Stage 0 构建并运行 67 个 Release tests
        | Stage 0 + cpp/mqb.json
        v
Stage 1（正式发布候选） ---> stable package mqb.exe
        |
        | 删除 cpp/.mqb
        | Stage 1 + cpp/mqb.json
        v
Stage 2（干净自举闭包证明）
```

Stage 0、Stage 1、Stage 2 都来自当前源码，且每一代构建都由 MQB 完成。

### 物理源码结构

`cpp/` 只有一套产品源码树：

```text
cpp/
├─ include/   # 唯一跨组件头文件根
├─ src/       # 唯一产品实现根
├─ tests/     # 唯一 C++ 测试根
└─ mqb.json   # 唯一 production manifest
```

`include/`、`src/`、`tests/` 内部再按 `core / config / discovery / modules / orchestration / msvc / platform` 等职责分层。禁止恢复组件级 `cpp/<component>/include`、`src` 或 `tests` 树。完整目录契约见 [`cpp/README.md`](../cpp/README.md)。

### 原生项目描述

`cpp/mqb.json` 是 MQB 构建自身的项目描述。它声明：

- x64 / C++23；
- executable target；
- MSVC runtime 与 console subsystem；
- 统一的 production include roots；
- `/W4` 与 `/permissive-`；
- 完整 production translation-unit manifest。

`tests/native/build_mqb.ps1` 会读取此文件，并要求：

- `src/app/main.cpp` 加上 `discovery.extra_sources` 与 `cpp/src/**/*.cpp` 的实际 production source set 完全一致；
- 每个源文件真实存在；
- 构建产物能运行；
- 内嵌版本与请求版本完全一致。

production TU 数量不是稳定契约，也不使用 hard-coded magic count；文件职责拆分只需保持 manifest 与真实 production source set 一致。

release version 唯一来源仍是 `release/VERSION`。构建 driver 通过结构化 `MQB_VERSION="<version>"` definition 注入版本，因此 `cpp/mqb.json` 不重复保存版本号。

### 原生测试图

`tests/native/run_native_tests.ps1` 是 stable-v5 的权威测试 driver。它不生成 Visual Studio solution，不调用 CMake，也不调用 CTest。

它会：

1. 从 `cpp/mqb.json` 取得全部 non-main production translation units，并与 `cpp/src/**/*.cpp` 的实际 non-main source set 做一致性校验；
2. 递归枚举 `cpp/tests/` 并强制要求恰好存在 67 个 `*_tests.cpp`；
3. 用当前 MQB 为每个 test entry 构建独立测试可执行文件；
4. 复用 `.mqb` incremental object/cache；
5. 向 CLI E2E 测试传入正在验证的当前 MQB；
6. 直接运行全部测试并要求 67/67 success。

日常开发入口：

```powershell
.\tests\native\develop.ps1
```

也可指定 seed：

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

### 手工构建 MQB

给定一个可运行的 MQB：

```powershell
$version = (Get-Content .\release\VERSION -Raw).Trim()
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb src\app\main.cpp --env vs --release --runtime MT -D $define
Pop-Location
```

原生产物：

```text
cpp\.mqb\bin\mqb.exe
```

`$define` 中是 literal quote characters。不要为 shell 预先插入反斜杠；MQB 自己负责 Windows argv encoding。

### Release artifact 规则

stable ZIP 只能包含 Stage 1，不能包含固定 seed 或 Stage 0。

发布前 workflow 必须证明：

- Stage 0 已由固定 seed MQB 构建；
- Stage 0 的 67/67 Release tests 全部由 MQB 构建并运行；
- Stage 0 → Stage 1 成功；
- 清空 `cpp/.mqb` 后 Stage 1 → Stage 2 成功；
- Stage 1 / Stage 2 报告相同 release version；
- ZIP 中 `mqb.exe` 与已验证 Stage 1 byte-identical；
- exact ZIP checksum、manifest 和 installer lifecycle 全部成功。
