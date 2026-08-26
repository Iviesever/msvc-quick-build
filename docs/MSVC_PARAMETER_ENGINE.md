# MSVC Parameter Engine

**English | [简体中文](MSVC_PARAMETER_ENGINE_ZH.md)**

MQB does not try to rename every MSVC switch into a second property system. Instead, every native MSVC option that enters MQB must have one deterministic ownership result before the compiler, linker, or librarian is launched.

## Ownership model

| Class | Meaning | Examples | MQB behavior |
|---|---|---|---|
| A — MQB-owned structural | Changes target/TU shape, primary artifacts, dependency metadata, or graph topology owned by MQB | `/Fo`, `/OUT`, `/ifcOutput`, `/ifcMap`, `/sourceDependencies`, `/scanDependencies` | Reject user/raw ownership escape with an explanation |
| B — Semantic typed | Already represented by the MQB build model | `/std:`, `/MD`/`/MT`, `/GL`, `/MACHINE`, `/SUBSYSTEM`, `/LTCG` | Normalize into typed policy before project-option resolution |
| C — Safe/conditional passthrough | Does not invalidate MQB structural ownership and can remain an ordinary MSVC argv element | `/W4`, `/WX`, `/fp:fast`, `/arch:AVX2`, `/favor:AMD64`, `/I`, `/D`, `/U`, `/external:I`, `/FI`, `/DEF`, `/ORDER`, `/STUB`, `/MANIFESTINPUT`, `/DEFAULTLIB`, `/STACK`, version-conditional `/DEBUG:FASTLINK` | Preserve native argv ownership/order; existing signatures/cache identity include it, expose non-owning graph/execution evidence where required, then apply toolchain lifecycle admission where required |
| D — Unsupported / conflicting | Changes an unmodeled pipeline, hides inputs, introduces untracked files/artifacts, is unconditionally obsolete, or conflicts with another semantic value | `/MP`, raw PCH `/Y*`, `/FU`, `/analyze`, `/experimental:log`, `@response` | Fail closed before invoking MSVC |

`unsupported` is not the same as `unregistered`. A current official option may intentionally be unsupported, but it still has an explicit registry entry and rationale. `unregistered` means the registry has no classification and is a coverage failure for the current reference snapshot.

Ownership and availability are deliberately separate. An option can be structurally safe for MQB to pass through while only existing on part of the supported MSVC toolset range. Those options survive semantic routing and are admitted after MQB discovers the actual toolchain.

Class C passthrough does not mean MQB must remain blind to every effect of an option. MQB may extract **non-owning semantic evidence** when another subsystem requires it, while keeping the original compiler/linker/librarian argv authoritative.

Native `/I` and `/external:I` expose their include roots to smart discovery, but remain raw compiler argv and therefore retain their ordering relative to other native compiler options. Native `/D` is parsed as preprocessor metadata for the same reason but is likewise kept raw. Native `/FI` remains raw compiler argv/compile identity while its ordered forced-header operands are observed by smart discovery. `/FI` operands are not rewritten relative to a config/profile/CLI path base because MSVC gives them quoted-include semantics: discovery resolves them from the selected entry source directory and then the effective include search roots. When a native `/I` or `/external:I` path is relative, MQB resolves its payload against the supplying layer's path base (project root for config/profile, invocation directory for CLI) without moving the token in argv.

Native linker `/DEF:<file>` follows the same evidence-without-ownership model. The raw LINK argument remains authoritative, but MQB resolves a relative definition-file payload inside the layer that supplied it and records the resolved file as a generic linker freshness input. Config/profile paths are based at project root; CLI paths are based at the invocation directory. The argument keeps its original argv position. Because LINK accepts one module-definition file for an invocation, a second effective `/DEF` is rejected before `link.exe` rather than relying on argv ordering.

