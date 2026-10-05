# Same-invocation module target cache evidence

[中文](MODULE_TARGET_CACHE_EVIDENCE_ZH.md) · [Lower wave contract](MODULE_CACHE_EVIDENCE.md)

## Contract

`MsvcModuleTargetCoordinator::run_recorded` now returns an owning
`ModuleCompileWaveCacheEvidence cache_evidence` beside its existing `result` and
`record`. The compile stage calls the existing lower `run_recorded` once and moves
that returned value through private optional recording storage. It is returned
only after the original whole target succeeds and all required stage values are
present. Empty/missing evidence is not manufactured to stand in for a lost value.

`compiles` follows the actual prepared source order: requested sources first,
then the selected toolchain `std`/`std.compat` providers. `header_unit_compiles`
follows the dynamically prepared HU order. Both align with their existing wave
results and projections; HUs have no fabricated scan. External provider files
remain references rather than invented locally produced cache entries.

## Preserved behavior

No additional cache reads, scans, signatures, compiles or links are introduced to
recover evidence. Default `run` still passes a null recording pointer; `inspect`,
CLI, routers, ordinary/static targets and default adoption are unchanged. The
original scan/graph/compile/link errors propagate; a failed target publishes no
successful partial cache record. Already written files are not rolled back.

Existing stage results, caller label, projections and typed cache save warnings
remain. The new owning value does not promote the legacy exact-capture flag or
producer/current-content/inventory/deletion authority. It is not a durable
producer identity, persisted history, writer lease, or permission to clean files.

## Validation

The existing ten mock and ten native target invocations remain, with all original
assertions and tool budgets. Their successful cold, warm, shared-output, repair,
configuration and injected-provider cases now check every attached source/HU
value against its matching record and same-call retained cache bytes. Warm calls
check one accepted compile-cache payload open per actual node and zero writes;
counters exclude test-only serialization. Stage failures remain failures without
successful evidence files. Earlier cold/warm and standard-library cache values
are serialized again after later overwrites and failures to check value ownership.

The portable suite reverses only the exact additive test extension and verifies
the old program's entire blob, preserving old guards rather than removing them.
It also rejects copied/default/missing evidence, duplicate calls, new I/O, changed
failure guards and default adoption. These source/serialization checks are not
MSVC execution; the candidate's own Windows artifacts are required separately.

## Cost and release boundary

The pass-through moves the already-created vectors instead of copying or
reloading them. They now remain alive until the caller releases the successful
target value; failure retains them until the invocation unwinds. This can increase
peak/resident memory compared with previously discarding the lower payload.
Existing explicit request/toolchain copies and allocation costs remain unmeasured;
no zero-cost, no-regression or default/high-frequency-use guarantee is made.

This is a single implementation step for #198, not v5.7.0 publication or full Rium
qualification. Existing adverse performance evidence, consumed experiments and
persistence/writer/cleanup restrictions remain. The candidate requires its own
functional acceptance and independent integration/performance decision.
