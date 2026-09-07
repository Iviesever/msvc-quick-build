# Bounded link and discovery cache readers

## Scope and exact baseline

This iteration is stacked on PR #157 at
`2c87b47d9fe4945a75077a8ded0bdfed5ca425ab`, not directly on main. It reuses
`core/BoundedCacheReader.hpp` without changing that helper or compile-cache
behavior. VERSION remains 5.4.0. This is not release preparation.

Only link and discovery cache transport changes. Cache formats, serializers,
save/replacement behavior, semantic validation, dependency freshness, CLI
reporting, scheduling, archive and toolchain loaders remain unchanged.

## Backend contracts

| Backend | Successful read before | Successful read now | Preserved policy |
|---|---|---|---|
| Link | pathname exists, pathname size, open/read | one open, bounded same-stream size/read/EOF | missing is a miss; other failures retain typed errors; v2/v3/v4 decoding |
| Discovery | ordinary-file query, pathname size, open/read | ordinary-file query, one open, bounded same-stream size/read/EOF | ordinary-file guard, best-effort fallback, request identity and all filesystem evidence |

The existing 64 MiB limits apply before allocating the payload. The shared
reader rejects failed/negative size queries, stream-size overflow, short reads,
observed growth and EOF I/O failures. Empty existing link caches remain invalid
magic, not misses. Link pathname queries are diagnostic-only after a failed
open; the file is not reopened. This can add a failed open on a missing link
cache and must be considered in cold-path evidence.

Discovery deliberately retains `is_regular_file`. Removing a type policy is not
necessary to remove a repeated pathname size query. Its source ordering,
indexed-file accounting, request semantics and file/directory timestamp checks
are unchanged. Any unusable cache still falls back to fresh discovery and
best-effort repair. A nonempty directory at the cache pathname is not deleted.

The helper does not provide an atomic snapshot against arbitrary concurrent
in-place or timestamp-preserving writes. No cross-stage filesystem evidence is
reused by this change, and no freshness comparison or retry is removed.

## Tests and instrumentation

The existing link-cache test executable adds empty-file and directory error
contracts, over-limit rejection, trailing-zero rejection with exact decoder
offset, removal/recreation and actual v2/v3 legacy payloads. Existing v4
round-trip, invalid magic, unsupported version and truncation tests remain.
Directory assertions derive the exact historical error category on the active
standard library rather than assuming POSIX behavior on MSVC.

The existing discovery-cache test executable adds empty, truncated, trailing,
oversized, missing and nonempty-directory cache cases. Each repairable case
requires correct fresh results followed by a warm hit. Existing Unicode alias,
header/source/directory changes, forced-include/request identity, stale-format
and explicitly disabled-cache tests remain. Deterministic stream failure and
mutation cases are already covered by the unchanged helper tests from #157.
No new product translation unit or test executable is introduced.

`ScopedCacheRead::opened` still observes the sized payload. These counters do
not count pathname metadata calls or failed opens. Normal scenario cache
hits/misses, payload bytes/opens, filesystem freshness probes, process launches
and output must be compared between exact base/head runs. A source-level
reduction in metadata calls is not itself a measured speedup.

## Independent performance decision

The PR's unchanged Performance Evidence workflow builds the exact PR base and
head with the same pinned MQB seed, then collects four alternating ABBA pairs
for all existing scenarios on one Windows runner. Using #157's head as the base
isolates this iteration from its compile-reader improvement. Raw artifacts,
run IDs and exact commits belong in the PR evidence record; no numeric result
is assumed in this design note.

Review both internal timings and the existing timing-disabled external no-op,
as well as cold/miss paths. Report paired deltas, adverse samples and counters.
Link/discovery cache-read work includes decoding and can overlap other fields;
it is not an isolated I/O percentage. With only four pairs, nearest-rank P95 is
the maximum sample, not an established production-tail estimate. Do not pool
this run with #157 or add successive PR percentages.

Native Debug/Release, self-host/package and independent ABBA evidence remain
review gates. Keeping both PRs as drafts does not authorize either merge.

## Follow-on boundaries

Archive needs a separate text-reader and writer-size contract with static-library
evidence; its current loader is not a copy of the binary link/discovery loaders.
Toolchain trust/age/PATH/newest-toolset/binary validation must remain intact.
Reporting needs full formatting/loop measurements before invasive object
evidence handoff is prioritized. Cache packs remain conditional on isolated
remaining I/O reaching the agreed 25-30% threshold. Resident processes,
watchers and USN work remain v6.0. Only a final independent release PR updates
VERSION/changelog after cumulative evidence against the released v5.4.0.