Native linker `/ORDER:@<file>` is also graph-aware, with an additional execution constraint. Every raw `/ORDER` stays in argv, while MQB tracks only the **last** order file as the effective freshness input because LINK applies the last `/ORDER` option. Config/profile/CLI-relative order-file paths are normalized inside the supplying layer. An unchanged warm build may still skip LINK completely; however, whenever an actual link is required while an effective `/ORDER` exists, MQB forces the final linker recipe to `/INCREMENTAL:NO` because `/ORDER` is incompatible with incremental linking. A later raw `/INCREMENTAL` cannot override that safety policy because MQB emits its authoritative full-link switch after raw linker arguments.

Native linker `/STUB:<file>` uses the generic freshness model without an always-full-link rule. Every raw `/STUB` remains in argv, while MQB tracks only the **last** effective MS-DOS executable as freshness evidence. Relative stub paths are normalized inside the supplying config/profile/CLI layer. An unchanged effective stub reuses the ordinary warm link cache; changing, removing, or re-resolving the effective stub invalidates generic linker-file freshness. Earlier overridden stub files are deliberately not tracked as effective freshness inputs, so changing one does not create a false relink.

Native linker `/MANIFESTINPUT:<file>` introduces a third file-input cardinality: **cumulative**. LINK merges every manifest input occurrence, so MQB preserves every raw `/MANIFESTINPUT` argument and records every resolved file as effective freshness evidence rather than treating later layers as replacements. Config/profile-relative inputs resolve from project root and CLI-relative inputs from the invocation directory. After all layers are merged, MQB validates the final linker manifest mode: if any `/MANIFESTINPUT` is active, the final ordered argv must select `/MANIFEST:EMBED` (including `/MANIFEST:EMBED,ID=...`). A later `/MANIFEST` or `/MANIFEST:NO` therefore invalidates the combination before LINK.

Native linker `/DEFAULTLIB:<library>` uses a separate evidence-without-ownership path because its payload is a **library search declaration**, not an explicit input position. Raw `/DEFAULTLIB`, `/NODEFAULTLIB`, and `/NODEFAULTLIB:<name>` argv remain authoritative so LINK keeps its native search priority. Bare library names stay bare search names; only path-bearing `/DEFAULTLIB` payloads are normalized relative to the supplying layer. After all linker layers are merged, MQB computes the effective set of **user-explicit** `/DEFAULTLIB` declarations under the final `/NODEFAULTLIB` policy and resolves currently available files only as freshness evidence. Those resolved files are never re-emitted as explicit library argv.

## Native argv token shape

Ownership is not enough to parse a native `cl.exe` command line. Before the CLI can decide that a bare token is a positional source, it must know whether the preceding MSVC option owns that token as an operand.

`MsvcParameterEngine::token_shape()` is the single source of truth for this boundary. The CLI asks it only how many following argv elements belong to the native compiler option; the CLI does **not** decide whether the option is allowed. The grouped argv then goes through the same ownership/semantic routing as config/profile `compiler_args` and `--compiler-arg`.

The current public shape model is intentionally small:

- `none`: the current argv element is self-contained;
- `single`: exactly one following argv element belongs to the option.

This distinction is spelling-sensitive. Attached forms such as `/Iinclude`, `/external:Ivendor`, or `/FIforced.hpp` consume no additional token. Exact split forms such as `/I include`, `/D NAME`, `/U NAME`, `/external:I vendor`, `/FI forced.hpp`, `/Tc file`, `/Tp file`, `/headerName:angle vector`, and `/ifcOutput path` consume one token. A split operand is grouped even when the option is later rejected; for example `/analyze:plugin checker.dll` and `/experimental:log sarif-out` reach the Parameter Engine as intact option/operand pairs instead of misclassifying the pathname as a source.

The shape table follows the actual MSVC spelling contract rather than guessing from an option prefix. Output switches that require attached pathnames remain self-contained: bare `/Fd`, `/Fi`, `/Fm`, and `/Fp` do not consume the next argv element. `/Fo` likewise remains self-contained in its ordinary form; the documented `/Fo: pathname` spelling is modeled as a one-operand form. `/Fe: pathname` is handled the same way. This prevents a malformed or incomplete structural option from stealing a legitimate following source token.

