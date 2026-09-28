# Terminal file observation (internal, opt-in)

[简体中文](TERMINAL_FILE_OBSERVATION_ZH.md)

## Successful invocation versus subsequent observation

```cpp
auto result = mqb::orchestration::observe_link_completion(
    linker.run_recorded(request),
    mqb::platform::windows::observe_storage_file);
```

Include `mqb/orchestration/ObservedLinkCompletion.hpp` and
`mqb/platform/windows/StorageFileObservation.hpp` explicitly. The adapter consumes
an already obtained `std::expected<RecordedLinkResult, IncrementalLinkError>`.
It has no coordinator, request, process runner or cache loader and cannot repeat
a build or inspection. In the example the caller executes `run_recorded` once.
A failed input returns its original typed error without invoking the observer. After success or reuse, only the main output from that invocation's actual link record is observed. Its own recorded absolute working directory resolves a relative output; the process working directory is not borrowed. PDB/ILK, objects, caches, other side outputs, whole targets, modules, PCH and LIB are not newly observed by this slice.

`ObservedLinkResult::build` retains the successful result, warnings and record unchanged. `observation` is separate: no observer is `not_attempted`; missing files, rejected paths, occupied files and callback exceptions cannot turn the build into a fabricated failure or an observation into a success. `observer_invoked` records callback dispatch, not successful native IO. A callback must echo the supplied `requested_path`; a mismatched path is unavailable. Callbacks are trusted composition/test boundaries, not a verifier of arbitrary third-party reports. Allocation failures still propagate.

Default `run`, `run_recorded`, CLI, target callers and `mqb storage` do not opt in. There is no new cache format, journal, automatic lock, retention or cleanup behavior.

## Bounded Windows handle observation

`observe_storage_file` accepts one ordinary absolute drive file path. It reuses the storage walk's native-path/read-pin/file-ID helpers. The existing prewrite inventory's strict path predicate is moved verbatim to a private shared header, not reimplemented with different alias rules. Device paths, UNC spellings, streams, reserved names and parent traversal are rejected before opening. Length is limited to 32,700 path characters and depth to 128 relative components (plus the root handle); this bounds work/handles, not synchronous kernel IO duration.

The observer pins ancestors and the leaf with read-data/read-attributes access and read sharing, checks each ancestor and final entry for reparse points, and never enumerates a directory. It requires a local volume-GUID NTFS result. It queries sizes/link count and the volume plus 128-bit file ID through the same leaf handle. A file can have several hard links: the count is an observation, not exclusivity. Sparse/compressed physical allocation remains unavailable rather than being equated with ordinary allocation.

A missing leaf is distinct from a missing/inaccessible parent, which is unavailable. Directories are not regular outputs. Attribute/identity failures retain native diagnostics and do not fabricate zero sizes or IDs. The function reads no content/cache and creates, writes or deletes nothing. All handles close before return, including failures; there is no returned lease or write-domain marker.

Native API basis: [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew), [FILE_ID_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info). Attribute-only access is not a substitute for the existing read-data sharing boundary. OS sharing is not a defense against every hostile process, mapping, filesystem/filter behavior or name-space change.

## Identity and authority limits

Observation occurs **after** link completion. Another actor can replace or remove the path before it opens; the returned ID does not prove the compiler produced that file. After handles close, the path/content can change again. Historical observation values stay immutable copies, not currentness certificates. An unchanged ID is not a content hash, an ownership claim or a cross-reboot permanent identifier.

`producer_identity_verified`, `current_content_verified`, `complete_producer_inventory` and `deletion_authorized` remain false. No observations are retroactively inserted into older build records or storage associations. There is no reclaimable-byte calculation, durable readback, writer exclusion across the whole invocation, retirement decision or `clean/prune` authorization.

## Fixed tests and pending qualification

The successor preserves all 88 current test sources, including V9 adoption;
the native inventory is 89. The migrated independent E2E also runs 26
deterministic completion-result adapter controls without invoking a linker:
all five typed error categories, nested diagnostics, successful/reused results,
warnings/record fields, recorded-cwd resolution, empty/mismatched/throwing
callbacks, refusal states and `bad_alloc` propagation. Per Debug/Release it allows one source compile, eight terminal calls (seven successes and one expected link failure), three actual link processes, twenty single-file observations, one successful program and one junction command. It covers cold/reuse, a writer opened after success, throwing/empty observers, deliberate post-success removal, save-warning preservation, a failed link suppressing observation, renamed original versus distinct replacement, hard links, missing parent/directory, reparse ancestors/leaves and invalid/deep paths. All mutations are within a fresh fixture; no failed link is repaired/retried.

Commands/results/diagnostics, selected input bytes, successful link records and observation metadata are retained in `storage-evidence/file-observations` before corresponding assertions. They are not a complete recipe/environment or atomic file-system snapshot. The original full native regression, self-host/package/installer and independent 19-by-4 ABBA remain required. The standard matrix does not directly measure explicit observation cost; default-off status is not a zero-overhead promise. Existing scale/no-op/link/module tails and recorded/PCH/static/join specialised gaps remain pending, as do real Rium multi-target/AOT qualification, persistence/concurrency and safe cleanup.

## Isolation revision and unchanged release block

This is the successor implementation selected by [review 5325617783](https://github.com/Iviesever/msvc-quick-build/pull/207#pullrequestreview-5325617783),
not a merge or rewriting of original #207. The two existing public headers
(`MsvcIncrementalLinkCoordinator.hpp` and platform `StorageInventory.hpp`) remain
byte-identical to main `433c127566619009579f3f09df3e5627279b24da`.
No existing production caller opts in. The new result adapter and native
observer each have their own product TU, both in the exact `cpp/mqb.json`
manifest. No global compiler/linker options, parallel library or source omission
is used to claim zero overhead.

`StorageReadPrimitives.hpp` shares the old inventory's exact RAII handle, native
path, read-pin and ID conversion bodies. `PhysicalPath.hpp` shares the original
write-domain predicate. The source contracts reverse these extractions and
hash the entire original implementations. This still recompiles the two old
TUs; namespace/inline and source-organization differences are NOT object-code,
ABI or performance equivalence. The literal-include check is not a preprocessor
or compiler dependency trace.

Original #207 remains Draft/unmerged/HOLD at `bd12a59e9894068384d1a9b56b052ce5fec86098`.
Its #701 failure (+3.0963 ms / +26.70%) and all adverse samples remain.
The separate V9 same-job result is `benefit_unproven`, not a repair certificate.
This revision allocates no performance experiment or manual rerun and does not
authorize merge/release from correctness alone. Complete candidate and resulting
main evidence, plus separately reviewed performance disposition, remain required.
Ownership, complete generations, writer concurrency, Rium and safe `clean/prune`
requirements in #198 are not reduced by this adapter.

Historical fact-only codec: [Link fact snapshots](LINK_FACT_SNAPSHOT.md).
