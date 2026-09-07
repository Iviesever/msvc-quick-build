# Target reporting: v5.5.0 development

## Scope and baseline

This independent iteration starts on main `8d9008a6031755feccf15d72a52c9fad137b4ad9`.
It does **not** include unmerged bounded-reader PRs #157/#158. VERSION remains
5.4.0. No cache format, cache reader, signature, freshness comparison, scheduler,
object-evidence handoff, process invocation or release policy is changed.

## Output contract

Default successful target reports summarize cached translation units before
constructing per-source display paths or UTF-8 labels. For a fully warm 129-TU
target without PCH or warnings, the result report is:

```text
[up-to-date] 129 translation units
[up-to-date] app.exe
output: <artifact path>
```

A mixed build still prints every recompiled source, its rebuild reasons and all
tool output. Its summary counts only reused TUs. A zero-length compile list
never emits a bogus summary; singular uses `translation unit`. `--verbose`
retains each reused source line, ordering, and existing target details. Scripts
that inspect individual TU progress must request verbose output explicitly.
Ordinary/module targets keep safe project-relative labels; static targets keep
their existing basename labels. PCH progress remains a separate line.

All warnings, error expansion, rebuild reasons and compiler/linker/librarian or
executed-program stdout/stderr are retained. Shared reporting also repairs a
pre-existing omission: static targets now print compile-cache warnings as well
as archive warnings, and expands nested compile-failure tool output. Failure exit codes and build-then-run behavior are unchanged.

The two shared result-report functions belong to app/diagnostics, not to target
adapters or the core. Their fixed 16 KiB buffer never captures an unbounded second
transcript, changes global stream buffers, changes flushing for running tools,
or redirects stderr into stdout. They flush pending completed-report bytes before
crossing into warnings/tool diagnostics and at report completion. Short writes
preserve ostream failure state. A throwing/partial write is not retried by the
destructor, avoiding duplicate prefixes.

Process output forwarding now copies bounded spans rather than calling `put`
for every byte. It retains the old CRLF-to-LF rule, standalone CR/NUL/UTF-8 bytes,
and the final newline rule for nonempty output without LF. This is an output
transport change only; captured process results remain owned by the process layer.

## Measurement definitions

The existing schema-2 `attribution.wall.reporting` keeps its exact meaning:
underlying stream-buffer write/flush time. It does not suddenly mean formatting.
The additive `attribution.work.target_reporting` measures successful target
result-report loops, their label/format construction and forwarding, target
verbose preambles, and PCH result reporting. It does not include building,
executing the resulting program, all application setup/help, or all failure
expansion. Its nested write time **overlaps** `wall.reporting`; do not sum them.
Instrumentation-disabled scopes do not read clocks. The old binary lacks this
inclusive field: comparisons must leave the baseline value unavailable, not zero.
Neither a smaller write counter nor a lower instrumented runtime alone proves
an end-to-end speedup.

## Tests and independent evidence

The new native `reporting_tests.cpp` exercises exact default/verbose/mixed output,
129-TU one-batch reporting, Unicode/NUL/CRLF/chunk boundaries, stdout/stderr order,
static compile warnings, empty results, safe labels, bounded storage, partial
writes and throwing sinks. Existing freshness tests keep their per-source
assertions and select `--verbose` **after** explicit `build`/`run` commands but
before native argument tails. Generated programs' argv is not changed. The native
suite inventory becomes 78 tests; no product translation unit is added.

`tests/native/compare_reporting.py` supplements, rather than replaces, the
unchanged 19-scenario Performance Evidence workflow. Its Windows job builds both
exact Release trees with the same pinned MQB seed and records Git SHAs, binary
SHA-256, runner identity and all raw stdout/stderr. It verifies default and
verbose output plus mixed default rebuilds, output repair, run and compiler error
behavior. The 26-case matrix covers small, 129-TU, static, modules and PCH targets,
default/verbose, instrumentation on/off, scale j1, and separate inherited CI output.
Each case has 12 alternating AB/BA pairs on the same fixture/cache pathname.

Every case is measured with the same external process start-through-completion
and drain clock, including instrumentation-enabled cases. Warm checks require
expected hits, no tool launches, no cache writes and unchanged project artifact/
cache metadata. Non-output counters must match exactly between A/B. Timings-off
and inherited-handle attribution are unavailable, never fabricated zero. Verbose
human output and no-op stderr must match between binaries. Raw adverse samples,
per-pair deltas, paired-percent medians, MAD and nearest-rank P95 are retained;
12-pair P95 is still merely the largest observation, not a production tail bound.

**Captured pipes and inherited GitHub Actions handles are not an interactive
terminal or ConPTY benchmark.** No claim about interactive terminal latency is
made from this matrix. Real terminal profiling remains useful before advertising
such a benefit. Different modes and successive PR results must not be pooled or
added together. Native Debug/Release and self-host/package remain separate gates.

## Remaining gates

Read numeric evidence, not just green job badges. This PR cannot settle the
unmerged readers' timing-disabled/cold-path tradeoffs. Their exact-head evidence
must remain separate. Discovery's earlier no-op scenario still needs a genuine
persistent-hit fixture and invalidation classification before multi-key storage
is justified. Archive/toolchain transport, safe object handoff and cumulative
released-v5.4.0 evidence remain independent themes. Only the final release PR
updates VERSION/changelog; cache packs and v6.0 residency retain their agreed gates.
