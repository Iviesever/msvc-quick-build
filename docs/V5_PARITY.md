# Stable v5 support policy

Stable v5 is **native-only**. This file replaces the former PowerShell ↔ C++ parity contract.

The old parity campaign served its purpose during the C++ refactor, but it is no longer a supported compatibility obligation.

## Supported surface

Stable v5 supports:

- `mqb.exe` as the only build implementation;
- `mqb` as the installed command;
- the native CLI documented by `mqb --help`;
- `mqb.json` as the project configuration format;
- the native installer and uninstaller;
- current C++/MSVC behavior covered by the C++ test suite.

## Explicitly unsupported legacy surface

Stable v5 does not support:

- `build.ps1`;
- a `build` compatibility command or PowerShell profile function;
- `msvc_list.json`;
- PowerShell-era CLI aliases such as `-config`, `-std`, `-type`, `-run`, `-env`, `-flags`, or `-link_flags`;
- automatic migration of old PowerShell installations;
- rollback from native v5 to the old PowerShell implementation;
- continuing PowerShell ↔ C++ parity CI as a release gate.

Unknown legacy CLI spellings fail as unknown options. Old config files are ignored because the native tool searches for `mqb.json` only.

## Historical note

The removed parity fixtures and PowerShell implementation remain available in Git history and in the commits/PRs that validated the refactor. They are not shipped or executed by stable v5.

## Stable release gate

The remaining v5 release gate is native release engineering:

1. Debug installed-MSVC tests;
2. Release installed-MSVC tests;
3. native installer lifecycle validation;
4. embedded version verification;
5. exact-artifact packaging and SHA-256;
6. publication of the exact tested native-only package.
