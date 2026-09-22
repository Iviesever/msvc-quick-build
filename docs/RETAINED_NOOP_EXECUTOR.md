# One-allocation executor for the retained #701 boundary study

**English | [简体中文](RETAINED_NOOP_EXECUTOR_ZH.md)**

This tool follows the accepted #211 helper preflight. It implements a separately
reviewed, explicitly allocated execution, not a performance repair. Opening,
synchronizing, marking Ready or merging a PR does not run the study. Automatic
CI runs synthetic contracts and Windows parsing/interpreter-binding checks only.

## Fixed inputs and source

`retained-noop-study.yml` admits only `workflow_dispatch`. It downloads the original
run35681224762/attempt1/artifact10675079360 as an unextracted ZIP: 4880704 bytes,
SHA256 `940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3`.
Both original binaries, source archives and before/after provenance are checked.
No rebuilding, seed substitution or current-main binary replacement is permitted.

The collector and auditor are unchanged. The [registered plan](EXTERNAL_NOOP_BOUNDARY.md)
still has 40 calls over 20 independent fixtures: 20 prime and 20 no-op, two methods,
four alternating A/B pairs and one B/B control per method. Arguments, sources,
order and 180/10/5-second waits stay fixed. Do not pool methods, subtract clocks
or B/B overhead, or replace the original score with new measurements.

`retained_noop_executor.pins.json` pins canonical-LF hashes of five execution-critical
files. Checkout and snapshot bytes are retained unchanged; CRLF normalization is
only a disclosed hash comparison. The collector checks its own raw-byte hashes.
The reviewed canonical manifest digest is supplied independently; merely trusting
a manifest that could be modified with its files is insufficient. The exact
approved commit additionally covers tests, documentation and contract workflow.

## Single admission

Before a real dispatch, formally review the exact main commit and manifest digest
and record one allocation in #198. Inputs are `reviewed_commit`, `manifest_sha256`
and fixed allocation `701-boundary-001`, passed through environment variables,
not interpolated into shell code. Admission checks repository, main ref, dispatch
event, matching event/workflow/checkout commit, workflow run number1 and attempt1.
The first request is recorded before download and consumes the opportunity even
if download, preparation or the first call fails. No second dispatch, rerun,
workflow deletion/rename to reset numbering, resume or sample refill is permitted.
These guards prevent mistakes; they do not constrain a maintainer who can modify
code or fabricate environment values.

Implementation and passing contracts are not allocation. This implementation
round performs no study dispatch. Formal review, merge and new-main acceptance
must precede freezing the final commit/manifest and explicitly allocating a run.

## Execution and evidence

The wrapper verifies admission, pinned source and input before saving snapshots,
the original ZIP, receipt and interpreter identity in a new root. It selects one
Python Application in lookup order, without fallback. A local inherited alias
binds the unchanged collector's Python checks to that path. Read handles deny
writes/deletion of that interpreter and retained source files while in use; this
is not a hermetic interpreter, stdlib, DLL or runner-image snapshot.

There is exactly one call to the original collector. The first unexpected result
retains the prefix and unused slots without repair/refill. Closure rechecks source,
original input, interpreter identity, all per-call judgments and saved study audit.
`cause` stays null and `clears_hold` stays false.

The collection step has a15-minute bound within a20-minute job, leaving a possible
upload window. `always()` upload covers all available `execution-out/`, including
hidden fixture caches, original input and failures. Permissions are contents:read
and actions:read; the download token is not passed to the execution step, and
checkout credentials are not persisted. Cancellation, runner loss or artifact
service failure can still leave missing evidence, which blocks acceptance. The
legacy synchronous call has no per-call interruption and arbitrary descendant
cleanup is not established.

## Read-only inspection

Inspect original inputs without launching a program:

```text
python tests/native/retained_noop_executor.py inspect-input ORIGINAL.zip --output NEW_IDENTITY.json
```

After a complete run, audit retained evidence offline to a new output file:

```text
python tests/native/retained_noop_executor.py audit EVIDENCE_ROOT --output NEW_REVIEW.json
```

Offline audit checks saved interpreter identities, not unavailable old-runner
executable bytes; live closure also checks the actual interpreter. Journal
consistency is not atomic provenance, absence of unlogged processes or causal
attribution. No root cause is assigned by the checker.

The original #207/#701 +3.0963ms/+26.70% hard failure and all adverse samples remain.
Successful collection does not clear HOLD or authorize default observation,
clean/prune, Rium qualification or a5.7 release.

## Primary workflow contracts

[Dispatch/ref/commit](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_dispatch),
[run and attempt numbers](https://docs.github.com/en/actions/reference/workflows-and-actions/variables),
[step/job timeouts](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax),
[artifact ID and unextracted download](https://github.com/actions/download-artifact/blob/v8/action.yml).
