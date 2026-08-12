# MQB 自举与发布契约

**简体中文 | [English](SELF_HOSTING_EN.md)**

本文描述 stable release 的**自举、验证与发布边界**。日常开发入口见 [`DEVELOPMENT.md`](DEVELOPMENT.md)；这里不重复 CLI、源码目录或配置说明。

## 1. 为什么需要 seed

MQB 使用 MQB 构建自己，因此第一代当前源码必须由一个已经存在的可信 `mqb.exe` 启动。

Stable v5 的 bootstrap seed 固定为历史 `v5.0.0-rc.2` release binary。CI 对 seed 做两项验证：

- 固定 Release ZIP 的 SHA-256；
- 可执行文件报告预期的 MQB 版本身份。

Seed 只用于构建当前源码的 **Stage 0**，不会进入 stable package。

## 2. 自举链

```text
pinned historical MQB seed
          ↓
      Stage 0
          ↓
  full Release test suite
          ↓
      Stage 1  ─────> stable package 使用的 mqb.exe
          ↓
    清空 MQB build state
          ↓
      Stage 2
```

Stage 0、Stage 1、Stage 2 都由**同一候选提交的当前源码**生成；每一代构建都由 MQB 完成。

含义：

- **Stage 0**：证明历史 seed 可以构建当前实现；
- **Stage 1**：由当前实现再次构建当前源码，作为正式发布候选；
- **Stage 2**：清空 MQB build state 后，由 Stage 1 再构建一次，用于证明干净自举闭包仍成立。

## 3. 项目描述与版本来源

MQB 构建自身时使用：

```text
cpp/mqb.json
```

该 manifest 必须与真实 production source set 一致，但 production TU 数量不是 stable contract。

Release version 的唯一仓库来源是：

```text
release/VERSION
```

构建 driver 将该版本作为结构化 `MQB_VERSION` definition 注入 binary；`cpp/mqb.json` 不重复保存 release version。

## 4. Release-blocking 验证

Stable candidate 必须在同一候选提交上证明：

1. pinned seed 的 checksum 与 executable identity 正确；
2. seed 能构建当前 Stage 0；
3. Stage 0 能通过完整 Release native test suite；
4. Stage 0 能构建 Stage 1；
5. 清空 MQB build state 后，Stage 1 能构建 Stage 2；
6. Stage 1 / Stage 2 报告正确的 release version；
7. stable ZIP 中的 `mqb.exe` 与已经验证的 Stage 1 binary 完全一致；
8. exact package manifest 与 SHA-256 sidecar 正确；
9. packaged installer 的 install / reinstall / uninstall lifecycle 通过。

任何一项失败都阻止 stable publication。

## 5. Stable package 规则

Stable ZIP：

- 只包含已经验证的 Stage 1 `mqb.exe`；
- 不包含历史 seed；
- 不包含 Stage 0；
- 不在 publication 阶段重新构建 binary。

版本 `X.Y.Z` 的 package/checksum：

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

安装行为见 [`INSTALLATION.md`](INSTALLATION.md)。

## 6. Tag publication

Stable publication 使用 immutable artifact 模型：

- 推送的 `vX.Y.Z` tag 必须与 `release/VERSION` 完全匹配；
- publication job 只消费**同一 workflow run 已经验证过的** ZIP 与 checksum；
- publication 阶段不 rebuild，以避免“验证的是 A、发布的是 B”。

历史 release/tag 不因后续文档或实现变化而重写。

## 7. 日常开发与 stable release 的区别

日常开发只需要验证当前 MQB 和 native test suite，推荐：

```powershell
.\tests\native\develop.ps1
```

Stable release 额外要求 pinned-seed bootstrap、Stage 1/Stage 2 closure、exact package/checksum 与 installer lifecycle。

开发流程见 [`DEVELOPMENT.md`](DEVELOPMENT.md)，内部构建模型见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。
