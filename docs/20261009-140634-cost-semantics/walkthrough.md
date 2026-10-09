# Cost semantics and blocked decision preparation: acceptance evidence

## Delivered change

This completes the fixed/static portion of [handoff 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173), following the [accepted request](issue.md) and [implementation plan](implementation_plan.md). Development starts from main `bf5df76079f224ea9bb5e7822be683cb3a2e4d45`, tree `46007c773858dd50653db0a49ce6861b15733fb2`.

The pure adapter verifies strict cost-output semantics against an immutable external-hash-bound call contract. Six new closed fixed modes exercise valid and invalid prepared/observation output. The format-2 driver preserves both streams before business validation, stops subsequent calls on the first error, and parses semantics again at final audit. The transport and capture implementations and prior eight modes' exact bytes are unchanged. Revision notices identify the old driver documents as the archived format-1 qualification.

A separately frozen 21,000-byte decision preparation, SHA-256 `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`, is accepted only by its exact source-pinned identity and an independently supplied matching hash. It always retains PREPARATION_ONLY_BLOCKED, 15 strict zero real budgets, 10 null future selections, 7 blockers and four false authorities. Product cost and PR250 integration stay HOLD. The verifier has no execution transition.

## Actual final verification

Command executed in the isolated worktree, with the retained outer harness:

```text
python3 -B /workspace/scratch/3d56575ba9f2/mqb-work/evidence/cost-semantics/execute_stage.py integration-full-001
```

The harness froze all 13 analysis Python files plus the decision JSON before execution, retained exact copies and stdout/stderr, and matched them again after execution. It admits only the closed Python -I -B -u fixture command, at most 40 direct substitutes per stage and at most 8 calls per driver protocol. Every real codec/MQB/MSVC/ETW/study budget remained zero.

Actual unittest output excerpt:

```text
test_bad_tail_scalar_return_cannot_retain_epoch_payload (test_analysis_cost_semantics.CostSemanticsTests.test_bad_tail_scalar_return_cannot_retain_epoch_payload) ... ok
test_final_gate_reparses_semantics_even_if_first_validation_was_wrong (test_analysis_cost_driver.CostDriverTests.test_final_gate_reparses_semantics_even_if_first_validation_was_wrong) ... ok
----------------------------------------------------------------------
Ran 90 tests in 6.249s

OK
```

The two test lines are selected from the complete retained log; the final footer is verbatim. The elapsed unittest duration is test-run bookkeeping, not a performance observation.

Final result: **90 tests = 59 prior + 21 semantic + 6 wiring + 4 decision**, 0 failures, 0 errors, 0 skips, exit 0. **26 actual fixed substitutes were launched and reaped**, with 0 budget violations and 0 emergency cleanup terminations. Final stdout is 2,914 bytes, SHA-256 `7e72d39b1dbdbe61a2cc0d2db8d90ff765a3b6bcf54353f2e4fbbfdd09dcc687`; stderr is 15,939 bytes, SHA-256 `148e61475b39e6f4534081e0f37dc490c151f8aa4dd3caea4e4c648493b17209`.

## Retained RED/GREEN sequence

| Stage | Tests | Failures / errors / skips | Exit | Actual fixed child launches |
|---|---:|---|---:|---:|
| `baseline-001` | 59 | 0 / 0 / 0 | 0 | 19 |
| `wiring-decision-red-001` | 10 | 10 / 0 / 0 | 1 | 0 |
| `semantics-red-001` | 20 | 20 / 0 / 0 | 1 | 0 |
| `semantics-green-001` | 20 | 0 / 0 / 0 | 0 | 0 |
| `wiring-green-001` | 6 | 0 / 0 / 0 | 0 | 7 |
| `bad-tail-live-red-001` | 1 | 2 / 0 / 0 | 1 | 0 |
| `bad-tail-live-green-001` | 1 | 0 / 0 / 0 | 0 | 0 |
| `decision-green-001` | 4 | 0 / 0 / 0 | 0 | 0 |
| `integration-full-001` | 90 | 0 / 0 / 0 | 0 | 26 |

All nine stages and their first failures remain. Across these stages **52 fixed child launches** were consumed, including the 19-child unchanged baseline; these are lifecycle tests, not research samples. The first 30 missing-feature failures were assertion failures without process launches. One attempted invocation had a mistyped harness path and exited 2 before entering a stage; its receipt is retained separately and no test or substitute started.

Independent review found a business rule missing after the first pure and wiring green runs: on these exact bad-tail paths, the nonowning scan refuses the appended byte, scoped Error owners are destroyed in the lambda, and observe snapshots the epoch only after a scalar offset returns. A positive live-at-return value must be rejected. The narrow two-case RED failed twice without a child, then the same test passed after adding the zero-live invariant. Cumulative allocation requests remain unconstrained by historical 2-call/44-byte values; Error is not claimed allocation-free. The final full run includes the regression.

The decision budget test key was aligned with the completed JSON's `codec_measurement_processes` before implementing its verifier; no document alias or budget was added to satisfy a test. The preparation JSON was frozen separately and included in final source identity checks.

## Files and exact final test inputs

Modified existing files are the driver, fixed fixture, and the two prior driver documents' revision notices. New files comprise the pure semantic adapter and tests, wiring tests, decision verifier and tests, two semantics documents, the decision JSON and its two documents, and this three-document request/plan/walkthrough set. Product sources, original tests, VERSION, all 51 workflows, PR250 and old evidence remain outside the implementation changes.

