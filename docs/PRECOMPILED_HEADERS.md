# First-class PCH

MQB treats MSVC precompiled headers as a first-class build artifact rather than as raw `/Yc`, `/Yu`, and `/Fp` escape hatches.

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

Disable an inherited PCH policy:

```powershell
mqb build --profile dev --no-pch
```

`build.pch` accepts either a non-empty header path string or `false`. Boolean `true` is rejected because enabling PCH without naming its header would leave its identity ambiguous.

## Precedence and path bases

PCH policy is a scalar build policy and follows the same precedence as other scalar project settings:

```text
explicit CLI > selected profile > base mqb.json > disabled default
```

Examples:

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

Paths in `mqb.json` and profiles are resolved relative to the directory containing `mqb.json`. A path supplied by `--pch` is resolved relative to the invocation directory.

## Ownership model

When PCH is enabled, MQB owns the entire structural contract:

- the synthetic PCH creator translation unit;
- the `.pch` artifact path;
- the paired creator `.obj`;
- `/FI<header>`;
- `/Yc<header>` on the creator;
- `/Yu<header>` on consumers;
- `/Fp<artifact>`;
- incremental PCH cache metadata and dependency tracking.

The same normalized header identity is used for `/FI`, `/Yc`, and `/Yu`. Raw PCH structural switches remain rejected by the MSVC Parameter Engine so that user arguments cannot silently compete with MQB-owned artifact routing.

The synthetic creator source is generated under `.mqb/pch/` and is written only when its fixed contents differ, so a normal warm invocation does not invalidate the PCH merely by touching the generated source.

## Build graph and cache behavior

A PCH creator is built before ordinary PCH consumers through the existing incremental compile coordinator. Its compile action produces two owned outputs atomically:

```text
creator.obj
project.pch
```

The creator cache records the header/include dependencies reported by MSVC `/sourceDependencies`. The `.pch` is also an explicit dependency of every consumer compile cache entry.

Consequences:

- a fully warm build reuses the creator, every consumer object, and the downstream target;
- changing only an ordinary source recompiles that source without rebuilding the PCH;
- changing the PCH header or one of its tracked dependencies rebuilds the PCH and invalidates consumers;
- deleting the `.pch` invalidates the creator cache and repairs the missing artifact;
- changing typed/raw compile semantics that participate in the creator signature rebuilds the PCH;
- changing the target name, configuration, or architecture selects separate PCH artifact state.

The paired creator object is deliberately retained as a downstream target input. Executable/DLL links include it, and static-library targets archive it. This preserves the MSVC PCH object/debug-information contract instead of treating `.pch` as a standalone link-independent file.

If the creator is rebuilt, MQB forces the downstream link/archive even before normal object freshness checks could otherwise decide that the target is warm.

## Generated state

For target `app`, Debug, x64, MQB uses:

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

All of these are writable MQB-owned state and remain under the project `.mqb/` root.

## Current boundary

The first-class PCH pipeline currently supports ordinary C++ source sets for executable, DLL, and static-library targets.

MQB fails closed when PCH is combined with:

- a C translation unit;
- a target that requires the Modules/Header Unit pipeline.

These combinations are rejected before trying to approximate incompatible graph semantics. They can be enabled later only when MQB can model and validate their artifact/dependency behavior explicitly.

## Diagnostics and timing

Cold/rebuilt PCH work is reported as:

```text
[pch] include/pch.hpp ...
```

A warm creator is reported as:

```text
[up-to-date] pch include/pch.hpp
```

PCH creator work contributes to compile timing/cache counters. It is not hidden as linker or generic setup time.
