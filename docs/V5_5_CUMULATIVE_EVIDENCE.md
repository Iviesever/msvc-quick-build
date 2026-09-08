# v5.5.0 cumulative release-source evidence

## Decision boundary

This iteration changes only the evidence harness, a read-only workflow and this
record. VERSION remains 5.4.0. No product source, cache/freshness policy, output,
release metadata, tag or publication step changes. This is not the release PR.

The cumulative baseline is the actual released v5.4.0 commit
`d041668de836b9eb9a36e2d6b96ff2114c5c358a`, not a later commit that happens to
have the same VERSION. The initial accepted candidate is
`6f24bb4d2f5e323eb7b3823c3acf367dcccf9446` (tree
`00c5b8e600ca188d3ce6158f002390307ee3c887`). It includes accepted #157, #159,
#160 and earlier mainline improvements; rejected #158/#161 are not reinstated.

## Reproducible experiment

Release Cumulative Evidence checks out the harness, historical baseline and
accepted candidate separately. The same checksum-verified historical MQB seed
builds both unmodified product trees in Release mode on one Windows runner,
using each tree's own build driver and VERSION. This compares release SOURCE
rebuilt with a common toolchain, not a downloaded historical package against a
newly built binary. Exact commits, trees, binary hashes and runner identity are
retained. A manual run can select a future candidate without changing the fixed
historical baseline; the resolved candidate remains explicit in its evidence.

All 584 scored pairs have timings OFF, identical A/B fixture/cache pathnames and
external process start-through-completion/pipe-drain timing. The fixed matrix is:

- Twelve warm cases, forty alternating AB/BA pairs each: small default/verbose,
  common-header129 default/verbose/serial, private-header129 default, static129
  default/verbose, modules, PCH, genuine persistent discovery and build-then-run.
- Four rebuild cases, twenty pairs each: common129 and static129 single-TU,
  common129 public-header, and missing executable repair.
- Four cold cases, six pairs each: small, common129, static129 and modules.

Source mutations use identical bytes within a pair and actual advancing write
timestamps, not manufactured future times. Only the cold cases reset project
build state. Counts and orientation are fixed before execution; no adverse
sample is dropped and no run/mode/PR percentages are pooled or added.

Paired median deltas, paired-percent medians, MAD, nearest-rank P95, extremes and
a deterministic 5000-resample paired-median interval are reported. The practical
adverse flag is fixed: paired median exceeds max(0.5 ms for warm / 5 ms otherwise,
3% of baseline median), with the interval entirely positive. This is a review
flag, not automatic release authorization or proof that unflagged cases never
regress. Serial correlation and small cold samples limit inference. Pipes are
not a real terminal benchmark; six-pair P95 is the maximum observed sample.

## Schema and correctness

Separate, unscored audits read old schema 1 and current schema 2. Both must report
expected compile/link/archive hits. Only schema 2 provides tool/cache-write and
filesystem counters; missing historical data remains null/unavailable. Old
product code is never instrumented retroactively. Phase boundaries changed, so
internal totals are not the cumulative end-to-end comparison. Candidate-only
attribution may guide the remaining route but overlapping work times are not
elapsed I/O shares or evidence for a cache pack.

Warm cases require no build-action lines, identical stderr and unchanged full
project cache/artifact metadata inventories, saved before and after. Current
schema audits also require zero compiler/linker/librarian launches and no cache
writes. Old schema cannot directly prove zero OS processes; hits, output and
metadata are the available historical observations. Default report wording is
intentionally different after #159, so stdout is retained rather than forced to
match. No error diagnostic is hidden to pass a benchmark.

Rebuild scope and subsequent full hits are checked. Both binaries must preserve
visible compiler failure/exit 4 and recovery; produced fixture programs are
executed as a separate correctness check. Raw stdout/stderr, every invocation,
audit and completed scenario survive a later failure. A failed run has no pass
marker and is never represented as a complete release gate.

## Remaining route

Numeric results and acceptance belong in the PR evidence record after the run,
not invented in advance. If cumulative evidence is acceptable, freeze the v5.5
performance scope and prepare the independent release PR. Object snapshot
handoff still needs a freshness-equivalent late observation and net probe
reduction; link resolution and multi-key discovery still need measured need.
No candidate is mandatory merely because it appeared in the original list.
Cache packs retain the isolated residual-I/O threshold; daemon/watcher/USN
remain v6.0. Final VERSION/changelog or release-note preparation is separate,
and the exact release commit still needs the full native/self-host/package gate.
