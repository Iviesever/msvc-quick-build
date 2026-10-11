# Implement the first offline observation of refusal 012

Status: `IMPLEMENTED_SYNTHETIC_VERIFIED_NOT_REGISTERED_NOT_EXECUTED`.

This executes the accepted
[012 handoff](../20261011-013406-producer-handoff/native_startup_handoff_012.md).
It does not reopen 011 or preselect a parser correction.

## 1. Baseline and finite change set

Start from `3bf20f99414cfaf059564773db365f8aabccde5b`, tree
`95f16e77b3d03f3066d11d4c9dedd84877311296`. Fresh remote checks retain main
`87f86770cfc7e1941603074ace914e0c7faf10d3`, draft/HOLD PR250/248/207 and 11
workflow runs, each attempt 1. There is no 012 registration or execution.
The existing linked worktree is clean and is not a submodule. Its 119 pure
baseline tests pass. Fifteen retained input/source/review hashes match 011.

Add small units under `tests/native/`, following the repository's diagnostic
location: `producer_bcrypt_refusal_012.py` for pure context and
`run_bcrypt_pe_diagnosis_012.py` for the offline owner. A focused IO/manifest
helper is permitted if it keeps ownership and verification independently clear.
Each source unit should stay approximately 200-350 lines, without copying the
frozen parser or introducing a general execution framework. Tests reuse the
existing synthetic PE fixture. The old workflow has exact path triggers that
do not include these new filenames; verify all workflows before publishing.

## 2. Pure refusal context

Expose `diagnose(module, image_bytes, init_rva)` returning a small JSON-compatible
object. It makes exactly one `inspect_bcrypt_pe` call. States distinguish located
refusal, unavailable context, unexpected success and unexpected exception;
`static_index_admitted` is always false. Do not retain a successful full index.

Bind the terminal frozen `_require` frame and unique innermost unwind guard by
code-object identity and exact source line (427/438/440). Bind outer unwind
frames, the root exception-table frame and entry call to their frozen code
objects and call sites. Require one shared `_Image` and exact input bytes object.
Root table ownership and leaf chain ownership remain separate.

Collect only whitelisted bounded scalar fields and the already-mapped leaf
envelope. The allocation guard at line 427 always reports current width as
unavailable; a previous loop value is stale. Likewise, do not read the root
frame's previous `info` result while its current RHS is failing. Header and slot
views reference the one envelope; they add no raw read. Do not call the parser's
map/read/inspection methods to obtain context. No full locals/frame/self dump.

Fixed caps: 64 traceback nodes, 32 chain RVAs, one 528-byte envelope plus one
12-byte root entry, 16,384 explicitly charged context work units, 8 KiB encoded
core result. Missing, ambiguous, wrongly typed or out-of-range mandatory context
ends in a bounded unavailable result. Discard traceback references afterward.

## 3. One offline invocation owner

The manifest fixes schema/stage, exact input and RVA, all implementation/frozen
source hashes, resolved CPython identity, argv, output directory and fixed caps.
Fully read and hash-check it before use. Verify source bytes before compiling or
importing them; avoid resolving a same-named unverified module through cwd.

The owner exclusively creates and fsyncs a <=1,024-byte consumed marker keyed by
the manifest hash before process launch. It never deletes, replaces or reuses
that marker. Existing output/marker refuses re-entry. Crash, timeout, missing
report and protocol failure consume the reservation; no new ID or automatic
retry follows. Persist only the marker and one <=15,360-byte report for this call.

Use at most one CPython worker with a bounded READY/GO pipe protocol. The worker
cannot invoke the parser until the owner grants its one GO. The owner starts an
absolute five-second deadline before launch; if startup has already exhausted
that deadline, do not send GO. Read bounded binary output, stop on cap/protocol
failure, and terminate the exact owned worker on deadline. No general command
execution, grandchildren or native workload is part of the worker.

