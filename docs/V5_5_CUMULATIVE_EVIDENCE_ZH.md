# v5.5.0 累计证据与发布边界

**[English](V5_5_CUMULATIVE_EVIDENCE.md) | 简体中文**

## 1. 冻结范围与精确身份

v5.5.0 冻结已接受的 warm-path 改动：可归因 timings/counters、带执行后复核屏障的
目标级文件系统证据复用、先检查再仅执行 miss 的调度、有界 compile-cache 读取、
默认汇总与批量输出，以及 archive-cache 的有界读写。v5.4.0 之后更早的 mainline
改进也包括在内。累计结果不是 #157、#159、#160 的孤立效果或百分比相加。

已否决的 link/discovery reader #158 和 toolchain reader #161 不重新纳入；发布准备
不再加入可选产品优化。Object handoff 尚未建立等价 freshness 边界且真正减少探测的
后阶段观察设计；link-resolution reuse 和 multi-key discovery 缺乏足够的剩余需求
证据。Cache pack 仍要求隔离后的残余 I/O 占比至少 25–30%，不能拿重叠工作时长充数。
Daemon、watcher、USN 和常驻进程留给 v6.0。

| 身份 | 精确值 |
|---|---|
| 已发布 v5.4.0 源码 | `d041668de836b9eb9a36e2d6b96ff2114c5c358a` |
| 基线源码树 | `555ebdb27ce8b7c379b03154578f55585601f905` |
| 被测的已接受产品 | `6f24bb4d2f5e323eb7b3823c3acf367dcccf9446` |
| 候选源码树 | `00c5b8e600ca188d3ce6158f002390307ee3c887` |
| 测量工具提交 | `f5303c351d983215827483ce9465e4c5d9100e54` |
| 仅证据的 #162 合并提交 | `9d6318b563c53c9b2795205c008f63744ebdb7e5` |

被测候选仍报告 VERSION 5.4.0。独立发布 PR 将 VERSION 改为 5.5.0，并记录既有结果，
不修改 C++ 源码。这不是对最终带新版本号二进制的新增性能测量。以后若修改产品代码，
必须补充新证据，不能默认沿用这些数字。本文不能证明版本已发布；发布状态以 GitHub
Releases 为准。

## 2. 兼容性与正确性边界

默认输出在构造逐源文件标签之前就汇总缓存命中的翻译单元。没有 PCH、警告或 discovery
前导信息的显式源文件 129-TU 全命中目标，结果从 131 行变为三行：

```text
[up-to-date] 129 translation units
[up-to-date] report.exe
output: <artifact path>
```

解析逐命中源文件日志的脚本必须使用 `--verbose`。实际重编译的源文件、重建原因、
全部警告和工具诊断、最终产物及运行行为仍可见。PCH 进度单独显示。普通、模块、静态库
目标共用该规则；静态目标不再遗漏 compile-cache warnings 与嵌套编译失败信息。
详见 [reporting 说明](REPORTING.md)。

接受范围内的 archive v1 字节和对象顺序保持兼容，但总读写上限变为 64 MiB，转义和
分隔符也计入。恰好 64 MiB 可以接受。旧的超限记录会告警并重新归档；新的超限保存会
保护原缓存，不使本来成功的构建失败。源文件、object 的 freshness、信任检查与保守
重试不因 benchmark 放宽。大小限制不是整个进程的内存上限，也不是原子文件系统快照。

当前 timing JSON 使用 schema 2；历史 schema 1 没有 counters/attribution，缺失项
保持不可用，不补零。阶段边界发生变化，内部总计不能直接替换比较口径。
`work.target_reporting` 包含格式化及嵌套写出，与 `wall.reporting` 重叠，不能相加；
包括解码的 cache-read work 也不是 elapsed I/O 占比。

## 3. 固定累计实验

