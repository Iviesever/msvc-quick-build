# Same-invocation module and header-unit cache evidence

[简体中文](MODULE_CACHE_EVIDENCE_ZH.md)

## Scope and result

`MsvcModuleCompileCoordinator::run_recorded` retains the existing `result` and
`record`, and adds an owning `ModuleCompileWaveCacheEvidence cache_evidence`.
Its `compiles` vector follows `request.sources`; `header_unit_compiles` follows
`request.header_units`. Successful waves contain one nonoptional
`CompileCacheEvidence` for every executed or reused local node. An external
provider remains a dependency reference and is never invented as a local producer.
The old `ModuleCompileArtifactRecord::exact_cache_entry_captured` stays false:
a legacy projection alone does not contain these newly attached cache values.

## Execution and ordering

Default `run` selects `run_impl<false>` and the original lower `run`; `inspect`,
CLI and upper-level targets do not adopt recording. The explicit path selects
exactly one lower `run_recorded` per node. Its owning cache result is moved, not
reloaded, rescanned, reconstructed or obtained by compiling a second time.
Provider force propagation, graph levels and error selection remain unchanged.

Before workers start, only the recorded instantiation sizes a vector of optional
internal slots and reserves the two public evidence vectors. `CompileCacheEntry`
has no default signature: unused slots do not fabricate entries. Each worker
writes its own slot and existing projection; vector sizes never grow in workers.
After all levels succeed, the calling thread moves values into nonoptional public
vectors in input order. Missing internal evidence is refused, not default-filled.
This is consistent with the C++ container rule permitting concurrent modification
of different elements except `vector<bool>`; the existing packed propagation
flags are still updated only after the scheduler joins a level.

## Failure and authority

Invalid requests and compile/scheduler failures return the original typed wave
error, not partial public success. Prior nodes may already have changed files:
there is no rollback guarantee. Cache-save failure remains a successful compile
with its warning, the exact attempted cache entry and typed save error. Reuse
retains the value accepted by that call's validation, without an extra save.

All producer-identity, current-content, full-inventory and deletion-authority
flags remain false. These owned in-memory values are not atomic observations,
writer leases, trusted persistence or permission to delete. Toolchain environment
values remain in memory and must not be dumped to evidence. No codec, cache
writer, ordinary caller, VERSION, clean/prune or release behavior changes.

## Verification and costs

The existing ten mock and six native recorded-wave calls remain, including their
original assertions, compiler budgets, three native scans and one consumer
link/run. Added checks cover all per-node dispositions, object/IFC ordering,
header identity, effective provider force, cache-byte copies, save-error/sentinel
preservation, one warm cache payload open per node and zero warm writes, and old
values surviving subsequent overwrite/failure. A bounded condition-variable
fixture forces mock callback completion in HU/A/B/consumer order, unlike public
source order; it does not time the product or claim control of every slot's store
instruction. The original graph and scheduler are not mocked or changed.

Snapshots are test-only serialization outside the measured collector activation;
open counters count successful payload opens, not failed opens. Existing full
request/environment ownership assertions are not all serialized. Portable source
and mutation contracts are not substitutes for real Windows execution.

Explicit capture copies each attempted node's effective request and coordinator
toolchain in the existing lower API and retains cache/dependency payloads until
its owner releases them. Internal slots, public vector storage and two result
projections add allocation/residency costs. Moves do not make those costs zero.
Default code layout can also change. This implementation allocates no performance
experiment and claims no timing or memory qualification. The candidate needs its
own CI and independent integration decision; all historical HOLDs remain.

## References

- C++ working draft, container data races: https://eel.is/c++draft/container.requirements.dataraces
- MSVC module command-line semantics: https://devblogs.microsoft.com/cppblog/using-cpp-modules-in-msvc-from-the-command-line-part-1/
- Existing lower contract: [COMPILE_CACHE_EVIDENCE.md](COMPILE_CACHE_EVIDENCE.md)
- Previous producer: [PCH_CACHE_EVIDENCE.md](PCH_CACHE_EVIDENCE.md)
