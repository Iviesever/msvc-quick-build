# MQB Performance Governance

Performance work in MQB is measured against the existing `--timings=json` instrumentation. Hosted-runner wall-clock time is deliberately **not** a correctness gate, but a performance PR must still provide reproducible before/after evidence.

## Required review evidence

For a PR whose primary claim is lower build latency or higher throughput:

1. benchmark a baseline MQB executable built from the PR base;
2. benchmark the candidate executable from the PR head on the same machine;
3. use the same iteration count for both executables;
4. report at least the standard scenarios emitted by `tests/native/benchmark_mqb.ps1`:
   - `cold`
   - `no-op`
   - `single-tu`
   - `public-header`
   - `build-run`
   - `link-only`
   - `modules-cold`
   - `modules-no-op`
5. attach or quote the comparison JSON/table in the PR description or review evidence;
6. explain any material regression in a core scenario rather than hiding it behind the scenario being optimized.

The canonical comparison command is:

```powershell
./tests/native/compare_mqb_benchmarks.ps1 `
  -BaselineMqbPath ./baseline/mqb.exe `
  -CandidateMqbPath ./candidate/mqb.exe `
  -Iterations 5 `
  -OutputPath ./benchmark-comparison.json
```

`delta_pct < 0` means the candidate is faster. The raw baseline and candidate benchmark records are embedded in the comparison JSON so phase timings and cache hit/miss counts remain auditable.

## What is and is not gated

Correctness CI continues to gate cache freshness, missing-output repair, scheduling bounds, dependency behavior, self-hosting, packaging, and native tests.

Performance evidence is a **review gate**, not a hosted-runner timing threshold. CI machine load, VM placement, antivirus activity, and unrelated system noise make fixed millisecond or percentage thresholds brittle. A reviewer should evaluate repeated medians and the relevant phase/cache evidence instead.

Structural performance tests remain useful when they prove deterministic properties such as “warm module builds launch zero dependency-scan processes” or “one logical worker creates no background thread.” They complement, but do not replace, before/after benchmark evidence for a `perf:` change.

## Benchmark evolution

When a new optimization targets a scenario not represented by the standard harness, add a deterministic fixture/scenario to `benchmark_mqb.ps1` and make the comparator consume it automatically. Do not remove an existing core scenario merely because a new optimization does not improve it.
