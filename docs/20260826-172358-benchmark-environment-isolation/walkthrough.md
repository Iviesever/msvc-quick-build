# Benchmark environment isolation and invocation warm-path walkthrough

## Outcome

The benchmark harness now resolves the requested tools before isolation,
filters inherited Visual Studio/Windows SDK paths and MSVC option-injection
variables, imports one verified x64 Visual Studio environment, records the
selected compiler/linker paths, asserts exact MQB cache transitions, and restores
the caller environment.

The product change replaces quadratic duplicate-source filesystem equivalence
checks with linear membership in the shared Windows path-identity domain. The
user-visible case-alias duplicate rejection remains covered by a native CLI E2E
test.

## Changed source files

- `cpp/src/app/cli/Invocation.cpp`
- `cpp/tests/app/cli/mqb_native_msvc_cli_e2e_tests.cpp`
- `tests/native/benchmark_build_systems.ps1`
- `docs/BUILD_SYSTEM_BENCHMARK.md`
- `docs/BUILD_SYSTEM_BENCHMARK_ZH.md`
- `docs/20260826-172358-benchmark-environment-isolation/issue.md`
- `docs/20260826-172358-benchmark-environment-isolation/implementation_plan.md`
- `docs/20260826-172358-benchmark-environment-isolation/walkthrough.md`

## Environment evidence

After running the administrator cleanup script on 2026-08-26, a fresh registry
and process audit reported zero stale matches in machine `PATH`, `INCLUDE`,
`LIB`, and `VCINSTALLDIR`, zero stale matches in user `PATH`, and zero stale
matches in the current Codex process `PATH`.

The post-cleanup benchmark smoke report verified:

```text
Visual Studio: C:\Program Files\Microsoft Visual Studio\18\Community
MSVC tools:    14.51.36231
Windows SDK:   10.0.26100.0
```

## Native build and test evidence

The current Debug MQB was rebuilt from the repository source with MQB. The
successful verification output included:

```text
C++ responsibility layout contract passed.
Verified project manifest: 73 production translation units
[up-to-date] cpp/src/app/cli/Invocation.cpp
[up-to-date] mqb.exe
output: ...\native-out\verification\mqb-debug.exe
```

All eight deterministic Debug native subshards ran against that candidate and
the verified shared product library:

```text
MQB-native shard 0/8:  8/8 selected tests passed.
MQB-native shard 1/8:  9/9 selected tests passed.
MQB-native shard 2/8:  9/9 selected tests passed.
MQB-native shard 3/8:  9/9 selected tests passed.
MQB-native shard 4/8:  9/9 selected tests passed.
MQB-native shard 5/8: 11/11 selected tests passed.
MQB-native shard 6/8: 11/11 selected tests passed.
MQB-native shard 7/8: 11/11 selected tests passed.
```

Total: **77/77 native tests passed** without CMake or CTest. The shard containing
`mqb_native_msvc_cli_e2e_tests.cpp` passed, including the new case-alias
duplicate-source contract.

## Benchmark evidence

The corrected 100-TU, five-iteration before/after reports retain 80 raw samples
each (eight scenarios, two systems, five iterations). Product warm-path medians
changed as follows:

| Scenario | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| ordinary no-op | 104.31 ms | 26.59 ms | 74.5% |
| ordinary single TU | 172.67 ms | 103.18 ms | 40.2% |
| PCH no-op | 107.04 ms | 33.14 ms | 69.0% |
| PCH single TU | 175.52 ms | 97.41 ms | 44.5% |

A fresh post-cleanup 8-TU smoke run completed all ordinary and PCH cold, no-op,
single-TU, and header transitions. The harness rejected no sample, so every MQB
sample matched its exact compile/link cache contract.

Cold builds and PCH-header rebuilds remain explicit follow-up optimization
targets; they are not claimed as improved by this change.

## Documentation and diff evidence

```text
Bilingual documentation parity check passed for 17 maintained pairs.
PowerShell parse and git diff checks passed.
```

An independent read-only review found no blocking issues after the environment
isolation follow-up fixes. No commit, push, pull request, merge, or release was
performed while producing this evidence.