The current model does not invent a generic stateful/variadic mode. If a future MSVC syntax cannot be represented as `none` or `single`, it must receive an explicit shape extension before the CLI is allowed to interpret it.

## Native linker and librarian boundaries

The public CLI has explicit one-way boundaries for downstream native tools:

```powershell
mqb build foo.cpp /O2 /link /DEBUG:FULL /STACK:8388608
mqb build foo.cpp --type static /O2 /lib /WX /EXPORT:foo
```

`/link` routes the remaining build argv to `link.exe`. Static targets use `/lib` to route the remaining build argv to `lib.exe`; uppercase `/LIB` is accepted too. The librarian boundary deliberately does **not** expose a single-dash alias. `-lib` and `-LIB` are rejected before the generic `-l...` shorthand so they cannot be interpreted as `-l ib`. MQB keeps `-l <name>` and `-l<name>` exclusively as link-library shorthand.

The `/lib` tail must contain at least one librarian argument. A second `/lib` or `/LIB` is rejected. After extraction, the tail goes through `MsvcParameterEngine::route_librarian()` exactly like `build.librarian_args`; Class A structural ownership such as `/OUT` remains unavailable to users, Class B `/MACHINE` and `/LTCG` merge into typed policy, Class C options remain ordered passthrough, and unsupported/wrong-tool options fail closed.

Librarian policy is valid only when the final target kind is `static`. Executable and DLL targets reject a non-empty effective librarian argument set before product execution. Conversely, static targets reject linker-only policy such as libraries, library directories, subsystem, and raw linker arguments.

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

1. `mqb.json` typed fields and `compiler_args` / `linker_args` / `librarian_args` are normalized together.
2. A selected profile is normalized as its own overlay, including its own librarian arguments.
3. CLI typed options and native/raw arguments are normalized together; a CLI `/lib` tail becomes CLI-layer librarian arguments before routing.
4. Conflicting typed/native values inside one layer are errors; tracked file inputs obey their LINK semantics across effective layers (`/DEF` is single-instance, `/ORDER` and `/STUB` are last-wins, `/MANIFESTINPUT` is cumulative). Path-bearing `/DEFAULTLIB` declarations are normalized in the same supplying layer while remaining default-library argv.
5. The project resolver applies `built-ins < base mqb.json < selected profile < CLI`. List-valued librarian policy therefore appends in the exact order `build.librarian_args -> profile librarian_args -> CLI /lib tail`.
6. Requirements or observations that depend on the **final merged linker argv** are derived after layering: `/MANIFESTINPUT` requires final `/MANIFEST:EMBED`, while user-explicit `/DEFAULTLIB` freshness is filtered by final `/NODEFAULTLIB` / `/NODEFAULTLIB:<name>` policy.
7. After MSVC discovery, remaining raw compiler, linker, and librarian arguments pass toolchain lifecycle admission.

Relative path-bearing evidence is resolved before layers are merged: project configuration/profile values use project root, while CLI values use invocation directory. This prevents cwd-dependent cache identity and keeps rewritten raw argv stable when the same project is invoked from a child directory.

This prevents argv ordering from becoming a second precedence system for typed semantic policy. Native passthrough options keep their own argv order rather than being silently converted into a reordered property list.

## Archive cache identity

Static-library freshness is defined by the complete effective archive recipe, not only by the object timestamps. The archive signature includes librarian/toolchain identity, target architecture, effective LTCG state, object set/output, and the final routed librarian argv.

That makes librarian policy a first-class cache dimension:

```text
unchanged librarian argv -> warm archive cache may be reused
changed librarian argv   -> archive_recipe_changed -> rearchive
```

`build.librarian_args`, profile librarian arguments, and the CLI `/lib` tail all converge on this same final recipe. The profile name or configuration-file timestamp is not itself a cache dimension; only the effective librarian semantics are.

## Tool semantics

Compiler option names are treated with compiler spelling/case semantics. Linker and librarian option names are normalized case-insensitively by their parameter routers. The CLI boundary itself deliberately recognizes the documented public `/lib` and `/LIB` spellings only; this is separate from case normalization of options *after* the boundary.

