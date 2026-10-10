# Walkthrough: ordinary-generation consumer/lifetime decision

## Completed design

The result is **`DEFER`**. The finite PR254 consumer/operation/owned-result design
is complete; no new ordinary-generation product consumer or accountable cost
contract is selected. This measurement path stops. Product-cost and PR250
integration remain HOLD, and real execution remains BLOCKED.

The [English decision](../GENERATION_CONSUMER_DECISION.md),
[Chinese decision](../GENERATION_CONSUMER_DECISION_ZH.md), and
[JSON source/decision index](../GENERATION_CONSUMER_DECISION.json) distinguish:

1. Owning results surviving a synchronous API return.
2. One caller retaining owning state across invocations in the same process,
   which does not itself require serialization.
3. A later process/restart, which needs separately chosen publication, transport,
   reader, provenance, concurrency and interruption/durability contracts.

The actual source ledger includes full validation models in archive projection,
encoding and decoding, association copies, independently held caller values and
owned public errors. Those facts establish possible semantic coexistence, not a
measured latency/allocation/RSS result. Test scopes and historical chain shapes
do not select product lifetimes or retention policy.

The alternatives are present inventory behavior, a future same-process explicit
library consumer, and future archive/value/byte consumption. Existing behavior is
retained. The latter two remain candidates because their actual use, trigger,
frequency, input support, release points and cost decision are unselected.
An explicit library contract remains a legitimate possible future requirement;
the absence of a default CLI caller is not a universal reason for rejection.

## Evidence and preservation

The starting main is `7dea446ea63c180e0af4a5ab5b499c244fbeaea6`, tree
`838071cea6120c99e3fd264dd407f9fb67ef716c`. PR250 remains frozen at
`a26b103685589ab8a5b4df60d1b760ec445efdf5`, tree
`4e777bdfc33b7befaf5e3769e27eb4fe910f9f03`. The exact #198 body, PR254
handoff/delivery and last-comment pagination were freshly retained; comment
count 324, the two expected tail comments and the empty following page agree.
This is a bounded read interval, not a claim about future updates.

The index binds 17 exact Git source identities, 11 remote receipt identities,
and separate lifetime and requirement audits. The lifetime audit covers 36
accurate source blobs and the requirement audit covers 21, with their selected
read scopes; they do not claim an exhaustive external-consumer census. Their
four final evidence files are hash-bound, not duplicated into repository source.

The original preparation stays byte-identical: 21000 bytes, SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`.
Its 15 strict integer zero budgets, 10 null future choices, seven blockers and
four false authorities remain unchanged. Historical failed-preflight/INVALID
closures, consumed calls, empty stream, missing observations and adverse/HOLD
evidence remain historical; no new recovery search or replacement occurs.
VERSION stays 5.6.0. PR250 and #248 remain separate Draft/HOLD work.

## Verification and integration boundary

This source slice adds exactly six documentation files. Its static gate compares
all 717 pre-existing tracked blob/mode pairs and all 51 workflow identities,
checks the complete candidate tree, strict JSON fields, source/audit/receipt
hashes, bilingual links and whitespace, then requests a fresh independent
semantic review. The immutable records in the delivery archive carry the actual
tree, checks, findings and resolution; this file does not predeclare a future
review or CI outcome.

Local activity is read-only Git/source/JSON/hash verification and administrative
commit-adoption/archive delivery work. No project module, analyzer, test, fixed
substitute, codec/compiler/MQB/MSVC/ETW or real workload is invoked. Automatic
documentation and native path-classifier/gate CI must be read back separately;
skipped native/performance jobs are not executed qualifications. The unchanged
documentation verifier enumerates existing pairs, so direct bilingual review
is required for the new pair.

GitHub mutations use the authorized Connector: create reviewed blobs/tree/commit,
open a focused Draft PR, review its exact head with COMMENT, mark Ready, and
normal-merge after actual applicable CI. Exact remote source bytes, full changed
files, run/job/step/log identities, any Ready-event run and the actual merge's
ordered parents/tree/main are retained. No local push, workflow dispatch or
rerun is selected. Publication/CI/merge receipts are external to their own source
commit and therefore are not fabricated here as self-referential identities.

The prior workflow-path lookup used three incorrect short filenames; the
read-only command reported missing files. The actual tracked filenames were
then enumerated and their source read. The lifetime auditor also rejected a
proposed citation ending one line beyond its exact source, corrected it before
writing the audit, and retained the observation. Both administrative events
remain in the evidence; neither is a product execution or a source failure.

## Concrete next product handoff

Return to #198 with **explicit-clean eligibility and refusal for user-selected,
known-rebuildable target outputs**. Identify one supportable positive path and
the evidence for each output, protected/shared/unknown exclusions, preview-to-
execution identity checks and the supported writer/running-product boundary.
If a positive path lacks authority, record exactly what is missing; do not ship
an always-refusing CLI or infer deletion permission from historical claims.

Existing known-write collection and write-domain admission are groundwork, not
a completed reusable writer protocol. `begin_writes` has no completion/reset;
scope exit and cleaner-only locking do not establish writer quiescence. #164
continues to own the broader single-writer/session/recovery route. This slice
does not implement clean/prune, a writer, retention counts, new budgets or release.

The final #198 handoff and the six published source references are sealed with
the evidence. The ZIP, its external builder report and independent actual ZIP
readback are delivered separately. The final ZIP hash and subsequent delivery
comment are not inserted back into the sealed archive.

Source handoff: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6084515239
