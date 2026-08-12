# MQB 原生开发与测试驱动

**语言：简体中文 | [English](README_EN.md)**

Stable v5 使用 MQB 自身完成 MQB 的开发构建、测试构建和发布代构建。仓库已经移除全部 `CMakeLists.txt`，不再维护第二套 CMake build graph。

- `acquire_seed.ps1`：CI 获取固定历史 `v5.0.0-rc.2` MQB seed，并校验 release ZIP SHA-256 与 executable identity。
- `build_mqb.ps1`：验证 `cpp/mqb.json` 的精确 42-TU production manifest，然后由 MQB 构建当前 MQB。
- `run_native_tests.ps1`：要求恰好 67 个 `*_tests.cpp`，由当前 MQB 构建每个测试程序并直接运行完整测试图。
- `develop.ps1`：本地一条命令完成“已安装/显式 seed MQB → 当前 MQB → 67 tests”。

此目录中的脚本不调用 CMake 或 CTest。MQB-native 的本地/CI 输出目录已经由根 `.gitignore` 管理。
