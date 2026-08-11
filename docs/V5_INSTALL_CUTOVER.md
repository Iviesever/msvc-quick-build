# Stable v5 installer / default-entry cutover

This document defines the Windows install and rollback contract for the stable-v5 C++ cutover.

## Installed command layout

The canonical stable command is:

```text
mqb
```

For users upgrading from the PowerShell release line, stable v5 also installs:

```text
build
```

`build` is a compatibility shim that forwards argv directly to `mqb.exe`. It is not a second PowerShell execution path. Legacy spellings already classified as compatible in `docs/V5_PARITY.md` continue to be parsed by the C++ CLI; intentionally migrated semantics such as structured program argv remain migrations rather than hidden emulation.

The default per-user install root remains:

```text
%USERPROFILE%\bin
```

This preserves the location used by the old installer while allowing both `mqb.exe` and `build.cmd` to work in PowerShell, cmd.exe, and other shells once that directory is on the user PATH.

## What the installer deploys

A stable package installs or maintains these files under the install root:

```text
mqb.exe
build.cmd
build-legacy.ps1
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

`build.ps1` from an existing PowerShell installation is never overwritten or deleted by the v5 cutover.

When upgrading from the legacy line, the first v5 install copies the existing installed `build.ps1` to `build-legacy.ps1`. On a clean install, the package Golden Reference is copied there instead. This makes rollback available without making PowerShell the default implementation.

## PowerShell profile policy

The old installer copied `Microsoft.PowerShell_profile.ps1` over the user's complete Windows PowerShell and PowerShell 7 profiles.

Stable v5 must not do that.

The new installer:

1. leaves unrelated profile files untouched;
2. recognizes the known legacy `build` function that invokes `%USERPROFILE%\bin\build.ps1`;
3. appends a clearly delimited MQB-managed block after that function so `build` resolves to the installed `mqb.exe`;
4. updates only that managed block on reinstall;
5. leaves unrelated custom `build` functions alone and emits a warning rather than silently taking ownership.

Clean installations do not need a PowerShell function at all: `mqb.exe` and `build.cmd` are shell-neutral commands through PATH.

Managed blocks are delimited by:

```text
# >>> MQB v5 C++ default >>>
# <<< MQB v5 C++ default <<<
```

Rollback uses a separate explicit legacy block:

```text
# >>> MQB v5 legacy rollback >>>
# <<< MQB v5 legacy rollback <<<
```

## PATH ownership

The installer adds the install root to the **current user's** PATH only when it is not already present.

`mqb-install-state.json` records whether the v5 installer added that PATH entry. Uninstall and rollback remove the entry only when v5 owns it; a pre-existing user PATH entry is not removed.

Reinstall is idempotent and must not duplicate the PATH entry.

## Install

A packaged stable build contains `mqb.exe` next to `install.bat` / `install.ps1`.

```powershell
.\install.bat
```

`install.bat` is now a non-interactive wrapper around Windows PowerShell. It does not change the user's execution policy and it no longer prompts for or extracts `portable_msvc.zip`.

The PowerShell implementation is also directly callable:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Action Install
```

For source-tree validation, `-MqbPath` can select an already-built `mqb.exe`.

## Uninstall

The installed maintenance entry is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

Uninstall removes the C++ binary, `build.cmd`, MQB-managed profile blocks, and installer-owned PATH state. Pre-existing legacy `build.ps1` and unrelated profile content remain untouched.

## Roll back to the PowerShell Golden Reference

Use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1" `
  -RestoreLegacy
```

Rollback removes the C++ default entry and its installer-owned PATH entry, preserves `build-legacy.ps1`, and installs an explicit profile fallback when necessary.

For an upgraded machine that still has the original `%USERPROFILE%\bin\build.ps1`, rollback points to that prior installed implementation. For a clean stable-v5 install, rollback points to `build-legacy.ps1` copied from the package Golden Reference.

## CI acceptance

`.github/workflows/install-cutover.yml` builds a Release `mqb.exe` on Windows and exercises all of the following against isolated install/profile directories:

- clean install through `install.bat`;
- deployed `mqb.exe` and `build.cmd` expose the same `--help` identity;
- unrelated profiles are not overwritten;
- user PATH insertion is idempotent and installer-owned;
- uninstall removes only v5-owned state;
- upgrade from a seeded PowerShell `build.ps1` + legacy profile preserves the original script and user profile content;
- the upgraded PowerShell `build` function resolves to `mqb.exe`;
- rollback removes the C++ default, preserves the old script, and restores a legacy `build` entry;
- a clean install can also roll back using the packaged Golden Reference.

This is the installer/default-entry gate required by issue #26. Stable release publication remains a separate follow-up and must package these validated installer files with the exact tested stable binary.
