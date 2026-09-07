# Bounded compile-cache reads: v5.5.0 development

## Scope and baseline

This iteration starts from `8d9008a6031755feccf15d72a52c9fad137b4ad9`, the merge of PR #156. `VERSION` stays `5.4.0`.

Only the compile-cache read transport changes. Link, archive, discovery and toolchain loaders, cache formats, serializers, signatures, dependency freshness, target scheduling and CLI output are unchanged. This is not a cache pack.

## Why this comes before object handoff or reporting

PR #156's retained Performance Evidence #492 (run `33946302228`, artifact `9963541165`) contains four alternating base/candidate pairs per scenario. Its candidate tree equals this iteration's base tree. The downloaded artifact SHA-256 is `b7fd55600f0e3d40a89bab6a3b10752b56207572834864206c3594b09754ff9f`.

Candidate per-field medians from that historical run, milliseconds:

| Scenario | Total | Compile-cache read work | Link filesystem work | Reporting wall |
|---|---:|---:|---:|---:|
| 129-TU no-op | 35.571 | 20.957 | 4.493 | 0.047 |
| 129-TU common-header no-op | 39.578 | 26.828 | 4.446 | 0.054 |
| 129-TU j1 no-op | 47.524 | 10.724 | 4.417 | 0.052 |

These are baseline observations, NOT this change's speedup. Parallel work durations overlap and include decoding; they cannot be divided by total wall time to establish a cache-I/O percentage. Piped-output reporting measurements do not establish interactive terminal cost.

The 129-TU fixtures read 129 compile payloads among 131 cache files. The old successful loader queries `exists(path)` and `file_size(path)` before opening each payload. Removing those repeated pathname preflights has a narrow causal scope without moving a dependency observation across compile execution. The new seek/tell/rewind and EOF check also cost work; net acceleration must be established by this PR's independent ABBA run.

## Read contract

The owner opens the binary stream once. Existing readable files obtain their length and bytes from that same stream. Failed-open paths retain diagnostic-only existence/size queries to distinguish a normal missing cache from an existing unreadable path; they never reopen the payload.

`read_bounded_cache_stream` is a toolchain-independent core persistence primitive. It checks the nonnegative stream length and the existing 64 MiB limit before allocation, rewinds, requires an exact read, and rejects observed extra data at EOF. An existing empty file remains invalid magic, not a cache miss. Payload magic/version/count/path/trailing-byte checks remain in the original decoder. Compile cache v2/v3/v4 compatibility and save behavior are unchanged.

The sized-read callback preserves the previous accepted-length instrumentation point. `cache_files_opened` is still a payload-level counter; it is NOT an OS metadata-query counter. Neither a reduced payload-open count nor reduced `filesystem_snapshot_requests` is claimed.

This does not provide an atomic snapshot against arbitrary concurrent in-place or timestamp-preserving writes. Growth seen at the final EOF check and short reads are rejected; no dependency freshness check, target revalidation barrier or conservative retry is removed.

## Regression and merge gates

The existing compile-cache test executable retains all previous tests and adds deterministic stream fixtures for exact/empty/binary payloads, limit rejection, size/seek failures, truncation, growth, growth from empty, EOF I/O failure and sized-read observations. Real-file facade tests cover empty files, directory paths and a file exceeding 64 MiB. No new test executable or product translation unit is added.

Supplemental local validation: standalone standard-library reader cases passed GCC C++23 warning-as-error compilation and AddressSanitizer/UndefinedBehaviorSanitizer. These Linux helper checks do not substitute for Windows/MSVC validation of the complete product.

Required before merge: the original Windows Debug/Release suites, self-host/package gates, deterministic instrumentation, and the unchanged Performance Evidence workflow comparing the exact PR base and head with four alternating pairs across all 19 scenarios. Retain the complete raw JSON, including adverse pairs and tails. For the common-header no-op, expect the established 129 compile hits, one link hit, 131 cache payload opens, zero writes/tool launches and 424 filesystem snapshots to remain. Evaluate total, cache-read work, j1/auto, single-TU rebuild and cold paths together; a lower source-level query count alone is not a speedup result.

At initial submission, native and independent ABBA results are pending. Keep the PR draft until those gates have been examined; do not publish a performance percentage from historical data.

## Remaining development decisions

After evaluating this reader, compare extending it to other cache owners against object-snapshot handoff. Object evidence must have invocation-local ownership, exact path identity and a defined validity boundary after miss execution/revalidation; do not turn public diagnostic `inspect()` results into reusable execution tickets. Keep real freshness checks for linker libraries and side outputs.

Reprofile before changing CLI output or adding link-resolution reuse. Measure actual terminal, redirected and verbose reporting separately. Investigate discovery cache writes and alternating request identities before choosing a multi-key store; a slow discovery sample is not proof of key thrashing. Also retain visibility into artifact-layout work rather than attributing all residual time to cache reads.

Each development PR has one theme and its own exact-base/head ABBA evidence. Consider a persistent cache pack only after isolated remaining cache I/O is credibly at least 25-30% of the relevant elapsed path, not from overlapping work totals. Daemon, watcher, USN and resident processes remain v6.0 work. End with cumulative released-v5.4.0 versus final-candidate evidence, documentation and a separate release-preparation PR; only that final PR changes VERSION/changelog for v5.5.0.
