# Bounded Visual Studio toolchain cache reads

## Scope

This v5.5.0 development iteration changes only the transport feeding the existing
v9 toolchain-cache decoder. VERSION remains 5.4.0. It neither reinstates the
rejected link/discovery experiment (#158) nor adds a cache pack or resident service.

The old loader checked the pathname's size and then decoded directly from an
opened file stream. If the file changed between that query and decoding, the
preflight did not impose a bound on the actual bytes read; quoted-string checks
happen after formatted extraction. The new path uses the already accepted
`BoundedCacheReader.hpp` to size and read the same opened stream, then exposes
its owned payload to the unchanged decoder through a read-only C++23 span stream.
No second whole-payload copy is required. It also removes the pathname size query
and formatted disk-stream extraction, but a speedup is not assumed.

## Preserved decisions and boundaries

The ordinary-file guard and last-write-time age validation still precede opening.
The original 1 MiB limit applies to the actual buffered payload before allocation.
Empty payloads, short reads, observed growth and EOF I/O errors fall back to
ordinary discovery. Exactly 1 MiB of valid v9 data, including allowed trailing
whitespace, is accepted; an extra byte is not. Field/count limits remain 256 KiB
and 64 entries. Serialized grammar and cache version do not change.

All key matching, exact ambient PATH comparison, latest-toolset selection, tool
existence, compiler binary stamp, INCLUDE/LIB/LIBPATH trust, trusted SDK/root
validation, standard-library module discovery and environment identity sealing
remain unchanged. Explicit discovery overrides, ambient adoption, save behavior,
replacement and error fallback also remain unchanged. No source/object freshness
comparison, process invocation policy or reporting change belongs to this PR.

The same-stream byte bound does not make the pathname age check atomic with the
open and is not a guarantee against arbitrary concurrent in-place or timestamp-
preserving modification. Existing freshness/trust validation is retained, not
replaced by the transport snapshot. The bound is on serialized bytes, not total
process memory. This PR does not add a total-byte bound to the toolchain writer;
its existing field/count limits and best-effort behavior are unchanged.

`ScopedCacheRead::opened` retains its healthy sized-payload observation point.
The overall toolchain-cache work field includes decoding AND trust validation;
its duration is not pure disk I/O and cannot justify a cache-pack threshold.
Payload-open counters do not count pathname metadata calls.

## Tests

A dedicated native toolchain-reader test first obtains a real validated Visual
Studio fixture. A rejecting process runner then requires a hit without any
subprocess, identical selected tools and sealed identity. Empty, truncated,
trailing NUL/non-whitespace, stale-version, missing, directory and over-limit
payloads must attempt normal discovery rather than fabricate a usable hit.
Every repairable case is followed by a restored valid hit. Exact-byte-limit,
expired (31 minutes), future-timestamp and restoration cases are explicit.

The shared reader's existing deterministic seek/short-read/growth/EOF tests
remain in the compile-cache suite. Existing Visual Studio tests for environment,
PATH, SDK roots, versions and related trust semantics are unchanged. No test
shim is committed; Windows native CI is the product validation source.

## Independent performance evidence

The unchanged general 19-scenario Performance Evidence harness remains in place.
A focused read-only Toolchain Cache Evidence workflow additionally builds exact
base/head Release binaries using one pinned MQB seed. It reuses the existing
external process recorder with common fixtures and cache pathnames.

Two- and 129-TU targets are measured separately with timings OFF and ON: four
scenarios, 24 alternating AB/BA pairs each. Warm pre/post audits require all
compile/link hits, one toolchain payload read, no cache writes or tool launches.
Instrumented pairs require equal non-output counters and hit counts; human
transcripts must match. Full before/after artifact/cache metadata inventories
are retained and must be equal. Missing instrumentation is unavailable, not zero.

Five additional real-build cases damage only the toolchain payload and require
successful ordinary rediscovery/resealing without TU recompilation or relinking,
then a true warm hit. These are correctness contracts, not a discovery-miss speed
claim. The native test separately proves that a hit does not invoke discovery.

All raw stdout/stderr, Git and binary identities, adverse observations and paired
statistics are retained. Pipe measurements are not interactive-terminal evidence.
A green workflow means successful collection/validation, not universal speedup.
Do not combine overlapping timings, pool runs or add successive PR percentages.
Native Debug/Release and self-host/package checks remain independent gates.
