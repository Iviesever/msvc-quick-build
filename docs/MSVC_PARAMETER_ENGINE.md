# MSVC Parameter Engine

MQB does not try to rename every MSVC switch into a second property system. Instead, every native MSVC option that enters MQB must have one deterministic ownership result before the compiler, linker, or librarian is launched.

## Ownership model

| Class | Meaning | Examples | MQB behavior |
|---|---|---|---|
| A — MQB-owned structural | Changes target/TU shape, primary artifacts, dependency metadata, or graph topology owned by MQB | `/Fo`, `/OUT`, `/ifcOutput`, `/ifcMap`, `/sourceDependencies`, `/scanDependencies` | Reject user/raw ownership escape with an explanation |
| B — Semantic typed | Already represented by the MQB build model | `/std:`, `/MD`/`/MT`, `/GL`, `/MACHINE`, `/SUBSYSTEM`, `/LTCG` | Normalize into typed policy before project-option resolution |
| C — Safe/conditional passthrough | Does not invalidate MQB structural ownership and can remain an ordinary MSVC argv element | `/W4`, `/WX`, `/fp:fast`, `/arch:AVX2`, `/favor:AMD64`, `/I`, `/D`, `/U`, `/external:I`, `/FI`, `/STACK`, version-conditional `/DEBUG:FASTLINK` | Preserve native argv ownership/order; existing signatures/cache identity include it, expose non-owning graph evidence where required, then apply toolchain lifecycle admission where required |
| D — Unsupported / conflicting | Changes an unmodeled pipeline, hides inputs, introduces untracked files/artifacts, is unconditionally obsolete, or conflicts with another semantic value | `/MP`, raw PCH `/Y*`, `/FU`, `/analyze`, `/experimental:log`, `@response` | Fail closed before invoking MSVC |

`unsupported` is not the same as `unregistered`. A current official option may intentionally be unsupported, but it still has an explicit registry entry and rationale. `unregistered` means the registry has no classification and is a coverage failure for the current reference snapshot.

Ownership and availability are deliberately separate. An option can be structurally safe for MQB to pass through while only existing on part of the supported MSVC toolset range. Those options survive semantic routing and are admitted after MQB discovers the actual toolchain.

Class C passthrough does not mean MQB must remain blind to every effect of an option. MQB may extract **non-owning semantic evidence** when another subsystem requires it, while keeping the original compiler argv authoritative. Native `/I` and `/external:I` expose their include roots to smart discovery, but remain raw compiler argv and therefore retain their ordering relative to other native compiler options. Native `/D` is parsed as preprocessor metadata for the same reason but is likewise kept raw. Native `/FI` remains raw compiler argv/compile identity while its ordered forced-header operands are observed by smart discovery. `/FI` operands are not rewritten relative to a config/profile/CLI path base because MSVC gives them quoted-include semantics: discovery resolves them from the selected entry source directory and then the effective include search roots. When a native `/I` or `/external:I` path is relative, MQB resolves its payload against the supplying layer's path base (project root for config/profile, invocation directory for CLI) without moving the token in argv.

## Native argv token shape

Ownership is not enough to parse a native `cl.exe` command line. Before the CLI can decide that a bare token is a positional source, it must know whether the preceding MSVC option owns that token as an operand.

`MsvcParameterEngine::token_shape()` is the single source of truth for this boundary. The CLI asks it only how many following argv elements belong to the native compiler option; the CLI does **not** decide whether the option is allowed. The grouped argv then goes through the same ownership/semantic routing as config/profile `compiler_args` and `--compiler-arg`.

The current public shape model is intentionally small:

- `none`: the current argv element is self-contained;
- `single`: exactly one following argv element belongs to the option.

