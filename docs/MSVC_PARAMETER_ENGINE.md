# MSVC Parameter Engine

MQB does not try to rename every MSVC switch into a second property system. Instead, every native MSVC option that enters MQB must have one deterministic ownership result before the compiler, linker, or librarian is launched.

## Ownership model

| Class | Meaning | Examples | MQB behavior |
|---|---|---|---|
| A — MQB-owned structural | Changes target/TU shape, primary artifacts, dependency metadata, or graph topology owned by MQB | `/Fo`, `/OUT`, `/ifcOutput`, `/sourceDependencies`, `/scanDependencies` | Reject user/raw ownership escape with an explanation |
| B — Semantic typed | Already represented by the MQB build model | `/std:`, `/MD`/`/MT`, `/GL`, `/MACHINE`, `/SUBSYSTEM`, `/LTCG` | Normalize into typed policy before project-option resolution |
| C — Safe passthrough | Does not invalidate MQB's structural ownership and can remain an ordinary MSVC argv element | `/W4`, `/WX`, `/fp:fast`, `/arch:AVX2`, `/favor:AMD64`, `/STACK` | Preserve spelling/order verbatim; existing signatures/cache identity include it |
| D — Unsupported / deprecated / conflicting | Changes an unmodeled pipeline, hides inputs, is obsolete, or conflicts with another semantic value | `/MP`, PCH `/Y*`, `@response`, removed `/DEBUG:FASTLINK` | Fail closed before invoking MSVC |

`unsupported` is not the same as `unregistered`. A current official option may intentionally be unsupported, but it still has an explicit registry entry and rationale. `unregistered` means the registry has no classification and is a coverage failure for the current reference snapshot.

## Layering and precedence

Native parameters are normalized inside the layer that supplied them:

1. `mqb.json` typed fields and `compiler_args` / `linker_args` are normalized together.
2. CLI typed options and `--compiler-arg` / `--linker-arg` are normalized together.
3. Conflicting typed/native values inside one layer are errors.
4. The existing project resolver then applies `CLI > mqb.json > built-in defaults`.

This prevents argv ordering from becoming a second precedence system. A CLI semantic option can still override a project semantic option, while contradictory values inside the same source are diagnosed instead of relying on MSVC's last-option-wins behavior.

## Tool semantics

Compiler option names are treated with compiler spelling/case semantics. Linker and librarian option names are normalized case-insensitively.

MQB compiles one TU per `cl.exe /c` invocation and owns concurrency, so `/MP` is rejected in favor of `-j/--jobs`. Compiler `/F` is also rejected: it asks `cl.exe` to control linker stack size, but MQB owns a separate `link.exe` invocation; use linker `/STACK` instead.

PCH switches are deliberately reserved for the later first-class PCH pipeline. Response files are rejected because they can hide options and input files from ownership, cache, and dependency classification.

File-bearing linker modes that introduce untracked graph inputs or secondary outputs fail closed until the corresponding artifacts are modeled. `/WHOLEARCHIVE:<library>` is a narrow exception: it may pass through when the same library is also declared through MQB's structured library inputs, which keeps freshness tracking authoritative.

## Coverage gate

`cpp/tests/msvc/parameters/msvc_parameter_engine_tests.cpp` is the executable coverage matrix. It contains representative argv for every option family in the current Microsoft references used by this registry and requires each family to resolve to A, B, C, or D rather than `unregistered`.

Reference snapshot for this PR:

- Microsoft C/C++ compiler options, alphabetical reference, metadata date **2026-05-25**
- Microsoft LINK options reference current at implementation time
- Microsoft LIB overview/options reference current at implementation time

The registry implementation lives under `cpp/src/msvc/parameters/`; the public routing API remains under the stable `cpp/include/mqb/msvc/` facade.

## Current typed-model limits

The engine intentionally fails closed where MQB cannot preserve semantics yet:

- target architecture: typed x86/x64 only;
- subsystem: typed console/windows only;
- LTCG: boolean on/off policy; specialized `/LTCG:*` modes are not silently collapsed;
- PCH: reserved for first-class PCH artifacts and producer/consumer dependencies;
- CLR/Windows Runtime/kernel/driver/ARM64EC target modes: no first-class MQB target model yet;
- PGO and other file-producing/file-consuming modes: rejected until their artifacts participate in freshness/cache identity;
- response files: rejected until MQB can expand and classify their contents safely.

## Next stage

This PR deliberately keeps the existing raw entry points. The next CLI stage can route native tokens such as:

```powershell
mqb main.cpp /W4 /arch:AVX2 /fp:fast
mqb main.cpp /O2 /W4 /link /STACK:8388608 /DEBUG:FULL
```

through the same engine without duplicating parameter semantics inside the CLI parser.
