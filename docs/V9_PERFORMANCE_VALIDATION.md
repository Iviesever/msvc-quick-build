# V9 before/after performance validation / V9 接入前后有限性能验证

**Implementation only; neither preparation nor measurement is allocated.**
Design review: [#198 / 5825622787](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-5825622787).
This follows #223/main #840 correctness acceptance, not another acceptance of the
prototype. Do not click Run/Rerun merely because the files are installed or CI is green.

## Question and fixed inputs

Does the actual V9 production reader integration reduce the external latency of a
valid two-source default no-op, with all original trust, PATH, tool selection,
file age, stamp and freshness checks intact? A benefit is not assumed. This is
not a rerun of original #701, a cause/fix for #207, or a v5.7 release gate waiver.

| Role | Exact source commit | Source tree |
|---|---|---|
| Before | `ed76d13df14acd680d5b523a27aa52fc99a1b09c` | `3258bd2f21c3982b8d9705ee5d50e56a02de5c0d` |
| After | `fea79539ba3cdb55fddf36158877bae961627c01` | `c7382dd4f6b33fa687a31b1689d199e23a120491` |

The harness's separately reviewed commit is another identity; it must match the
manual workflow/checkout and remain the same across both phases. A/B are not
rebased or rebuilt from whatever happens to be current main. The fixed source
commits/tree IDs are checked before and after preparation, with clean tracked
files and identical VERSION/production manifest/build policy. Original product,
collector, gate, #701 inputs and consumed workflows are unchanged by this work.

## Two phases, with an actual binary-freeze boundary

One **manual-only** workflow, `v9-noop-validation.yml`, has two modes. There is no
`push`, `pull_request`, `workflow_run`, schedule or automatic dispatch on this
workflow. Separate contracts CI uses only synthetic inputs and in-process doubles.

**Preparation, run number 1 / attempt 1.** After its own registration, build both
fixed sources on the same disposable hosted Windows x64/PS7 machine. Reuse the
pinned `acquire_seed.ps1`, `build_mqb.ps1` and each source's layout checks; fresh
build directories, Release/MT/C++23 and identical 5.6.0 version definition. The
seed archive's existing SHA check remains. No self-host-generation substitution.

Preparation ceiling is **five MQB invocations**, not two: one seed `--help`, two
seed build invocations and one `--help` of each produced program. The three helper
admissions record ceilings 1/2/2 before entering the unchanged helpers. Completed
fixed helpers support the five-call count; a failed helper's admitted ceiling is
not an exact native process census. Compiler/linker children are build work, not
hidden measurement samples. Git, gh, Python and two vswhere inventory queries are
auxiliary operations, not MQB calls; no target fixture is primed in this phase.

Save source archives, raw build logs, executable hashes, seed identity, environment
before/after and `manifest.json`. Original logs and partial prefixes stay on
failure. **Review this original artifact and freeze its run ID, artifact ID, ZIP
SHA256, exact manifest SHA256 and both binary SHA256 values before measurement.**
Preparation success cannot dispatch or authorize phase two.

**Measurement, run number 2 / attempt 1.** A separately registered execution uses
that exact ZIP and manifest. It does not build A/B again or probe them with
`--help`. It checks the original preparation request/phase/run, same harness
commit, all input hashes and completed preparation ledger before the first prime.
The original A/B files are pinned read-only during the group.

The measurement machine is a different hosted allocation. Both A/B measurements
are on that single machine. Its image version, PowerShell version, installed MSVC
key-file inventory and SDK version directories must match preparation. A mismatch
refuses before study calls; it is not permission to retry hosts until one matches.
`cl.exe`, `link.exe`, `lib.exe`, `c1xx.dll`, `c2.dll` hashes cover key tool identities,
not the entire compiler/DLL/header/library/OS closure or a continuous file lock.
A/B use the same seed/selection options on the preparation host; no per-compiler-
child provenance attestation is claimed. PATH is hashed, not printed: it must stay
constant within each host but can differ between allocations. Developer-shell
variables and ambient compiler option variables are refused, not silently unset.

The reserved labels are `v9-reader-prepare-001` and `v9-reader-noop-001`. They are
**not authentication of an issue approval**. Source/run/event/host checks prevent
accidental reuse; governance still requires the two separate registrations. A
failed first attempt consumes its phase opportunity. No retry/resume, new alias,
new commit name to get favorable results, or filling unused calls.

## Fixed single-scenario plan and bounds

Four ordered pairs: **A/B, B/A, A/B, B/A**. Each of eight independent equal-content
fixtures gets an adjacent **prime -> final no-op**, using the same two original
source strings and arguments:

```text
main.cpp helper.cpp --output timing_bench -j 1
```

Budget **16 study MQB calls = 8 primes + 8 finals**. No mid-call adaptation,
unlisted warmups, target-output execution, U/T variants, 19-scenario comparator,
ETW/WPR, priority/service/console change, compensating sleep or overhead deduction.
After the complete group the history would be 120+16=136 study calls; the five
preparation MQB calls are listed separately, not silently omitted or pooled with
old studies. Until actually executed, both totals are proposed, not observed.

Only original `Invoke-LegacyBoundary`, `Get-FileManifest`, `Get-Digest` and
`Write-NewJson` definitions are imported through the AST, not the old script entry.
The unchanged gate's `evaluate_pairs` is reused directly. The old full comparator
is unsuitable here because it requires all 19 scenarios. No old checker is
monkey-patched to accept a different A/B identity.

The manual job ceiling is 30 minutes: preparation step 25, measurement step 10.
Preparation requires 8 GiB free at admission/build boundaries; measurement 4 GiB
at each call boundary. Each fixture has a 64 MiB post-call checkpoint (eight
admitted fixtures at most 512 MiB), inherited 2048-file/256-directory/per-file
bounds, and the downloaded bundle has 64 MiB compressed/128 MiB expanded limits.
These are **admission/checkpoint limits, not a hard filesystem quota or a bound
on a synchronous inherited launcher call**. Hard timeouts, disk/runner loss and
journal failure can prevent cleanup/upload. No global process cancellation is
added. First detected call, metadata, identity or journal error stops the prefix.

Before/started/result/after records are written in order. Results are saved before
semantic checking. The next call cannot start after a known error. Every final
must exit normally, show the exact two-source compact no-op output, make no
compile/link, and leave the observed file manifest unchanged from the prime.
Each prime must produce the V9 auto/x64/x64 cache, select an inventoried tool root,
and match the other fixtures' full cache byte digest. This is an observed cache
identity check, not a replacement for production trust/freshness logic.

There is no extra process probe between a prime and final: only the registered
file/cache metadata and journal operations. Python/gate execution and host
inventory queries occur outside the sixteen-call group. File hashing itself can
warm filesystem state; this is the fixed observation policy, not an unperturbed
replay or an estimate of its removable cost.

## Privacy and interpretation

The public artifact allowlist uploads call JSONs, cache **projections**, source
archives/build logs and preparation binaries. It never uploads fixture/cache
contents, PATH/environment values or a full environment dump. Tool installation
paths and ordinary output lines remain visible. Raw caches only exist in the
temporary runner workspace and are **not retained as a private downloadable
artifact**. Consequently later reviewers can replay manifest/digest checks, not
independently reconstruct private cache values from this export. No actual user
cache, old original EXE or system-level ETL is distributed or executed by CI.

Primary metric is the inherited `native_start` to `native_end` QPC envelope;
`outer_start` to `outer_end` is separately reported. Neither is root process
lifetime nor pure CPU time. Preserve all integer QPC endpoints and the frequency.
An exactly representable Decimal conversion feeds the unchanged rational gate;
unsupported nonterminating units fail before the group, never round across a
strict boundary. Clock descriptions: [Stopwatch.Frequency](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.stopwatch.frequency).

The unchanged rule takes the **median of four pairwise B-A milliseconds** and the
**median of four pairwise `(B-A)/A` percentages**. It fails only when both are
strictly greater than 1 ms and 10%. No ratio of pooled medians, no rounded inputs,
no dropped samples and no 19-scenario placeholder data. Boundary/absence tests
exercise the actual inherited evaluator. A crossed gate saves `regression_hold`
and causes a failed workflow exit. Invalid evidence also fails with its prefix.

When the gate is not crossed, four negative observed differences are labeled
`limited_observed_improvement`; every other combination is `benefit_unproven`.
Even four negative signs do **not** establish statistical significance or
population equivalence. Scope is one scenario/limited host and build cohort,
including source-layout effects; no function-level causal allocation is proven.
All outcomes retain `clears_hold=false`, `cause=null` and no release acceptance.

Regression calls for pausing promotion and reviewing revert/correction, not
rerunning to green. No demonstrable benefit calls for saying so rather than adding
complexity or samples automatically. An observed improvement still cannot erase
original #701 +3.0963ms/+26.70%, #207 HOLD, other adverse scenarios or missing Rium
qualification. No new observation/persistence/clean/prune/deletion authority.

## Current handoff

Accept this exact implementation's first contracts and applicable ordinary
regressions, review it normally, then accept resulting main separately. Only then
may a preparation opportunity be registered. After preparation's own evidence
review/freeze, register measurement. **Neither phase has been run or allocated by
this implementation PR.** Manual trigger semantics: [GitHub events](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_dispatch).
