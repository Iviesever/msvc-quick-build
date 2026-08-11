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
| `.cpp`, `.cc`, `.cxx` | ready | **ready** |
| `.ixx` / named modules | ready for project-local providers | **ready**, with #16 boundaries documented |
| project-local header units | native pipeline is ready | **ready** |
| `.c` translation units | rejected by native CLI today | **implement** |
| `.hpp` / `.h` as positional legacy discovery seeds | native CLI treats headers as non-TU inputs | **migrate** unless a real user workflow requires positional header seeds |
| C++14 / C++17 | ordinary native targets map to `/std:c++14` / `/std:c++17`; module pipeline remains C++20+ | **ready** for ordinary non-module targets |
| C++20 / C++23 / latest | ready | **ready** |

## Common command-line options

| Legacy spelling / behavior | Native equivalent | Stable-v5 decision |
| --- | --- | --- |
| `-o`, `-output` | `-o`, `--output`; legacy `-output` accepted | **compat** |
| `-run` | `--run`; legacy `-run` accepted | **compat** |
| `-std` | `--std`; legacy `-std` accepts 14/17/20/23/latest | **compat** |
| `-x86` | `--x86`; legacy spelling accepted | **compat** |
| default x64 | `--x64` / x64 default; legacy `-x64` accepted | **compat** |
| `-I`, `-include` | `-I`; legacy `-include` accepted | **compat** |
| `-L`, `-libpath` | `-L`, `--lib-path`; legacy `-libpath` accepted | **compat** |
| `-libs` | `-l`, `--lib`; legacy `-libs <value>` accepted and repeatable | **compat** for scalar/repeated values; document that shell array tokenization is not emulated |
| `-D`, `-defines` | `-D`; legacy `-defines` accepted | **compat** |
| `-config debug/release` | `--debug`, `--release`, `--config`; legacy `-config` accepted | **compat** |
| `-env vs/portable/port/p/auto` | `--env auto/vs/portable`; legacy spelling and portable aliases accepted | **compat** |
| `-help`, `-?` | `--help`, `-h`; legacy help aliases accepted | **compat** |
| `-a "..."` single-string program arguments | structured `-- program-args...` | **migrate**: keep structured argv semantics; decide whether a compatibility tokenizer is worth the ambiguity |

## Target/output kinds

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| executable target | ready | **ready** |
| DLL target (`-type dll`) | not exposed by native target model | **implement** |
| static library target (`-type static`) | not exposed by native target model | **implement** |
| automatic `.exe/.dll/.lib` suffix by target kind | executable only today | **implement** with target-kind work |
| console subsystem | native default | **ready** |
| windows subsystem | link model has typed subsystem but CLI does not expose it | **implement** |

## Compiler and linker policy

The native backend already has typed `CompilerOptions` / `LinkOptions`, including ordered additional argument vectors. Compile and link signatures include those vectors, so future pass-through flags can remain incrementally correct.

| Legacy option | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| `-optimize Od/O1/O2/Ox` | debug/release presets cover common cases, no first-class override | **implement** or explicitly migrate to raw compiler flags after parity E2E |
| `-runtime MD/MDd/MT/MTd` | presets choose MDd/MD; release package itself uses static CRT only for MQB | **implement** target runtime selection |
| `-warnings W0/W1/W3/W4/Wall` | fixed W3 today | **implement** |
| `-WX` | not exposed | **implement** |
| `-debug_info off/Zi/ZI/Z7` | presets use Z7 | **implement** or migrate to raw compiler flags with documented precedence |
| `-exceptions EHsc/EHa/off` | fixed EHsc today | **implement** or migrate to raw compiler flags |
| `-rtc1` | not exposed | **implement** or migrate to raw compiler flags |
| `-jmc` | not exposed | **implement** or migrate to raw compiler flags |
| `-sdl` | not exposed | **implement** or migrate to raw compiler flags |
| `-permissive` | native currently always uses `/permissive-` | **migrate**: strict conformance is the native baseline; add an escape hatch only if parity fixtures require it |
| `-fp precise/strict/fast` | not exposed | **implement** or migrate to raw compiler flags |
| `-charset unicode/mbcs` | native uses UTF-8 source/execution compile policy; Windows macro charset is not exposed | **implement** if legacy projects rely on `_UNICODE/UNICODE` vs `_MBCS` macros |
| `-ltcg` | not exposed | **implement** because it couples compile `/GL` and link `/LTCG` |
| `-incremental` linker switch | native build engine is always incrementally cached, but linker `/INCREMENTAL` is not exposed | **migrate** terminology; expose linker incremental only if required |
| `-flags` | Core already supports ordered compiler additional args but CLI/config do not expose them | **implement next** |
| `-link_flags` | Core already supports ordered linker additional args but CLI/config do not expose them | **implement next** |

