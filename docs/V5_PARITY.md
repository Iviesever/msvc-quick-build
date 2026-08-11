# Stable v5 PowerShell -> C++ parity inventory

This document is the contract inventory for issue #26. It separates behavior required for the stable C++ cutover from optional legacy syntax and from C++ Modules expansion tracked independently in #16.

Status meanings:

- **ready**: native `mqb.exe` already owns the behavior and has C++ test coverage.
- **compat**: native behavior exists and the legacy command spelling is accepted by the C++ parser.
- **implement**: required before the stable v5 cutover unless explicitly reclassified with rationale.
- **migrate**: stable v5 intentionally changes the interface; migration/diagnostics must be documented and tested instead of pretending to be byte-for-byte compatible.
- **legacy-only**: intentionally not part of the stable native contract; must remain available through an explicitly retained legacy path if still promised.
- **#16**: module policy is tracked independently by issue #16 and is not silently folded into parity work.

## Baseline command and source contract

| Legacy PowerShell surface | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| `build <sources...>` | `mqb <sources...>` is ready | **implement** installer/command cutover; decide whether `build` remains a compatibility command |
| one focused source with smart dependency discovery | ready | **ready** |
| multiple explicit source files | ready | **ready** |
| `.c` | native C recipe, smart discovery, mixed C/C++, and language-isolated discovery are covered | **ready** |
| `.cpp`, `.cc`, `.cxx` | ready | **ready** |
| `.ixx` / named modules | ready for project-local providers | **ready**, with #16 boundaries documented |
| project-local header units | native pipeline is ready | **ready** |
| `.hpp` / `.h` as positional legacy discovery seeds | native CLI treats headers as non-TU inputs | **migrate** unless a real user workflow requires positional header seeds |
| C++14 / C++17 | ordinary native targets map to `/std:c++14` / `/std:c++17`; CLI and `mqb.json` accept both; module pipeline remains C++20+ | **ready** for ordinary non-module targets |
| C++20 / C++23 / latest | ready | **ready** |

## Common command-line options

| Legacy spelling / behavior | Native equivalent | Stable-v5 decision |
| --- | --- | --- |
| `-o`, `-output` | `-o`, `--output`; legacy `-output` accepted | **compat** |
| `-run` | `--run`; legacy `-run` accepted for executable targets | **compat** |
| `-std` | `--std`; legacy `-std` accepts 14/17/20/23/latest | **compat** |
| `-type exe/dll/static` | `--type exe/dll/static`; legacy `-type` accepted; same typed target kind is available in `mqb.json` | **compat** |
| `-x86` | `--x86`; legacy spelling accepted | **compat** |
| default x64 | `--x64` / x64 default; legacy `-x64` accepted | **compat** |
| `-I`, `-include` | `-I`; legacy `-include` accepted | **compat** |
| `-L`, `-libpath` | `-L`, `--lib-path`; legacy `-libpath` accepted | **compat** |
| `-libs` | `-l`, `--lib`; legacy `-libs <value>` accepted and repeatable | **compat** for scalar/repeated values; shell-array tokenization is not emulated |
| `-D`, `-defines` | `-D`; legacy `-defines` accepted | **compat** |
| `-config debug/release` | `--debug`, `--release`, `--config`; legacy `-config` accepted | **compat** |
| `-runtime MD/MDd/MT/MTd` | `--runtime`; legacy `-runtime` accepted | **compat** |
| `-ltcg` | `--ltcg` / `--no-ltcg`; legacy `-ltcg` enables the same coupled typed policy | **compat** |
| `-subsystem console/windows` | `--subsystem`; legacy `-subsystem` accepted | **compat** |
| PowerShell `-env vs/portable/port/p/auto` | native `--env auto/vs/portable` (and parser-compatible `-env`) is invocation-scoped, while PowerShell `-env` writes/removes a persistent preference file and exits | **migrate** semantics; keep native invocation-scoped selection and document the legacy preference-file transition |
| `-help`, `-?` | `--help`, `-h`; legacy help aliases accepted | **compat** |
| `-flags` | `--compiler-arg <one argv>`; legacy `-flags <one argv>` accepted and repeatable | **compat** with explicit one-argv-per-occurrence semantics |
| `-link_flags` | `--linker-arg <one argv>`; legacy alias accepted and repeatable | **compat** with explicit one-argv-per-occurrence semantics |
| `-a "..."` single-string program arguments | structured `-- program-args...` | **migrate**: keep structured argv semantics; decide whether a compatibility tokenizer is worth the ambiguity |

