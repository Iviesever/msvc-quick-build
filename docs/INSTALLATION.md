# 安装 MQB

**简体中文 | [English](INSTALLATION_EN.md)**

MQB 的 stable Windows 包安装一个命令：`mqb`，对应原生可执行文件 `mqb.exe`。

## 安装

解压 GitHub Release 的 Windows x64 包后运行：

```powershell
.\install.bat
```

默认当前用户安装目录：

```text
%USERPROFILE%\bin
```

安装完成后，新开的终端中应可以运行：

```powershell
mqb --help
```

## 安装器拥有的文件

默认安装目录中，MQB installer 只管理：

```text
mqb.exe
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

安装器不会修改 PowerShell profile，也不会创建旧版 `build` compatibility command。

## PATH 规则

如果用户 PATH 中还没有安装目录，installer 会添加它，并在安装状态中记录该 PATH 条目由 MQB 创建。

规则：

- 重复安装不会重复添加 PATH；
- 如果 PATH 原本就由用户维护，MQB 不取得其所有权；
- 卸载时只删除安装状态明确记录为 MQB-owned 的 PATH 条目。

## 重装 / 升级

再次运行新发布包中的：

```powershell
.\install.bat
```

即可更新当前用户安装。Installer 的文件与 PATH 操作是幂等的，不需要先手工删除旧 stable 安装。

## 卸载

默认安装位置下运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

卸载会删除 MQB-owned 安装文件，以及仅在安装状态证明由 MQB 添加时才删除对应 PATH 条目。

## 旧版兼容行为

Stable v5 不恢复旧 PowerShell 实现，也不自动迁移旧配置：

- 不创建 `build` compatibility command；
- 不修改 PowerShell profile；
- 不读取或转换旧 `msvc_list.json`；
- 已淘汰的 PowerShell-era 单横线 CLI 别名会作为 unknown option 被拒绝。

当前 CLI 以 `mqb --help` 为权威，项目配置只使用 `mqb.json`。

## 发布包完整性

版本 `X.Y.Z` 的稳定发布使用：

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

Release workflow 在发布前验证 binary、package manifest、checksum、installer lifecycle 与 self-host closure。自举和发布门禁的技术细节见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。

用户使用方式见仓库根目录 [`README.md`](../README.md)。
