# Terminal file observation (internal, opt-in)

[简体中文](TERMINAL_FILE_OBSERVATION_ZH.md)

## Successful invocation versus subsequent observation

```cpp
auto result = linker.run_observed(request, mqb::platform::windows::observe_storage_file);
```

The new `MsvcIncrementalLinkCoordinator::run_observed` calls the existing `run_recorded` once. A link/validation failure returns its original typed error without invoking the observer. After success or reuse, only the main output from that invocation's actual link record is observed. Its own recorded absolute working directory resolves a relative output; the process working directory is not borrowed. PDB/ILK, objects, caches, other side outputs, whole targets, modules, PCH and LIB are not newly observed by this slice.

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

The new independent E2E preserves all 87 existing test sources; native inventory is 88. Per Debug/Release it allows one source compile, eight terminal calls (seven successes and one expected link failure), three actual link processes, twenty single-file observations, one successful program and one junction command. It covers cold/reuse, a writer opened after success, throwing/empty observers, deliberate post-success removal, save-warning preservation, a failed link suppressing observation, renamed original versus distinct replacement, hard links, missing parent/directory, reparse ancestors/leaves and invalid/deep paths. All mutations are within a fresh fixture; no failed link is repaired/retried.

Commands/results/diagnostics, selected input bytes, successful link records and observation metadata are retained in `storage-evidence/file-observations` before corresponding assertions. They are not a complete recipe/environment or atomic file-system snapshot. The original full native regression, self-host/package/installer and independent 19-by-4 ABBA remain required. The standard matrix does not directly measure explicit observation cost; default-off status is not a zero-overhead promise. Existing scale/no-op/link/module tails and recorded/PCH/static/join specialised gaps remain pending, as do real Rium multi-target/AOT qualification, persistence/concurrency and safe cleanup.