## Target/output kinds

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| executable target | ready | **ready** |
| DLL target (`-type dll`) | typed Core/CLI/config target kind, `.dll` layout, `/DLL` + deterministic `/IMPLIB`, cache-correct side-output repair, real DLL load/call E2E | **ready/compat** |
| static library target (`-type static`) | dedicated `lib.exe` backend, archive identity/cache/coordinator, `.lib` output, CLI/config/legacy spelling, real consumer-link E2E, and config-only routing/CLI precedence E2E for ordinary C/C++ | **ready/compat** for ordinary targets; static+Modules remains fail-closed |
| automatic `.exe/.dll/.lib` suffix by target kind | typed layout is ready | **ready** |
| console subsystem | typed CLI/config policy; default console | **ready/compat** |
| windows subsystem | typed CLI/config policy with link-only cache invalidation E2E | **ready/compat** |

DLL link identity is typed and separate from executable identity while preserving the historical executable link-signature byte stream. For exporting DLLs, MQB owns a deterministic `.mqb/bin/<target>.lib` import-library path. Linker side outputs that were observed after a successful link are persisted in link-cache v3; v2 executable caches remain readable. Removing a recorded import library/export file invalidates only the link and repairs it without recompiling fresh translation units.

Static libraries have a separate build owner rather than being modeled as `link.exe` targets. `MsvcLibrarian` invokes `lib.exe` over the compiled object set, archive identity hashes object order/output/librarian identity and typed archive policy, and static metadata lives under `.mqb/cache/archive/<target>.archivecache` so it cannot collide with established executable/DLL link caches. Missing archives repair without recompiling fresh TUs; changed library sources recompile only affected TUs and re-archive. Each rebuild is created from a clean temporary archive before installation, so shrinking the source/object set cannot retain stale members. Linker-only policy is rejected for static targets instead of being silently ignored. Static targets that require the named-module/header-unit pipeline remain fail-closed until archive topology is explicitly validated.

## Compiler and linker policy

The native backend has typed `CompilerOptions` / `LinkOptions` plus ordered additional argument vectors. CLI and `mqb.json` expose those vectors, and compile/link/archive signatures hash the relevant typed and raw policy. Config arguments are applied first and CLI arguments append afterward. High-value typed policy remains authoritative over conflicting raw flags: typed runtime and LTCG compile policy are emitted after raw compiler arguments, while typed LTCG downstream policy and structured target-kind/subsystem/output routing are emitted after raw linker arguments.

Typed LTCG is a single coupled policy, not two user-maintained raw lists. Enabling it adds `/GL` to affected compilation and `/LTCG` to the downstream target recipe. Executable/DLL targets use `link.exe /LTCG`; ordinary static-library targets use `lib.exe /LTCG`. Debug executable/DLL LTCG forces `/INCREMENTAL:NO` rather than generating an incompatible `/INCREMENTAL` + `/LTCG` recipe. Compile, link, and archive identities all distinguish LTCG-on from LTCG-off, while the historical non-LTCG signature byte streams remain unchanged. Real installed-MSVC E2E covers cold/warm executable LTCG, `mqb.json`/CLI precedence, static LTCG archive creation, and an LTCG executable consuming the generated static library.

