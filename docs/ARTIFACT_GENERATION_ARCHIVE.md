# Versioned ordinary-target generation evidence

**English | [简体中文](ARTIFACT_GENERATION_ARCHIVE_ZH.md)**

## Scope and API

`ArtifactGenerationArchive` is an owning, unverified selection of completed
ordinary EXE/DLL/static-target evidence. It gives the existing generation model a
stable v1 byte representation and a strict read-back path. It is an explicit
pure API: there is no default CLI adoption, file writer, automatic persistence,
trusted producer registration, recovery, or clean/prune operation.

The public interface is `mqb/orchestration/ArtifactGenerationArchive.hpp`:

| API | Input and result |
|---|---|
| `project_artifact_generation_archive` | Borrowed completed ordinary records, explicit retention requests, lexical root and path key → owning selected claims |
| `encode_artifact_generation_archive` | Owning archive and lexical key → v1 bytes, after whole-document measurement and strict model admission |
| `decode_artifact_generation_archive` | Byte view and lexical key → owning archive, after a complete non-owning scan and strict model admission |
| `model_archived_artifact_generations` | Owning claims and lexical key → the existing rich generation model and per-source contexts |

The codec belongs to orchestration because its selected requests and diagnostic
types belong there. It does not introduce a core → orchestration dependency or
another cache validator. `Archived*Completion` and `Archived*Claim` are separate
types: read-back never constructs a `Recorded*Result`, `ProcessResult`, process
specification, environment, plan, timer, or executable recovery command.

## Selected facts and shared validation

The archive owns the explicit historical source locator, project/target and
optional generation claims, retention requests, root spelling, caller labels,
ordinary target records, source-ordered requests and cache evidence. It preserves
both the inspection and cache compiler identities, original opaque signatures,
every selected compiler/LINK/LIB option in order, repeated paths/options, recorded
dependencies and include roots, and each stage's own working directory. Compile
save errors retain code/file/offset/message; compile and terminal warnings retain
code/path/message. Force, completion and save/reuse outcomes remain diagnostics,
not recipe identity. Only the accepted final ordinary compile wave is selected.

Live and archived records use the same `project_recorded_target` validator and
the same generation admission, recipe traversal and `finish_model` association.
Source/order/request/cache/terminal or real role conflicts still reject the
whole request with the original issue, source slot, message and batch record
slot. Unsupported module-scan graphs are not traversed or copied; typed admission retains
the original ordinary-projector refusal. Wire module-scan presence is refused.

Missing compiler/generation/origin, duplicate declarations, mixed reuse, recipe
disagreement, and a valid but contradictory historical snapshot remain the
existing model's visible issues. Read-back does not fill missing context from
the host or choose an origin from input order. A schema-invalid snapshot cannot
be encoded; a schema-valid snapshot that disagrees with a terminal remains a
`snapshot_mismatch`. A LINK snapshot does not attest a static archive.

## Fixed binary schema v1

All integer framing is unsigned little-endian with the specified width. Text is
UTF-8 without NUL; paths preserve their captured spelling, with no normalization
or filesystem lookup. A string is a u32 byte length followed by exactly those
bytes. A vector is a u32 element count followed by its elements. An optional is
one byte, exactly 0 or 1, followed by its value only when present. Booleans are
also exactly 0 or 1. There is no padding, host-object dump, checksum, or trailing
data. The in-memory `recipe_evidence` string is not part of the wire format.

| Document order | Representation |
|---|---|
| Magic | Eight ASCII bytes `MQBGARCH` |
| Version | u32, exactly 1 |
| Document type | u8, exactly 1: unverified ordinary generation claims |
| Authority flags | u8, exactly 0; any set or unknown bit is refused |
| Lexical root | Path string |
| Records | Vector of records below |
| Retention requests | Vector of project string, target string, generation string |

Each record is source-id string, project string, target string, optional
generation string, target variant, then optional snapshot. Variant tag 1 is a
LINK-backed ordinary target (EXE or DLL); tag 2 is a static LIB target. A snapshot
is a string containing the existing strict `mqb.link-fact-snapshot` v1 encoding;
its own field/size/UTF-8 rules are retained.

The following order is fixed for each selected value. Nested vectors and
optionals use the framing above; signatures are high-u64 then low-u64, copied
from the original digest without recomputation.

