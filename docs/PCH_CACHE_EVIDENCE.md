# Same-invocation PCH cache evidence

[简体中文](PCH_CACHE_EVIDENCE_ZH.md)

## Explicit result, not authority

`MsvcIncrementalPchCoordinator::run_recorded` returns the existing `result` and
`record` plus mandatory owning `cache_evidence` (`CompileCacheEvidence`). The
legacy `PchArtifactRecord` remains a projection with
`exact_cache_entry_captured=false`; copying that projection alone cannot acquire
an entry. The attached evidence describes the cache accepted/sealed during this
invocation, not a producer identity, a current-content certificate, a complete
inventory, durability or deletion permission. All four authority flags stay false.

## One inspection path

On a warm hit, a private PCH-only handoff captures the value accepted by the
original compile inspection (including ordered include-root freshness). PCH uses
its original read-only result path; it does not call `run_recorded` again to
recover a cache value. No public or persistent execution ticket is introduced.

On a cold/repaired path, initial inspection, directory/creator materialization
and the original post-write compile recheck remain. Only that existing lower
`run` is replaced by its recorded counterpart. The evidence is the final accepted
or sealed value, not the pre-repair cache. Same-timestamp creator repair still
forces the final recheck; the evidence stores that effective forced request while
the existing PCH result preserves semantic `source_changed` diagnostics/warnings.
Failures return the original typed PCH/compile error without a successful record.
A successful compile with a cache save error retains the entry offered for saving
and the original typed error. There is no extra cache load, signature rebuild,
scan or compile for recording.

## Default path and costs

Default `run` uses `run_impl<false>`; public `inspect`, CLI and upstream target
callers do not opt in. The explicit path copies effective requests and inspection
toolchain context, and retains an owning entry. Environment values remain in
memory, including order/duplicates, and are not serialized. Copies/allocations
also occur on some failures; no zero-cost or bounded-memory claim is made.
Compile-time exclusion is not generated-code or runtime equivalence. New default
performance qualification and an independent integration decision remain required;
prior #241/#232 risk decisions do not automatically transfer. No performance
execution is registered by this implementation.

## Regression scope

The existing PCH E2E test still makes ten mock-backed API calls (five successes,
five expected errors, six fake processes) and six native PCH API calls (five
successes, one expected compile failure), plus its existing consumer build/run.
The original assertions, failure logs and legacy projection output are retained.
Added checks examine these same returns, paired outputs, effective options,
force flag, typed save failure, blocking bytes and old values after later failures.
Cold/reused returned cache values are copied by the existing codec **after** the
API returns into test evidence, not by product code. Warm calls require exactly
one successful payload open and no cache write; other original paths have fixed
upper bounds. Failed opens are not counted by this counter; source contracts
separately guard the inspect/recheck sites. Collection overhead is not a benchmark.

The exact test extension is reversed and both complete file fingerprints verified
before old whole-suite preservation checks. Portable source/synthetic contracts
and Linux syntax checks do not replace Windows Debug/Release execution. Native
CI retains success/failure evidence using its existing upload paths. No workflow,
96-program registration, product TU count, release version, persistence, writer
protocol, safe cleanup or Rium qualification is changed here.