| Legacy option | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| `-optimize Od/O1/O2/Ox` | debug/release presets cover common cases; raw compiler args can express an override | **migrate** long-tail override to `--compiler-arg`; typed option remains optional unless parity fixtures justify it |
| `-runtime MD/MDd/MT/MTd` | typed CLI/config policy; unset preserves Debug `/MDd` and Release `/MD`; explicit runtime is compile identity | **ready/compat** |
| `-warnings W0/W1/W3/W4/Wall` | fixed W3 baseline; raw compiler args can override later in argv | **migrate** long-tail override to `--compiler-arg` |
| `-WX` | raw compiler args support `/WX` | **migrate** to `--compiler-arg /WX` |
| `-debug_info off/Zi/ZI/Z7` | presets use Z7; raw compiler args exist | **migrate** long-tail override, but preserve parallel-safe default recipe |
| `-exceptions EHsc/EHa/off` | fixed EHsc baseline; raw compiler args exist | **migrate** long-tail override |
| `-rtc1` | raw compiler args exist | **migrate** to raw build policy |
| `-jmc` | raw compiler args exist | **migrate** to raw build policy |
| `-sdl` | raw compiler args exist | **migrate** to raw build policy |
| `-permissive` | native baseline is `/permissive-`; raw args can add a later MSVC conformance switch where supported | **migrate** strict conformance remains default |
| `-fp precise/strict/fast` | raw compiler args exist | **migrate** to raw build policy |
| `-charset unicode/mbcs` | UTF-8 source/execution is native baseline; Windows macro charset is not typed | **implement** only if parity fixtures show `_UNICODE/UNICODE` vs `_MBCS` is a required stable contract |
| `-ltcg` | typed CLI/config coupled `/GL` + downstream `/LTCG`, cache identities, legacy spelling, and real MSVC E2E are covered | **ready/compat** |
| `-incremental` linker switch | native build engine is incrementally cached; linker `/INCREMENTAL` can be passed raw | **migrate** terminology; expose raw linker switch if specifically desired |
| `-flags` / arbitrary compiler switches | ordered CLI/config pass-through is wired and cache-safe | **ready/compat** |
| `-link_flags` / arbitrary linker switches | ordered CLI/config pass-through is wired and cache-safe | **ready/compat** |

## Project configuration

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| `msvc_list.json`, upward search up to five levels | native uses nearest upward `mqb.json` | **migrate** to `mqb.json`; provide upgrade guidance/tooling rather than keeping two authoritative schemas forever |
| CLI overrides scalar config | ready, including type/runtime/LTCG/subsystem | **ready** |
| additive list precedence | config lists first, CLI lists append | **ready** |
| 14/17/20/23/latest standard config | ready | **ready** |
| target-kind config | strict `build.type` supports `exe`/`dll`/`static` (`lib` alias accepted); real static config routing and CLI target-kind precedence are covered | **ready** |
| runtime/subsystem config | typed strict-schema fields with real cache-invalidation E2E | **ready** |
| LTCG config | strict boolean `build.ltcg`; CLI `--ltcg`/`--no-ltcg` precedence and real executable/static E2E | **ready** |
| include/lib/define/library config | ready in `mqb.json` | **ready** |
| `compiler_args` / `linker_args` | ready in strict `mqb.json` v1 schema | **ready** |
| source discovery excludes | ready with the native discovery schema | **ready** |
| legacy config keys for remaining coupled policies | no remaining high-value coupled policy is accepted by default solely through raw-list synchronization | **implement** only when parity fixtures prove another typed policy is required |

## Toolchain/environment behavior

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| Visual Studio discovery | ready | **ready** |
| portable MSVC discovery | ready | **ready** |
| automatic preference | ready | **ready** |
| environment aliases `portable/port/p` | native parser accepts the legacy spellings for invocation-scoped selection | **compat spelling only**; PowerShell `-env` command semantics still migrate |
| legacy `.msvc_build_env` preference file | native does not use it | **migrate** to explicit CLI/config/environment contract; document upgrade |
| cached vcvars environment text file | native locator owns process environment differently | **migrate** implementation detail; no parity requirement |

