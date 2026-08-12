# MQB Self-Hosting Contract

**Language: [简体中文](SELF_HOSTING.md) | English**

MQB is its own development, test, and stable-release build system. CMake/CTest are not part of the stable-v5 development, test, or publication chain.

This is a release-blocking contract, not an optional demo.

### Bootstrap Problem

Any self-hosting compiler or build tool requires an existing executable version as its first seed. The first stable v5 release uses historical `v5.0.0-rc.2` `mqb.exe` as the **pinned seed**:

- CI fetches the historical ZIP from GitHub Release;
- Enforces verification of the pinned SHA-256;
- Enforces verification of the `MQB 5.0.0-rc.2` identity;
- The seed is used only to build Stage 0 of current source code;
- The seed never enters the stable package.

For local development, an installed stable MQB should be preferred as the seed.

### Four Generations

```text
pinned historical seed MQB
        ↓  MQB + cpp/mqb.json
Stage 0 (current source)
        ↓  Stage 0 builds and runs all 67 Release tests with MQB
Stage 1 self-hosted release candidate ---> stable package mqb.exe
        ↓  delete cpp/.mqb
Stage 2 clean self-host closure proof
```

Stage 0, Stage 1, and Stage 2 all come from current source code, and each generation is built by MQB.

### Physical Source Structure

`cpp/` has only one product source tree:

```text
cpp/
├─ include/   # single cross-component header root
├─ src/       # single product implementation root
├─ tests/     # single C++ test root
└─ mqb.json   # single production manifest
```

`include/`, `src/`, and `tests/` are layered by responsibility (`core / config / discovery / modules / orchestration / msvc / platform`). Re-introducing component-local `cpp/<component>/include`, `src`, or `tests` trees is forbidden. See [`cpp/README_EN.md`](../cpp/README_EN.md) for the full layout contract.

### Native Project Description

`cpp/mqb.json` is the native project description used by MQB to build itself. It specifies:

- x64 / C++23;
- executable target;
- MSVC runtime and console subsystem;
- unified production include roots;
- `/W4` and `/permissive-`;
- complete production translation-unit manifest.

`tests/native/build_mqb.ps1` reads this file and requires:

- `src/app/main.cpp` plus `discovery.extra_sources` must exactly match the actual `cpp/src/**/*.cpp` production source set;
- Every source file actually exists;
- Build outputs can be executed;
- Embedded version matches requested version exactly.

The production TU count is intentionally not a stable contract or hard-coded magic number; responsibility-driven file splits only need to keep the manifest identical to the actual production source set.

The single source of release version truth remains `release/VERSION`. Build drivers inject the version via structured `MQB_VERSION="<version>"` definitions, so `cpp/mqb.json` does not duplicate version numbers.

### Native Test Graph

`tests/native/run_native_tests.ps1` is the authoritative test driver for stable-v5. It does not generate Visual Studio solutions, call CMake, or invoke CTest.

It will:

1. Retrieve all non-main production translation units from `cpp/mqb.json` and verify that set against the actual non-main `cpp/src/**/*.cpp` source set;
2. Recursively enumerate `cpp/tests/` and enforce exactly 67 `*_tests.cpp` entries;
3. Build an independent test executable for each test entry using current MQB;
4. Reuse `.mqb` incremental object/cache;
5. Pass the current MQB under verification to CLI E2E tests;
6. Run all tests directly and require 67/67 success.

Local development entry:

```powershell
.\tests\native\develop.ps1
```

Or provide a seed explicitly:

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

### Manual MQB Self-Build

Given a runnable MQB:

```powershell
$version = (Get-Content .\release\VERSION -Raw).Trim()
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

Push-Location .\cpp
mqb src\app\main.cpp --env vs --release --runtime MT -D $define
Pop-Location
```

Native output:

```text
cpp\.mqb\bin\mqb.exe
```

`$define` contains literal quote characters. Do not pre-insert backslashes for shell; MQB handles Windows argv encoding internally.

### Release Artifact Rules

The stable ZIP can only contain Stage 1, not the pinned seed or Stage 0.

Pre-release workflow must prove:

- Stage 0 has been built by the pinned seed MQB;
- Stage 0's 67/67 Release tests were all built and executed by MQB;
- Stage 0 → Stage 1 succeeded;
- After clearing `cpp/.mqb`, Stage 1 → Stage 2 succeeded;
- Stage 1 / Stage 2 report identical release version;
- `mqb.exe` in ZIP is byte-identical with verified Stage 1;
- Exact ZIP checksum, manifest, and installer lifecycle all succeeded.
