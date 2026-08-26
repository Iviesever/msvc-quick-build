# 构建系统对比方法

**[English](BUILD_SYSTEM_BENCHMARK.md) | 简体中文**

MQB 提供了一套可复现的 Windows/MSVC 对比基准，用同一份生成源码树比较 MQB 与 CMake + Ninja。它的目标是产出可检查的工程证据，而不是宣称某个构建系统在所有场景下都一定更快。

基准脚本为 [`../tests/native/benchmark_build_systems.ps1`](../tests/native/benchmark_build_systems.ps1)。

## 范围

首版对比覆盖两类普通 MSVC 构建形态：

- 多 translation unit 的 executable；
- 同类 executable，但由各构建系统使用自身的一等 PCH 机制管理预编译头。

每类 fixture 覆盖以下状态变化：

| Fixture | 场景 | 变化 |
|---|---|---|
| ordinary | `ordinary-cold` | 没有已有 object/link state |
| ordinary | `ordinary-no-op` | 紧接着进行完全不变的 rebuild |
| ordinary | `ordinary-single-tu` | 只修改一个 `.cpp` |
| ordinary | `ordinary-public-header` | 修改所有 TU 都包含的公共 header |
| PCH | `pch-cold` | 没有已有 PCH/object/link state |
| PCH | `pch-no-op` | 紧接着进行完全不变的 rebuild |
| PCH | `pch-single-tu` | 只修改一个 `.cpp` |
| PCH | `pch-header` | 修改 PCH header |

C++ Modules 与 Header Units 有意不放进第一版对比矩阵。它们的支持边界不同，应当使用独立的方法学验证，而不是混入普通/PCH 的总数字中。

## 公平性规则

脚本对“可比较”的定义采取偏保守策略：

1. **每次 iteration 使用同一份物理源码树。** MQB 状态放在 `.mqb/`；CMake/Ninja 状态放在 `cmake-build/`。
2. **使用同一个干净的 MSVC 环境。** 脚本先解析指定的测量工具并保存调用者的进程环境，移除继承的 Visual Studio/Windows SDK `PATH` 项及 `CL`/`LINK` 选项注入变量，再通过 `vswhere` + `VsDevCmd.bat` 导入最新安装的 x64 Visual Studio 开发环境；两套系统完成后，脚本会在 `finally` 中恢复调用者环境。脚本还会验证 `cl.exe` 和 `link.exe` 确实来自选中的 Visual Studio 安装。
3. **显式使用同一并行上限。** `-Jobs N` 分别映射为 MQB `-j N` 与 Ninja `-j N`。
4. **对齐 Release 编译/链接策略。** 生成的 CMake target 使用与本 fixture 中 MQB 相同的核心 MSVC Release 参数：`/utf-8`、`/W3`、`/EHsc`、`/permissive-`、`/Zc:__cplusplus`、`/Zc:preprocessor`、`/diagnostics:column`、`/O2`、`/Oi`、`/MD`、`/Z7`、`/DNDEBUG`、`/std:c++23preview`；Release link 策略使用 `/INCREMENTAL:NO`、`/OPT:REF`、`/OPT:ICF`、x64 与 console subsystem。
5. **构建系统自己的 dependency 工作保留在测量中。** CMake/Ninja 可能加入自己的依赖跟踪参数，MQB 也会使用自己的 metadata 路径；这些属于被比较系统本身，不会人为移除。
6. **CMake configure 时间不计入 build timing。** Configure 会被单独测量并写入报告；`ordinary-cold` 与 `pch-cold` 开始时 `build.ninja` 已经生成。
7. **计时阶段直接调用 Ninja。** CMake/Ninja 样本运行 `ninja -C ...`，而不是 `cmake --build`，因此不会把 CMake 命令包装层的额外开销计入对比。
8. **交替执行顺序。** MQB 与 Ninja 会根据 iteration/场景交替先后执行，以降低固定“先跑/后跑”带来的系统性偏差。
9. **每个成功计时样本都必须执行产物。** 生成的 executable 必须返回 0，样本才有效。
10. **不设置 hosted-runner 数值阈值。** 结果属于证据；脚本不会把某个百分比直接变成产品质量的 pass/fail 结论。

这里有一条规则对 CMake/Ninja 是明显有利的：它的一次性 configure 成本会记录在 JSON 中，但不计入成对的 build median。

## 本地运行

使用一个 Release MQB executable，并给出适合机器的显式 worker 数：

```powershell
.\tests\native\benchmark_build_systems.ps1 `
  -MqbPath .\path\to\mqb.exe `
  -Iterations 5 `
  -TranslationUnits 100 `
  -Jobs 8 `
  -OutputPath .\build-system-comparison.json
```

要求：

- Windows x64；
- MSVC x64 tools；
- CMake 3.20 或更新版本；
- Ninja；
- 待测的 MQB executable。

脚本会先过滤继承的 Visual Studio/Windows SDK 路径及 MSVC 选项注入变量，再全新导入一次 x64 MSVC 环境，避免已卸载或混合版本的安装，以及不对等的 `CL`/`LINK` 参数污染任一构建路径。

调试生成 fixture 时可使用 `-KeepWorktree`。否则运行结束后会删除临时 fixture 树。

## 输出契约

JSON 报告记录：

- MQB、CMake、Ninja、`cl.exe` 的实际路径与 SHA-256 identity；
- 可取得的工具版本字符串；
- Windows / PowerShell / CPU width / MSVC 环境信息；
- translation unit 数、iterations、jobs、architecture 与 configuration；
- 每一个原始 build sample 及其 execution order，以及 MQB 的阶段计时和 compile/link cache 计数；
- 与 build 时间分离记录的 CMake configure samples 和 median；
- 每个场景下 MQB 与 CMake/Ninja 的 median；
- MQB 相对 CMake/Ninja 的绝对差值与百分比差值；
- `cmake+ninja / mqb` median ratio。

对于 `mqb_delta_pct_vs_cmake_ninja`，负值表示 MQB 的 wall-clock time 更低。Ratio 只是便于阅读；任何数字都不应脱离同一报告里的机器、工具链和方法信息单独引用。

## CI 契约

`.github/workflows/build-system-evidence.yml` 会在相关 pull request 上运行一个小规模 correctness fixture。它从当前仓库源码构建 MQB，使用较小 TU 数执行一次对比，并上传 JSON 报告。

这个 workflow 证明对比 harness 仍可执行、两条构建路径都能产生正确程序，并且 MQB 的每个 cold/no-op/incremental 场景都发生了预期的 cache transition。它**不是**稳定的性能排行榜：hosted runner 的 VM 分配、系统负载、杀毒状态、文件系统缓存等都会明显影响 wall-clock 数字。

真正用于公开引用的测量，应在明确说明的物理机器上运行多次，保留完整 JSON，并把机器与 toolchain 信息和 median 一起发布。
