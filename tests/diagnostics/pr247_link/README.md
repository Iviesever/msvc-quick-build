# PR247 LINK boundary diagnosis / LINK 边界诊断

This is an isolated, single-use instrument, not a product fix or a replacement
benchmark. The independent development-integration HOLD in #247 remains in force.

本目录是独立、一次性诊断工具，不修改 #247 产品候选，也不替代 Performance919。
诊断结果不自动解除人工集成 HOLD，更不批准发布。

## Frozen inputs / 冻结输入

- Allocation: `pr247-link-boundaries-002`; diagnostic branch: `diag/pr247-link-boundaries-001`.
- Original run: `37754575256 / attempt1`, artifact `11539029999`.
- Original ZIP: 11,056,074 bytes, SHA256
  `1641d0d9b8af917fc1ea883f76b54056d7ae233af1754d2d6ce81e8aee106ce6`.
- A: `399e06fa43d2052ac0d452e1320438c015c65145`; executable SHA256
  `38f1693634f85ef8e8dc0d31c3438e0e0718dc80ec2e74cb83cb116cd804191b`.
- B: `f0ef131321dc0720c83ce55766edde4b751495ad`; executable SHA256
  `6d52d638f89336217ce8dd1ae277088bdda591445fc7aecb6f7edcee7f4d5546`.

原始 A/B 和源码归档均按字节摘要校验。原919全部样本、失败及原判定保持原样；
本次不会运行 `compare_mqb_benchmarks.ps1`，也不触发旧性能工作流。

## Hypotheses and boundaries / 假设与可辨认边界

H1: the retained link-only slowdown is concentrated in process creation, root
completion waiting, or pipe-reader tail rather than post-return library parsing.
H2: the post-return observation method itself has appreciable cost on fixed output.
Neither hypothesis is assumed true, and new measurements cannot prove what caused
an earlier VM's historical delay.

假设一是耗时集中在进程创建、根进程完成等待或管道线程收尾；假设二是返回后的
库观察方法有显著成本。两者都不能预先当成事实。新的机器/样本不能倒推旧机器根因。

Two cohorts are deliberately kept separate:

1. The original A/B binaries, unmodified, four fixed AB/BA/AB/BA pairs (8 calls).
2. Diagnostic A'/B' rebuilt from the exact archived sources, with identical boundary
   insertions into only WindowsProcessRunner and the LINK coordinator, four more
   fixed pairs (8 calls). The actual library parser is not changed. Full patched
   source archives, original/patched file digests, builder identity and output
   executable hashes are retained before use. These are NOT the original binaries.

两组不能混算：原程序8次调用；另编译的诊断副本8次调用。诊断副本的时间不能冒充
原程序内部时间。产品分支/主线/manifest/旧门槛均不修改；临时诊断头只加入副本根目录。

QPC timestamps bracket CreateProcessW, root wait, successful pipe joins, the runner
return, the caller return and the complete `observed_library_paths` method. Marks
preserve GetLastError so diagnostic clock calls do not overwrite native errors.
GetProcessTimes is read once after joining readers, plus GetProcessId; its process
lifetime and CPU counters are separate from QPC intervals. Serialization and three
CreateNew writes occur after the last parsing boundary, before the old sanitizer.

QPC标记分别包围创建、等待、成功路径管道join、runner返回、调用者返回和完整库解析。
时钟调用保留原生错误码。进程时间/ID是新增诊断读取；序列化与三个不可覆盖文件写入
放在解析边界之后。整个MQB耗时仍包含仪器开销，不扣除它来制造分数。

Important limitations / 重要限制:

- A wait interval includes child work and scheduling; a join tail is not total
  overlapping reader work. Child lifetime overlaps launch/wait; do not add them.
- Parsing time covers the complete observation method, not just the new predicate,
  and does not include later file-input sealing, signing or cache saving.
- The Python byte-capture launcher differs from 919's PowerShell merged-text
  launcher. Both streams here are the launcher outputs; diagnostic LINK buffer
  snapshots are separately named. There is no ETW, stack sampling or full OS census.
- 仪器会改变代码布局、调用和文件访问；CPU数据不是独占墙钟分解。不得据此宣称
  环境噪声已被证明、所有原回退已解释或其余重大场景已经放行。

## Fixed work and stopping / 固定工作与停止规则

The build job uses the original B as builder, calling the unchanged original
self-build helper once per side. Each helper contains a build plus `--help`:
4 root MQB calls, not a new benchmark. A fresh VM performs measurement.

On that VM one original-B preparation compiles the three source files retained
from 919's ordinary fixture (2 translation units plus one header) and links once.
The resulting *same absolute-path* fixture is snapshotted, then restored with exact
file bytes and modification timestamps before every measurement. Each sample adds
`--linker-arg /MAP`, reproducing the link-option change, and must show exactly
2 compile hits, 0 compile processes, and 1 LINK process. Original order is executed
first, then diagnostic order; no primes, adaptation, repeats or favorable selection.

构建副本上限4次根MQB调用；新机器准备输入1次；测量16次，总上限21次。
预期动作级编译为192个诊断源码TU+2个准备TU，动作级链接为2+1+16；这不是工具发现、
系统子进程或OS全进程总量。没有新ETW、标准全矩阵、产物运行或重试预算。

Every root invocation reserves budget and writes a start record before launching.
Raw stdout/stderr, exit/timeout, input byte/time manifests and each post-call fixture
are preserved. A failure stops the study; a timeout records descendant quiescence
as unproven, not successful cancellation. No later sample is intentionally launched.
Build-helper budgets are declared from its pinned implementation, not an OS trace.

工作流只接收#248冻结base、同仓库指定分支从6074de8e6f7e7946ac35933d093109f47ced118a
直接前进的synchronize事件/attempt1，不设手动或重复运行入口。
失败后保留附件、退出且不采到绿。原始失败、旧403/abandoned、819 HOLD、不利历史、
依赖完整性/显式内存成本及持久化、writer、clean-prune、Rium/#164限制全部保留。

## Offline replay / 离线重放

After retrieving the input and result artifacts, an audit does not run any binary:

```text
python tests/diagnostics/pr247_link/study.py audit --inputs diag-input --out diag-output --report audit-replay.json
```

The report path must be new. No score or acceptance threshold is introduced.
The portable tests use fake subprocesses and are not Windows/MSVC execution.

## API references

- https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
- https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request

## First-run failure and bounded correction / 首次失败与限定修订

Run37767763007/attempt1 (harness6074de8e) stopped before instrument compilation:
38 of39 contracts passed; the synthetic backslash-name rejection failed. On Windows,
ZipInfo normalizes a backslash to a slash in both writing and reading paths. The old
fixture could therefore contain a valid name rather than the intended invalid one.
The corrected tests construct exact raw local/central ZIP filename bytes, and the
reader validates orig_filename before accepting normalized filename. NUL truncation
is rejected too; valid nested paths remain accepted on both separator models.

首次39项中38通过、1项失败，仪器构建及真实诊断未开始（该诊断内真实MQB调用0）。
原run/日志及旧测试完整保留；没有附件产生，因为失败发生在prepare之前。修订添加
preflight原输出附件，不重写旧证据。原程序、标记位置、采样矩阵和21次上限均不变。

Allocation002 is a new, explicitly registered corrected-harness run, not a retry of
unchanged code or a new round of performance samples. It accepts only PR248's direct
successor of the failed commit, checks the Git parent, and rejects other transitions
or attempts. The repository's ordinary Native/Documentation workflows are separate
CI work, not included in the21 diagnostic root-call ceiling. No result releases HOLD.