MQB compiles one TU per `cl.exe /c` invocation and owns concurrency, so `/MP` is rejected in favor of `-j/--jobs`. Compiler `/F` is also rejected: it asks `cl.exe` to control linker stack size, but MQB owns a separate `link.exe` invocation; use linker `/STACK` instead.

Raw PCH switches remain reserved for MQB's first-class PCH pipeline. Response files are rejected because they can hide options and input files from ownership, cache, and dependency classification.

Raw forced include `/FI` is graph-aware. With smart discovery enabled, MQB observes the ordered `/FI` operands while leaving the original raw argv authoritative. Each forced header is resolved with quoted-include semantics from the selected entry source and then the effective include search roots. The current promotion requires the result to be an indexed project header; an unresolved, external/unindexed, or non-header forced target fails closed rather than creating a compiler-visible/discovery-invisible dependency. The forced header is attached to the selected entry only, so it does not become an artificial global hub in MQB's intentionally undirected source-selection graph; its normal include and same-stem ownership edges then expand the closure. `--no-discover` remains the explicit source-set-managed escape hatch. Compile freshness is still driven by MSVC `/sourceDependencies`; native Windows E2E coverage proves a forced-header mutation rebuilds the affected object and executable. Raw `/FU` remains rejected because it introduces a metadata file input that is absent from compile freshness identity.

MSVC code-analysis execution is also fail-closed for now. `/analyze` can create analysis logs and `/analyze:*` forms may introduce plugin/ruleset inputs or explicit log outputs; those artifacts are not yet represented by MQB's graph. `/analyze-` is the narrow safe exception because it disables the analysis pipeline. `/experimental:log` is rejected for the same artifact-ownership reason even though its filename/directory operand is grouped correctly by the token-shape layer.

`/DEF:<file>` is the first raw LINK file-bearing option promoted onto MQB's generic linker-file freshness path. Link cache v4 stores the resolved file-input list separately from ordinary object/library inputs and side outputs. Missing file inputs, changed paths, newer timestamps, or lost cache metadata invalidate reuse. `file_inputs_changed` is execution-only evidence: in Debug it forces a one-shot full link so an existing `.ilk` cannot retain an obsolete export table; object-only changes still use normal incremental linking. The raw `/DEF` argument remains in the link signature and final LINK argv, so a pure option change and a file-content freshness change remain distinguishable.

`/ORDER:@<file>` reuses the generic linker-file freshness path but has stronger execution semantics. MQB preserves all raw `/ORDER` arguments because LINK owns their order-sensitive last-wins behavior, while the cache tracks only the last effective order file. Missing/changed effective order files invalidate reuse through ordinary `file_inputs` evidence. In addition, the presence of an effective `/ORDER` sets execution-only `requires_full_link` evidence. The warm no-op path still returns before invoking LINK, but any real Debug link—whether caused by an object change, explicit rebuild, linker-option change, or order-file freshness—ends with MQB's authoritative `/INCREMENTAL:NO`. This avoids relying on LINK to diagnose and override an incompatible raw `/INCREMENTAL` request.

`/STUB:<file>` also reuses LinkCache v4 `file_inputs`, but it does not permanently force non-incremental linking. MQB preserves every raw `/STUB` argument while tracking only the last effective MS-DOS executable input. Unchanged input state keeps the zero-LINK warm path intact. A missing, changed, or re-resolved effective stub produces ordinary `file_inputs_changed` evidence; the existing Debug one-shot full-link safety for linker-file changes then prevents stale incremental state from surviving a stub-only mutation without turning every future `/STUB` link into a full link. Because only the last effective stub is tracked, mutating an earlier overridden stub does not create a false freshness invalidation.

