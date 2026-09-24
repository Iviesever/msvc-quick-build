# O-only N/P observation-policy diagnostic / 无跟踪与采样策略对照

Implementation follows [the static follow-up proposal](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-5808350978)
and [implementation-only registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-5810554897).
**Not allocated. Do not run the manual workflow until exact-head and resulting-main
acceptance plus a separate execution registration.** Installing its reserved label
`701-noop-profile-policy-001` is not permission and does not read an issue for authorization.
#207 remains Draft/HOLD; original #701 +3.0963ms/+26.70% is not waived.

## Question and fixed plan

Only original #701 artifact10675079360/run35681224762 is accepted by the existing
archive verifier. Original A/B executables, source fixtures, argv, PowerShell
launcher, console/output semantics and timings-off calls stay unchanged. No build
of replacement A/B, execution of the fixture output, or U/T middle call.

Each cell executes **prime-off -> final-off**. N creates no *study-owned* ETW
session; this does not certify absence of unrelated OS tracing. P starts a unique
owned session after its prime/checkpoint and records only its final. The exact order:

- Block 1: N/A, N/B, P/A, P/B.
- Block 2: P/B, P/A, N/B, N/A.

Ceiling: **16 calls = 8 primes + 8 finals**, 4 P windows, 4 traced process instances,
8 markers and 20 session-control commands (4 starts, 8 markers, 4 status, 4 stops).
There are additionally **two read-only `wpr -profint` queries**, before the first
prime and after the last cell, so the total WPR-command ceiling is **22**, not20.
They save verbatim interval output and query QPC endpoints; no set/reset or global
frequency changes. These observations do not prove the interval stayed constant
throughout sampling. Decode the actual recorded sampling information separately.
History remains104 recorded calls; proposed cumulative120 is not execution.

The factor is the **entire observation strategy**, including start-induced delay
and host effects, not an isolated sampling tax, layout fix or unperturbed replay.
Keep both directions and every adverse result. No compensating sleep, changed
console/redirection/priority/services, warmups, adaptive repetitions or19-scenario
benchmark. Do not pool with earlier experiments, subtract observer overhead, or
claim equivalence from two pairs per policy. No automatic next experiment.

## Implementation and refusal boundaries

`noop_profile_policy.py` uses the existing archive verifier and call/manifest
checks. Source snapshots retain the original launcher, owned control and window
helpers by their canonical hashes. The new PowerShell entry imports **definitions
only** with the AST; it never executes an old collector entry. The original
row validator is enclosed by the new16-call ceiling. The old files/profile/manual
workflows and product are unchanged. N has `window=null` and no trace directory.

P adds strict `SampledProfile`, `DPC`, `Interrupt` keywords and SampledProfile stacks
to the previous scheduler/file-I/O profile, in a separate profile file. The old
profile hash is unchanged. Strict keywords/parse success do not guarantee usable
samples or schema support on the later capture host.

Retain256MiB sequential kernel-file and merged-segment limits,512MiB trace/temp
checkpoints,4GiB free admission/boundary checks,15-minute entry step/20-minute job
on a disposable GitHub-hosted Windows x64/PS7 runner. These are not hard disk or
synchronous-call deadlines. First call/control/journal/space error stops the plan;
only an owned instance receives one stop attempt. A failed/uncertain stop, even
a successful native stop whose journal failed, prevents the next N or P prime.
No outer retry, global cancel, deletion, prefix refill or resumed directory.
Hard timeout, runner/disk loss can defeat evidence saving and cleanup.

The manual wrapper preserves allowlisted request fields before checkout/download,
uses environment data rather than script interpolation, checks exact main/event/
workflow/profile/host/run1/attempt1 and invokes the entry once. Read-only token,
pinned actions, no persisted checkout credentials. Only the execution directory
is uploaded, including failure evidence. ETW is system-wide on this disposable VM;
raw ETLs require separate privacy review before redistribution. Download preceding
capture is not a secrecy guarantee. Never use a personal desktop/self-hosted runner.

## Evidence interpretation and tests

A complete journal remains `policy_journal_complete_trace_unreviewed`,
`trace_health_verified=false`, `cause=null`, `clears_hold=false`. It is not an ETL
health or performance pass. Record each of the four P traces independently: loss
fields, schema/version/timebase, two actual markers, exact final process and all
its thread identities, per-thread samples and matching stacks, module+RVA identity,
and interruption records. No samples is **inconclusive**, not no execution. One
sample permits description, not precision. Missing schemas/stacks remain missing;
CSwitch/wakeup stacks are not substituted for CPU sample stacks. No original PDB
exists: do not invent function names from unrelated symbols.

`assess-samples` is only a fail-closed guard on a **separately produced/reviewed
numerical summary**, not an ETL decoder or authenticator. It rejects malformed,
missing/reordered-window or duplicate-thread reports; missing loss/schema/clock/
interval/identity/coverage fields,0 samples or unmatched stacks remain inconclusive.
Even a complete supplied summary is only `descriptive_samples_only` with the same
false health/HOLD flags. It is not used to automatically stop at the instant of
lost events or to certify later attribution. No broad decoder is introduced here.

CI tests synthetic preparation/journals/absence guards and actual extracted
PowerShell policy/owned-control/wrapper functions with in-process fakes, plus
Windows WPR profile enumeration. It never executes the manual entry or starts a
study session. Native/Release regressions are ordinary correctness checks, not
new study allocations. Their exact candidate and main results must be accepted
before a separate execution freeze. No version/release/default observation,
persistence, clean/prune, Rium qualification or deletion authority is conferred.

Primary references: [keywords and Strict](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/keyword--in-systemprovider-),
[WPR interval queries](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options),
[sequential limits](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/maximumfilesize).
