# Contributing to MQB

**English | [简体中文](CONTRIBUTING_ZH.md)**

Thanks for taking the time to improve MQB.

MQB is intentionally focused on **Windows + MSVC**. The most useful contributions are reproducible correctness cases, compatibility fixes, focused performance improvements, documentation corrections, and changes that deepen the existing MSVC-native model without weakening ownership boundaries.

## Before opening an issue

For a bug report, please try to include:

- the MQB release/tag or commit you tested;
- your Windows version and MSVC / Visual Studio toolset information;
- the exact `mqb` command that reproduces the problem;
- the relevant `mqb.json`, if any;
- a minimal source tree or reduced reproduction when possible;
- expected behavior and actual behavior;
- verbose diagnostics when they materially help explain the failure.

Build-system bugs are often identity or dependency bugs. A small reproduction that demonstrates exactly which input changed and which artifact was incorrectly reused/rebuilt is especially valuable.

## Development setup

The normal repository development entry point is:

```powershell
.\tests\native\develop.ps1
```

Read [`docs/DEVELOPMENT_EN.md`](docs/DEVELOPMENT_EN.md) for the contributor workflow and native gates. The C++ source-layout and dependency contract lives in [`cpp/README_EN.md`](cpp/README_EN.md), and the high-level architecture is documented in [`docs/ARCHITECTURE_EN.md`](docs/ARCHITECTURE_EN.md).

## Pull requests

Please keep pull requests focused. Changes should:

- preserve the repository's responsibility-first C++ layout;
- keep product code at the project's C++23 baseline;
- respect typed ownership boundaries instead of introducing parallel ad-hoc implementations;
- update tests for behavior changes;
- update user-facing documentation when CLI/config semantics change;
- keep MQB-owned writable build state under project `.mqb/`;
- preserve fail-closed behavior where identity or ownership is ambiguous.

For module work, provider truth belongs to the typed P1689/provider model. For MSVC invocation work, keep native parameter ownership in the established MSVC layer. For Windows-specific behavior, keep platform details behind the Windows boundary.

## Scope

MQB is not currently a cross-platform build system. Requests that improve the Windows/MSVC experience are a much better fit than broad portability layers that would dilute the product's core model.

Both English and Chinese reports are welcome. English is preferred when a report is intended to be useful to the widest contributor audience.