This distinction is spelling-sensitive. Attached forms such as `/Iinclude`, `/external:Ivendor`, or `/FIforced.hpp` consume no additional token. Exact split forms such as `/I include`, `/D NAME`, `/U NAME`, `/external:I vendor`, `/FI forced.hpp`, `/Tc file`, `/Tp file`, `/headerName:angle vector`, and `/ifcOutput path` consume one token. A split operand is grouped even when the option is later rejected; for example `/analyze:plugin checker.dll` and `/experimental:log sarif-out` reach the Parameter Engine as intact option/operand pairs instead of misclassifying the pathname as a source.

The shape table follows the actual MSVC spelling contract rather than guessing from an option prefix. Output switches that require attached pathnames remain self-contained: bare `/Fd`, `/Fi`, `/Fm`, and `/Fp` do not consume the next argv element. `/Fo` likewise remains self-contained in its ordinary form; the documented `/Fo: pathname` spelling is modeled as a one-operand form. `/Fe: pathname` is handled the same way. This prevents a malformed or incomplete structural option from stealing a legitimate following source token.

The current model does not invent a generic stateful/variadic mode. If a future MSVC syntax cannot be represented as `none` or `single`, it must receive an explicit shape extension before the CLI is allowed to interpret it.

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

Raw forced include `/FI` is graph-aware. With smart discovery enabled, MQB observes the ordered `/FI` operands while leaving the original raw argv authoritative. Each forced header is resolved with quoted-include semantics from the selected entry source and then the effective include search roots. The current promotion requires the result to be an indexed project header; an unresolved, external/unindexed, or non-header forced target fails closed rather than creating a compiler-visible/discovery-invisible dependency. The forced header is attached to the selected entry only, so it does not become an artificial global hub in MQB's intentionally undirected source-selection graph; its normal include and same-stem ownership edges then expand the closure. `--no-discover` remains the explicit source-set-managed escape hatch. Compile freshness is still driven by MSVC `/sourceDependencies`; native Windows E2E coverage proves a forced-header mutation rebuilds the affected object and executable. Raw `/FU` remains rejected because it introduces a metadata file input that is absent from compile freshness identity.

MSVC code-analysis execution is also fail-closed for now. `/analyze` can create analysis logs and `/analyze:*` forms may introduce plugin/ruleset inputs or explicit log outputs; those artifacts are not yet represented by MQB's graph. `/analyze-` is the narrow safe exception because it disables the analysis pipeline. `/experimental:log` is rejected for the same artifact-ownership reason even though its filename/directory operand is grouped correctly by the token-shape layer.

File-bearing linker modes that introduce untracked graph inputs or secondary outputs fail closed until the corresponding artifacts are modeled. `/WHOLEARCHIVE:<library>` is a narrow exception: it may pass through when the same library is also declared through MQB's structured library inputs, which keeps freshness tracking authoritative.

## Coverage gates

`cpp/tests/msvc/parameters/msvc_parameter_engine_tests.cpp` is the executable ownership and token-shape coverage matrix. It contains representative argv for every option family in the current Microsoft references used by the registry and requires each family to resolve to A, B, C, or D rather than `unregistered`. It also locks split-vs-attached forms so a future registry expansion cannot silently consume a positional source.

`cpp/tests/app/cli/mqb_native_msvc_cli_e2e_tests.cpp` independently verifies the CLI boundary and native execution path: fixed operands stay in ordered compiler argv, attached forms do not consume the next source, and graph-aware native options such as `/FI` retain their split operand and rebuild behavior under the real candidate MQB/MSVC pipeline.

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
- raw forced metadata `/FU`: rejected until its dependency input participates in freshness identity;
- code-analysis and diagnostic-log artifact modes (`/analyze`, `/experimental:log`): rejected until their inputs/outputs participate in artifact/freshness identity; `/analyze-` remains a safe disable switch;
- PGO and other file-producing/file-consuming modes: rejected until their artifacts participate in freshness/cache identity;
- response files: rejected until MQB can expand and classify their contents safely.

The parameter engine therefore has four independent correctness questions for native arguments: **what argv tokens belong to this option?**, **who owns the semantics?**, **does MQB need non-owning evidence for another subsystem?**, and, where necessary, **does this exact toolset still provide the option?**