| Final test input | SHA-256 |
|---|---|
| `docs/ANALYSIS_COST_DECISION_PREPARATION.json` | `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae` |
| `tests/analysis/analysis_cost_decision.py` | `85590375be9d21a190cec651288e5b9b0ad54df6671d939a940201cb5615e3bf` |
| `tests/analysis/analysis_cost_semantics.py` | `476d0ef8f17ec1fc212ef70c7d8b546be340659c721f5271628e58f29e6fca5a` |
| `tests/analysis/analysis_evidence_capture.py` | `cdbf21a3a79f3646c6417565f59aa9af662cb9cacca730e7e2a90159495f1458` |
| `tests/analysis/analysis_fixed_substitute.py` | `a4b31f2f0741b3b543d6e879d0d84b034932af0ba7099734b4d417653341eb67` |
| `tests/analysis/analysis_substitute_driver.py` | `dbe0e1a25bf2dd13033c2faba1f5ac3a571dd1cd5b1fe6d296a9639ba365a8c3` |
| `tests/analysis/analysis_substitute_transport.py` | `b114322e0e84056def0b9be577f19073b421c83aa5df9843d34a2b88e10cd837` |
| `tests/analysis/test_analysis_cost_decision.py` | `6943d8b6109bd4ebeff88dd434779193483f226822d106a1e91f43d99beb1454` |
| `tests/analysis/test_analysis_cost_driver.py` | `4f01db11e9f60b60f7e328c8f82043bbc979aac69699c97e3b00c5381c700c6b` |
| `tests/analysis/test_analysis_cost_semantics.py` | `5163455339272b4549618ea7a54d8d8b9849471e50a261315b3bce1882ebeda0` |
| `tests/analysis/test_analysis_evidence_capture.py` | `02e598e9d4160e004c22eddbf5e9311978a965d474a6a9efac31c083aa3203f8` |
| `tests/analysis/test_analysis_evidence_capture_faults.py` | `c9000fb4fffd4c37458d96b5ac11b685b6fe8f9be83eb8454d4699fd883fa2d6` |
| `tests/analysis/test_analysis_substitute_driver.py` | `c34016a0853f0e59f50912e812e49b4ad5c9d9d3a06884f3bd004a1ac581aae5` |
| `tests/analysis/test_analysis_substitute_driver_faults.py` | `d5fe2ef8769c86c7e2820dc231c4f14ea5a5edde5dabc1ea5ccbd4f301551e3e` |

The exact source artifacts are recorded here without a self-referential commit hash. The later PR review, actual CI runs, Git Data commit/tree/parents and merge receipt are recorded in the delivery evidence and issue #198; this pre-submission walkthrough does not claim future CI has already run.

## What the checks establish

Pure tests cover 96 positive case/operation/mode combinations and four bad-tail combinations, exact fields and prepared bindings, integer representation limits, time/allocation null separation, HWM monotonicity, requested-payload relations, mixed-unit consumed sentinels, JSON/framing limits and hash-bound immutable contracts. The fixed allocation/time/HWM values are literal substitutes.

Real-pipe tests establish that valid fixed records survive spool/raw/backup unchanged; semantically invalid but byte-intact output prevents the second call; and final business parsing rejects invalid output even if the first validation was deliberately made to return a false success. The capture helper and transport success in that last fault isolate the business gate. Source drift and rehashed forged fixture contracts reject before launch or evidence creation.

Static decision tests establish exact-artifact acceptance, required external SHA and raw-size/type limits, independent result copies, and refusal of a rehashed edit that removes blockers, inserts a real budget, selects a future purpose or changes the status. Preparation integrity never implies a real execution grant.

## Retained limits and CI scope

This is Linux fixed-substitute qualification. The existing exclusive-reaper/SIGCHLD policy, original-pipe EOF, known-consumption anchors and no-refund failure behavior remain. Source hashes do not attest loaded modules, all runtime dependencies or a producer; final audit is sequential, not atomic. Copies share a fault domain. Post-supervision flush/fsync/close, copying/parsing and final audits are not all covered by the active deadline and are not free or immune to interference.

The unchanged Native workflow classifies analysis/docs-only changes and can skip native builds/shards; its old classifier and gate are separate from these new tests. The unchanged Documentation workflow checks its original 22 explicit pairs, not the new semantic/decision pairs. Performance comparison is title-gated. Actual run/jobs/logs, checkout tree identities and applicable Ready/main events must be inspected after PR creation. No dispatch or rerun is needed for this slice. No Windows/MSVC or real codec cost claim follows from these checks.

## Historical state and next handoff

PR250 remains Draft/HOLD at `a26b103685589ab8a5b4df60d1b760ec445efdf5`, with original base `2762749b27fc89156b724d19e89cce84e388ef52`. Study 001 remains failed-preflight; its earlier 20 preparation processes and 100 public API calls remain consumed. Study 002 remains INVALID after 100 measurement processes and 1340 total public API calls; 99 prepared and 495 observations survive, and five observations remain missing. The empty original 070 stream stays empty. No old runner, binary, sample or search is repeated or repaired.

The next handoff is a static independent review of whether any new real-cost question is necessary and justified, with acceptance criteria fixed before new data. Resolve that first decision against the seven explicit blockers before proposing real producer support, a global budget or a separately registered execution. The current closed driver cannot silently turn its 8 calls / 3-second call / 20-second overall contract into the old 100-call / 25-second child / 600-second study. Nothing here allocates study003, fills five missing records, reopens 001/002, authorizes PR250 integration or releases v5.7.0.
