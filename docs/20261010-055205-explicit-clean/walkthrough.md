# Walkthrough: explicit-clean positive eligibility and refusal

## Result and interpretation

The [English contract](../EXPLICIT_CLEAN_CONTRACT.md),
[Chinese contract](../EXPLICIT_CLEAN_CONTRACT_ZH.md) and
[decision/source index](../EXPLICIT_CLEAN_CONTRACT.json) define
`CONTRACT_DEFINED` / `CURRENT_SESSION_EXE`. The current base remains
`EXECUTION_BLOCKED`: no cleaner, CLI spelling, deletion operation or execution
budget is added. This distinction completes the finite contract requested after
PR255 while preserving the remaining implementation work in #198.

The exact checked main is `c96c75750bcdebb1e8e31f69841d33ec4752e175`, tree
`d049d9fc95071796e5232f79655f6735e7322f89`. The authoritative request is
[#198 comment6091785630](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6091785630),
followed by [delivery6091873908](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6091873908).
The live read at intake found PR255 merged, 326 #198 comments, an empty next
comment page and five open Draft PRs, with no competing clean contract.

## Why this is the first positive path

The selected use is explicit discard of one current ordinary EXE after its
qualifying producer completes in the same controlled session. That producer,
the actual writer gateway and the Windows object owner can join their existing
responsibilities without inventing a persistent history format. Reopening an
old EXE from a pathname or saved report would additionally need durable
authenticity, replacement detection and recovery. Broader historical cleanup
would need separate symbol/ILK and shared-use policies.

This minimum path retains every PDB, ILK, MAP/manifest/import/export output,
object/PCH/IFC/HU artifact, dependency record and cache. It deliberately does
not satisfy the PDB-heavy space benefit or full Rium/product acceptance in
#198. A current EXE used as another target's tool/input, a release asset or a
running product is also retained. Target kind and file extension cannot waive
the use veto.

## Source findings which change the design

The eligibility audit binds 40 exact source identities and the writer audit
binds 49, with overlap; these are not additive counts of distinct files. Their
full reports and original administrative failures are included in final evidence.
The JSON index joins their source references by exact path/blob and retains
which audit read which semantic ranges.

| Finding | Consequence |
| --- | --- |
| Target EXE layout has no configuration/architecture parameters | Bind the resolved request and actual current object; same pathname can be a different build |
| Ordinary compile results now capture exact compile-cache values | Preserve that capability; do not repeat the obsolete claim that the exact cache value is missing |
| Actual terminal LINK recipe and main-EXE post-completion seal are not retained by current output records | Require a receipt from the actual terminal producer; a candidate recipe/cache signature cannot mint it |
| Source intermediates can be shared and ILK remains an explicit native gap | Keep intermediates, symbols and all caches outside the first selected role set |
| Ordinary production paths do not call write-domain admission or prewrite inventory | Integrate real owners before their first effect; a new cleaner-only lock would leave current builders outside |
| Root completion, callback drain, tracked descendants and native service completion differ | Account for the exact participating writer effects; `/Z7`, portable tools and an idle/PID snapshot are insufficient |
| The write inventory retains private exact-parent pins but releases observed leaves; single-file observations release leaf/ancestors | Preserve existing parent-pin capability and supply the missing continuous leaf/full-ancestry handoff; do not reopen by path to upgrade delete rights |
| LINK already checks `missing_output` | Keep caches; later qualified clean/rebuild tests must exercise the existing missing-output decision and retained-target behavior |

Official Microsoft references in the contract support only access/sharing,
held-handle file identity and disposition primitives. They do not establish
completed MQB producers, shared-use safety, running-product exclusion or
immediate space recovery. No unqualified advanced deletion mode is selected.

## Complete contract, explicit present blockers

The 12 bilingual sections define target selection, producer-owned receipt,
exclusions, admission closure, product use, retained physical identity, read-only
preview, exact consent, execution revalidation, outcome accounting, interruption
and rebuild behavior. The JSON gives 28 named future acceptance scenarios.
Logical size, observed allocation, name absence and measured volume change are
separate values; unavailable evidence is never converted to reclaimed bytes.

Five concrete authorities are still absent at the checked base:

| ID | Missing implementation |
| --- | --- |
| A1 | Actual terminal producer receipt for the current main EXE and effective recipe |
| A2 | Ordinary production writer gateway with admission closure and qualified completion |
| A3 | Accounted shared-use / runtime launch boundary, including uncontrolled actors |
| A4 | Continuous owning leaf/full-ancestry capability through producer, preview and execution, beyond the inventory's existing private exact-parent pins |
| A5 | Qualified native executor, result/record failure handling and real Windows/rebuild evidence |

The next implementation is A1/A2 with the inseparable A4 handoff, on the real
selected ordinary path, ending in a non-destructive cleaner handoff. It must
show a positive transfer and refuse stale output/epoch or unaccounted writers.
An unused collector, a caller-created proof token or a bare `complete()` marker
removal does not meet this handoff. Native completion remains an exact blocker
until proved. A3/A5 and actual deletion remain disabled, with no full #164
recovery or reopened generation-consumer cost study implied.

## Verification and delivery reading guide

Local verification for this slice is static document/source/JSON/hash and Git
inspection only. No project code import, test, analyzer, runner, fixed substitute,
compiler, MQB/MSVC/ETW or real workload is executed locally. Read test assertions
are source evidence, not a new pass. The 28 scenarios are neither executed nor
registered as an execution matrix or budget.

The focused PR adds exactly six docs files and keeps all 723 baseline leaf
entries and 51 workflow blobs unchanged. Its actual applicable CI, exact-head
review, merge identities, main CI and #198 completion readbacks are recorded
after they occur in #198 and the sealed evidence. This immutable source does
not predeclare a future CI run or merge successful. In particular, the old
bilingual verifier enumerates 22 maintained pairs; direct independent semantic
review covers this new English/Chinese pair. Native/performance skips remain
skips and add no clean or cost qualification.

Administrative guard rejections, missing-path reads and transport/prewrite
interruptions are retained with their read-only recovery and no-product-
execution scope. There is no permission rejection to reinterpret as a pass.
Evidence is written once, read back, sealed once and audited against the actual
ZIP; later delivery receipts remain outside it rather than changing its hash.

## Preserved state

Ordinary-generation consumer adoption remains DEFER. Frozen preparation remains
21000 bytes / SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`, with
15 strict-int zeros in `authorized_real_budgets`, 10 nulls in `future_selection`,
7 blockers and 4 false authorities. PR250 remains
Draft/unmerged, product-cost/integration HOLD and real execution BLOCKED; #248
remains independently Draft/unmerged. The finished failed-preflight/INVALID
attempts, previously spent budget, adverse samples, missing streams and all
unresolved native/capability gaps remain preserved.

No existing file, VERSION or release is changed. VERSION remains 5.6.0. #198
stays open for implemented safe reclamation, wider outputs, real Windows/Rium
acceptance and release work. #164 retains its broader session/service/recovery
roadmap. The next handoff is implementation of the concrete non-destructive
production bridge above.
