# Explicit historical snapshot files

[简体中文](LINK_FACT_FILE_ZH.md) · [Pure codec](LINK_FACT_SNAPSHOT.md)

## Scope and API

`mqb/platform/windows/LinkFactSnapshotFile.hpp` supplies two **non-default** calls:
`create_link_fact_snapshot_file(path, snapshot)` and `read_link_fact_snapshot_file(path)`.
Neither is connected to build/CLI/cache/observation. This slice writes/reads one
strict JSONv1 historical fact record, never a producer inventory or cleanup plan.
All four authority flags remain false. Valid JSON does not authenticate the caller
or establish that the described artifact still exists or has unchanged content.

## Create-only protocol

Encode and validate before filesystem mutation. The parent must already exist.
Open the new leaf with `CREATE_NEW`, no read/write/delete sharing and no inherited
handle. A conflicting existing name is never opened for overwrite; no `exists`
check followed by a truncating open, append, replacement or automatic retry.
Complete bounded chunks, respecting short successful transfers; zero progress and
native write errors stop. Success requires `FlushFileBuffers` then `CloseHandle`
to succeed. Known completed bytes and native errors are reported separately.

An error after creation can leave a **partial OR complete** new file at that name.
It is deliberately retained, not automatically unlinked, renamed, overwritten or
adopted on a retry. The error says whether creation happened, known transferred
bytes and any additional close failure. The bytes are evidence, not proof of flush
or power-loss durability. A later read of syntactically valid bytes cannot prove a
previous writer received a success receipt. This is not crash-atomic publication,
a committed-record marker, transaction log or durable writer coordination.

## Bounded reads and paths

Only ordinary absolute drive paths on local volume-GUID NTFS are admitted.
Validate path syntax and UTF-16 before normalization; reject device/UNC/ADS, parent
traversal, reserved aliases, excessive path length/depth. Reuse the existing
private physical-path and read-pin helpers. Pin root and ancestors until return,
refuse reparse components and wrong types, including the leaf on reads.

Read the leaf through one data/read-attributes handle with `FILE_SHARE_READ`, not
attribute-only access. This rejects conflicting ordinary writer/delete handles.
Obtain size before allocating (at most the codec's 1 MiB), read all declared bytes,
probe one byte beyond EOF, recheck size, close, then invoke the **original** codec.
Truncated/grown/oversized files, invalid JSON/UTF8/schema and native read errors
remain refusals; no cache fallback or replacement with an empty snapshot.

Pins apply only during this call, not across operations. No claim is made against
privileged volume/drive remapping, kernel writers, every mapped-file scenario or
host loss. Same-size hostile mutation is not content-authenticated. Bounded bytes,
components and progress do not impose a timeout on synchronous kernel I/O. Parent
pin RAII cleanup and allocation exceptions retain existing private-helper behavior;
`bad_alloc` can propagate, with any created file retained.

## Tests, evidence and release limits

The two new native programs exercise the production transfer templates with
synthetic short/error/EOF/flush/close transports and actual Windows new-file,
no-clobber, read-back, limits, invalid paths/UTF16, hardlink, writer conflict,
simultaneous creators and real-junction refusals. Windows test files and progress
are retained under `storage-evidence/snapshot-file/` by the unchanged native CI.
Synthetic transport success is not native filesystem qualification. The existing
91 native programs, codec and observation remain unchanged; the driver is 93 and
the production graph is 94 TUs including main. No new workflow or benchmark entry.

This does not release v5.7, enable default snapshot capture, grant deletion, retire
generations, coordinate full project writers, qualify Rium or resolve historical
performance failures. Never rerun consumed studies to obtain favorable samples.

Microsoft contracts: [CREATE_NEW and sharing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers),
[reparse operations](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-points-and-file-operations).
