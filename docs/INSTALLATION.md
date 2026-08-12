# MQB installation

Stable v5 installs one command and one implementation:

- executable: `mqb.exe`
- command: `mqb`
- project config: `mqb.json`

## Install

The Windows release package contains `mqb.exe`, `install.ps1`, `uninstall.ps1`, and `install.bat`. Run:

```powershell
.\install.bat
```

The default per-user installation root is:

```text
%USERPROFILE%\bin
```

The installer owns only these files in that root:

```text
mqb.exe
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

It does not modify PowerShell profiles and does not create compatibility commands.

## PATH ownership

The installer adds the installation root to the user PATH only when it is missing. The install state records whether MQB added that entry.

Reinstall is idempotent: the same PATH entry is not duplicated.

Uninstall removes the PATH entry only when the current install state says MQB owns it.

## Uninstall

Use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

Uninstall removes installer-owned files and an installer-owned PATH entry.

## Existing legacy installations

Stable v5 does not automatically migrate or restore older PowerShell-based installations. If an old installation left a custom `build` command, `build.ps1`, or profile modification on a machine, those files are outside the v5 installer contract and may be removed manually.

The native installer intentionally keeps one authoritative MQB installation path.

## CLI and configuration compatibility

Stable v5 accepts the native CLI documented by `mqb --help` and reads `mqb.json` only. Known obsolete single-dash aliases are rejected as unknown options; old `msvc_list.json` files are not searched, parsed, or translated.

Native short options documented by `mqb --help`, including `-h`, `-v`, `-j`, `-o`, `-I`, `-D`, `-L`, and `-l`, remain supported.

## Installer CI acceptance

The `Native Installer` workflow validates on Windows that:

1. the validated `mqb.exe` is installed by the public batch entry;
2. only native MQB files are installed;
3. no compatibility command or profile artifact is created;
4. reinstall does not duplicate PATH state;
5. uninstall removes installer-owned files and PATH state;
6. obsolete installer parameters are rejected rather than silently activating migration behavior.

## Stable package contract

For version `X.Y.Z`, the `Native Release` workflow produces:

```text
vscode-msvc-quick-build-vX.Y.Z-windows-x64.zip
vscode-msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

The stable ZIP contains:

```text
mqb.exe
install.bat
install.ps1
uninstall.ps1
README.md
LICENSE
MQB_CONFIG.md
ARCHITECTURE.md
INSTALLATION.md
SELF_HOSTING.md
RELEASE_NOTES.md
```

Before upload, CI verifies the full Release test graph, the self-host closure, Stage 1 byte identity, exact package manifest, checksum sidecar, embedded version, and install/reinstall/uninstall lifecycle against the extracted package itself.

Tag publication is immutable and exact-artifact based. A pushed `vX.Y.Z` tag must exactly match `release/VERSION`; the publication job downloads the already-validated artifact from that same workflow run and publishes the ZIP and checksum without rebuilding it.
