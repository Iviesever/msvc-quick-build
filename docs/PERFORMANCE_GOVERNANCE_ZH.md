# MQB 性能治理

**[English](PERFORMANCE_GOVERNANCE.md) | 简体中文**

MQB 的性能工作以现有 `--timings=json` instrumentation 为测量基础。Hosted runner 的 wall-clock time 有意**不作为** correctness gate，但以性能为主要诉求的 PR 仍必须提供可复现的 before/after evidence。

> **已登记的 5.6 评估：**[#164](https://github.com/Iviesever/msvc-quick-build/issues/164) 中明确的预算与数值审阅门槛优先于下文的一般指引。冻结的 #188 累计预算已经消耗，采集成功不等于批准发布。已接受身份、未改阈值、保留失败和剩余风险条件见[候选发布边界](V5_6_RELEASE_BOUNDARY_ZH.md)。工作流按钮可用不意味着获得新的测量预算。

## 必需的 review evidence

对于主要声称“降低 build latency”或“提高 throughput”的 PR：

1. PR 标题使用 `perf:` 前缀，以激活仓库自动化的 performance-evidence contract；
2. 从精确的 PR base 构建 baseline MQB executable 并 benchmark；
3. 从 PR head 构建 candidate executable，并在同一台机器上 benchmark；
4. baseline 与 candidate 使用相同 iteration count；
5. 至少报告 `tests/native/benchmark_mqb.ps1` 产生的标准场景：
   - `cold`
   - `no-op`
   - `single-tu`
   - `public-header`
   - `build-run`
   - `link-only`
   - `discovery-cold`
   - `discovery-no-op`
   - `discovery-header`
   - `modules-cold`
   - `modules-no-op`
6. 在 PR description 或 review evidence 中附上或引用 comparison JSON/table；
7. 如果核心场景出现 material regression，要解释原因，不能用“本 PR 优化的是别的场景”来隐藏它。

标准本地比较命令：

```powershell
./tests/native/compare_mqb_benchmarks.ps1 `
  -BaselineMqbPath ./baseline/mqb.exe `
  -CandidateMqbPath ./candidate/mqb.exe `
  -Iterations 5 `
  -OutputPath ./benchmark-comparison.json
```

负 delta 表示 candidate 更快。Raw baseline 与 candidate benchmark record 会嵌入 comparison JSON，因此 phase timing 与 cache hit/miss count 仍可审计。

如果任一 benchmark report 削弱标准 evidence contract，`compare_mqb_benchmarks.ps1` 会 fail closed。每个 required scenario 必须在 summary 中恰好出现一次，必须报告请求的 sample count，并且每个请求的 iteration 都必须有一份 raw sample。允许额外 scenario，但删除或静默跳过标准 scenario 不能得到成功的 comparison。

## 自动化 PR evidence

`.github/workflows/performance-evidence.yml` 会对标题以 `perf:` 开头的 pull request 自动运行，也可以手动触发。因此，`perf:` title prefix 是普通 correctness CI 与强制 benchmark evidence 之间的 machine-readable boundary。

该 workflow：

- 响应 opening、synchronization、reopening、ready-for-review transition 与 title edit；
- 当已有 PR 被改名为 `perf:` 时，无需新 push 也会启动 performance evidence；
- 忽略无关的 body-only edit，避免重复运行昂贵 benchmark suite；
- 把 candidate 与精确的 `pull_request.base.sha` checkout 到独立 working tree；
- 获取一个 pinned historical MQB seed；
- 分别使用各自 tree 的 self-build script 与 manifest contract，从 base 和 candidate 独立构建 Release MQB binary；
- 在同一个 Windows runner 上按相同 iteration count 串行运行两套 benchmark suite；
- 在生成 comparison output 前验证 mandatory standard-scenario/sample contract；
- 把 comparison table 写入 GitHub Actions job summary；
- 上传 `benchmark-comparison.json` 作为保留的 review artifact。

Base 有意使用不可变的 PR base SHA，而不是会移动的 branch name。这样即使 `main` 之后继续前进，该 PR head 的记录性能比较仍可复现。

交替配对顺序可以减少、但不能消除顺序／缓存影响。这些结果是 review evidence，不是独立的因果解释；明确登记的数值发布门槛仍然适用。对于差距很小或结果异常的 case，应保留首批证据并检查 phase-level timing。新实验必须单独审阅假设、源码／程序身份、固定预算和停止规则；不得重跑至绿或用新结果覆盖首批结果。

## 什么被 gate，什么不被 gate

Correctness CI 继续 gate cache freshness、missing-output repair、scheduling bounds、dependency behavior、self-hosting、packaging 与 native tests。

Performance evidence 通常仍需**人工审阅**相关阶段、缓存及全部不利样本。明确预登记的外部 no-op 规则是硬性例外：候选减基线的配对耗时差中位数必须大于 **1 毫秒**，且各配对百分比变化的中位数必须大于 **10%**，同时满足即 HOLD。环境波动可以是假设，不能充当豁免或已证实原因。未越过这条门槛也不等于整体性能或发布批准。

Structural performance test 仍然有价值，例如它们可以证明“warm module build 启动零个 dependency-scan process”或“一个 logical worker 不创建 background thread”这类 deterministic property。它们可以补充、但不能替代 `perf:` change 的 before/after benchmark evidence。

## Benchmark 演进

当新优化针对标准 harness 尚未覆盖的场景时，应向 `benchmark_mqb.ps1` 增加 deterministic fixture/scenario，并让 comparator 自动消费它。额外 scenario 可以扩展 evidence set，但标准 scenario 是 append-only compatibility floor：不能仅仅因为一个新优化没有改善某个旧核心场景，就把该场景从标准集合中删除。

## 可归因 timing schema v2 与配对执行

`--timings=json` schema v2 完整保留 schema v1 的 `phases` 与 `cache` 对象，并增量增加两类不可混淆的归因数据：

- `attribution.wall` 是 project setup、artifact layout、toolchain discovery、target validation、CLI reporting 等墙钟区间；
- `attribution.work` 是可在多个 TU 间并行重叠的累计工作量，包括 inspection、execution、cache I/O、link resolution 与 filesystem snapshot。不得把这些字段相加成总和为 100% 的墙钟占比。

记录同时增加确定性总计数器和按域 breakdown：cache 打开次数/读取字节、filesystem snapshot 请求/唯一路径/证据复用、后台线程创建、MSVC 进程启动及输出行数/字节数。最终 timing 记录会在输出 observer 脱离后再写出，因此不会把自身计入 reporting 和 output counters。

性能比较改为交替配对执行（`baseline -> candidate`、`candidate -> baseline`，循环重复），报告 paired delta 中位数、paired MAD 和 nearest-rank paired P95。标准场景保持 append-only，并新增 129-TU 公共头 warm build、automatic 与 `-j 1`、timings enabled/disabled no-op 场景。

## 可执行的外部 no-op 验收

`tests/native/check_external_noop_gate.py` 不启动 MQB，执行当前 schema-v3、
19 场景／四配对的 CI 固定配置。它检查完整配对网格及四份 `external_stopwatch`
原始样本的顺序、时间一致性和内部向量缺失标记。使用精确有理数从原始时间重算，
不用四舍五入的摘要、两个独立中位数的比值、删除离群值或扣除观察开销。
固定配置演进需要显式修改相应契约；通用采集器支持的其他迭代数不属于本项 CI 验收配置。

```powershell
python tests/native/check_external_noop_gate.py benchmark-comparison.json `
  --output noop-acceptance.json
# 退出 0：未越过门槛；1：HOLD；2：证据非法或不完整。
```

输出必须是新文件。缺失、损坏、重复或自相矛盾的输入拒绝通过，并在能写入时保留
判定 JSON；不会覆盖旧输出。退出 0 不证明完整源码／程序来源或其他场景已经放行，
这些审阅要求仍然保留。

Performance Evidence 工作流保留原准入条件、四次迭代、采集器、摘要和身份检查。
门槛步骤位于身份核对之后、`always()` 附件上传之前，原样传递非零退出状态。
越界时仍上传原始输入和 `noop-acceptance.json`。这是失败保留接线，不保证上传服务
永远可用；没有使用 `continue-on-error`。独立的 Performance Gate Contracts 工作流
在 Linux 和 Windows 上执行数值、命令行与工作流结构测试，不运行 benchmark。

对已保留的 ZIP，加上 `--artifact-sha256 <expected-digest>`，程序只读取唯一的
comparison 成员，不解包或执行其中的程序。提交的 `fixtures/pr207-external-noop.json`
只含四份历史外部行和来源，**不是完整报告**；测试里其他生成报告明确属于合成数据。
未改写的 #701 原附件
`940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3`
返回 HOLD（+3.0963 毫秒、+26.70024178525%）。接入门槛不修复该性能失败，也不改变
#207 的 HOLD；接入前历史工作流的绿色状态不会被追改为已自动执行的门槛失败。
