# v5.5.0 cumulative evidence and release boundary

**English | [简体中文](V5_5_CUMULATIVE_EVIDENCE_ZH.md)**

## 1. Frozen scope and exact identities

v5.5.0 freezes the accepted warm-path series: attributable timings/counters,
target-wide filesystem evidence reuse with a post-execution revalidation barrier,
inspect-then-execute-misses scheduling, bounded compile-cache reading, default
report summaries/batched output, and bounded archive-cache read/write transport.
Earlier mainline improvements after v5.4.0 are included too. The cumulative
result is not the isolated effect or sum of PRs #157, #159 and #160.

Rejected link/discovery reader PR #158 and toolchain reader PR #161 remain
excluded. No further optional product optimization is admitted in the release
preparation. Object handoff has no established freshness-equivalent, net-saving
late observation design; link-resolution reuse and multi-key discovery lack
sufficient residual evidence. Cache packs still require isolated residual I/O
of at least 25–30%, not overlapping work counters. Daemon/watcher/USN/residency
remain v6.0 work.

| Identity | Exact value |
|---|---|
| Released v5.4.0 source | `d041668de836b9eb9a36e2d6b96ff2114c5c358a` |
| Baseline source tree | `555ebdb27ce8b7c379b03154578f55585601f905` |
| Measured accepted product | `6f24bb4d2f5e323eb7b3823c3acf367dcccf9446` |
| Candidate source tree | `00c5b8e600ca188d3ce6158f002390307ee3c887` |
| Evidence harness commit | `f5303c351d983215827483ce9465e4c5d9100e54` |
| Evidence-only #162 merge | `9d6318b563c53c9b2795205c008f63744ebdb7e5` |

The measured candidate still reported VERSION 5.4.0. The separate release PR
changes VERSION to 5.5.0 and documents this result without changing C++ source.
It is not a new benchmark of the final versioned binary. A future product change
would require new evidence rather than inheriting these measurements silently.
This record does not establish publication; consult GitHub Releases for that state.

## 2. Compatibility and correctness boundaries

Default output now summarizes reused translation units before constructing their
individual display labels. A fully warm explicit-source 129-TU target without
PCH, warnings or discovery prelude has three result lines instead of 131:

```text
[up-to-date] 129 translation units
[up-to-date] report.exe
output: <artifact path>
```

Scripts parsing individual cached-source lines must request `--verbose`.
Recompiled sources, rebuild reasons, all warnings and tool diagnostics, final
artifacts and run behavior remain visible. PCH progress remains separate.
Ordinary/module/static targets share this policy; static compile-cache warnings
and nested compiler failures are no longer omitted. See [reporting](REPORTING.md).

Archive v1 bytes and object ordering stay compatible for accepted records, but
64 MiB is now the total read/write limit, including escapes and delimiters.
Exactly 64 MiB is accepted. An older oversized record falls back to a warned
re-archive; an oversized new save preserves the previous cache and does not make
an otherwise successful build fail. The unchanged source/object freshness,
trust checks and conservative retries are not traded away for benchmark scores.
These limits are not whole-process memory caps or atomic filesystem snapshots.

Current timing JSON uses schema 2. Historical schema 1 has no counters or
attribution; absent fields remain unavailable, never zero. Phase boundaries
changed, so internal totals are not interchangeable. `work.target_reporting`
includes formatting and nested writes and overlaps `wall.reporting`; do not add
them or use cache-read work (which includes decoding) as elapsed I/O share.

## 3. Fixed cumulative experiment

