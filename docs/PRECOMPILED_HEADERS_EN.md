# First-class PCH

**[简体中文](PRECOMPILED_HEADERS.md) | English**

MQB treats MSVC precompiled headers as a first-class build artifact instead of exposing raw `/Yc`, `/Yu`, and `/Fp` switches as an escape hatch.

## Quick start

Project configuration:

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp"
  }
}
```

CLI:

```powershell
mqb build --pch include/pch.hpp
mqb run --pch include/pch.hpp
```

Disable PCH inherited from a project or profile:

```powershell
mqb build --profile dev --no-pch
```

`build.pch` accepts either a non-empty header path string or `false`. Boolean `true` is rejected because enabling PCH without naming the header would leave its identity ambiguous.

## Precedence and path bases

PCH is scalar build policy and follows the normal scalar precedence:

```text
explicit CLI > selected profile > base mqb.json > disabled default
```

Example:

```json
{
  "version": 1,
  "build": {
    "pch": "include/base.hpp"
  },
  "profiles": {
    "dev": {
      "build": {
        "pch": "include/dev.hpp"
      }
    },
    "clean": {
      "build": {
        "pch": false
      }
    }
  }
}
```

```powershell
mqb build                         # include/base.hpp
mqb build --profile dev           # include/dev.hpp
mqb build --profile dev --pch alt.hpp
mqb build --profile dev --no-pch  # disabled
mqb build --profile clean         # disabled
```

PCH paths from `mqb.json` and profiles resolve relative to the directory containing `mqb.json`. A `--pch` path resolves relative to the invocation directory.

## Ownership model

With first-class PCH enabled, MQB owns the complete structural contract:

- the synthetic creator translation unit;
- the `.pch` artifact path;
- the paired creator `.obj`;
- `/FI<header>`;
- `/Yc<header>` on the creator;
- `/Yu<header>` on consumers;
- `/Fp<artifact>`;
- incremental PCH cache metadata and dependency tracking.

The same normalized header identity is used for `/FI`, `/Yc`, and `/Yu`. Raw PCH structural switches remain rejected by the MSVC Parameter Engine, so user arguments cannot silently compete with MQB-owned artifact routing.

The synthetic creator is generated below `.mqb/pch/` and rewritten only when its fixed contents differ. A normal warm invocation therefore does not invalidate PCH by touching the generated source.

## Build graph and cache behavior

The creator is built before ordinary PCH consumers through the existing incremental compile coordinator. Its compile action owns two outputs:

```text
creator.obj
project.pch
```

The creator cache records header/include dependencies emitted by MSVC `/sourceDependencies`. The `.pch` is also recorded as an explicit dependency of every consumer compile cache entry.

This gives the following behavior:

- a fully warm build reuses the creator, all consumer objects, and the downstream target;
- changing only an ordinary source recompiles that source without rebuilding PCH;
- changing the PCH header or a tracked dependency rebuilds PCH and invalidates consumers;
- deleting the `.pch` invalidates creator cache and repairs the missing artifact;
- changing compile semantics included in creator identity rebuilds PCH;
- target name, build configuration, and architecture select separate PCH state.

The creator object deliberately remains a downstream target input. Executable/DLL links include it, and static-library targets archive it. This preserves the MSVC PCH object/debug-information contract instead of pretending that `.pch` is link-independent.

When the creator rebuilds, MQB forces the downstream link/archive even if normal object freshness would otherwise consider the target warm.

## Generated state

For target `app`, Debug, x64:

```text
.mqb/
├─ pch/
│  └─ app/
│     └─ debug/
│        └─ x64/
│           ├─ creator.cpp
│           ├─ creator.obj
│           ├─ creator.deps.json
│           └─ project.pch
└─ cache/
   └─ pch/
      └─ app/
         └─ debug/
            └─ x64/
               └─ creator.mqbcache
```

All paths are writable MQB-owned state under the project `.mqb/` root.

## Current boundary

First-class PCH currently supports ordinary C++ source sets for executable, DLL, and static-library targets.

MQB fails closed when PCH is combined with:

- a C translation unit;
- a target that requires the Modules/Header Unit pipeline.

These combinations are not approximated. They should only be enabled later when their artifact and dependency semantics can be modeled and validated explicitly.

## Diagnostics and timing

Cold or rebuilt PCH work is reported as:

```text
[pch] include/pch.hpp ...
```

A warm creator is reported as:

```text
[up-to-date] pch include/pch.hpp
```

PCH creator work contributes to compile timing and cache counters rather than being hidden as generic setup or link work.
