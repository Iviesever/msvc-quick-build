# Recorded ordinary-target storage projection

**English | [简体中文](RECORDED_STORAGE_PROJECTION_ZH.md)**

## Scope

`project_storage_references(RecordedTargetResult, path_key, limits)` and the
`RecordedStaticTargetResult` overload are explicit, pure, owning consumers of
already captured final-wave evidence. EXE/DLL and static targets retain their
source order and the original terminal LINK/LIB projection. Default
`run/inspect/CLI`, admission-stop, generation models, persistence, writer
coordination and `clean/prune` are unchanged. This does not release v5.7.0.

## Selected facts and correspondence

The result owns `references` plus one `CompileStorageContext` per compile stage.
Each stage uses its own recorded working directory, compile configuration,
completion and exact `saved/reused/save_failed` outcome. Captured dependencies
and include-search namespace roots are input references; object is a declared
output, dependency JSON and cache paths are metadata references. A consumed PCH
remains input. Additional objects remain upstream terminal inputs, not invented
local producers. Paths preserve their spelling, order and repeated mentions.
Unresolved noncritical inputs stay literal for the existing join to report.

The two compiler identities are copied separately from the inspection context
and captured cache. Differences are flagged, not normalized into a fictitious
single actual compiler. Captured signatures are opaque values: the projector
does not rehash recipes, read files or authenticate supplied structs. Compile
warnings and the precise save error (including code, file, offset and message)
remain owned values. Full process output and toolchain environment are not copied.
A `save_failed` cache is an attempted value, not successful persistence; reuse
does not create a new execution generation. Caller labels remain annotations.

Mismatched source counts/order, source/object/metadata paths, complete structured
compiler options (including ordered arguments/providers), cache output shape,
completion/warning/save outcomes or ordered terminal objects reject the whole
projection with a typed issue and optional source index. Ordinary overloads do
not reinterpret a module/HU/PCH-creator record. This is selected-field coherence,
not a new compiler validator or proof that an opaque signature matches a recipe.

## Path and resource boundaries

Critical source/output/metadata paths must be absolute or resolve from their own
recorded absolute cwd; empty/NUL/parent-traversal or unresolvable critical paths
are refused. No cwd is borrowed from LINK, the process or a directory walk.
The caller supplies the existing pure platform path key. Duplicate sources,
colliding output/metadata paths, protected-input aliases and local-object
context disagreement at the terminal are refused. Key exceptions propagate.
Lexical identity is never physical identity or current-content proof.

Defaults cap sources at **100000**, selected items at **1000000** and selected
text at **64 * 1024 * 1024 native code units**. Callers may lower but cannot raise
these caps. Checked subtraction precedes copying/bulk allocation; exceeding a
budget returns an error without truncated/partial success. These bounds cover
selected traversal/output, not already allocated caller input, allocator
metadata or execution time of the trusted key callback. No exhaustive memory
or timing qualification is implied. All ownership/inventory/deletion authority
flags remain false. Record-only overloads retain their original unknown fields.

## Verification and remaining gates

Portable C++ contracts exercise typed refusals, EXE/static output, independent
ownership, mixed toolchains/reuse, failed saves, relative contexts, aliases,
terminal associations and lowered limits. Existing real recorded EXE/DLL/static
lifecycles consume their own values without extra cl/link/lib calls or evidence
file writes. Historical source tests use a whole-file pinned exact inverse;
old hashes/assertions are not discarded. Python contracts do not substitute for
Windows Native/Release execution. Explicit copying/allocation, peak/resident
memory and default-path generated-code/performance changes require separate
qualification; this interface is not approved for default/high-frequency use.
