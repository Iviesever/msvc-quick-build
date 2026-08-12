# MQB-Native Development & Test Drivers

**Language: [简体中文](README.md) | English**

Stable v5 uses MQB itself for MQB development builds, test builds, and release generations. All `CMakeLists.txt` files have been removed; the repository no longer maintains a second CMake build graph.

- `acquire_seed.ps1`: acquires the pinned historical `v5.0.0-rc.2` MQB seed in CI and verifies the release ZIP SHA-256 plus executable identity.
- `build_mqb.ps1`: verifies the exact 42-TU `cpp/mqb.json` production manifest, then builds the current MQB with MQB.
- `run_native_tests.ps1`: requires exactly 67 `*_tests.cpp` entries, builds every test executable with MQB, and directly runs the full test graph.
- `develop.ps1`: local one-command flow from an installed/explicit MQB seed to the current MQB and all 67 tests.

No script in this directory invokes CMake or CTest. MQB-native local/CI outputs are covered by the repository root `.gitignore`.
