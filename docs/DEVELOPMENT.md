# 开发 / Development

## 简体中文

MQB 使用 MQB 自身进行日常开发构建与测试。

推荐入口：

```powershell
.\tests\native\develop.ps1
```

如果 `mqb` 不在 PATH：

```powershell
.\tests\native\develop.ps1 -SeedMqbPath C:\path\to\mqb.exe
```

该流程先使用 seed MQB 构建当前源码的 MQB，再由当前 MQB 构建并运行完整 67-test graph。开发/测试链不调用 CMake 或 CTest。

### C++ 源码结构

MQB 只维护一套清晰的 C++ 物理树：

```text
cpp/
├─ include/   # 唯一跨组件头文件根
├─ src/       # 唯一产品实现根
├─ tests/     # 唯一 C++ 测试根
├─ README.md  # 目录与依赖规则
└─ mqb.json   # 自构建 production manifest
```

三个根内部按职责镜像，例如 `core / config / discovery / modules / orchestration / msvc / platform`。禁止新增 `cpp/<component>/include`、`src` 或 `tests`。详细规则见 [`cpp/README.md`](../cpp/README.md)。

更底层的 driver 位于 `tests/native/`：

- `build_mqb.ps1`：MQB → 当前 MQB；
- `run_native_tests.ps1`：当前 MQB → 67 个测试程序 → 直接执行；
- `acquire_seed.ps1`：CI 固定历史 seed 的获取与校验。

`cpp/mqb.json` 是 MQB 自身的权威 native project description。当前 production manifest 以 `src/app/main.cpp` 为入口，并使用统一的 `include` 与 `src/app` include roots。

## English

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

The C++ tree is deliberately singular: `cpp/include` is the only cross-component header root, `cpp/src` is the only product implementation root, and `cpp/tests` is the only C++ test root. Responsibilities are mirrored beneath them; component-local `include/src/tests` trees are forbidden. See `cpp/README.md` for the enforced layout contract.

`cpp/mqb.json` is MQB's authoritative native project description, with `src/app/main.cpp` as the executable entry and only `include` plus `src/app` as include roots.