[Release Cumulative Evidence #1](https://github.com/Iviesever/msvc-quick-build/actions/runs/34187064173)
使用同一个经校验和验证的历史 MQB seed、Windows 工具链和各源码树自身的 build driver，
分别构建未改动的两侧源码。比较的是**重新构建的发布源码**，不是旧下载包对新编译产物。
环境为 Windows Server 2025，镜像 `win25-vs2026`，版本 `20260824.214.3`。

全部 584 对主测量关闭 timings，使用相同 A/B 项目与缓存路径，以外部进程启动至完成、
管道排空的时间为口径。12 个 warm 场景各有 40 对交替 AB/BA，四个重建场景各有 20 对，
四个项目冷构建场景各有六对。项目冷构建只重置 `.mqb`，不清空操作系统缓存。每对源码
修改使用相同字节和真实推进的写入时间，不制造未来时间戳。不删除任何不利样本或运行。

预先固定的实用退化标记要求：配对中位数大于 warm 的 0.5 ms／其他的 5 ms 与基线
中位数 3% 两者中较大者，并且固定 5,000 次重采样的配对中位数区间完全为正。
这只是审阅辅助，不是自动发布授权，也不是零回归证明。序列相关性和少量冷构建样本限制
推断。管道不是真实终端；这些合成项目不能保证 Unreal Engine 或任意工作负载的表现。

独立、不计入主性能得分的审计兼容 schema 1/2 的缓存命中检查。Warm 范围内完整缓存／
产物元数据清单、stderr 保持不变，且没有构建动作行；当前版本另外要求零构建工具启动
和零缓存写入。历史计数器不能证明当时没有测量的进程情况。重建范围、缺失产物修复、
编译失败／退出码 4、恢复及生成程序运行均有检查，没有放宽 freshness。

## 4. 完整记录结果

各差值都是逐对“候选减基线”的中位数，负数为更快；不是两个独立中位数的差或比值。
全部使用关闭 timings 的相同外部时钟；不把不同场景合并为一个总分。

| 原始场景标识 | 对数 | 差值 ms | 百分比差值 | 更快对数 |
|---|---:|---:|---:|---:|
| `small-default-jauto` | 40 | -0.433 | -3.87% | 36/40 |
| `small-verbose-jauto` | 40 | -0.443 | -3.77% | 36/40 |
| `common-default-jauto` | 40 | -29.705 | -41.29% | 40/40 |
| `common-verbose-jauto` | 40 | -29.125 | -40.44% | 40/40 |
| `common-default-j1` | 40 | -40.934 | -42.55% | 40/40 |
| `private-default-jauto` | 40 | -27.858 | -38.18% | 40/40 |
| `static-default-jauto` | 40 | -30.896 | -42.11% | 40/40 |
| `static-verbose-jauto` | 40 | -31.415 | -40.09% | 38/40 |
| `modules-default-jauto` | 40 | -0.515 | -4.00% | 30/40 |
| `pch-default-jauto` | 40 | -0.762 | -6.04% | 33/40 |
| `discovery-default-jauto` | 40 | -0.590 | -4.76% | 35/40 |
| `run-default-jauto` | 40 | -0.049 | -0.27% | 21/40 |
| `common-single-tu` | 20 | -8.127 | -2.20% | 10/20 |
| `static-single-tu` | 20 | -38.572 | -17.69% | 17/20 |
| `common-public-header` | 20 | +5.707 | +0.17% | 9/20 |
| `small-output-repair` | 20 | +1.209 | +2.08% | 9/20 |
| `small-cold` | 6 | -0.862 | -0.05% | 3/6 |
| `common-cold` | 6 | +6.009 | +0.12% | 3/6 |
| `static-cold` | 6 | -12.869 | -0.25% | 5/6 |
| `modules-cold` | 6 | -5.351 | -0.29% | 4/6 |

主要得到证明的是大型目标 no-op 延迟改善。共享头默认、共享头 verbose/j1、私有头及
静态库默认场景的 40 对全部更快。小目标的绝对收益较小，构建后运行接近持平。
任何百分比都没有与之前某个 PR 的百分比相加。

## 5. 已知不利结果与发布风险

**尽管实用标记未触发，冷构建尾部问题仍未定位。** 共享头 129-TU 项目冷构建包含两对
很大的不利样本：

| 样本对 | 基线 ms | 候选 ms | 差值 ms |
|---|---:|---:|---:|
| 1 | 5026.8598 | 10380.6252 | +5353.7654 |
| 5 | 5059.0821 | 11539.5039 | +6480.4218 |

接近持平的中位数不能消除这些观测。六对样本的 P95 就是最大值，配对中位数区间很宽：
[-62.99515, +5917.0936] ms。关闭 timings 的实验未定位操作系统、工具链或调度原因，
本文不宣称任何一种原因已被证实。正式发布必须明确接受这一披露的限制，或延后并单独
调查；正确性／打包 CI 通过不会解决它。不能宣传冷构建加速。

公共头重建和缺失可执行文件修复的中位数略偏不利。可执行目标单 TU 重建的 MAD 为
98.86185 ms，区间跨越 [-111.7721, +77.4873] ms，只有 10/20 对更快，不能把有利的
中位数说成稳定收益。静态库重建仍有 +91.9177 ms 的不利样本，static verbose 有两对
更慢。这些观测必须与收益一起保留。

## 6. 保留证据与复算

[原始 artifact](https://github.com/Iviesever/msvc-quick-build/actions/runs/34187064173/artifacts/10041161376)
包含调用日志、审计与元数据清单。其 ZIP SHA-256 为：

```text
d508c45fcab94ec9e20ec7e05b6dc047c5fc303f450171ac8581e2075fc733a5
```

Artifact 保存期有限，本轮设置为 90 天，应在过期前另存。仓库同时保留 [584 对主测量
的无损数值投影](evidence/V5_5_CUMULATIVE_SAMPLES.json)，包含原始身份、时钟定义与
样本顺序。它不是新测量，也不能替代原始日志；两次冷构建异常均保留，可在托管 artifact
过期后复算数字。从仓库根目录运行：

```python
import json
from statistics import median
with open("docs/evidence/V5_5_CUMULATIVE_SAMPLES.json", encoding="utf-8") as stream:
    evidence = json.load(stream)
for case in evidence["scenarios"]:
    pairs = case["elapsed_pairs_ms"]  # [baseline, candidate], in original order
    print(case["name"], median(b - a for a, b in pairs),
          median(100 * (b - a) / a for a, b in pairs))
```

完整运行审阅检查了 1,446 次记录调用、2,892 份原始日志哈希、12 组一致的 warm 前后
清单、20 个场景的统计／标记，以及 258 份 timing 审计（两种 schema 各 129）。
与 workflow 清单交叉核对的记录二进制哈希为：

```text
baseline:  bad0124f1c6f549a794eb58c5442428c581d6aeb80c1dd304158de9394728c28
candidate: b937f84c91eadc809fd33373eed83f1fca803365683d0518190cb9c16b6fea4f
```

Benchmark 二进制没有归档；这些是记录的身份，不是对保留二进制进行本地重算的哈希。
源码修改哈希是 runner 声明，不是已归档的源码字节。最终生成程序检查已通过，但其日志
不在 1,446 次调用记录器内。复用证据时必须继续说明这些限制。

## 7. 最终发布关卡，而非新一轮优化

累计证据 PR 已通过 Native C++ #688、完整 Native Release #588（含自举／打包验证）、
Documentation #147 与累计 #1，并合并为 #162。它没有改动产品源码或 VERSION；
不能把这些 5.4.0 验证重新标记为 5.5.0 包的验证。

独立发布准备 PR 必须验证嵌入的 5.5.0、所有原生测试分片、Stage 1/Stage 2 自举闭环、
runtime-only 包、校验和、安装器生命周期及实际打包二进制身份。PR 通过与最终 main
精确提交发布是两回事。按照现有流程，**将 VERSION 修改合并到 main 会在完整 main
提交验证通过后触发发布**。这是有意保留的最后审批边界，不是无副作用的文档合并。
不要手工创建／移动 tag，也不要把 PR 的 ZIP 当成最终 main 的包发布。

[发布约定](SELF_HOSTING.md) 从合并历史生成 GitHub Release notes，不维护平行的仓库
changelog。发布 PR 描述应突出日志兼容性变化、archive 大小边界、限定范围的 warm
收益与未定位的冷构建尾部风险。不能仅为准备版本而改动 workflow、安装器、编译参数、
产品源码或 freshness 策略。
