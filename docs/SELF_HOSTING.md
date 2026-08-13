# MQB 自举与发布契约

**简体中文 | [English](SELF_HOSTING_EN.md)**

本文描述 stable release 的**自举、验证与发布边界**。日常开发入口见 [`DEVELOPMENT.md`](DEVELOPMENT.md)。

## 1. Bootstrap seed

MQB 使用 MQB 构建自己，因此当前源码的第一代 binary 必须由一个已经存在的可信 `mqb.exe` 启动。

Stable v5 使用历史 `v5.0.0-rc.2` release binary 作为固定 seed。CI 校验该 seed 的 Release ZIP SHA-256 与 executable identity。Seed 只负责构建当前源码的 **Stage 0**，不会进入正式包。

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

- **Stage 0**：证明历史 seed 可以构建当前实现。
- **Stage 1**：由当前实现再次构建当前源码，作为正式发布候选。
- **Stage 2**：清空 MQB build state 后由 Stage 1 再构建一次，证明 clean self-host closure。

三代 binary 都来自同一候选提交。

## 3. 版本来源

MQB 构建自身使用 [`cpp/mqb.json`](../cpp/mqb.json) 描述 production source set。

Release version 的唯一仓库来源是根目录：

```text
VERSION
```

构建 driver 将该值作为结构化 `MQB_VERSION` definition 注入 binary。版本不会复制到 `mqb.json` 或单独的 release-notes 文件中。

## 4. Release-blocking 验证

Stable candidate 必须在同一候选提交上证明：

1. pinned seed 的 checksum 与 executable identity 正确；
2. seed 能构建当前 Stage 0；
3. Stage 0 能通过完整 Release native test suite；
4. Stage 0 能构建 Stage 1；
5. 清空 build state 后，Stage 1 能构建 Stage 2；
6. Stage 1 / Stage 2 报告正确的 release version；
7. ZIP 中 `mqb.exe` 与已验证 Stage 1 binary byte-identical；
8. ZIP 只包含 runtime / installer payload 与 Apache-2.0 `LICENSE`，且 manifest 与 SHA-256 sidecar 精确匹配；
9. packaged installer 的 install / reinstall / uninstall lifecycle、BAT wrapper 与 PATH ownership 通过。

任何一项失败都阻止 publication。

## 5. Stable package

版本 `X.Y.Z` 的正式资产：

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

当前 `main` 的下一次 stable ZIP 契约是**扁平、runtime-only** 发布包，精确包含：

```text
mqb.exe
VERSION
install.bat
install.ps1
uninstall.bat
uninstall.ps1
LICENSE
```

README、架构、配置、安装说明、自举说明和 release notes 等用户文档**不进入 ZIP**；它们保留在仓库与 GitHub Release 页面。`LICENSE` 作为 Apache-2.0 二进制再分发所需的许可证副本继续随包发布。

已经发布的 Release/tag 与资产保持不可变，因此历史 v5.1.0 包仍是当时验证过的六文件快照；`uninstall.bat` 从包含本次主线改动的下一次 stable release 起进入正式包。

历史 seed、Stage 0、Stage 2、测试文件和源码同样不会进入正式 ZIP。

安装行为见 [`INSTALLATION.md`](INSTALLATION.md)。

## 6. Publication

正常 stable publication 由根 `VERSION` 的变更驱动：

1. `VERSION` 变更随候选提交合入 `main`；
2. `Native Release` 在该 exact `main` commit 上重新执行完整 build/test/self-host/package gate；
3. gate 全绿后，workflow 使用已经验证的 ZIP 与 checksum 创建 `vX.Y.Z` tag 和 GitHub Release；
4. Release notes 由 GitHub 根据自上一个 tag 以来的 PR/commit 历史生成，不再保存在源码树中；
5. publication 阶段不 rebuild binary。

Release workflow 自身的修复也可以重新触发同一版本的 gate，用于恢复一次失败但尚未发布的 release。若同版本 GitHub Release 已经存在，publication 会安全跳过；若只存在 tag 而没有 Release，则失败关闭，不猜测或覆盖历史状态。

已有 Release/tag 与二进制资产保持不可变，不会被 workflow 覆盖。

## 7. 日常开发与发布

日常开发推荐：

```powershell
.\tests\native\develop.ps1
```

Stable release 在此基础上额外要求 pinned-seed bootstrap、Stage 1/Stage 2 closure、exact runtime-only package/checksum 和 installer lifecycle。

开发流程见 [`DEVELOPMENT.md`](DEVELOPMENT.md)，内部构建模型见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。
