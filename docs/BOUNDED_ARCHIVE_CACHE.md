# Bounded archive-cache transport

## Scope

This v5.5.0 development iteration follows the accepted compile-reader (#157)
and reporting (#159) changes. The rejected link/discovery experiment (#158) is
not included. VERSION remains 5.4.0; no release metadata is changed.

Only archive-cache transport and its write-size contract change. The shared
bounded reader is reused without modification. Archive signatures, object
freshness, librarian invocation, retry/fallback, link/discovery/toolchain
loaders, and CLI reporting remain unchanged. This is not a cache pack.

## Why this remaining reader is different

The previous archive loader sized its opened stream but imposed no total-byte
limit, then decoded quoted fields directly from the file stream. Its writer
also had no serialized-byte bound. A singleton reader optimization alone need
not be worthwhile: #158 was closed after its incremental evidence showed too
little benefit. Here a consistent read/write safety boundary is independently
valuable, and moving formatted decoding to a bounded in-memory payload is
measured on actual static-library targets rather than inferred from executable
link scenarios.

## Read contract

Open the file once in binary mode. The existing shared reader obtains length
and payload from that same stream, rejects sizes over 64 MiB before payload
allocation, requires a complete read and rejects observed growth or EOF I/O
failure. A read-only C++23 span stream then exposes the owned bytes to the
unchanged archive v1 formatted decoder, without a second payload copy.

A failed open still distinguishes an ordinary missing cache from an error via
diagnostic-only pathname checks. Existing empty files are corrupt, not misses.
Magic/version/object-count validation, object order, lexical path treatment,
quoted fields and trailing-whitespace acceptance are retained. No earlier
object snapshot is substituted for the coordinator's later observation.

This is not an atomic filesystem snapshot against arbitrary concurrent
in-place or timestamp-preserving edits. It also does not cap total process
memory at 64 MiB: decoded strings/paths and caller-owned records still consume
memory. The bound applies to serialized payload bytes and the writer's buffer
growth requests, not allocator overhead or all model allocations.

## Write contract and compatibility

Serialize the same v1 text grammar into a private bounded stream buffer before
creating directories or touching a temporary/destination file. Capacity growth
requests and writes are capped by the same 64 MiB limit. Quoted fields are
escaped directly into this destination in bounded spans, avoiding an unbounded
escaped temporary. Quotes, backslashes, whitespace, UTF-8 and embedded NUL bytes
retain the original std::quoted representation. Accepted bytes are written in
one operation through the existing temporary-file replacement sequence.

The object-count limit remains 100000. The byte limit counts the serialized
representation, including escaping and delimiters. An over-limit save fails
with file_write_failed and leaves the prior cache untouched; an over-limit
read is corrupt_data. An exactly 64 MiB valid record is accepted by both sides.

The total-byte bound is an intentional new policy: an older archive cache over
64 MiB is no longer reusable even when its object count is permitted. The
unchanged coordinator warns and re-archives instead of treating it as fresh.
If the current record itself cannot fit, the successful build remains usable
but cache saving reports a warning. The archive format version is not bumped
because accepted v1 bytes and semantics are unchanged.

The historical archive write instrumentation used opened(0). That convention
is retained, rather than silently changing byte-accounting definitions in the
same experiment. Read work still includes decoding; neither work time nor
payload-open counters represent isolated OS I/O cost or syscall counts.

## Validation

A new native archive-cache executable adds an independent legacy-writer oracle,
256 deterministic binary-string cases, exact field/order round trips, Unicode
paths, empty/truncated/trailing/unsupported-version/count failures, missing and
recreated files, over-limit read rejection, escaped-size overflow, prior-cache
preservation, no temporary residue, and exact-limit read/write consistency.
The native inventory becomes 79 tests; no product translation unit is added.
Shared-reader mutation/seek/EOF tests remain in the accepted compile-cache suite.

Supplemental GCC C++23 warning-as-error and ASan/UBSan tests pass locally with
an external no-op performance-counter test shim. Those are not claimed as
Windows/MSVC product validation. Required product gates remain the complete
Debug/Release suites, self-host and packaged-artifact checks.

## Independent evidence

The unchanged original 19-scenario Performance Evidence workflow remains in
place. Archive Cache Evidence separately builds the exact PR base and head
with one pinned MQB seed. It reuses the existing external process recorder,
without modifying that recorder or product instrumentation.

Eight static-library warm scenarios cover 2/129 TUs, default/verbose output,
timings on/off and j1, with 24 alternating AB/BA pairs each. Two more scenarios
measure 129-TU cold builds and single-TU rebuilds with six pairs each: 204 pairs
in total. Every row has external process start-through-completion/drain timing;
internal work is auxiliary and never added to elapsed time. Cold and mutation
samples are not pooled with warm samples.

Warm pre/postconditions require expected hits, no tool launches or cache writes,
unchanged project cache/artifact metadata and matching human transcripts.
Instrumented pairs require equal counters and hit/miss counts; unavailable
fields remain unavailable. Separate candidate-only contracts corrupt, truncate,
append to, oversize and remove the archive cache, requiring a real re-archive
without TU recompilation and a subsequent warm hit. Malformed-cache warnings
must stay visible.

All raw stdout/stderr, exact Git and binary identities, adverse samples and
partial failures are retained. Captured pipes do not establish interactive
terminal performance. Numeric results and final gate status belong in the PR
record, not assumed in this design note. No cumulative v5.4.0 speedup can be
obtained by adding percentages from this or prior iterations.
