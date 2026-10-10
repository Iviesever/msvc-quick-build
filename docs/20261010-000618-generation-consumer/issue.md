# Request: finish the ordinary-generation consumer/lifetime handoff

## User intent

The user asked: "继续完成下一步的接手点". Continue the established
Iviesever/msvc-quick-build workflow from the latest authoritative #198 handoff,
complete its bounded subject, publish through the authorized GitHub Connector,
verify applicable CI and normal merge, and leave evidence and a concrete handoff.
Local `git push` is not an authorized submission route.

## Verified starting point

- Main/base: `7dea446ea63c180e0af4a5ab5b499c244fbeaea6`.
- Tree: `838071cea6120c99e3fd264dd407f9fb67ef716c`.
- PR254 is merged; #198 comment `6084515239` defines this task and `6084622741`
  records its previous delivery. Both were freshly read in full. The issue has
  324 comments; the last two match these IDs and the following page is empty.
- PR250 remains Draft/open/unmerged at `a26b103685589ab8a5b4df60d1b760ec445efdf5`;
  #248 remains a separate Draft/open/unmerged candidate.
- A new sibling worktree and branch isolate this documentation decision from
  baseline and every previous worktree. No repository AGENTS.md was found.

## Acceptance

Complete one product decision about the intended ordinary-generation consumer,
operation and owned-result lifetime across invocation boundaries. Distinguish
synchronous return, several calls in one process, and cross-process/restart use.
Account for borrowed inputs, owning live/model/archive/wire/decoded values,
internal validation copies, release responsibility and missing persistence rules.
Compare current behavior with concrete same-process and archive-consumer options.
Locate the accountable cost requirement or explicitly record its missing source.

Finish with `CONSUMER_SELECTED` or `DEFER`. The selected result here is `DEFER`:
no new contract or real-cost proposal is selected. End the measurement path and
return a bounded #198 product handoff instead of another synonymous audit loop.

## Constraints

The frozen preparation remains byte-identical: 21000 bytes and SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`.
Its 15 strict integer zeros, 10 null choices, seven blockers and four false
authorities stay unchanged. Preserve historical studies, absent evidence,
HOLDs, #164 boundaries, VERSION 5.6.0 and all existing product/test/workflow files.

This slice has no project tests/imports, fixed-substitute or real workload
launch, matrix/registration selection, held-PR integration, writer/cleanup
implementation or release. Read-only source/Git/JSON/hash and administrative
delivery checks are allowed; applicable automatic docs/classifier CI is separate.

## Deliverables

Six added repository documents: bilingual decision, JSON source/decision index,
this request, implementation plan and walkthrough. Preserve independent audits,
actual remote/CI/merge/handoff receipts and an independently read-back evidence ZIP.

Source: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6084515239
