# MSVC Parameter Engine

MQB does not try to rename every MSVC switch into a second property system. Instead, every native MSVC option that enters MQB must have one deterministic ownership result before the compiler, linker, or librarian is launched.

## Ownership model

| Class | Meaning | Examples | MQB behavior |
|---|---|---|---|
| A — MQB-owned structural | Changes target/TU shape, primary artifacts, dependency metadata, or graph topology owned by MQB | `/Fo`, `/OUT`, `/ifcOutput`, `/sourceDependencies`, `/scanDependencies` | Reject user/raw ownership escape with an explanation |
| B — Semantic typed | Already represented by the MQB build model | `/std:`, `/MD`/`/MT`, `/GL`, `/MACHINE`, `/SUBSYSTEM`, `/LTCG` | Normalize into typed policy before project-option resolution |
| C — Safe/conditional passthrough | Does not invalidate MQB structural ownership and can remain an ordinary MSVC argv element | `/W4`, `/WX`, `/fp:fast`, `/arch:AVX2`, `/favor:AMD64`, `/I`, `/D`, `/STACK`, version-conditional `/DEBUG:FASTLINK` | Preserve native argv ownership/order; existing signatures/cache identity include it, then apply toolchain lifecycle admission where required |
| D — Unsupported / conflicting | Changes an unmodeled pipeline, hides inputs, is unconditionally obsolete, or conflicts with another semantic value | `/MP`, raw PCH `/Y*`, `@response` | Fail closed before invoking MSVC |

`unsupported` is not the same as `unregistered`. A current official option may intentionally be unsupported, but it still has an explicit registry entry and rationale. `unregistered` means the registry has no classification and is a coverage failure for the current reference snapshot.

Ownership and availability are deliberately separate. An option can be structurally safe for MQB to pass through while only existing on part of the supported MSVC toolset range. Those options survive semantic routing and are admitted after MQB discovers the actual toolchain.

Class C passthrough does not mean MQB must remain blind to every effect of an option. MQB may extract **non-owning semantic evidence** when another subsystem requires it, while keeping the original compiler argv authoritative. Native `/I` is the first such case: its include root is exposed to smart discovery, but the `/I` argument remains in raw compiler argv and therefore retains its ordering relative to other native compiler options. Native `/D` is parsed as preprocessor metadata for the same reason but is likewise kept raw. When a native `/I` path is relative, MQB resolves its payload against the supplying layer's path base (project root for config/profile, invocation directory for CLI) without moving the token in argv.

## Toolchain lifecycle admission

`ToolchainIdentity.version` records the discovered `VCToolsVersion` (for example `14.44.x` or `14.50.x`). `MsvcParameterCapabilities` uses that real toolset identity after discovery instead of baking a single forever-current answer into the ownership registry.

The initial lifecycle rules are:

- `14.50+` is treated as the Visual Studio 2026 / v145 boundary;
- compiler `/await` is accepted on older toolsets and accepted with an MQB deprecation warning on `14.50+`; `/await:strict` is not included in that rule;
- linker `/DEBUG:FASTLINK` is accepted on pre-`14.50` toolsets and rejected before `link.exe` on `14.50+`, with guidance to use `/DEBUG:FULL`;
- an unparseable toolset version fails closed only for an option whose lifecycle actually depends on the version; ordinary options such as `/W4` remain unaffected.

This two-stage design preserves the existing early config/profile/CLI normalization and precedence model while still making final option admission a property of the toolchain that will execute it.

## Layering and precedence

Native parameters are normalized inside the layer that supplied them:

1. `mqb.json` typed fields and `compiler_args` / `linker_args` are normalized together.
2. A selected profile is normalized as its own overlay.
3. CLI typed options and native/raw arguments are normalized together.
4. Conflicting typed/native values inside one layer are errors.
5. The project resolver then applies `built-ins < base mqb.json < selected profile < CLI`.
6. After MSVC discovery, remaining raw arguments pass toolchain lifecycle admission.

This prevents argv ordering from becoming a second precedence system for typed semantic policy. Native passthrough options keep their own argv order rather than being silently converted into a reordered property list.

## Tool semantics

Compiler option names are treated with compiler spelling/case semantics. Linker and librarian option names are normalized case-insensitively.

MQB compiles one TU per `cl.exe /c` invocation and owns concurrency, so `/MP` is rejected in favor of `-j/--jobs`. Compiler `/F` is also rejected: it asks `cl.exe` to control linker stack size, but MQB owns a separate `link.exe` invocation; use linker `/STACK` instead.

Raw PCH switches remain reserved for MQB's first-class PCH pipeline. Response files are rejected because they can hide options and input files from ownership, cache, and dependency classification.

File-bearing linker modes that introduce untracked graph inputs or secondary outputs fail closed until the corresponding artifacts are modeled. `/WHOLEARCHIVE:<library>` is a narrow exception: it may pass through when the same library is also declared through MQB's structured library inputs, which keeps freshness tracking authoritative.

## Coverage gates

`cpp/tests/msvc/parameters/msvc_parameter_engine_tests.cpp` is the executable ownership coverage matrix. It contains representative argv for every option family in the current Microsoft references used by the registry and requires each family to resolve to A, B, C, or D rather than `unregistered`.

`cpp/tests/msvc/parameters/msvc_parameter_capabilities_tests.cpp` independently locks toolset lifecycle boundaries so ownership coverage cannot silently collapse back into one static, version-agnostic answer.

Reference snapshot lineage:

- Microsoft C/C++ compiler options alphabetical reference metadata date: **2026-05-25**;
- Microsoft LINK options reference current at the original registry implementation;
- Microsoft LIB overview/options reference current at the original registry implementation;
- lifecycle updates are modeled against the discovered `VCToolsVersion`, beginning with the Visual Studio 2026 / v145 (`14.50+`) transition.

The registry/capability implementation lives under `cpp/src/msvc/parameters/`; the public APIs remain under the stable `cpp/include/mqb/msvc/` facade.

## Current typed-model limits

The engine intentionally fails closed where MQB cannot preserve semantics yet:

- target architecture: typed x86/x64 only;
- subsystem: typed console/windows only;
- LTCG: boolean on/off policy; specialized `/LTCG:*` modes are not silently collapsed;
- CLR/Windows Runtime/kernel/driver/ARM64EC target modes: no first-class MQB target model yet;
- PGO and other file-producing/file-consuming modes: rejected until their artifacts participate in freshness/cache identity;
- response files: rejected until MQB can expand and classify their contents safely.

The parameter engine therefore has three independent correctness questions for native arguments: **who owns the semantics?**, **does MQB need non-owning evidence for another subsystem?**, and, where necessary, **does this exact toolset still provide the option?**
