# MQB 安装 / Installation

**语言：简体中文 | [English](INSTALLATION_EN.md)**

Stable v5 只安装一个命令和一个实现：

- 可执行文件：`mqb.exe`
- 命令：`mqb`
- 项目配置：`mqb.json`

## 安装

Windows 发布包包含 `mqb.exe`、`install.ps1`、`uninstall.ps1` 与 `install.bat`。运行：

```powershell
.\install.bat
```

默认的当前用户安装目录为：

```text
%USERPROFILE%\bin
```

安装器只拥有该目录中的以下文件：

```text
mqb.exe
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

安装器不会修改 PowerShell profile，也不会创建兼容命令。

## PATH 所有权

只有当用户 PATH 中不存在安装目录时，安装器才会添加该目录。安装状态会记录这条 PATH 是否由 MQB 添加。

重复安装是幂等的：不会重复添加同一个 PATH 条目。

卸载时，只有当前安装状态表明该 PATH 条目由 MQB 拥有，安装器才会移除它。

## 卸载

使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

卸载会删除安装器拥有的文件，以及由安装器拥有的 PATH 条目。

## 已存在的旧版安装

Stable v5 不会自动迁移或恢复旧的 PowerShell 实现安装。如果旧安装在机器上留下自定义 `build` 命令、`build.ps1` 或 PowerShell profile 修改，这些内容不属于 v5 安装器契约，可由用户手动移除。

原生安装器只维护一个权威的 MQB 安装路径。

## CLI 与配置兼容性

Stable v5 只接受 `mqb --help` 中记录的原生 CLI，并且只读取 `mqb.json`。已知的旧单横线别名会作为 unknown option 被拒绝；旧 `msvc_list.json` 不会被搜索、解析或转换。

`mqb --help` 记录的原生短选项仍受支持，包括 `-h`、`-v`、`-j`、`-o`、`-I`、`-D`、`-L` 与 `-l`。

## Installer CI 验收

`Native Installer` workflow 会在 Windows 上验证：

1. 经过验证的 `mqb.exe` 能通过公开的 batch 入口安装；
2. 只安装原生 MQB 文件；
3. 不创建兼容命令或 profile artifact；
4. 重装不会重复 PATH 状态；
5. 卸载会清除安装器拥有的文件和 PATH 状态；
6. 已淘汰的 installer 参数会被拒绝，而不是静默激活迁移行为。

## Stable package 契约

对于版本 `X.Y.Z`，`Native Release` workflow 生成：

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

Stable ZIP 包含原生 binary / installer，以及完整的简体中文与 English 文档面：

```text
mqb.exe
install.bat
install.ps1
uninstall.ps1
README.md
README_EN.md
LICENSE
MQB_CONFIG.md
MQB_CONFIG_EN.md
ARCHITECTURE.md
ARCHITECTURE_EN.md
INSTALLATION.md
INSTALLATION_EN.md
SELF_HOSTING.md
SELF_HOSTING_EN.md
RELEASE_NOTES.md
RELEASE_NOTES_EN.md
```

必要时，仓库专属的文档链接会在打包时改写为 exact-tag GitHub URL；发布包内部文档之间的链接则保持 package-local。上传前，CI 会逐个验证解压包内所有 Markdown 相对链接，拒绝缺失目标或逃出 package root 的链接。

上传前，CI 还会针对解压后的实际包验证完整 Release test graph、self-host closure、Stage 1 byte identity、exact bilingual package manifest、checksum sidecar、embedded version，以及 install/reinstall/uninstall lifecycle。

Tag publication 是 immutable 且基于 exact artifact 的。推送的 `vX.Y.Z` tag 必须与 `release/VERSION` 完全匹配；publication job 只下载同一 workflow run 已验证的 ZIP 与 checksum 并发布，不会重新构建。
