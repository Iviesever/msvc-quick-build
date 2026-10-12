# Implementation plan 015

Baseline: `68d5a913399a64e8c99e8ecbfa073b668f603430`, tree
`16fdc9fc472bd974adb5d699d17d23783ddf541a`. Existing isolated worktree:
`/workspace/scratch/3d56575ba9f2/mqb-work/producer-clean-handoff`.
The user's continuation accepts the scope and sequence of the published 015
handoff. Routine implementation choices proceed within that authorization.

## Design decision

Use new 015 policy, selector, preparation, owner, bootstrap and outer modules.
Pin and reuse the unchanged 014 strict JSON/Refusal, listing and owned-IO
modules. Full copying adds a second general implementation; extracting a new
framework expands this task. Neither alternative is selected.

The new selector validates the admitted complete index/image association,
section mapping and runtime-function 105. It must not require this helper to
be the PE entrypoint or rewrite admission.concrete_next_static_target.
The old owner/run modules are absent from every loading set. No monkeypatch
or module alias changes old policy. New IO subclasses only tighten named caps.

## Small source units and interfaces

All new source/test paths are under `tests/native/`.

| File | Responsibility |
|---|---|
| `bcrypt_disassembly_model_015.py` | Phase-specific exact manifest fields, source sets, fixed identities/candidate, limits and new policy |
| `bcrypt_selection_015.py` | Pure index/admission/caller mapping validation and selection from the same fully hashed image snapshot |
| `prepare_bcrypt_selection_015.py` | One new preparation reservation, identities, 167 B retention, small report and durable completion |
| `bcrypt_disassembly_owner_015.py` | One GNU reservation, fixed target, bounded capture, reaping and durable completion |
| `run_bcrypt_disassembly_015.py` | Validate all selected source bytes before compiling any module; dispatch only the registered phase |
| `run_bounded_cli_015.py` | One owner process, bounded streams, process-group cleanup and a final deadline after close |

The preparation phase loads old model/IO, new policy/selector/preparation and
the new bootstrap (six pinned files). Actual loads old model/listing/IO, new
policy/selector/owner and the new bootstrap (seven pinned files). The outer
source has its own separately pinned identity. All bytes of a loading set
must be verified before core members are executed, and verified bytes are
compiled without reopening source paths. Root first verifies the executing
outer/bootstrap entrypoints and complete phase set before launch; executing
bootstrap self-validation alone is not pre-execution validation of itself.

## Ordered execution and review gates

1. **Prerequisites and scope.** Full 014 admissions and required direct refs;
   latest feature/main, three PRs, feature PR count, all runs, issue count,
   registration and closure anchors. Record complete differences. Preserve
   clean baseline and unrelated worktrees.
2. **Preparation contract and implementation.** Finalize
   `selection_resource_contract_015.md`; write genuinely failing pure tests
   before new behavior. Implement the new phase/source/selection boundaries.
   Independently review exact preparation source, finite allowed reads/writes,
   process and payload bounds. No original image open before this gate passes.
3. **Two preparation slots.** Consume one primary preparation under its exact
   outer argv. If clean and complete, use the separately allocated independent
   one-image-read slot to verify the same snapshot/range/digest and all primary
   outputs. An attempted slot is permanently consumed on any failure. The
   independent verifier does not call the subject selector or write a target.
4. **Actual implementation and finite validation.** Complete the new owner and
   outer. Pure tests cover relevant lifecycle and mapping boundaries. Before
   the first GNU child, seal a finite plan for exactly five constructed calls:
   167 NOP; 166 zero bytes plus ret; a known 15 B instruction; a short separated
   address-domain branch/RIP specimen; and a partial instruction refusal.
   No actual bytes are synthetic input. New real Python cap children: zero.
   Existing cap evidence is a reference; pure changed-owner boundary tests and
   independent review must justify reuse. A concrete uncovered risk requires
   revising the unconsumed plan before any extra child, not an implicit rerun.
5. **Source and resource review.** Review all new and reused code, final
   numerical payload/output/process/deadline proof, retained RED/GREEN logs and
   all five constructed outcomes. Check exact new paths against the complete
   retained workflow contents and current baseline. Required findings close
   before actual registration.
6. **Immutable publication and registration.** Through Connector, create blobs,
   tree and commit with the exact old closure parent. Read back every new blob,
   full tree and commit. Keep the feature at its old HEAD. Freeze a canonical
   manifest with independently admitted non-null slice SHA, exact source/tool/
   interpreter/input identities, environment, argv and both exclusive outputs.
   Independently verify the entire manifest. Post and fully read back the exact
   #198 registration; reconcile the only expected ledger change. Then make the
   expected-old, nonforced feature update and read back the full scope.
7. **One actual observation.** Recheck identities and unused outputs. Submit
   exactly the registered new outer argv once. Setup, timeout, cap, crash,
   interruption or late persistence failure consumes the attempt. Never widen
   the range, follow a destination, replace the tool or retry at another path.
8. **Result and delivery.** Independently reconstruct all bytes/address domains
   and review the full CLI/reaping/identity/persistence chain. Root jointly
   admits the result before writing interpretation/next handoff. Publish the
   two closing documents and exact ledger body, read back and transition the
   feature under a fresh old-value guard. Independently check final scope.
   Freeze the new evidence and package it with one exact prior ZIP and Git
   references; issue package checks outside the archive, save and deliver.

## Meaningful pure verification

Test helper-not-entrypoint, separated file/RVA/VMA mapping, exact 167 B end,
runtime/unwind mismatch, strict phase/source identities, unknown or unadmitted
digest, old 014 permits, already-consumed paths, byte mismatch/extra lines,
partial instructions, caps, nonzero/crash/timeout/unreaped child, SIGCHLD,
already-reaped process-group ownership, and late persistence/readback/close.
The last bad source identity must prevent execution of every loaded source.

Run only affected pure tests and planned constructed cases. Preserve every
RED and adverse result. No test command may accidentally discover/run native
fixtures or consume old 013/014 budgets. New contracts describe logical payload
and output bounds; no parent RSS, host-wide write sandbox or native benchmark
claim is made.

## Checkpoint at source publication

The prerequisite, implementation, finite validation and two preparation gates
are complete, with immutable preparation-contract snapshots retained outside
the repository so the original review basis does not change. The five GNU
cases were independent of the original selection and ran after their own
fixed-plan admission while the independent selection parameters were reviewed.
Each prepared case was used exactly once; no extra child or tool probe was
added. Source code stayed frozen after its final pure integration.

The six new implementation files and six new test files, together with the
five source documents, form the exact 17-path source publication. Resource
proof and validation record the actual preparation outcomes and complete
review identities. Remaining steps are immutable source readback, canonical
actual-manifest review and ledger registration, expected-old feature
activation, one actual observation, independent admission, and the separate
two-document result/handoff closure. Only a completed latter checkpoint may
claim an observed 015 result.
