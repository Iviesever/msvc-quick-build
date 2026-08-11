# Shared PowerShell ↔ C++ parity fixtures

This directory backs the stable-v5 migration parity campaign.

The goal is **observable behavioral parity**, not identical implementation details. The PowerShell Golden Reference and native C++ `mqb.exe` intentionally use different internal artifact layouts, cache formats, and orchestration code.

## Harness model

`mqb_parity_e2e_tests` copies each committed fixture into two isolated temporary sandboxes:

1. the PowerShell sandbox is built by the repository-root `build.ps1` Golden Reference;
2. the C++ sandbox is built by the test build's `mqb.exe`;
3. each produced executable is launched independently;
4. process success and normalized program output are checked against the fixture contract and against each other.

Build-log wording, `.cache/` vs `.mqb/`, object names, and other implementation-private details are not parity assertions unless a later migration requirement explicitly makes them observable API.

## Initial scenarios

- `fixtures/single`: one translation unit.
- `fixtures/multi`: explicit multi-source target.
- `fixtures/configuration`: explicit Debug and Release configuration behavior through `_DEBUG` / `NDEBUG`.

Follow-up slices should extend the same harness for project/config precedence, incremental rebuild behavior, libraries, run argv, x86/x64, and failure behavior.

## Important legacy semantic difference

Do **not** translate native `mqb --env vs` into `build.ps1 -env vs` inside a build scenario.

The legacy PowerShell `-env` spelling is a persistent preference command: it writes/removes `$HOME\bin\.msvc_build_env` and exits immediately. Native `--env` is invocation-scoped toolchain selection. On installed-MSVC CI, the Golden Reference is therefore allowed to auto-detect Visual Studio while native MQB explicitly selects `--env vs`.

This is representative of the parity campaign rule: similarly named CLI spellings are not assumed to have identical semantics; the shared fixtures verify the user-visible contract.
