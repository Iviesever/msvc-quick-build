# Explicit completed-link archiving

[简体中文](LINK_COMPLETION_ARCHIVE_ZH.md)

## Scope

`archive_link_completion(completed, destination, capture_label)` borrows a const
`ObservedLinkResult` which the caller already obtained. It projects that result
with the existing `capture_link_fact_snapshot`, then invokes the existing
`create_link_fact_snapshot_file` once. It does not start another build, call an
observer, inspect the current output, load a cache, or choose a default path.
There is no default build/CLI integration. A failed build does not contain an
`ObservedLinkResult`; the caller handles that original failure without archiving.

## Independent outcomes

The result is a file receipt or `LinkCompletionArchiveError`, a variant of the
original capture error and original file error. All native stage/code/known-byte,
file-created, secondary-close and nested-codec fields remain intact. The input's
build success/reuse, warnings, process diagnostics and observation are not moved
or rewritten. Capture refusal calls no writer; file failure is not retried.
Allocation and unexpected exceptions propagate; the caller retains its input.

```cpp
// completed is an existing successful ObservedLinkResult; destination is explicit.
auto archived = mqb::orchestration::archive_link_completion(
    completed, destination, "caller annotation");
// Handle archived separately: its failure does not erase completed.build.
if (archived) {
    auto historical = mqb::platform::windows::read_link_fact_snapshot_file(destination);
    // This is a separate later read, not an automatic write verification/receipt.
}
```

## File and authority boundaries

The original [single-file contract](LINK_FACT_FILE.md) and strict bounded JSON v1
codec apply without modification: absolute local NTFS paths, create-new only,
1 MiB bound, no overwrite, parent creation, retry, rename or cleanup. A failed
write can leave a partial or complete file. No implicit readback attempts to
turn that failure into success. Neither a receipt nor valid historical JSON
proves producer identity, current content, a complete inventory or deletion
permission. Annotation is not a trusted clock or generation identifier.

No crash-atomic commit, directory durability, transaction, cross-call writer
coordination or protection from every mapping/privileged mutation is added.
Synchronous file IO has no hard timeout. Explicit file IO cost is not covered by
the normal benchmark matrix, and non-default use is not a zero-cost claim.

## Validation and retained evidence

The private operation seam tests the same capture/forwarding body with synthetic
writers: successful receipt, original capture refusal, all file-stage fields and
exceptions. These are not Windows disk-failure tests. Windows E2E uses one real
source compile and four link completions (three linker processes, including an
expected failure); three observations precede seven explicit archive attempts.
It checks cold/reused/warning-bearing roundtrips, existing-name conflict, missing
parent, capture refusal and sharing refusal. Archive attempts must add no tool,
build or observer calls; selected output/cache bytes and timestamps stay unchanged.

Evidence is retained under `storage-evidence/completion-archive/`: per-process
requests/results, archive attempts/outcomes, original image/cache bytes, three
history JSON files, checks and completion counts. Failure prefixes are preserved.
The existing 93 native test programs remain unchanged; two additive programs
bring the manifest to 95. The native CI must run on the exact candidate before
Windows acceptance; local synthetic checks do not replace it. No new performance
run, default adoption, Rium/clean/prune authority or release follows from this adapter.