`/MANIFESTINPUT:<file>` also reuses LinkCache v4 `file_inputs`, but every occurrence remains effective. The incremental linker re-observes the **final argv** and snapshots each cumulative manifest input separately, so a missing, newer, changed-path, or re-resolved file invalidates reuse without introducing a new cache format or a permanent full-link mode. Unchanged cumulative inputs retain the ordinary zero-LINK warm path; a manifest-file-only change uses the existing linker-file change safety path and does not recompile source TUs. Final manifest-mode validation remains separate from freshness: MQB rejects the invocation before LINK unless the final ordered raw arguments select `/MANIFEST:EMBED`.

User-explicit `/DEFAULTLIB:<library>` is freshness-aware without being promoted into the explicit-library argv tier. `MsvcDefaultLibraryPolicy` preserves raw `/DEFAULTLIB` / `/NODEFAULTLIB` options and computes only the effective **user-declared** default-library set from the final linker arguments. `MsvcLibraryResolver::resolve_available()` uses the same `-L -> link working directory -> MSVC LIB` search order as structured libraries but skips currently missing default libraries instead of making MQB stricter than LINK. Resolved files are appended only to LinkCache v4 generic `file_inputs`; they are never inserted into `LinkAction.libraries`. An unchanged provider therefore retains the zero-LINK warm path, while a changed/re-resolved available provider invalidates freshness and uses the existing one-shot Debug full-link safety. `/NODEFAULTLIB` and `/NODEFAULTLIB:<name>` remove suppressed declarations from freshness evidence as well as remaining raw LINK argv semantics.

This DEFAULTLIB promotion is intentionally narrower than LINK's entire transitive default-library model. Default libraries injected by `.obj` or `.lib` `.drectve` sections are still interpreted by LINK but are **not** yet freshness-tracked by MQB unless the same library also appears through an explicit MQB library input or a user-written `/DEFAULTLIB`. Extending the claim to transitive directives requires parsing or otherwise observing those embedded directives rather than guessing from the final executable.

Other file-bearing linker modes beyond promoted `/DEF`, `/ORDER`, `/STUB`, `/MANIFESTINPUT`, and user-explicit `/DEFAULTLIB` that introduce graph inputs or secondary outputs remain fail closed until their corresponding semantics are modeled. `/NATVIS`, `/SOURCELINK`, signing inputs, PGO inputs/outputs, and similar options are **not** implicitly admitted just because generic `file_inputs` exists; options that affect PDB/manifest/signing artifacts may need additional output modeling before promotion. `/WHOLEARCHIVE:<library>` remains a narrow separate exception: it may pass through when the same library is also declared through MQB's structured library inputs, which keeps freshness tracking authoritative.

## Coverage gates

`cpp/tests/msvc/parameters/msvc_parameter_engine_tests.cpp` is the executable ownership and token-shape coverage matrix. It contains representative argv for every option family in the current Microsoft references used by the registry and requires each family to resolve to A, B, C, or D rather than `unregistered`. It also locks split-vs-attached compiler forms so a future registry expansion cannot silently consume a positional source.

`cpp/tests/app/cli/build_policy_cli_tests.cpp` locks the librarian-facing UX contract: `/lib` and `/LIB` route a static-target tail, `-l` remains the link-library shorthand, `-lib` / `-LIB` are rejected with `/lib` guidance, empty and duplicate librarian boundaries fail, and MQB-owned/wrong-tool librarian options remain fail-closed. The same test also treats `mqb --help` text as part of the public contract.

`cpp/tests/config/resolution/project_options_tests.cpp` proves librarian list precedence independently from CLI parsing: base `build.librarian_args`, selected-profile librarian arguments, and CLI overrides append in base -> profile -> CLI order without loss.

`cpp/tests/orchestration/incremental/incremental_archive_coordinator_tests.cpp` proves archive cache identity: native librarian argv reaches the actual archive invocation, an unchanged recipe is reusable, and changing only librarian argv produces `archive_recipe_changed` and exactly one additional librarian invocation.

`cpp/tests/core/cache/link_state_tests.cpp` locks the generic linker file-input execution evidence independently from object/library freshness: warm inputs reuse, while newer/missing/re-resolved inputs or lost metadata invalidate reuse and mark `file_inputs_changed` without contaminating `library_inputs_changed`.

