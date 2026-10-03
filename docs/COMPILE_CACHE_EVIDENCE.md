# Same-invocation compile-cache evidence

[简体中文](COMPILE_CACHE_EVIDENCE_ZH.md)

## Explicit API and provenance

`MsvcIncrementalCompileCoordinator::run_recorded(request)` returns a
`RecordedIncrementalCompileResult`: the original incremental result and an
owning `CompileCacheEvidence` value. It is opt-in. Existing `run`, public
`inspect`, the invocation-owned target wave, and CLI callers do not request
records. `IncrementalCompileResult` and the public inspection structure do not
acquire a cache entry or an execution ticket.

On reuse, capture moves the very `CompileCacheEntry` accepted by the original
cache validator **and** the ordered include-root freshness check. On execution,
capture moves `executed->cache_entry` only after the existing module-scan sealing
and cache-save attempt. There is no additional cache load, signature
reconstruction, scan, compiler invocation or post-hoc sourceDependencies read
for the record. The existing scan-sealing implementation still performs its
original dependency read; this change does not remove or duplicate it.

The record owns the original request and the coordinator's inspection toolchain
context, including ordered environment values. Those values can be sensitive;
there is no product serializer or environment dump. A separately constructed
executor can have another toolchain. `inspection_toolchain` describes the
coordinator, while `cache_entry.toolchain` remains the executor's actual value;
the API does not conceal a mismatch or certify the external toolchain.

## Outcomes and limits

`CompileCacheEvidenceState` distinguishes `reused`, `saved` and `save_failed`.
Reuse performs no save. A failed save retains the exact entry offered to the
original serializer and the original typed `CompileCacheFileError`; the original
warning remains in the result. This is not evidence of durable persistence.
Planning/compiler failures return their original typed nested errors without a
success record. An inconsistent internal success lacking an entry is refused
with `cache_evidence_unavailable`, not repaired by another operation.

The opt-in path owns independent values, so later request edits, disk overwrites,
failed compilations and mutation of a copied record cannot rewrite an earlier
record. Allocation failure still propagates; this is not transactional rollback.
A successful record does not prove that a later failed compile left the files
unchanged.

`exact_cache_entry_captured` describes the API's capture, not a trust credential
for caller-constructed aggregates. Producer identity, current contents, complete
producer inventory and deletion authority remain statically false. The cache
format, source/output order, duplicate metadata, optional scan evidence and
existing warning/error behavior are retained. There is no default adoption,
physical identity check, cross-invocation generation store, writer lease,
retention policy, persistence layer or clean/prune permission.

The older PCH/module/target artifact-record APIs still use their existing paths;
their `exact_cache_entry_captured=false` flags remain correct. This bottom-layer
API is a prerequisite for a separately reviewed future connection, not an
implicit upgrade of those records or of the generation model.

## Tests and retained evidence

The native extension runs inside the existing `incremental_loop_tests.cpp`;
there are still 96 native test programs and 96 production translation units.
Every original assertion is retained. A strict historical adapter validates the
entire extended program, reverses its documented additions, and verifies the
complete predecessor Git blob. The original reporting-extension protections
remain intact. Portable tests also reverse the small capture wiring and verify
the complete original coordinator implementation.

The additional native portion uses the already-discovered real toolchain. It
has 13 recorded calls (12 successes and one deliberate compiler failure), nine
compiler processes, one actual named-module scan and no links. Cases cover
ordinary cold/reuse/forced/corrupt-cache/save-failed/compiler-failed paths,
PCH-creator cold/reuse, IFC-only header-unit cold/reuse, and named-module
cold/reuse/stale-scan sealing. The stale-scan case uses an explicitly controlled
source timestamp, not a performance measurement. Configuration follows the
new test program's Debug/Release build; the pre-existing test retains its own
original fixed recipes.

A separate deterministic portion uses real coordinator/cache I/O and a labeled
mock compiler: six API calls (five recorded, one default), five mock process
calls. It tests duplicate accepted dependencies, ordered request/environment
ownership, unchanged warm bytes/mtime, original nested error forwarding, missing
output and corrupt dependency metadata. It does not execute a native compiler.
These counts are additional to the unchanged original test/discovery work, not
totals for the whole executable.

Fresh `storage-fixtures/compile-cache-evidence` and
`storage-evidence/compile-cache-evidence` directories are required. Existing
always-upload rules retain successful entries serialized with the original codec,
selected record fields, the deliberately corrupt input, process argv, raw output,
exit/error data and the first failure prefix. No environment values are exported.
Reading the persisted cache for equality is an independent **test oracle**, not
how the product constructs its record. These test copies are not product
persistence or complete filesystem snapshots. A missing completion marker is
not success. No failed native case or performance run is retried by these tests.

## Acceptance and follow-up

This source change does not establish a performance pass or zero overhead.
Compile-time capture selection avoids unconditional default-path record copies,
but wrappers, code layout and opt-in allocation costs still need evaluation under
the existing review rules. The original Performance819 failure and adverse
samples are unchanged. The consumed slot diagnostic is not reallocated.

Accept this candidate's own native Debug/Release, original artifacts,
self-host/package and applicable correctness results before a merge decision.
Windows CI is required; portable source checks cannot substitute for it.
Broader artifact-record adoption, persistence, supported-writer coordination,
physical identity and safe reclamation remain separate work. VERSION remains
5.6.0; this change is not a v5.7.0 publication.