## Project configuration

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| `msvc_list.json`, upward search up to five levels | native uses nearest upward `mqb.json` | **migrate** to `mqb.json`; provide upgrade guidance/tooling rather than keeping two authoritative schemas forever |
| CLI overrides config | ready | **ready** |
| include/lib/define/library config | ready in `mqb.json` | **ready** |
| source discovery excludes | ready with the native discovery schema | **ready** |
| legacy config keys for tuning/target kind | not all represented | **implement** only for capabilities accepted into the stable parity surface |

## Toolchain/environment behavior

| Legacy behavior | Native C++ status | Stable-v5 decision |
| --- | --- | --- |
| Visual Studio discovery | ready | **ready** |
| portable MSVC discovery | ready | **ready** |
| automatic preference | ready | **ready** |
| environment aliases `portable/port/p` | parser compatibility added | **compat** |
| legacy `.msvc_build_env` preference file | native does not use it | **migrate** to explicit CLI/config/environment contract; document upgrade |
| cached vcvars environment text file | native locator owns process environment differently | **migrate** implementation detail; no parity requirement |

## Incremental/artifact behavior

Stable v5 does not require byte-for-byte artifact layout parity with PowerShell. It requires equivalent observable correctness: cold build succeeds, warm no-op avoids unnecessary work, source/header/library/config/toolchain changes invalidate the right work, missing required artifacts repair correctly, and outputs are collision-free.

The native `.mqb/obj`, `.mqb/deps`, `.mqb/scan`, `.mqb/ifc`, `.mqb/cache`, and `.mqb/bin` layout is therefore the stable direction rather than the old PowerShell temporary layout.

## C++ Modules boundary (#16)

Project-local named modules and project-local header units are in the RC/stable-capable surface, but they require C++20 or newer. C++14/17 are ordinary-target compatibility modes only; attempting to scan or compile a module/header-unit contract below C++20 fails before MSVC is launched. External/prebuilt named-module providers and `import std` remain tracked in #16. Stable v5 may ship with those cases still fail-closed if the release notes keep that boundary explicit; they must not be accidentally accepted through compatibility parsing.

## Parity campaign order

1. Legacy CLI spelling compatibility for behaviors the native engine already supports. **Done in #27.**
2. Restore C++14/17 ordinary-target modes while preserving the C++20+ module boundary. **Done in #28.**
3. Raw compiler/linker pass-through with cache-invalidation E2E.
4. Add `.c` translation units.
5. Target kinds: DLL/static plus subsystem/runtime policy.
6. First-class/high-value MSVC tuning options; use raw flags for the long tail where that produces an explicit, testable contract.
7. Shared PowerShell-vs-C++ fixtures for ordinary targets, config precedence, incremental rebuilds, libraries, run argv, x86/x64, Debug/Release, and failure behavior.
8. Installer/profile/`build` command cutover and clean-machine upgrade/rollback validation.
9. Generalize the release workflow and publish stable v5 only from the exact validated artifact.
