# Installing MQB

**[简体中文](INSTALLATION.md) | English**

The stable Windows package installs one command: `mqb`, backed by the native `mqb.exe` executable.

## Install

Extract the Windows x64 GitHub Release package and run:

```powershell
.\install.bat
```

`install.bat` is designed for direct double-click/manual use and pauses after either success or failure so the command window does not disappear immediately. Automation can set:

```powershell
$env:MQB_NO_PAUSE = '1'
```

This only disables the BAT wrapper pause; installation semantics and exit codes are unchanged.

The default per-user installation directory is:

```text
%USERPROFILE%\bin
```

After installation, a new terminal should be able to run:

```powershell
mqb --help
```

## Release ZIP contents

The next stable ZIP contract on current `main` is a flat, runtime-only package containing exactly:

```text
mqb.exe
VERSION
install.bat
install.ps1
uninstall.bat
uninstall.ps1
LICENSE
```

Documentation such as READMEs, configuration, architecture, installation, self-hosting, and release notes is not shipped inside the ZIP; read it directly in the repository and on GitHub Releases. `LICENSE` remains as the Apache-2.0 license copy required for binary redistribution.

Already-published versions are immutable historical snapshots. For example, v5.1.0 is not rewritten by later installer improvements. `uninstall.bat` enters the official ZIP starting with the next stable release that includes this change.

## Installer-owned files

In the default installation directory, the MQB installer manages only:

```text
mqb.exe
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

The installer does not modify the PowerShell profile and does not create the legacy `build` compatibility command.

## PATH behavior

If the installation directory is not already present in the user PATH, the installer adds it and records that the entry was created by MQB.

Rules:

- reinstalling does not duplicate the PATH entry;
- if the PATH entry was already user-managed, MQB does not take ownership of it;
- uninstall removes the PATH entry only when the installation state proves it is MQB-owned;
- persistent PATH changes broadcast the Windows environment-change notification, and the current installer process PATH is updated as well.

## Reinstall / upgrade

Run the new release package's installer again:

```powershell
.\install.bat
```

This updates the current-user installation. File and PATH operations are idempotent; a stable installation does not need to be removed manually first.

## Uninstall

A release package that includes the new uninstall wrapper can be removed directly with:

```powershell
.\uninstall.bat
```

Like the install wrapper, it pauses by default for manual use; automation can use `MQB_NO_PAUSE=1`. If the original release package has already been deleted, use the maintenance script from the default installation location:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

Uninstall removes MQB-owned installation files and removes the PATH entry only when installation state records that MQB added it. A matching PATH entry that existed before installation remains user-owned and is preserved.

## Legacy behavior

Stable v5 does not restore the retired PowerShell implementation and does not migrate old configuration automatically:

- no `build` compatibility command;
- no PowerShell profile edits;
- no reading or conversion of legacy `msvc_list.json`;
- retired PowerShell-era single-dash CLI aliases are rejected as unknown options.

`mqb --help` is authoritative for the current CLI. `mqb.json` is the only project configuration format.

## Release package integrity

A stable version `X.Y.Z` uses:

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

Before publication, the release workflow validates Stage 1 binary identity, the runtime-only package manifest, checksum, BAT/PowerShell installer lifecycle, PATH ownership, and self-host closure. See [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md) for the technical release gates.

For normal usage, return to the root [`README_EN.md`](../README_EN.md).
