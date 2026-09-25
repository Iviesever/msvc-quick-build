# Explicit V9 preparation/execution rebound / 显式版本衔接（未分配）

Implements the next boundary in #198 comment5830631792 after the accepted #225
validator repair. **This change is not an execution allocation.** Do not dispatch
until exact candidate and resulting-main acceptance, followed by a separate issue
registration of the new execution commit. No product changes or performance claim.

## Three distinct identities, two original opportunities left consumed

The preparation remains run36106155413/artifact10852045267 from harness
`194f3cad63a963c944b8b44f81a41a0ef7b22cb3`. Its exact 4,744,328-byte ZIP is
`b01cb62dafd12a1b5e3d812d6165a712042d7db8a5391f76f4680a20895f3282`; manifest is
`d9f4ce843b3696a037cbeea2028e12d26bf564aaf2398558622b86b6df3f4fe0`.
The new JSON identity inventory pins all19 original member digests, not just the
manifest. It includes the already accepted original source archives and binaries:

- A: product ed76d13df14acd680d5b523a27aa52fc99a1b09c, EXE7a97242e95dd6df43830553f42a2671b4b7636458f46648f543b4139971e7caa.
- B: product fea79539ba3cdb55fddf36158877bae961627c01, EXE657aba1114be4f250799d3880110f35f7c61d27786f78f8178a83a7f127e00d8.

Do not replace these with current development packages or rebuild either product.
The new execution has its own reviewed commit, workflow identity, run and source
ZIP. Preparation request/manifest are copied under explicitly named files **without
changing their bytes**. The stopped run36107949309/artifact10851277845 is also copied
unchanged; it still represents1prime/0no-op/0pairs and supplies no performance result.
No old request/environment is forged, no old admit function is monkeypatched, and
no original protocol or manual workflow is changed to admit run3.

`v9_noop_rebound.py` first authenticates the exact old19-member input and calls the
unchanged `check_prepared` without a fabricated execution request. Independently,
it validates the new request, source snapshot and plan. Only then can the shared
call audit run. The old `audit` retains its original admission/preparation/plan
checks; its evidence-only body is extracted verbatim into `audit_calls`, with a
source-body regression and old/new result comparisons. Both entry paths retain
stopped-input rejection before the inherited rational gate. The shared helper is
not a standalone provenance validator: callers must authenticate their own inputs.

## Reserved new entry, no automatic execution

`v9-noop-rebound.yml` has only workflow_dispatch, two inputs (`reviewed_commit`,
`allocation`), and a separate run1/attempt1 rule. Reserved label
`v9-reader-noop-002` does not itself prove an issue registration or grant execution.
Its immutable preparation run/artifact are not editable inputs. It cannot build,
start the old entry, auto-dispatch another workflow or refill a failed prefix.

If separately registered later:4pairs AB/BA/AB/BA,8new directories eachprime→no-op,
16calls maximum; original argv/two-source bytes/PowerShell output capture/four QPC
endpoints. This is a **new whole experiment**, not the missing15old calls. History
remains121 study calls plus5preparation calls separately; only16actual new completions
would reach137, not136. Earlier15and2unexecuted slots remain unused. No help, timing
instrumentation, ETW, sleep, console/priority/service changes, generated-target
execution, adaptive repetitions or retry-until-matching host.

The PowerShell entry imports only already accepted definition bodies for the
launcher, filesystem manifests, cache projection, call checks and16-call loop.
It does not execute the old top-level script or its preparation function. The
original source file and legacy helper digests remain pinned. It uses the actual
CLI cache `.mqb/cache/toolchain/vs-x64.cache`, not the bare-locator fallback.

Before the first prime, the recorded image/PowerShell/key-MSVC/SDK inventory must
match the original preparation. PATH hashes are host-local. Binary read pins,
8/4GiB admissions,64MiB fixture checkpoints, source/archive bounds and the original
30-minute job/10-minute invocation limit are retained. They are not guarantees of
continuous file identity, complete DLL/header/library dependency closure, hard
synchronous deadlines or evidence survival after runner/disk loss.

The original strict-AND rule (>1ms AND >10%) is unchanged. All pairs and both native
and outer timing envelopes are retained. Crossed gates fail; non-crossing is not
statistical equivalence. Even limited observed improvement cannot clear #207 or
replace original#701 +3.0963ms/+26.70%. No automatic extra samples or release waiver.

## Correctness evidence, not a new measurement

Python controls cover separate versions, unchanged original metadata, each input
member, old opportunity confusion, stopped evidence, missing/extra calls, source
bytes, final compilation, cache/environment absence and exact threshold boundaries.
Synthetic fixtures substitute only their own expected metadata in the test process;
there is no such override in executable admission. A separate CI step downloads
and hashes the actual original preparation, checks it without running either EXE,
and proves the original failed prefix still returns INVALID.

PowerShell controls exercise the actual new orchestrator plus retained call loop,
manifest/read/hash/projection on real **synthetic** files. Only the host/launcher
are faked. Controls include original missing-cache failure, pins released on failure,
extra call refusal, escaped/UTF8 roots, binary-LF expectations and no private-value
projection. Source tests bind the real C++ writer's quoted, UTF8-path, binary/LF
conventions; they are not a claim of executing the C++ writer in these new tests.
Existing production adoption and V9 reader differential tests remain separate.

CI archives only existing source, numerical metadata and synthetic controls. The
manual output excludes fixture directories, actual cache bodies and environment
values. The original prepared programs are read from their existing public artifact,
not copied into a new release. New native correctness/selfhost/package runs are
ordinary CI, not this proposed experiment. Exact-head and main artifacts must be
accepted before any new execution registration. #207 and v5.7.0 release remain HOLD.