The installed PowerShell profile prefers `pwsh.exe -NoProfile -File build.ps1` and falls back to the current host only when PowerShell 7 is unavailable. Shared parity tests mirror that host-selection behavior. They intentionally do not invoke `build.ps1 -env vs` as part of a build, because that legacy command persists the preference and exits instead of selecting Visual Studio for only that invocation.

## Incremental/artifact behavior

Stable v5 does not require byte-for-byte artifact layout parity with PowerShell. It requires equivalent observable correctness: cold build succeeds, warm no-op avoids unnecessary work, source/header/library/config/toolchain/build-policy changes invalidate the right work, missing required artifacts repair correctly, and outputs are collision-free.

Typed runtime changes are compile-recipe identity and therefore force affected compile + downstream target work. Typed target-kind/subsystem changes are target recipe identity; subsystem changes relink without recompiling otherwise-fresh TUs. Typed LTCG changes compile identity and the corresponding downstream link/archive identity as one coupled policy. Static archives have their own archive identity/cache and do not reuse `LinkCache`.

Raw compiler argument changes are compile-recipe identity and therefore force affected compile + downstream target work. Raw linker argument changes are link-recipe identity and therefore relink without recompiling otherwise-fresh TUs. Linker-only options are invalid for static targets in the current public contract; typed LTCG is valid because it owns both compile and archive halves.

The native `.mqb/obj`, `.mqb/deps`, `.mqb/scan`, `.mqb/ifc`, `.mqb/cache`, and `.mqb/bin` layout is the stable direction rather than the old PowerShell temporary layout.

## C++ Modules boundary (#16)

Project-local named modules and project-local header units are in the RC/stable-capable executable/DLL surface, but they require C++20 or newer. C++14/17 are ordinary-target compatibility modes only; attempting to scan or compile a module/header-unit contract below C++20 fails before MSVC is launched. Static targets currently support ordinary C/C++ TUs only and fail closed if discovery or explicit sources require the module/header-unit pipeline. External/prebuilt named-module providers and `import std` remain tracked in #16. Stable v5 may ship with those cases still fail-closed if release notes keep that boundary explicit; they must not be accidentally accepted through compatibility parsing.

## Parity campaign order

1. Legacy CLI spelling compatibility for behaviors the native engine already supports. **Done in #27.**
2. Restore C++14/17 ordinary-target modes while preserving the C++20+ module boundary. **Done in #28.**
3. Raw compiler/linker pass-through plus config build-policy expansion and cache-invalidation E2E. **Done in #29.**
4. Native `.c` translation units and mixed-language execution. **Done in #30.**
5. Typed runtime/subsystem CLI policy and cache semantics. **Done in #31.**
6. Isolate C smart discovery from C++ module lexical syntax. **Done in #32.**
7. Typed `mqb.json` runtime/subsystem policy with CLI precedence and real invalidation E2E. **Done in #33.**
8. Native DLL target kind with typed linker routing, deterministic import library, and side-output repair. **Done in #35.**
9. Static-library target with an explicit `lib.exe`/librarian backend, archive cache owner, strict `mqb.json` target kind, and real CLI/config consumer coverage. **Done in #36 + #37.**
10. Close the high-value coupled MSVC LTCG gap with typed `/GL` + downstream `/LTCG`, config/CLI precedence, cache identities, and real executable/static E2E. **Done in #39.** Charset remains evidence-driven rather than an automatic typed-surface requirement.
11. Shared PowerShell-vs-C++ fixtures for ordinary targets, config precedence, incremental rebuilds, libraries, run argv, x86/x64, Debug/Release, and failure behavior. **Harness foundation in #40:** same committed fixtures run through isolated PowerShell/C++ sandboxes with observable-result comparison; initial single-TU, explicit multi-TU, Debug, and Release scenarios are covered. Config/incremental/libraries/run argv/x86/x64/failure scenarios remain follow-up slices.
12. Installer/profile/`build` command cutover and clean-machine upgrade/rollback validation.
13. Generalize the release workflow and publish stable v5 only from the exact validated artifact.
