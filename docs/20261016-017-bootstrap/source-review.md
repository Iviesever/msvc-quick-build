# 017 bootstrap source review: startup and final record integrity

2026-10-16. Reviewed baseline: `b6fb3f73febac0e6b8e54129dfae4a289479b200`.
This is a focused review of the existing launch/binding/receipt path, not a rewrite,
full-repository audit, independent execution admission, or formal target observation.
The fixes below were authored and tested in this review; they have no second-reviewer sign-off.

## R1: native threads before preexec

The owner used Python thread bookkeeping, while the native adapter invokes a
Python `preexec_fn`. That bookkeeping is not a complete census of threads created
by native libraries. The containing supervisor's earlier single-task check also
cannot establish the later owner's task state after imports and preparation.

The adapter now checks `/proc/self/task` immediately before its sole Popen call,
after output preparation. It reads at most two entries and accepts exactly the
current process leader. Missing/inaccessible procfs, empty/foreign/multiple task
views refuse with PREPARATION; there is no alternate PID or host-scan fallback.
The launch reservation remains consumed; existing empty captures are retained.
No argv, child limit, shell/environment rule or retry policy has changed.

This is a checkpoint, NOT a lock against later thread creation. A qualified
single-threaded preload/library policy remains necessary. No native thread was
created by the new tests: finite task views and Popen are explicit test doubles.
Existing unit paths may read their own actual task directory but launch no workload.

## R2: saved controller records were not rechecked at closure

ControllerRecords previously checked a record only while saving that record and
then closed only the directory descriptor. An earlier reservation could change,
disappear, gain another link, or be replaced with equal bytes; an extra directory
entry could also appear without preventing a successful controller return.
A real temporary-file counterexample mutated reservation.json after assessment
was saved and reproduced this incorrect success on the reviewed baseline.

The existing class now retains bounded immutable bytes and post-write identity
for each owned record. Directory checks admit exactly those owned names, not just
any name in the allowed vocabulary. Before closing, it rereads all saved records
and verifies bytes and descriptor/path identities, then checks the whole set again.
Unknown/partial files remain on disk; no truncation, overwrite, deletion or retry.
The directory descriptor is closed even when verification fails, and the first
verification error is preserved if descriptor closure also reports an error.
Controller.run already propagates this as persistence failure, removes the returned
success assessment, and leaves old candidate records unchanged.

This check does not establish an atomic filesystem snapshot or non-descendant
writer exclusion. It does not replace the existing externally pinned writer
premises or the containing controller process's independently recorded clean exit.

## Added resource cost

- Launch checkpoint: one self-task directory, at most two entries, closed by context.
- Retained controller payload: at most five immutable records x 32768 B = 163840 B,
  plus five small identity tuples and bounded names; no unbounded directory listing.
- Final readback: at most five positional reads, each requested at saved length + 1;
  total declared content <=163840 B plus <=5 overflow/EOF-probe bytes.
- Existing per-save fsync/readback stays intact. Namespace/stat checks inspect only
  the five owned names and stop at the first unknown entry.
- The controller's existing 16-second cooperative deadline still covers closure.
  Synchronous calls and process creation are not given a new hard-return guarantee.
  These added reads/storage are separate from the previously counted identity passes.

## Distinguishing tests

The original 292 unit methods remain unchanged. The first 14 new review methods
produced 14 failure records on the baseline (including subtests; not 14 defects).
The identical assertions passed after these two repairs. Three further controls
cover the maximum final-read budget, a final read error with descriptor closure,
and retention/refusal of a partial write. Total: **309 PASS, 0 FAIL, 0 ERROR, 0 SKIP**
locally; CI status must be established from this revision's own run and artifact.

Prior source, the first 14-method test version and RED log are retained in the
delivery package. The old 292-method log and final 309-method log are distinct.
The unchanged five-case dev004 smoke and its consumed workflow are not rerun.
The existing bootstrap contract workflow is extended, not replaced by another
study. Its dev004 manifest stays immutable; only the reviewed native-adapter
source hash is explicitly superseded, all other retained hashes remain enforced.

## Actual historical inputs recovered (not mock provenance)

The Library archive `msvc-quick-build-017-preflight-20261013.zip` was recovered:
7,095,226 B, SHA256 `4f91da0a801efd07d99e8ac5a80a4be2a04f1cb82b8a28327a6b339a17a1b5d8`.
Only the five named index/caller/runtime/closure/proposal JSON members were selected;
all **318,238 B** matched the unchanged preflight module's published length/SHA256
pins. The original `preflight` returned `METADATA_READY_EXECUTION_NOT_ADMITTED`.
No PINS were patched for this check. The exact metadata and read result remain in
the private delivery, not copied into the source repository.

This is reuse of archived metadata, not rerunning 016 or performing a target
preparation. Nested archives and original image contents were not opened; the
103-byte target SHA remains null, and both formal preparation/observation counts
remain zero. Those historical inputs need not be rediscovered on the next handoff.
They are not substitutes for NEW 017 source/loader/writer qualification or two
independent, admitted target preparations.

## Handoff

Review this limited repair and its resource delta, then obtain the missing actual
017 host/loading/writer qualification and separately admitted preparations before
a registered end-to-end invocation. Existing bootstrap, controller, transport,
subreaper and completion evaluator should not be rewritten or rerun as dev004.
The recovered five metadata inputs are ready; formal input/authority records are not.
Main, PR250/248/207, VERSION5.6.0, HOLD/DEFER, deletion and release gates are unchanged.

Primary interface references:
- https://docs.python.org/3/library/subprocess.html (preexec_fn thread warning)
- https://docs.python.org/3/library/threading.html (active_count/enumerate scope)
- https://www.kernel.org/doc/html/latest/filesystems/proc.html (task directory)