| Value | Ordered fields |
|---|---|
| Target claim | Target record, compile evidence vector, source completion vector, any-compiled boolean, terminal-executed boolean, terminal warning vector |
| Target record | Optional caller label (target, generation), compiler options, source association vector, additional object inputs, LINK or LIB record |
| Source association | Source, object, dependency JSON, compile-cache path, completion, warning boolean |
| Compile evidence | Request, inspection compiler identity, cache entry, evidence state, optional save error |
| Compile request | Translation unit, compiler options, cache path, dependency JSON, optional scan-output path, optional cwd, force boolean |
| Compile cache | Source, unit kind, compiler identity, signature, artifacts, dependencies, include roots, module-scan-present byte (must be 0) |
| Source completion | Source, compiled boolean, warning vector |
| Compiler identity | Compiler path, version string, opaque stamp string |
| Compiler options | Configuration, architecture, standard, optional runtime, LTCG boolean, defines, include paths, arguments, optional PCH binding, external providers |
| Translation unit | Source, kind, optional header-unit identity, dependencies, module references, header-unit references, artifacts |
| PCH binding | Header path, artifact path, role |
| External provider / module reference | Logical name, interface path |
| Header-unit identity / reference | Header name, lookup method; a reference additionally has its interface path |
| Artifact | Path, kind |
| LINK record | Completion, cache state, LINK cache, LINK options, cache path, optional cwd |
| LINK cache | Linker identity (path/version/stamp), signature, objects, main output, libraries, file inputs, side outputs |
| LINK options | Configuration, architecture, target kind, subsystem, LTCG, optional ASan runtime, optional VCAsan runtime, optional fuzzer runtime, OpenMP boolean, library directories, library names, arguments |
| LIB record | Completion, cache state, LIB cache, architecture, LTCG, arguments, cache path, cwd |
| LIB cache | Librarian identity (path/version/stamp), signature, objects, main output |
| Warning | Code, path, message |
| Compile save error | Code, file, u64 offset (must fit host size_t), message |

Enums use explicit consecutive wire IDs starting at 1 in the orders below,
independent of C++ enum ordinals. No other value is accepted.

| Enum | Wire order |
|---|---|
| Configuration / architecture | debug, release / x86, x64 |
| C++ standard | cpp14, cpp17, cpp20, cpp23, latest |
| Runtime | md, mdd, mt, mtd |
| LINK target / subsystem | executable, dynamic_library / console, windows |
| PCH role / header lookup | create, use / angle, quote |
| Unit kind | source, module_interface (ordinary semantic admission still rejects a module unit) |
| Artifact kind | object, module_interface, precompiled_header, executable, dynamic_library, static_library |
| Completion | executed, reused |
| Artifact cache state | saved, reused, save_failed |
| Compile evidence state | reused, saved, save_failed |
| Compile/LINK/LIB warning | cache_load_failed, cache_save_failed, file_snapshot_failed |
| Compile save error | file_open_failed, file_read_failed, file_write_failed, invalid_magic, unsupported_version, corrupt_data, replace_failed |

## Bounds, errors and ownership

| Limit | Maximum |
|---|---:|
| Document bytes / one text field | 16 MiB / 1 MiB |
| Total UTF-8 text payload, including snapshots | 8 MiB |
| Total vector elements / scalar framing fields | 65,536 / 262,144 |
| Records / retention requests | 256 / 256 |
| Total compile-plus-terminal stages | 4,096 |
| Source associations or compile/result entries in one target | 4,095 |
| Selected path fields | 65,536 |

Both the wire limits and the original model/projection limits apply; neither can
be raised by an archive. The scanner checks the entire outer document's bounds,
counts, flags, enums, UTF-8 and exact end before document string/path assignment,
vector growth, snapshot decoding or lexical callbacks. Its temporary typed
objects hold no document-owned values. The second pass uses the same traversal
to allocate selected values, then calls the shared semantic model. The existing
bounded snapshot decoder may allocate only after outer preflight has succeeded.

Encoding first measures the same schema, then admits the complete model, then
allocates output bytes. Live capture measures the selected wire fields and
validates the original live model before copying an archive. Bounded path UTF-8
conversion and the established snapshot encoder can use temporary storage.
Errors return no partial archive or byte result. Allocation and pure callback
exceptions propagate. These are logical limits, not a total heap, allocation
count, peak-memory, resident-memory or execution-time guarantee.

The same captured path can have different lexical meaning on Windows and Linux;
host path semantics and the supplied pure key must be compatible when comparing
models. Byte framing is portable; physical path identity and cross-platform
equivalence are not inferred.

## Verification and remaining boundaries

The native archive suite exercises deterministic round trips, owning history,
EXE/DLL/LIB cold and reused claims, partial/failed saves, ordered options,
per-source identities, provenance and snapshot contradictions, strict errors,
malicious lengths/counts/tags/UTF-8, and every truncation of a populated fixture.
Existing generation checks remain. The existing real lifecycle helper adds
in-memory round-trip assertions after evidence preservation without adding
MQB/compiler/linker/librarian/observer calls. The new native program itself has
normal build and runtime cost; these assertions are not a performance study.

Producer identity, current content, complete inventory and deletion authority
remain false. Valid bytes, successful read-back or a selected retention request
do not authenticate a writer, prove existing files or authorize deletion.
Physical publication/atomicity, reparse handling, concurrent writer exclusion,
recovery, clean/prune and Rium acceptance still require independent work. No
unfinished #164 facility is assumed. Original performance failures and adverse
samples, incomplete dependency coverage and unmeasured explicit allocation/
latency costs remain; this codec does not authorize default/high-frequency use
or release v5.7.0. VERSION stays 5.6.0.
