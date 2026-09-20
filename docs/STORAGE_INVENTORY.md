# Read-only storage inventory

**English | [简体中文](STORAGE_INVENTORY_ZH.md)**

This is the first development slice of #198, based on v5.6.0. It is not part of the published v5.6.0 executable and does not complete v5.7.0. VERSION remains 5.6.0 until an independent release PR.

## Commands and scope

```powershell
mqb storage
mqb storage --project C:\work\Rium --format json > C:\reports\rium-storage.json
mqb storage --help
```

The default project is exactly the current directory; the command does not search for mqb.json, load configuration, discover a toolchain, build, run a product, or create .mqb. It reuses ProjectArtifactLayout for the requested existing project directory. A missing .mqb produces an empty observation. Unknown/duplicate arguments are usage errors. No clean, prune, execution switch, age filter, or retention policy is implemented.

Stop builders and other writers first. Output goes only to stdout/stderr; redirect outside .mqb. Read handles temporarily deny write/delete sharing and can therefore cause a competing writer or rename to fail. This is not a build/clean mutual-exclusion protocol, a project lease, or a guarantee against external noncooperating activity.

## Bytes and evidence categories

JSON schema_version 1 supplies per-file raw unsigned logical_bytes, allocated_bytes, hard_links and physical_id; unavailable fields are null, never guessed zero. Decimal GB is 1,000,000,000 bytes, GiB is 1,073,741,824 bytes. Text reports include the same raw file list and summaries.

Directory groups contain direct children only. Extension, direct-directory and evidence groups are disjoint partitions of observed regular-file rows; their known logical totals close against totals. Path allocation repeats each hard-link pathname deliberately. unique_file_allocated_bytes deduplicates observed volume/file IDs; conflicting sizes or any observation issue make it unavailable. Allocation is not reclaimable capacity: links outside the root, filesystem deduplication, metadata and unobserved activity are not established by this count. Compressed/sparse physical allocation is explicitly unavailable in this slice. Alternate data streams and directory metadata are excluded.

Evidence is protected, unknown, cache_reference, or shared_cache_reference. Extensions are descriptive, not authority. Logs, generated inputs, release packages, VS state, unknown directories and non-files remain protected. The ordinary bin/obj/ifc/deps/scan/cache/pch locations are not thereby authorized for deletion: their lifecycle remains unknown. reclaimable_bytes, live_bytes and obsolete_bytes are always null; deletion_authorized is always false.

## Historical references are not ownership

The existing LinkCacheFile and ArchiveCacheFile readers are reused only for observed cache/link/*.linkcache and cache/archive/*.archivecache files of at most 16 MiB. They are never saved or upgraded. Corruption, unsupported data, oversize data and decoding errors remain visible. Only absolute references are compared with observed paths using MQB's established Windows path identity. Referenced external paths are not opened; relative legacy paths have no established base and are not guessed.

Each record reports its cache path, output, opaque signature and tool version. More than one record referencing a row means shared historical references, not proven current shared ownership. Per-target totals overlap and must not be added together. The old signature cannot reveal Debug/Release or a complete target configuration; by_configuration is therefore unknown. Neither a missing source, an old hash nor omission from the current invocation proves retirement. Tracking trustworthy current targets, shared dependencies and explicit AOT generations remains a separate prerequisite for future deletion.

## Filesystem and error boundary

Windows metadata reads use handle-based FileStandardInfo and FileIdInfo. Each opened file and its ancestors stay pinned during its observation; reparse points are refused rather than traversed. Pins request FILE_READ_DATA (FILE_LIST_DIRECTORY for directories) together with FILE_READ_ATTRIBUTES, not attribute-only access, so the read-only sharing mask participates in Windows sharing checks. Permission to read data/list directories is therefore required even when only metadata is reported. An access failure remains a partial observation; there is no attribute-only fallback. Enumeration is bounded to 1,000,000 rows and depth 128. The read is not an atomic filesystem snapshot: atomic_snapshot is always false, including a successful walk. Permissions, sharing failures, unsupported identity/allocation, skipped reparse subtrees and enumeration/decoder errors produce issues and exit 1 with a partial report where rendering remains possible. Fatal setup/render failures may yield diagnostics without a complete JSON document. Consumers must check exit status, parse success, complete and issues.

Exit 0 means the bounded walk completed without recorded issues, not that the tree is immutable or cleanup is safe. Exit 1 means observation/output failure or partial coverage. Exit 2 means invalid arguments/project setup. No file is removed, no process/service is killed, and no compiler flags or ordinary build/cache behavior change.

Primary contracts: [FILE_STANDARD_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_standard_info), [GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex), [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew). These APIs alone do not establish reclamation or concurrent-writer safety.

## Reproduction and acceptance

The new mqb_storage_e2e_tests.cpp retains all original 79 native programs and becomes the 80th. Portable model/report contracts exercise byte closure, reference deduplication, protected entries, unavailable/overflow values, JSON escaping and output failures; they do not test Windows.

The Windows fixture covers an absent root, valid/shared/corrupt legacy records, real hard links, Unicode/space paths, existing writers, attempted file/ancestor renames and write/delete access during observation, successful access/rename controls after handles close, unchanged file names/contents/mtime and a real junction refusal. Each pinning result and native error code is saved before its assertion, including on failure. Each Debug/Release native test execution has exactly eight two-TU builds: stable cold/repeat, source rename, target rename, configuration switch, two AOT-named targets and last-target repeat. It runs the last produced program once, preserves old objects/targets and checks inventory byte/file closure without deleting them. These AOT names are a minimal lifecycle model, not a run of Rium's AOT generator.

Raw argv/stdout/stderr/exit and original successful/failed inventories are retained in storage-evidence, with selected sources, target outputs and link-cache files in CI artifacts for 30 days. Existing native Debug/Release, self-host/package, applicable compatibility workflows and independent 19-by-4 ABBA remain acceptance gates; a portable pass is not their substitute. No fixed space-saving percentage is promised. Actual Rium validation, trustworthy ownership/configuration state, compressed/sparse accounting and safe clean/prune remain pending.
