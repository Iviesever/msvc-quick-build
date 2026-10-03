# Explicit target generations and retention claims

**[简体中文](ARTIFACT_GENERATIONS_ZH.md) | English**

## Scope and use

`model_artifact_generations` consumes completed ordinary, static-LIB, or module
`TargetArtifactRecord` values and explicit retention requests. It returns owned
historical associations, conflicts and selected references. It performs no build,
scan, clock read, file IO, registry update, deletion, migration or automatic GC.
There is no default build/CLI integration and no persistent model format.

Each input supplies a unique historical `source_id`, explicit project/target keys,
an optional generation key, the original compiler context (or unknown), its typed
target record, and optionally a link-main-output snapshot. Caller annotations stay
separate. Keys are case-sensitive opaque claims, not names recovered from paths,
labels, timestamps, or cryptographic/authenticated writer identities.

## Identity and lineage

The model preserves selected effective compiler/link/LIB fields, toolchain path,
version and opaque stamp, terminal signature, ordered options, declared paths and
module compile/import evidence. Length framing compares these fields exactly;
it is not a replacement cache signature. Duplicates and option order are not
normalized. The old records do not contain the compiler context, so missing
context remains `missing_compiler` rather than being reconstructed.

An executed record with a distinct explicit key is an **unverified generation
claim**, not a proof of production. Reuse must name a single executed claim in
this batch, with the same project/target and selected recipe evidence. Reuse
never creates an origin, follows a chain of reuse-only records, or picks the
latest matching signature. Executed stages inside a claimed whole-target reuse
are reported as `mixed_reuse`. Duplicate provenance/generation claims, malformed
completion pairs, absent origins, recipe conflicts and snapshot mismatches remain
visible; duplicates are not silently merged and unrelated generations are not
ordered by input position.

`recipe_evidence` is diagnostic in-memory data, not stable serialization or a
complete content/environment identity. In particular, compilation content stamps,
ProcessSpec environments and the entire P1689 graph are not captured here. The
model does not prove equal runtime behavior, validate an arbitrary forged record,
or authenticate that the caller supplied the actual compiler context. An optional
snapshot uses the existing strict codec and is checked against the terminal link;
it never fills missing target identity or invents a complete artifact inventory.

## Path conflicts and explicit retention

The existing `project_storage_references` and `associate_artifact_storage` own
role projection and lexical path joining. Supply the platform's existing pure
`StoragePathKey` and an absolute lexical root. The model passes an empty inventory,
not a filesystem walk. Relative paths need their own recorded stage cwd: the link
cwd, inventory root and current process cwd never substitute for missing source
context. Parent traversal remains unresolved. The key callback must be stable,
lexical-only and side-effect-free; its exceptions propagate.

The output preserves source/stage/path indices and input/output/metadata roles.
Shared object/PCH/IFC references, several executed generations at the same output,
different target/recipe claims and unresolved members are flagged. These are
possible sharing/overwrite conflicts, not observations of the current file.
Different lexical paths may alias physically; this layer does not discover such
aliases or turn an old observed file ID into trusted producer identity.

Each explicit retention request is kept, including duplicates, missing origins
and ambiguities. `selected` means that the request identifies a unique historical
claim; associated reuse records and their reference paths are included. It is not
proof that every referenced file exists or is covered. Unresolved paths remain
visible even with a selected claim. Unselected, missing and conflicting records
are **not retired**, and this API returns no delete list or reclaimable-byte sum.

## Limits and tests

There are at most 256 input records and 256 retention requests, 4,096 projected
stages, 65,536 paths/items, and an 8 MiB cumulative selected text budget. Invalid
explicit keys, exhausted budgets or invalid path-key/root input reject the whole
result; there is no truncation or partial-success fallback. Path conversion,
allocation and callback exceptions may propagate without modifying caller inputs.
Result copies keep source IDs, labels, supported snapshots and references valid
after the caller changes or destroys its input records.

`artifact_generation_model_tests.cpp` exercises regular/static/module records,
explicit reuse and missing origins, every existing option category, order changes,
duplicate claims, cross-configuration outputs, shared object/PCH/IFC references,
IFC-only producers, malformed/mismatched snapshots, missing compiler context,
relative paths, bounds and callback failures. These are pure model tests, not
real Windows filesystem, cross-process writer or performance tests. Existing CI
runs this program with the unchanged native driver apart from its new manifest
count. Python checks maintain the original test/recipe/IO authority boundaries.

## Authority and next boundaries

Producer identity, current content, complete producer inventory and deletion
permissions remain **false**. No record absence, cache miss, generation name or
retention selection is authorization to remove files. This first model does not
register trustworthy writers, allocate globally unique generations, merge records
across processes, commit/recover durable state, or revalidate file identities at
cleanup time. Those mechanisms and Rium qualification remain separate required
work. The existing performance evidence and adverse samples are unchanged; this
non-default model is not evidence of zero cost or permission to publish v5.7.0.
