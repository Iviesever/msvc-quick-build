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

更底层的 driver 位于 `tests/native/`：

- `build_mqb.ps1`：MQB → 当前 MQB；
- `run_native_tests.ps1`：当前 MQB → 67 个测试程序 → 直接执行；
- `acquire_seed.ps1`：CI 固定历史 seed 的获取与校验。

`cpp/mqb.json` 是 MQB 自身的权威 native project description。

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

`cpp/mqb.json` is MQB's authoritative native project description.
