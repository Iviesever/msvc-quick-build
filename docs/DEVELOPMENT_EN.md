# MQB Development

**Language: [简体中文](DEVELOPMENT.md) | English**

MQB uses MQB itself for day-to-day development builds and tests.

Recommended entry point:

```powershell
.\tests\native\develop.ps1
```

Or provide a seed explicitly:

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

The seed builds the current-source MQB, then that current MQB builds and directly executes the complete 67-test graph. The development/test chain does not invoke CMake or CTest.

### C++ Source Layout

The C++ tree is deliberately singular:

```text
cpp/
├─ include/   # single cross-component header root
├─ src/       # single product implementation root
├─ tests/     # single C++ test root
├─ README.md  # enforced layout contract
└─ mqb.json   # self-build production manifest
```

Responsibilities are mirrored beneath them (e.g. `core / config / discovery / modules / orchestration / msvc / platform`). Component-local `include/src/tests` trees are forbidden. See [`cpp/README_EN.md`](../cpp/README_EN.md) for the enforced layout contract.

Lower-level drivers reside under `tests/native/`:

- `build_mqb.ps1`: MQB → current MQB;
- `run_native_tests.ps1`: current MQB → 67 test executables → direct execution;
- `acquire_seed.ps1`: CI acquisition and verification of pinned historical seed.

`cpp/mqb.json` is MQB's authoritative native project description, with `src/app/main.cpp` as the executable entry and only `include` plus `src/app` as include roots.
