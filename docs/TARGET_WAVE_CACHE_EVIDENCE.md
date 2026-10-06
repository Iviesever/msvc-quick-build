# Same-invocation ordinary target compile-cache evidence

[中文](TARGET_WAVE_CACHE_EVIDENCE_ZH.md)

## Scope

The explicit EXE/DLL and static-library `run_recorded` results now own a
`TargetCompileCacheEvidence` alongside their existing result and artifact record.
Its `compiles` vector contains one `CompileCacheEvidence` for each original source,
in input order. Additional object inputs are not new local compile producers.
The terminal link/archive record and existing false physical-identity, current-
content, complete-inventory and deletion-authority flags are unchanged.

The shared `TargetCompileWave` captures only on the explicit recording path.
Default `run/inspect/CLI` and the separate admission-stop API remain non-recording.
No public reusable inspection ticket, another compiler invocation, cache reread,
reconstructed signature, second scan or alternative scheduler is introduced.

## Capture and private ownership

Small/forced targets use the existing lower `run_recorded` exactly once per
attempted source. Large targets still inspect all sources once and execute only
the compact miss list. The private inspection seam moves the actually accepted
cache entry on a hit and retains the original inspection toolchain context on a
miss. The private execution seam consumes that same request/inspection/context
and moves the actual generated entry and typed save failure, if any.

Each worker owns a preallocated optional slot. Slots are indexed by original
source, including noncontiguous misses. No worker appends to a shared result
vector. The owner collects values after the work joins, in original input order.
Copies of returned evidence own their request, options, toolchain and cache data;
references do not dangle after the target or its work slots are destroyed.

## Revalidation, retry and failures

The shared-filesystem revalidation barrier stays **after miss execution**. If it
rejects the shared snapshots, the original conservative whole-target forced wave
still runs. Starting that wave clears all prior capture slots. Final success
contains only its new values, never accepted entries from the invalidated wave.
Snapshots remain suspended during execution and cache sealing; original TLS
activation/restoration and scheduler stop rules remain.

A source failure preserves original source-order error selection. A terminal
link/archive failure does not publish partial successful cache evidence. Successful
compile with failed cache save still returns the actual generated entry, original
`save_failed` / typed error and warning. Missing internal capture is refused;
it is never synthesized as an empty successful record. Failure is not a promise
to roll back files already written by the original operations. Allocation failures
retain the existing exception behavior, rather than becoming false success.

## Validation and evidence limits

The existing **96** native test programs remain; no new executable is added.
A bounded block of **15** deterministic target invocations in the existing target
TU covers direct cold/hot/force, split inspection, controlled out-of-order
completion, noncontiguous misses, post-execution invalidation, cache save failure,
source/terminal errors, historical ownership, and both EXE and static owners.
Expected warm reads are exactly one successful cache load per source, zero saves.
The invalidation cases retain the original one miss plus a complete forced wave.
Raw counts are written before assertions. Test-only cache copies are serialized
**outside** measured collector scopes and are not attributed to product writes.

Existing real EXE/static artifact-record cases attach the same checks without
adding target/compiler calls. Their actual MSVC execution remains mandatory in
new candidate **Native** and **Release** jobs, with full original regressions,
self-hosting and package acceptance. Portable source contracts and any GNU/Linux
in-process mock run using an explicitly declared Win32 shim do **not** replace
Windows execution, DLL-specific timing, native side effects or CI acceptance.

Historical strict source checks use a whole-file pinned, exact inverse of this
extension. Both the current file and reconstructed old file must match, and new
helper assertions are pinned. A separate new suite checks the actual new code.
This is not an exemption for unrelated changes or permission to weaken old tests.

## Cost and release boundary

Explicit capture copies original request/toolchain data, retains cache payloads
through terminal work and until the returned object is released, and allocates
per-source slots. Retained/peak memory and copy/allocation/time costs are not
quantified. Default-off is not generated-code or zero-overhead proof. Independent
performance qualification and integration review follow functional acceptance;
no default/high-frequency use is approved merely by portable tests passing.

Existing performance failures, all adverse samples and consumed diagnostics stay
retained. This feature does not authorize persistence, a writer protocol, physical
producer identity, safe clean/prune, complete Rium validation, or a **v5.7.0**
release. Version and workflows are not changed by this implementation.
