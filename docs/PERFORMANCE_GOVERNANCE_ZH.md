# MQB 性能治理

**[English](PERFORMANCE_GOVERNANCE.md) | 简体中文**

MQB 的性能工作以现有 `--timings=json` instrumentation 为测量基础。Hosted runner 的 wall-clock time 有意**不作为** correctness gate，但以性能为主要诉求的 PR 仍必须提供可复现的 before/after evidence。

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

先跑 base、后跑 candidate 仍可能引入一些 operating-system cache/order bias。因此这些 measurement 是 review evidence，而不是 numerical merge threshold。对于差距很小或结果异常的 case，reviewer 应重新运行 workflow，或在本地复现并检查 phase-level timing，而不是把一个百分比当成绝对事实。

## 什么被 gate，什么不被 gate

Correctness CI 继续 gate cache freshness、missing-output repair、scheduling bounds、dependency behavior、self-hosting、packaging 与 native tests。

Performance evidence 是 **review gate**，不是 hosted-runner timing threshold。CI machine load、VM placement、antivirus activity 与无关 system noise 会让固定毫秒或固定百分比 threshold 很脆弱。Reviewer 应评估重复测量的 median，以及相关 phase/cache evidence。

Structural performance test 仍然有价值，例如它们可以证明“warm module build 启动零个 dependency-scan process”或“一个 logical worker 不创建 background thread”这类 deterministic property。它们可以补充、但不能替代 `perf:` change 的 before/after benchmark evidence。

## Benchmark 演进

当新优化针对标准 harness 尚未覆盖的场景时，应向 `benchmark_mqb.ps1` 增加 deterministic fixture/scenario，并让 comparator 自动消费它。额外 scenario 可以扩展 evidence set，但标准 scenario 是 append-only compatibility floor：不能仅仅因为一个新优化没有改善某个旧核心场景，就把该场景从标准集合中删除。
