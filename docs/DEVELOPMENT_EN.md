# Developing MQB

**[简体中文](DEVELOPMENT.md) | English**

This document covers **repository development and validation** only. User installation and usage live in the root [`README.md`](../README.md); architecture boundaries live in [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md).

## Requirements

- Windows;
- a usable Visual Studio / MSVC C++ toolchain;
- an already-working `mqb.exe` to use as the seed.

MQB builds its current source using MQB itself. The normal development chain does not depend on CMake/CTest.

## Recommended entry point

From the repository root:

```powershell
.\tests\native\develop.ps1
```

If `mqb` is not on PATH, provide the seed explicitly:

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

Use the script's `-Configuration` / `-Version` parameters when you need an explicit build configuration or development version.

The entry point performs this chain:

```text
seed MQB
   ↓
build MQB from the current source
   ↓
use the current MQB to build and execute the full native test suite
```

For normal development, prefer `develop.ps1` instead of maintaining a second development-only build system.

## Self project description

[`../cpp/mqb.json`](../cpp/mqb.json) is MQB's production manifest for building itself.

It must match the real `cpp/src/**/*.cpp` production source set. The number of production translation units is not a documentation contract; when sources are added, moved, or split, keep the manifest aligned with the actual source set.

Source-layout rules are authoritative in [`../cpp/README_EN.md`](../cpp/README_EN.md).

## Native test driver

The lower-level test entry point is:

```text
tests/native/run_native_tests.ps1
```

It:

1. validates the production-source identity described by `cpp/mqb.json`;
2. discovers native tests from the unified `cpp/tests/` tree;
3. builds test programs with the current MQB under validation;
4. executes those tests directly;
5. passes the current MQB itself into CLI E2E scenarios.

The test count may evolve with the repository and is intentionally not hard-coded here. The scripts and CI are authoritative.

## Directory constraints

C++ product code has exactly three physical roots:

```text
cpp/
├─ include/
├─ src/
└─ tests/
```

Each root is organized by responsibility. Do not add component-local `cpp/<component>/include`, `src`, or `tests` project trees.

See [`../cpp/README_EN.md`](../cpp/README_EN.md) for the detailed dependency and file-placement rules.

## Before submitting changes

For C++ product changes, verify at least that:

- the current MQB builds successfully from a seed MQB;
- the native test suite passes;
- `cpp/mqb.json` matches the production source set;
- new code follows the responsibility boundaries in `cpp/README_EN.md`;
- user-visible behavior changes update the README/configuration docs;
- self-hosting or release-pipeline changes update [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md).

Stable release CI has stricter requirements than day-to-day development; see [`SELF_HOSTING_EN.md`](SELF_HOSTING_EN.md).

## Performance evidence records and historical experiments

`Recorder` in `tests/native/compare_reporting.py` appends each complete call to `calls.jsonl` with consecutive sequence numbers. Use its context manager, or explicitly call `finalize()` on normal and exceptional exits; it validates the journal and creates the compatible `calls.json`. Read the journal for live progress instead of expecting a full JSON snapshot after every call. Preserve original diagnostics for nonzero exits, timeouts and write failures; a valid journal prefix does not prove experiment completion. This is a single-writer recorder, not a power-loss durability guarantee.

Reporting journal correctness runs Python contracts on both platforms and replays historical rows as data on relevant PRs. It never executes MQB commands from those rows. Native tests, self-hosting, packaging and independent ABBA remain separate gates. New ABBA artifacts retain the actual A/B executables, seed, source archives and before/after hashes; these must not be used to pretend missing identities from older experiments were retained.

Cumulative Entry Validation now preserves three distinct identities: released v5.5.0 source, the current frozen complete candidate, and the harness. Relevant PRs run Windows/Linux preflight only: exact source trees and preserved parents, source archives, and inherited release HOLDs. `preflight-completed.json` explicitly means zero new MQB invocations and no completed cumulative measurement. The migration protocol has zero cumulative sampling budget; a manual `measure` request fails before building. The unchanged full-matrix policy (584 pairs / 1,446 calls) needs a separately reviewed experiment before it can run again. Preflight or later green samples never clear earlier adverse evidence or missing historical executable identities.

The old v5.4.0 and original v5.5.0 experiments remain in their immutable source/evidence history, not relabelled as current measurements. Fixed Binary Warm/Cold retain their frozen-base checks before runner allocation; unrelated identities do not restart old diagnostics or ETW. Native tests, self-hosting, actual packaging/installer checks and independent per-PR ABBA remain required independently of preflight. The roadmap and detailed risk disposition stay in issue #164, not in a parallel release roadmap.
