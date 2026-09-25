# Single-job V9 validation: new A2/B2, not a retry

**Implementation only. No execution allocation is granted by this document,
workflow installation, a green contract job, or the reserved allocation string.**
Reference disposition: #198 comment5835549462 (002 refused before any MQB call).

## Deliberate approval-boundary change

The previous preparation and measurement were separate hosted jobs. The second
job did not match the first image/PowerShell/key-tool files. Its refusal was correct.
This protocol does NOT delete those checks or attempt repeated hosts. Instead it
builds independent **A2/B2 in the same job that measures them**. Human review of the
exact protocol/code precedes allocation; immutable manifest checks automatically
admit the intermediate pair; human artifact acceptance follows execution. There
is no promise of human inspection between those steps. A future dispatch requires
an explicit `accept_new_binaries_and_automatic_gate=true` as well as the separately
reviewed commit and allocation. The reserved name `v9-reader-samejob-001` alone is
not an authenticated approval record. Keep the manual workflow unrun until exact
candidate/main acceptance and separate registration.

Use the fixed original sources: A2 from ed76d13df14acd680d5b523a27aa52fc99a1b09c,
B2 from fea79539ba3cdb55fddf36158877bae961627c01. These are not the execution
harness commit, and no old development package is substituted. Original A/B,
prepare36106155413/artifact10852045267 and its19members are immutable history.
001/run36107949309 remains1prime/0no-op/0pairs;002/run36156676599 remains0calls.
The new binaries get their OWN preparation/manifest and SHA256s even if their
bytes happen to match an older pair. Do not overwrite or relabel old evidence.

## Fixed phases in one job

1. Preserve allowlisted request; require exact reviewed main/checkout/workflow,
   hosted Windows x64, job `validation`, this workflow's run1/attempt1, and explicit
   consent. Check inherited full-source pins and exact clean A2/B2 source commits.
   Source checkouts are done once; save execution source via git archive.
2. Call the unchanged preparation helper with fresh directories. It records E0,
   downloads/verifies the fixed historical seed, runs its help once, and builds
   each fixed source in Release with the same seed and original helper. Each
   build/helper includes one help check: **at most5 preparation MQB invocations**.
   It saves source archives, full logs, helper-start/finish records and E1. Check
   exact clean source again after building. Preparation admission counts are
   helper ceilings; failed/incomplete helpers are not a complete process census.
3. Hold read-only binary handles and write the new immutable manifest. The Python
   verifier checks17 preparation files, both exact Git trees reconstructed from
   raw source ZIPs, all helper completions, fixed seed identity, both program
   hashes and E0==E1. `ready.json` and the measurement plan are created only after
   a second manifest verification. No old request or environment is spoofed.
4. Still in the same PowerShell process/job, record E1b and require equality with
   the prepared environment, INCLUDING the host-local PATH hash. Use the exact
   retained launcher, corrected CLI cache projection and16-call loop: **4 pairs
   AB/BA/AB/BA,8 fresh directories, each prime then no-op**. No download, install,
   rebuild, help, warmup, cooling sleep, trace, priority change or adaptive sample
   follows readiness. The original fixture bytes and four QPC endpoints remain.
5. Record E2 and require equality again. Keep all records, binary pins until the
   loop finishes/fails, cache summaries and stopped prefixes. The evidence-only
   original rational gate is called after authenticating the new manifest/plan.
   Full environment snapshots are key-file inventories, NOT complete dependency
   closures or continuous locks. Source trees/build-helper identity do not provide
   every compiler-child command or DLL/SDK/header/library loaded by a process.

Same VM prevents the specific cross-job image mismatch, not scheduling noise,
CPU-state differences, background interference, or short-call bias. E0/E1/E1b/E2
are checks at boundaries, not continuous monitoring. Preparation warms the host;
this is a new experiment and cannot be pooled with earlier samples. Image/PS/tool
comparisons remain strict; they compare this NEW job to itself, not the old image.

## Limits and disposition

**5 preparation +16 study=21 maximum new MQB calls**, never refill a failed prefix.
Historical study121 and preparation5 stay separate. Only16 actual completed new
study calls would raise study history to137; only5 completed preparation calls
would raise preparation history to10. Old unused15/16/2 slots grant no authority.

Keep inherited8/4GiB disk admissions,64MiB source/binary/log/fixture checkpoints,
128MiB source expansion and the same no-op validity checks. The single combined
invocation has a35-minute step/40-minute job limit to contain preparation plus
measurement; these replace the old separate25/10-minute steps, not an adaptive
retry allowance. They are external timeouts, not synchronous-I/O deadlines.
Host loss, hard timeout or journal/disk failure can prevent final preservation.
Logs/results are never rewritten to success. The always-upload allowlist preserves
preparation binaries/source/logs and measurement JSON, but not fixture directories,
raw V9 caches or private environment values. New binaries are diagnostic inputs,
not release assets. The fixed seed acquisition is the only extra download within
the preparation helper and occurs BEFORE the intermediate manifest.

The primary metric remains the native invocation envelope, not CPU time; report
outer endpoints separately. Original strict-AND rule is unchanged: paired median
candidate-minus-baseline **>1ms AND >10%** means regression_hold and exit1. Invalid
or missing evidence means exit2. All four negative differences allow only
limited_observed_improvement; otherwise a noncrossed gate means benefit_unproven.
No case clears #207 or the original #701+3.0963ms/+26.70%, and none alone qualifies
v5.7.0 for release. Review keep/revise/withdraw disposition from actual results;
do not automatically collect more data. No product/VERSION/clean/prune changes.

## Correctness coverage, not performance evidence

Python controls exercise source trees (independently against git), distinct
execution/manifest binding, consent, budgets, history, failed/altered preparation,
strict gate boundaries, stopped prefixes, real001 metadata, source/cache/clock
and environment invalidation. Existing core and old workflows are untouched.

PowerShell controls extract the ACTUAL coordinator and reuse actual JSON/hash,
manifest and cache-projection functions. Only preparation, process launching,
source checkout and host discovery are simulated. The positive path executes the
REAL new Python freeze/ready/audit chain, using the old source ZIPs only as input
bytes, new non-executable synthetic binary contents, and synthetic call records.
Negative controls include failed build/freeze/ready, all environment boundaries,
missing CLI cache/fallback-only cache, failed no-op and journal loss. No real MQB,
compiler, WPR or manual entry executes in the contract workflow. Tests using the
old preparation perform a fixed read-only artifact download; they do not re-run it.

The manual workflow is separate from automatic contracts. Exactly-reviewed
candidate and subsequent main require their own applicable native/Release and
platform evidence before a future allocation. A green synthetic suite is not a
successful study or a complete Windows build/measurement-path proof.

Primary platform reference (checked2026-09-26): GitHub documents that all steps
inside one hosted job execute on its VM, whereas each job gets a new VM:
https://docs.github.com/en/actions/how-tos/manage-runners/github-hosted-runners/use-github-hosted-runners
