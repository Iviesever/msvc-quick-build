# Stable v5 native-only install contract

Stable v5 is a **clean break** from the old PowerShell implementation.

There is one supported runtime implementation and one supported project configuration format:

- executable: `mqb.exe`
- command: `mqb`
- project config: `mqb.json`

The repository no longer ships or supports the old PowerShell build implementation, PowerShell profile shim, `build` compatibility command, `msvc_list.json` migration path, legacy CLI aliases, or rollback to the PowerShell implementation.

## Installation

The Windows package places `mqb.exe`, `install.ps1`, `uninstall.ps1`, and `install.bat` together. Run:

```powershell
.\install.bat
```

The default per-user installation root is:

```text
%USERPROFILE%\bin
```

The installer copies only native-v5 assets into that root:

```text
mqb.exe
mqb-install.ps1
uninstall-mqb.ps1
mqb-install-state.json
```

It does **not** create any of the following compatibility artifacts:

```text
build.cmd
build.ps1
build-legacy.ps1
Microsoft.PowerShell_profile.ps1
```

## PATH ownership

The installer adds the installation root to the user PATH only when it is missing. The install state records whether this v5 installation added the entry.

Reinstall is idempotent: the same path is not duplicated.

Uninstall removes the PATH entry only when the current native-v5 install state says the installer owns it.

## PowerShell profiles

Stable v5 does not modify Windows PowerShell or PowerShell 7 profile files.

There is no `build` function injection and no profile migration logic. Users invoke `mqb` directly.

## Uninstall

Use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$HOME\bin\uninstall-mqb.ps1"
```

Uninstall removes installer-owned native-v5 files and an installer-owned PATH entry. There is no `-RestoreLegacy` mode.

## Existing old installations

Stable v5 does not provide an automatic migration or rollback contract for old PowerShell installations.

If an old installation left `build.ps1`, a custom `build` function, or an old profile modification on a machine, those files are outside the v5 installer contract. Remove them manually if they are no longer wanted.

This is intentional: stable v5 does not keep two authoritative build implementations alive.

## CLI compatibility policy

Stable v5 accepts the native CLI documented by `mqb --help`.

PowerShell-era aliases such as the following are rejected as unknown options:

```text
-config -std -type -runtime -run -env -x86 -x64 -output
-include -defines -libpath -libs -flags -link_flags -ltcg
-subsystem -help -?
```

Normal native short options remain supported where documented, including `-h`, `-v`, `-j`, `-o`, `-I`, `-D`, `-L`, and `-l`.

## Configuration compatibility policy

Stable v5 reads `mqb.json` only. The old `msvc_list.json` format is not accepted, translated, or searched for by the native tool.

## CI acceptance

The `Native Installer` workflow validates on Windows that:

1. the validated Release `mqb.exe` is the binary installed by the public batch entry;
2. only native-v5 files are installed;
3. no `build`/PowerShell compatibility artifacts are created;
4. reinstall does not duplicate PATH state;
5. uninstall removes installer-owned native files and PATH state;
6. old installer parameters such as `-LegacyBuildPath` are rejected instead of triggering migration behavior.

The normal C++ Debug and Release gates remain authoritative for the build engine itself.

## Release boundary

The already-published `v5.0.0-rc.2` remains a historical prerelease and is not rewritten. The final `v5.0.0` package must be produced from the native-only mainline and must not reintroduce legacy assets for compatibility.