[Release Cumulative Evidence #1](https://github.com/Iviesever/msvc-quick-build/actions/runs/34187064173)
builds both unmodified source trees using the same checksum-verified historical
MQB seed and Windows toolchain, with each tree's build driver. This compares
**rebuilt release source**, not old downloaded packages against newly built code.
The environment was Windows Server 2025, `win25-vs2026`, image `20260824.214.3`.

All 584 scored pairs use timings OFF, common A/B fixture/cache paths and external
process start-through-completion/pipe-drain time. Twelve warm scenarios have
40 alternating AB/BA pairs, four rebuild scenarios have 20, and four project-cold
scenarios have six. Project-cold resets `.mqb`; it does not evict operating-system
caches. Mutations use equal source bytes within a pair and actual advancing
write timestamps, not future timestamps. No sample or adverse run is discarded.

The predeclared practical flag requires a paired median above the greater of
0.5 ms (warm) / 5 ms (otherwise) and 3% of the baseline median, plus a wholly
positive deterministic 5,000-resample paired-median interval. It is a review aid,
not automatic publication approval or a proof of no regression. Serial
correlation and small cold samples constrain inference. Pipes are not terminals;
these synthetic fixtures are not an Unreal Engine or arbitrary-workload guarantee.

Separate unscored audits check expected cache hits under schema 1 and 2. Warm
scopes keep full cache/artifact metadata inventories and stderr unchanged, with
no build-action lines; current audits additionally require no build tools/cache
writes. Historical counters cannot prove processes they did not measure.
Rebuild scopes, missing-output repair, compiler failure/exit 4, recovery and
produced-program execution are checked without weakening freshness.

## 4. Complete recorded result

Each delta is the median of paired candidate-minus-baseline values (negative is
faster), not a difference or ratio of independent medians. All rows use the same
timings-OFF external clock; rows are not pooled into one score.

| Recorded scenario | Pairs | Delta ms | Delta percent | Faster pairs |
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

The main demonstrated benefit is large-target no-op latency. Shared-header
default, shared-header verbose/j1, private-header and static-default targets are
faster in all 40 pairs. Small cases improve less in absolute time; build-then-run
is effectively neutral. No percentage is added to an earlier PR's percentage.

## 5. Known adverse results and release risk

**Cold-build tails are unresolved, even though no practical flag fired.** The
shared129 project-cold case has two large adverse pairs:

| Pair | Baseline ms | Candidate ms | Difference ms |
|---|---:|---:|---:|
| 1 | 5026.8598 | 10380.6252 | +5353.7654 |
| 5 | 5059.0821 | 11539.5039 | +6480.4218 |

Its near-neutral median does not negate those observations. With six pairs,
P95 is the largest sample and the paired-median interval is very wide:
[-62.99515, +5917.0936] ms. This timings-OFF experiment does not identify an OS,
toolchain or scheduler cause. None is asserted. Publication must explicitly
accept this disclosed limitation or defer for a separate investigation; passing
correctness/package CI does not resolve it. Do not advertise cold acceleration.

Public-header rebuild and missing-executable repair have slightly adverse
medians. Executable single-TU rebuild has MAD 98.86185 ms, an interval spanning
[-111.7721, +77.4873] ms and only 10/20 faster pairs, so its favorable median is
not a stable-gain claim. Static rebuild includes a +91.9177 ms adverse pair.
Static verbose has two slower pairs. Keep these observations alongside the gains.

## 6. Retained evidence and reproducibility

The [raw artifact](https://github.com/Iviesever/msvc-quick-build/actions/runs/34187064173/artifacts/10041161376)
contains invocation transcripts, audits and inventories. Its ZIP SHA-256 is:

```text
d508c45fcab94ec9e20ec7e05b6dc047c5fc303f450171ac8581e2075fc733a5
```

Artifacts have finite retention (this run requested 90 days). Preserve a copy
before expiry. The repository retains a [lossless numerical projection of all
584 scored pairs](evidence/V5_5_CUMULATIVE_SAMPLES.json), with original identities,
clock definition and pair order. It is not a new run or a replacement for raw
transcripts. It retains both cold outliers and permits numerical recomputation
after the hosted archive expires. From the repository root:

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

The complete-run review checked 1,446 recorded calls, 2,892 raw transcript hashes,
12 equal warm before/after inventories, all 20 statistics/flags and 258 timing
audits (129 of each schema). Recorded binary hashes, cross-checked with the
workflow manifest, are:

```text
baseline:  bad0124f1c6f549a794eb58c5442428c581d6aeb80c1dd304158de9394728c28
candidate: b937f84c91eadc809fd33373eed83f1fca803365683d0518190cb9c16b6fea4f
```

The benchmark binaries were not archived; these are recorded identities, not
locally recomputed hashes of retained binaries. Mutation source hashes are
runner declarations, not archived source bytes. Final fixture-program checks
passed but their transcripts are outside the 1,446-call recorder. These limits
must remain visible when reusing the evidence.

## 7. Final release gate, not another optimization round

The cumulative evidence PR passed Native C++ #688, full Native Release #588
(including self-host/package validation), Documentation #147 and cumulative #1.
It changed no product source or VERSION and was merged as #162. Those 5.4.0
validation results are not relabelled as validation of a 5.5.0 package.

The separate release-preparation PR must validate embedded 5.5.0, all native
shards, Stage 1/Stage 2 self-host closure, the runtime-only package, checksum,
installer lifecycle and exact packaged-binary identity. PR success is distinct
from publishing the exact final main commit. With the existing workflow,
**merging the VERSION change into main activates publication** after the full
main-commit gate passes. This is an intentional final approval boundary, not a
harmless documentation-only merge. Do not create/move a tag or publish a PR ZIP
as the final main artifact manually.

[The release contract](SELF_HOSTING_EN.md) generates GitHub Release notes from
merged history rather than maintaining a parallel source-tree changelog.
The release PR description should foreground the logging compatibility change,
archive size boundary, scoped warm gains and unresolved cold tails. No workflow,
installer, compiler flags, product source, or freshness policy needs modification
merely to prepare this release.
