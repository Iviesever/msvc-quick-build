# Ordinary-generation consumer and result-lifetime decision

**English | [简体中文](GENERATION_CONSUMER_DECISION_ZH.md)**

## 1. Decision and completion

**`DEFER`: no new ordinary-generation product consumer is selected, and this
cost-measurement path stops.** The finite consumer/operation/owned-result design
requested by the [PR254 handoff][handoff] is complete. This is a decision about
the current evidence and product choice, not another necessity-review protocol.
Product-cost and PR250 integration remain **HOLD**; real execution remains
**BLOCKED**. No new measurement proposal is queued by this decision.

The [source and decision index](GENERATION_CONSUMER_DECISION.json) binds the
reviewed records and independent audits. This document adds no implementation,
CLI, producer, writer, matrix, registration, budget or release authority.

| Identity | Exact reference |
|---|---|
| This design's main/base | `7dea446ea63c180e0af4a5ab5b499c244fbeaea6` |
| Base tree | `838071cea6120c99e3fd264dd407f9fb67ef716c` |
| Frozen Draft PR250 | `a26b103685589ab8a5b4df60d1b760ec445efdf5` |
| PR250 tree | `4e777bdfc33b7befaf5e3769e27eb4fe910f9f03` |

[#198][issue] supplies a real need: attributable storage inventory and explicit,
safe reclamation of no-longer-needed MQB-managed artifacts. Its current record
does not select an ordinary-generation historical reader, when it is invoked,
which owning values it retains, or an accountable cost requirement. Its report
figures are not new acceptance limits. The latest comment tail was read through
the PR254 delivery with an empty following page; no later product choice was
found in that bounded readback.

## 2. Three different lifetime boundaries

| Boundary | What the existing values support | What remains a caller/product choice |
|---|---|---|
| Return from one synchronous API | Owning projections, models, archives and strings can survive return without borrowing the original input. Borrowed invocation references must stay alive and unchanged during the call. | Which returned value is used, who holds it, and its destruction point. |
| Several invocations in one caller process | A caller can retain completed `RecordedTargetResult` / `RecordedStaticTargetResult` values and supply borrowed references to a later pure batch model. An independently owned selected model can also be retained. | The actual consumer, input population, retention duration, repeat frequency and maximum simultaneously retained state. A wire representation is not required merely to cross these calls. |
| Another process or a restart | PR250 can produce an owning byte string and consume a supplied byte view. | The carrier, record publication, read admission, provenance, concurrent-writer and interruption rules. A pure codec supplies none of those persistence guarantees. |

These are semantic ownership facts from the [recorded model][model] and
[archive API][archive-api]. They do not claim a particular allocation count,
RSS, optimizer behavior or runtime performance. Cross-invocation use must not be
silently redefined as cross-process persistence.

## 3. Three alternatives and their tradeoffs

| Alternative | Concrete consumer/operation | Benefit and missing decision |
|---|---|---|
| A. Keep the current surfaces | A user invokes `mqb storage`; [the command][storage-command] scans one project artifact root, associates legacy cache references and writes its report. Existing explicit C++ value APIs remain available under their current restrictions. | Preserves useful present observation. It does not add ordinary-generation historical consumption, current liveness, reclaimable bytes or safe cleanup. **Chosen present disposition: keep existing behavior.** |
| B. Select a same-process library consumer later | A named caller would retain completed ordinary EXE/DLL/static invocation values, then call `model_recorded_artifact_generations` with explicit source/target/generation claims and retention keys; it would consume the owning model/compile context. | Avoids requiring serialization for same-process analysis. No present requirement selects this caller, trigger, population, retained lifetime or cost consequence. **Candidate only.** |
| C. Select archive/byte consumption later | A named caller would select an owning archive from completed records, optionally encode it for an explicitly chosen carrier, then decode/model supplied historical claims in a compatible path environment. | Permits separation from large live invocation values and optional transfer. It also adds semantic-validation models, owned copies and carrier/reader obligations. No ordinary-generation producer/publication/reader or accountable cost contract is selected. **Candidate only.** |

For B/C, frequency and lifetime are **not specified by the existing requirement**.
"Explicit" is not a frequency budget. No default/high-frequency build adoption is
selected. Existing test helpers demonstrate finite calls and retained values;
their scopes, phase names, empty retention vectors and historical study shapes
do not define a product workload or retention policy. [Generation contract][model]

The desired future output is itself unresolved: a diagnostic model, selected
historical claims for transfer, and trusted cleanup eligibility answer different
questions. Choosing a convenient codec API does not choose among them. A future
consumer must name that output and the decision it changes before a cost proposal.

## 4. Actual operation and ownership ledger

The table describes the exact inspected implementation, including its temporary
owning state. It does not prescribe a future measurement chain or require all
objects to coexist throughout a product operation.

| Operation | Borrowed/caller-owned input | Function-owned work and coexistence | Return and release responsibility |
|---|---|---|---|
| Current `mqb storage` | Parsed command arguments; explicit/current project path | The command owns one `StorageInventory` while scan, legacy association and reporting complete. Observations remain partial when issues exist. | The inventory ends with command scope. Report bytes go to the selected output stream; no ordinary-generation model is returned or journal written. |
| Strict recorded projection | One completed ordinary/static result plus the lexical key | [Projection][projection] creates selected references and per-source compile contexts; original process output/environment are not copied into this result. | The returned projection owns selected values. The caller owns its lifetime independently of the original completed result. |
| Recorded live model | A span of inputs whose reference wrappers borrow completed ordinary/static results; retention span/root/key | [Model implementation][model-source] preflights the batch, owns projected references, compile contexts and recipe/model data. `finish_model` passes the projections to an association that owns copies; local projected values coexist with those copies during construction. | The returned model and `compiles` own their selected state. Function locals release on return/unwind; caller inputs and the returned result have separate release points. |
| `project_artifact_generation_archive` | The same borrowed live inputs and retention/root/key | [PR250 projection][archive-model-source] retains a full successful validation model while it copies selected claims into the new owning archive. Live input, validation model and archive can coexist. | The archive is returned; the local validation model releases as the call ends. Original live results remain the caller's responsibility. |
| `encode_artifact_generation_archive` | A const archive reference and key | [Encoder][codec-source] builds a full validation model, then constructs the owning wire string while archive and model remain alive. | Wire is returned; the local model releases as the call ends. The caller separately owns archive and wire lifetimes. |
| `decode_artifact_generation_archive` | A `string_view` into caller-owned wire and key | [Decoder][codec-source] performs non-owning structural admission, constructs the decoded owning archive, then holds a full validation model before returning. Caller wire, decoded archive and validation model can coexist. | The archive owns its values; it does not borrow the wire after success. Function locals release at call end; the caller decides whether to keep wire and decoded archive together. |
| `model_archived_artifact_generations` | A const archive reference and key | It uses the shared strict model over archived claims directly; it never reconstructs a successful `Recorded*Result`. It also owns projection/model temporaries. | The model and compile contexts are independently owned; the caller decides when to release both archive and model. |
| Errors and exceptions | Inputs keep their original ownership obligations | Public typed errors own strings and may own nested model/projection diagnostics. Conversion/allocation/key exceptions can propagate. | The caller owns a returned error; completed locals unwind normally. A scalar test wrapper dropping diagnostics does not establish zero public-error ownership or total allocation. |

The association's copy boundary is explicit in [its contract][association] and
[implementation][association-source]. Moves or copy elision do not turn a
semantic coexistence ledger into a byte-accurate peak measurement. Internal
temporary values, selected-output limits and already retained caller values must
not be confused. Format limits bound admitted structure, not all heap/RSS or time.

A shorter same-process operation can release its live input after producing a
usable owning result; a transfer consumer could release its archive after the
carrier has accepted the required bytes. Those are **possible designs**, not
selected release points, measured savings, or permission to discard required
diagnostics. Carrier buffering, formatting, copying, parsing, hashing and final
flush/fsync/close can add work outside a single codec API's return window.

## 5. Claims, retention and persistence remain distinct

An explicit retention key selects an unverified historical claim in the supplied
batch. It is not a count/age policy, a durable retain decision, proof of file
existence, retirement of unselected records, or a delete list. Source IDs,
project/target/generation keys and caller labels are not authenticated writers.
Lexical path matches and an empty-inventory model do not observe current files.
Producer identity, current content, complete producer inventory and deletion
authority remain **false**. [Generation and retention contract][model]

The existing [LINK completion archive][link-archive] is a real, separate explicit
single-snapshot file facility. It does not publish the ordinary-generation
archive, bind a generation reader to its producer, or supply a whole-record-set
transaction. Legacy LINK/LIB cache references used by [storage][inventory] also
do not supply the missing ordinary-generation consumer.

For any future cross-process design, an accountable owner must separately select
what is published, destination/carrier, record-set version and identity, admitted
reader, completeness/failure reporting, compatible path semantics, atomicity and
concurrency boundaries, and interruption/durability behavior. Neither codec
round-trip success nor a hash supplies those guarantees. The selected archive
does not serialize a full process environment, timing collector or executable
recovery request; preserving selected claims does not preserve a live invocation.

## 6. Cost decision and finite stop

The reviewed records do not supply a product cost requirement for B/C. Thus
consumer, trigger/frequency, selected operation/result lifetime, supported input
population/platform and accountable metric/window/decision consequence remain
unselected. #198's ordinary-build no-global-scan constraint remains meaningful,
but it does not answer the cost question of an unspecified explicit consumer.

The already completed [necessity review][necessity] preserves the legitimate
historical explicit-library integration purpose. A default CLI caller is not a
universal prerequisite. It also explains why the separate external-no-op
`>1 ms AND >10%` rule, schema/output limits, fixed-substitute values and INVALID002
observations do not supply this consumer's product criterion. No replacement
numerical or decorative nonnumeric threshold is introduced here.

**Decision consequence: do not continue designing or running a real-cost proposal
on this path.** The seven original execution blockers remain. The exact frozen
[preparation][preparation] remains `PREPARATION_ONLY_BLOCKED`: 21000 bytes,
SHA-256 `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`,
15 strict integer zeros, 10 null future choices and four false authorities.
001/002 stay closed with their failed-preflight/INVALID states, consumed ledger,
empty stream and missing observations. No refill or recovery search is performed.

Reconsider only after materially new, accountable consumer/decision evidence
selects a contract and a defensible cost rule. That would be a distinct proposal
subject to all seven prerequisites, not a transition out of the frozen file.
No further synonymous consumer/necessity review is an automatic handoff.

## 7. Return to the #198 product need

The next bounded product handoff is an **explicit-clean eligibility and refusal
contract for user-selected, known-rebuildable target outputs**. It must identify
one supportable positive path, the exact evidence that makes each output eligible,
protected/shared/unknown exclusions, preview-to-execution identity checks, and
the supported writer/running-product boundary. If a positive path cannot be
supported, record the concrete missing authority; do not ship an always-refusing
command or manufacture eligibility from historical claims.

Use existing typed layout, producer and physical identity authorities. The
[write-domain groundwork][write-domain] already contains known-write collection
and a conservative admission primitive. Its current [API][write-domain-api] has
no completion/reset after `begin_writes`; a cleaner-only marker cannot exclude
legacy builders, and no stopped-writer certificate follows from scope exit.
Its historical VERSION text does not override the current `VERSION` 5.6.0.
[#164][roadmap] remains the owner of the broader single-writer/session/recovery
route. This handoff neither assumes that route complete nor expands it here.

This is a concrete cleanup contract task, independent of ordinary-generation
codec adoption. It does not select automatic `prune`, a retention-count policy,
a writer implementation, new execution budgets or a launch in this slice.
#198 remains open; its cleanup, Windows/Rium and release acceptance are unfinished.
PR250 and #248 remain separate Draft/HOLD work. VERSION stays 5.6.0.

## 8. Verification scope

Independent lifetime and product-requirement audits support this decision.
All product code, tests, workflows, prior documents and frozen evidence are
preserved. Only this English/Chinese decision, its JSON index and the dated
issue/plan/walkthrough are added. Local verification uses read-only Git, source
text, JSON and hashes; no project module, analyzer, test, fixed substitute,
codec/compiler/MQB/MSVC/ETW or real workload is launched.

Direct bilingual review covers the new pair. Applicable automatic documentation
and path-classifier CI, exact source identities, normal merge and delivery are
recorded separately with the [walkthrough](20261010-000618-generation-consumer/walkthrough.md).
Documentation CI's existing enumerated pairs do not by themselves verify this
new pair. Static lifetime reasoning is not native or product-cost qualification.

[handoff]: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6084515239
[issue]: https://github.com/Iviesever/msvc-quick-build/issues/198
[roadmap]: https://github.com/Iviesever/msvc-quick-build/issues/164
[model]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ARTIFACT_GENERATIONS.md
[model-source]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp
[projection]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/RECORDED_STORAGE_PROJECTION.md
[association]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ARTIFACT_STORAGE_ASSOCIATION.md
[association-source]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/core/cache/ArtifactStorageAssociation.cpp
[inventory]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/STORAGE_INVENTORY.md
[storage-command]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/app/targets/StorageCommand.cpp
[archive-api]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/include/mqb/orchestration/ArtifactGenerationArchive.hpp
[archive-model-source]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp
[codec-source]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/src/orchestration/incremental/ArtifactGenerationArchive.cpp
[link-archive]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/LINK_COMPLETION_ARCHIVE.md
[necessity]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ANALYSIS_COST_NECESSITY_REVIEW.md
[preparation]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ANALYSIS_COST_DECISION_PREPARATION.json
[write-domain]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/WRITE_DOMAIN_ADMISSION.md
[write-domain-api]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/include/mqb/platform/windows/WindowsWriteDomain.hpp