The five seconds is an observed execution-admission deadline. OS process
creation, termination and fsync cannot be promised to return within an absolute
wall-clock limit; delayed return is recorded as failed/consumed. Do not silently
add the Windows runner's ten-second cleanup allowance. Verify image and source
hashes before/after the observation. Report <=15 KiB including owner metadata,
with the core bounded to <=8 KiB, and marker+report <=16 KiB. Output uses exclusive
create, file and directory fsync, then independent complete byte readback.

## 4. Implementation and verification sequence

1. Write meaningful synthetic context tests and observe assertion failures for
   the missing feature. Cover all three same-reason guards, stale width after a
   multi-slot operation, multiple table entries, one/multi-slot progression,
   chain lineage, missing values, map truncation, identity mismatch and caps.
2. Implement the smallest context unit; retain the RED/GREEN logs. Confirm the
   frozen parser's existing accept/reject tests and source hash are unchanged.
3. Write owner tests before its implementation: altered manifest/input/source,
   consumed marker, existing output, unexpected worker data, bounded output,
   timeout/startup delay and zero calls before GO. Use only synthetic data.
4. Integrate the two units, run the focused suite and required existing pure
   suites, inspect the exact diff and all workflow triggers. Check code size and
   avoid incidental refactoring or duplicate fixtures.
5. Independently review exact source, tests, bounds and error paths. Close
   Critical/Important findings before real-input admission.
6. Create reviewed blobs, based tree and immutable commit through Connector.
   Write and fully read back the exact invocation manifest and #198 registration.
   Freshly verify refs/runs, then update the feature ref with expected old HEAD
   and force=false. This source update must not activate a workflow.
7. Only after those gates, run the owner once against the retained 011 bytes.
   Independently read back marker/report and validate every context field against
   frozen source and raw bytes without invoking the parser a second time.
8. Retain the exact raw envelope and a minimal synthetic regression preserving
   the observed predicate. An inability to locate context is also retained and
   stops this step. Determine any next compatibility proposal from primary rule
   evidence; this observation does not itself qualify a correction or objdump.
9. Update current progress, result and next handoff; include the smallest new
   synthetic regression in `test_bcrypt_refusal_regression_012.py` if context is
   located. Review that finite documentation/test closure through Connector,
   verify its exact paths activate no workflow, and deliver bounded evidence.

## 5. Permanent boundaries

Preserve original result `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`, outer Job
total 3 versus required 2, no native_summary, original PE refusal and all spent
001-011 counts. Do not reacquire or substitute the DLL, execute it, download
symbols, call objdump, change privileges or modify production C++/VERSION/cost.
All A1-A5, deletion, ordinary qualification and canonical_evidence_sealed remain
false. Only source-level context is newly observable in this step.

## 6. Completed synthetic checkpoint, before registration

The five new source/test files are frozen for independent review. The context,
owner and IO helper are 275, 262 and 198 lines; they do not copy the PE parser.
The integrated test command runs the four original pure modules plus
`test_producer_bcrypt_refusal_012` and `test_bcrypt_pe_diagnosis_012`: 174 tests
pass. `integration-validation-012.json` records the exact command, source
fingerprints before/after, and output; its SHA-256 is
`e61d181805a0c61224e620e60778322a83c3a8c3edf667cda84a43f1fff70555`.

Actual RED evidence was retained for the missing features, DEL escaping before
JSON serialization, bounded exception failure codes, and manifest postcheck.
The serializer counts ASCII DEL as six escaped bytes before encoding. The
owner verifies the manifest after GO as well as the input/source/tool bytes.
Its stream acceptance ceiling is 8,230 bytes; at most one overflow-detection
byte can be read, and 8,231 bytes always fails. A located report is admitted
only jointly with a clean owner exit: a save/fsync/readback returning after the
deadline produces a nonzero exit and consumes the call.

Real-input calls, new native work, workflow allocations and objdump calls in
012 remain zero at this checkpoint. Publication and registration do not follow
until the independent source and current scope reviews pass.