`cpp/tests/orchestration/incremental/incremental_link_coordinator_tests.cpp` locks `/ORDER` execution semantics: repeated order options preserve raw argv but track only the last effective file, malformed `/ORDER` fails before LINK, warm no-op skips LINK, effective order-file mutation invalidates freshness, and every actual `/ORDER` link appends authoritative `/INCREMENTAL:NO` after raw linker arguments.

`cpp/tests/e2e/mqb_dll_target_e2e_tests.cpp` proves the real `/DEF` pipeline on Windows/MSVC: a cold DLL exposes the first `.def` export, an unchanged build is a link-cache hit, a definition-file-only mutation leaves the TU up-to-date but relinks the DLL, and the resulting export table removes the old symbol and exposes the replacement. The fixture also verifies project-root-relative `mqb.json` `/DEF` normalization from a child invocation directory and rejects a second CLI `/DEF` before LINK.

`cpp/tests/e2e/mqb_stub_linker_e2e_tests.cpp` proves the real `/STUB` pipeline on Windows/MSVC. The fixture creates valid DOS MZ executables itself, passes two raw `/STUB` arguments, and verifies that the final PE contains the DOS program from the last effective stub rather than the overridden one. It then proves an unchanged build is a zero-LINK cache hit, mutating only the overridden stub does not cause a false relink, mutating only the effective stub leaves the source TU up-to-date but relinks the executable and replaces the embedded DOS program, and empty `/STUB:` fails before LINK.

`cpp/tests/e2e/mqb_manifest_input_e2e_tests.cpp` proves the real `/MANIFESTINPUT` pipeline on Windows/MSVC. The fixture gives LINK two manifest files under `/MANIFEST:EMBED`, proves the cold link succeeds and the unchanged build becomes a zero-LINK cache hit, then mutates each cumulative input independently and verifies each mutation relinks while the source TU remains up-to-date. A second project fixture proves project-root-relative config input and invocation-relative CLI input remain cumulative across layers and independently invalidate freshness. It also proves missing final EMBED, a later `/MANIFEST:NO`, and an empty `/MANIFESTINPUT:` fail before LINK.

`cpp/tests/msvc/linker/library_resolver_tests.cpp` locks explicit-vs-default library resolution policy. Structured libraries remain require-all, while user DEFAULTLIB evidence is available-only; the test also covers case-insensitive `/NODEFAULTLIB:<name>` suppression, optional `.lib` matching, path-bearing layer-base normalization, and shared library-search precedence.

`cpp/tests/e2e/mqb_default_library_e2e_tests.cpp` proves the real user-explicit `/DEFAULTLIB` pipeline on Windows/MSVC. A consumer resolves a symbol only through raw DEFAULTLIB, an unchanged build becomes a zero-LINK cache hit, a default-library-only archive mutation leaves the TU up-to-date but relinks and changes runtime behavior, `/NODEFAULTLIB:<name>` suppresses the raw default library, and a structured explicit library retains priority over a competing DEFAULTLIB. The fixture also proves a suppressed missing DEFAULTLIB does not make MQB stricter than LINK and that project-root-relative `mqb.json` DEFAULTLIB paths remain correct when MQB is launched from a nested directory.

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
- file-bearing LINK options other than promoted `/DEF`, promoted `/ORDER`, promoted `/STUB`, promoted `/MANIFESTINPUT`, user-explicit `/DEFAULTLIB`, and separately constrained `/WHOLEARCHIVE`: rejected until their inputs/outputs can be represented without weakening cache or artifact correctness;
- transitive default-library directives embedded in object/library `.drectve`: interpreted by LINK but not yet part of MQB freshness evidence;
- PGO and other file-producing/file-consuming modes: rejected until their artifacts participate in freshness/cache identity;
- response files: rejected until MQB can expand and classify their contents safely.

The parameter engine therefore has four independent correctness questions for native arguments: **what argv tokens belong to this option?**, **who owns the semantics?**, **does MQB need non-owning evidence for another subsystem?**, and, where necessary, **does this exact toolset still provide the option?**