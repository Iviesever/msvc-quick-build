# PR247 remaining-scenario evidence / 剩余三场景验证

## Purpose / 目标

Follow #198/6059099426 after the completed link-only study. This is one bounded
study of `target-scale-single-tu`, `discovery-header`, and `modules-cold`, not
another full Performance919 run. Product #247 remains on
`f0ef131321dc0720c83ce55766edde4b751495ad` and manual integration HOLD remains.
The earlier link-only result, all 919 adverse values and its unchanged no-op gate
are retained. This does not add a new automatic performance threshold.

接续已完成的链接诊断，只验证剩余三个场景。不重跑 link-only 或19场景矩阵，
不新建计时框架，不改产品。新值不能替代919或自动解除HOLD。

## Frozen inputs / 冻结输入

Reuse the unchanged `study.py` collector (Git blob
`6e30f1d7a6a9f13c0bbf71d1db41ba2f58990a7c`), including its raw byte capture,
call budget, timing decoder, original archive verification and file manifests.
Only the original A/B executables from artifact11539029999 are executed:

- ZIP SHA256: `1641d0d9b8af917fc1ea883f76b54056d7ae233af1754d2d6ce81e8aee106ce6`.
- A/main399e06fa: `38f1693634f85ef8e8dc0d31c3438e0e0718dc80ec2e74cb83cb116cd804191b`.
- B/f0ef1313: `6d52d638f89336217ce8dd1ae277088bdda591445fc7aecb6f7edcee7f4d5546`.

No rebuild, seed acquisition, instrumented copy, ETW or produced-program execution.
Use the retained pair1 baseline final fixture sources and exact argv. For the two
incremental fixtures, undo only the unique recorded trailing mutation to prepare
with B once, then restore that exact mutation and save a timestamped pristine
snapshot. Each measurement restores that scenario to its same absolute path,
full bytes and recorded modification times. Validate both preparation and samples.

使用归档原字节和参数；准备时只撤销唯一的历史追加注释，准备后恢复注释并固定时间。
每次恢复同路径的完整状态，不能只重写源文件而复用上一次被改过的缓存。
准备来自B是此新实验的明确条件，不冒充919中各侧独立准备的顺序。

## Single allocation / 唯一分配

`pr247-remaining-scenarios-001`, only PR248 synchronize from/direct Git parent
`625e70b1a68e476fd07a7e581846b1a557b27ba3`, base
`399e06fa43d2052ac0d452e1320438c015c65145`, original diagnostic branch, attempt1.
No manual entry or retry admission. The old LINK boundary workflow is unchanged;
its different pinned predecessor does not admit this transition.

Prepare the 129-TU and discovery fixtures once each: **2 root MQB calls**.
Then four paired blocks AB / BA / AB / BA. Each side executes the three scenarios
in the listed order: **24 measured calls**, 12 pairs, 8 measurements per scenario.
**Total ceiling26 root MQB calls.** No extra warm-up, no supplemental samples.

| Phase/scenario | Compile hits/misses | cl/link/lib per call |
|---|---|---|
| 129-TU preparation | 0/129 | 129/1/0 |
| discovery preparation | 0/2 | 2/1/0 |
| target-scale-single-tu | 128/1 | 1/1/0 |
| discovery-header | 0/2 | 2/1/0 |
| modules-cold | 0/2 | 4/1/0 |

The module `cl` count includes two dependency-scan launches. Expected registered
work is187 cl and26 link launches, including preparation, not an OS process census.
No module preparation: restore its two source files with no project cache before
every cold call. Project-state cold is not OS-cache/machine cold. Record all stage
vectors; toolchain discovery can be a large part of modules-cold.

固定上限26次根调用，所有准备也计入。模块每次从只有两源文件的项目状态开始，
不是声称操作系统缓存清空。失败、超时、输入/工作量不符立即停止并保存；不采到绿。
超时保留quiescence_proven=false，不把Python子进程返回等同于所有后代静默。

## Evidence and interpretation / 证据与解释

Preserve exact request/harness, original ZIP and executables, before/after hashes,
all stdout/stderr bytes, exit codes, charged calls, raw timing, preparation state,
pristine and every post-state. Archive extraction does not preserve all timestamp
precision: offline replay compares actual names/bytes and original recorded times,
not reconstructed nanosecond filesystem times. Live restore checks actual times.

Reuse Python launcher from #248, which differs from919's PowerShell capture.
The restored state, new runner and smaller sequence do not reproduce the old
machine/history. Never pool new/old scores, subtract observation cost, discard slow
values or infer a historical cause from non-reproduction. Work intervals can overlap.
Report all12 pairs, full stage deltas and whole-counter equality separately.
`product_acceptance`, `release_authorized`, `raw_scores_replace_919` stay false.

新实验用于风险处置，不保证建立旧机器根因。全部新旧样本保留，不能以未复现
抹去历史失败。最后必须独立作集成决定；其它功能验收及依赖、内存、持久化、
clean/prune、Rium/#164边界不扩大。

## Portable tests and read-only replay

```text
PR247_TEST_ARCHIVE=<original919.zip> python -m unittest discover -s tests/diagnostics/pr247_link -p test_remaining.py -v
python tests/diagnostics/pr247_link/remaining.py audit --out remaining-output --report new-audit.json
```

Tests use simulated subprocesses only. The audit never executes the archived EXEs.
Report/output paths must be new; existing evidence is never overwritten.
