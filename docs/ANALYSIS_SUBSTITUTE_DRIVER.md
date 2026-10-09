# Fixed-substitute analysis driver and frozen protocol

[简体中文](ANALYSIS_SUBSTITUTE_DRIVER_ZH.md)

## Purpose and inherited decisions

This slice implements the next point in [handoff 6079598410](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6079598410): connect the merged PR251 capture component to a new analysis driver, freeze its protocol before launch, and exercise dual-pipe reads, process termination, failed prefixes and final sealing with fixed substitutes.

The development base is main `d4c0d5f143ea52862329180512caa1cf5ffd0244`, tree `e4cbc65ea1db62d3735397417656b155eff89bf7`. PR250 remains Draft/HOLD at `a26b103685589ab8a5b4df60d1b760ec445efdf5`. Study 001 remains failed-preflight and study 002 remains INVALID. The lost original `070-history_256-chain-0.stdout`, the five missing observations and the previously consumed budgets retain their existing status. This slice neither modifies those files nor explains their loss mechanism.

The [written request](20261009-114613-analysis-driver/issue.md) and [implementation plan](20261009-114613-analysis-driver/implementation_plan.md) define the accepted scope. This document describes the driver contract; actual acceptance requires the retained source identities, test outputs and review. It does not assert passing test counts or CI results in advance.

## Implementation and execution boundary

The implementation is divided into a closed [fixed substitute](../tests/analysis/analysis_fixed_substitute.py), [pipe transport](../tests/analysis/analysis_substitute_transport.py) and [protocol/driver layer](../tests/analysis/analysis_substitute_driver.py). It uses the unchanged [PR251 capture helper](../tests/analysis/analysis_evidence_capture.py), whose independent contract remains in [Analysis evidence capture](ANALYSIS_EVIDENCE_CAPTURE.md).

The driver runs a restricted Python substitute process on Linux. The substitute accepts only bundled fixture modes and emits predetermined bytes. The public call plan supplies `call_id` and `mode`; it cannot supply an arbitrary executable, shell command, input corpus or workload. stdin is `DEVNULL`, `shell=False`, `close_fds=True`, and the child starts a new POSIX session. At most one direct child is launched for an attempted call, and there is no retry.

The host must keep `SIGCHLD` at `SIG_DFL` and give this driver exclusive responsibility for reaping each child throughout the run. No signal handler, thread, host framework or other waiter may reap it. The driver checks the signal disposition at protocol freeze, run/source checks and immediately before `Popen`. Those checks observe the disposition at particular points; they do not prove exclusive ownership, establish an atomic lease or enforce the host's continued behavior.

This slice runs no real codec, MQB, MSVC, ETW or performance study. Every corresponding real-measurement budget is zero. The substitute lifecycle itself is exercised through real subprocess pipes; its byte counts, process counts and supervision durations are synthetic test evidence, not replacement observations for study 002.

All pre-existing tracked files, the 51 existing workflows, historical fixtures and study evidence remain unchanged. The new document pair is reviewed independently; it does not add an entry to the existing 22-pair documentation checker or modify an index to change CI impact.

## Frozen protocol and identities

The public API is:

```python
freeze_protocol(*, protocol_id, calls, limits=DriverLimits()) -> FrozenProtocol
run_frozen_substitutes(root, *, protocol, expected_protocol_sha256,
                      seal_path) -> dict
audit_driver_evidence(root, *, protocol, expected_protocol_sha256,
                      expected_driver_seal_sha256) -> dict
```

`calls` is a nonempty ordered list. Each object has exactly the keys `call_id` and `mode`, call identifiers are unique strings of 1–128 characters, and the mode belongs to the bundled fixed substitute. `protocol_id` is also a string of 1–128 characters. `FrozenProtocol.canonical` retains canonical bytes, `sha256` exposes their hash, and `document()` returns a fresh decoded copy. Later mutation of the supplied list, dictionaries, limits object or decoded copy cannot revise the frozen execution contract.

Before launch, the protocol binds the complete ordered calls, exact expected output byte identities and stdout record identities, resolved executable, argv, cwd, explicit environment, capture limits, lifecycle limits, explicit `identity_limits` and zero real-measurement budgets. The child argv uses the resolved Python executable with `-I -B -u`, the fixture path and one mode. cwd is the fixture's directory; the explicit child environment contains only `LANG=C.UTF-8` and `LC_ALL=C.UTF-8`. `runtime.reaper_policy` is frozen as `exclusive-driver-waitpid-sigchld-default`. The protocol also records SHA-256 identities for the driver, transport, substitute, unchanged capture helper and resolved Python executable. Local source identities are checked before launch, between calls and before sealing. A mismatch does not authorize execution with a replacement source.

Source-file hashes describe bytes read from the named on-disk paths at the recorded checks. They do not independently attest already loaded Python module code or prevent changes between a check and use. `development_base` identifies the main commit from which this slice started; it does not claim that the newly written driver files already belong to that commit. A later Git commit identity must be established separately from the submitted tree. Hashing the resolved Python executable also does not attest the whole interpreter, standard library, shared-library or kernel stack, or authenticate an output producer.

The caller must retain the expected protocol SHA-256 independently. A hash read only from a mutable evidence bundle does not establish the caller's original expectation. The supplied protocol and expected hash are required again for offline audit, which starts no process.

## Fixed output contract

Every bundled mode has a finite deterministic expected-byte specification. Before launch, the protocol freezes the full expected stdout and stderr byte identities, including byte counts, SHA-256, LF counts and final-LF flags, plus the ordered stdout record identities required by PR251. The output contract covers the complete bytes, so a parseable record with an altered extra value cannot pass by keeping only its identity fields.

| Mode | Fixed substitute behavior |
|---|---|
| `normal` | One UTF-8 stdout record with CRLF; stderr includes NUL and LF; exit 0 |
| `empty_stderr` | One stdout record and an empty stderr; exit 0 |
| `dual` | Writes 192 KiB to stderr before four stdout records with 48 KiB payloads; exit 0 |
| `nonzero` | Emits its fixed streams and exits 7 |
| `timeout` | Emits its fixed streams and then waits with both pipes open |
| `ignore_term` | Emits its fixed streams, ignores TERM and waits |
| `closed_pipes` | Emits its fixed streams, closes both pipes and keeps waiting |
| `busy` | Emits initial fixed streams and repeatedly adds its stdout record |

The larger fixture exercises pipe backpressure. Failure modes deliberately violate a successful lifecycle or finite output contract: in particular, `busy` can emit beyond its one frozen initial stdout block. All modes remain fixed substitutes; a planned failure is still INVALID evidence and does not become a successful call because a test expected that failure.

The driver compares received bytes with the frozen full-output identities. The unchanged capture layer also checks its NDJSON contract: strict UTF-8, JSON objects, exact ordered typed identities, record count and LF closure. These are complementary checks. Exact fixed bytes and a successful synthetic process do not qualify business values, units, performance equivalence or a real workload.

## Limits fixed before output creation

`DriverLimits` accepts strict positive integers; booleans, non-integers, zero, negative values and values above the hard ceilings are rejected. Each default is also a hard ceiling, so callers may only lower it. The frozen protocol retains the effective policy.

| Field | Default and hard ceiling | Meaning |
|---|---:|---|
| `max_calls` | 8 | Planned calls in one protocol |
| `max_stream_bytes` | 1,048,576 bytes (1 MiB) | Admitted bytes for one stdout or stderr |
| `max_total_bytes` | 8,388,608 bytes (8 MiB) | Admitted bytes across both roles and all calls |
| `chunk_bytes` | 32,768 bytes (32 KiB) | Maximum ordinary pipe-read request |
| `call_timeout_ms` | 3,000 ms | Active supervision deadline for one call |
| `terminate_grace_ms` | 200 ms | Grace interval after TERM before escalation |
| `kill_wait_ms` | 1,000 ms | Bounded wait/drain interval after KILL |
| `poll_ms` | 10 ms | Selector/poll checkpoint interval |
| `overall_timeout_ms` | 20,000 ms | Active supervision deadline for the protocol |

Driver protocol, call-record and result metadata files each have a separate 262,144-byte (256 KiB) ceiling. Source-file identities have a 16,777,216-byte (16 MiB) ceiling. The resolved Python executable has a separate 67,108,864-byte (64 MiB) ceiling; its identity is hashed in chunks of at most 65,536 bytes (64 KiB) without retaining the whole binary in memory. These limits are frozen as `identity_limits`.

PR251 receives the same call, stream, total and chunk limits, with `max_plan_bytes` set to 256 KiB for its manifest/result. Its other limits remain at their existing defaults: 256 records per call, 64 KiB per record, 64 KiB per journal line and 8 MiB for the whole journal. All effective capture limits are frozen explicitly and stay within that helper's existing hard ceilings.

The stream and total budgets count admitted transport input once. The spool, raw and backup copies therefore occupy additional storage; the logical input ceiling is not a ceiling on their combined on-disk size, metadata or process memory. Lowering a policy may prevent a fixture from fitting or prevent metadata publication. A protocol that cannot complete within its chosen policy remains invalid without expanding the limit.

These limits are controller supervision and evidence bounds. They are not OS-wide CPU/address-space quotas or an absolute wall-clock return guarantee. Process creation, regular-file I/O and `fsync` can block inside the kernel. The contract concerns deadline checks and bounded escalation at the controller's execution checkpoints.

## Concurrent transport and process lifecycle

The transport uses nonblocking stdout and stderr in one Linux selector loop. It drains both streams to separate, exclusively created spool files and checks monotonic deadlines even while data is continuously ready. After both pipes close it still supervises the direct child; pipe closure alone does not establish a successful process exit.

The overall deadline begins after the capture session is created, before the call loop. A call's active deadline starts on entry to transport, before its spool opens and process creation, and is capped by the remaining overall deadline. Source checks and handoff work between calls consume elapsed overall time, which is checked before another launch. Writes to spools inside the loop belong to active supervision, and the deadline is checked before the successful reaped/both-EOF break. The final spool flush/sync/close happens after supervision and is outside that deadline. `finished_monotonic_ns` is recorded after those operations, so the recorded finish-minus-start interval can be longer than the active supervision interval. The final capture sealing, driver publication and read-only audit also have no additional call/overall-deadline gate. Failure cleanup has the separately bounded grace/KILL interval described below.

The first transport error or an active deadline locks failure. While the direct child remains waitable, the controller sends TERM to its new process group, allows the frozen grace interval, and sends KILL if needed. It continues bounded draining and attempts to reap the direct child within the frozen lifecycle bounds. Before any signal it checks again for a termination status. Once it has reaped the child or lost wait-status ownership, it does not signal that old PID/process-group ID, even if buffered pipe data still needs draining. Signals and reap outcomes are recorded; sending a signal does not by itself prove termination or reap. These fixed fixtures do not grant general descendant supervision or cleanup guarantees for arbitrary workloads.

The supervisor obtains status directly through `os.waitpid(pid, os.WNOHANG)`. Only a terminating status returned for that exact child sets its `returncode` and `reaped=True`; the same code is then recorded on the `Popen` object. `Popen.poll()` and `Popen.wait()` do not supervise or reap the child, including in cleanup. `ECHILD` or an invalid status latches `wait_status_unavailable=True`, leaves `returncode=None` and `reaped=False`, makes the call INVALID and stops subsequent launches. Both pipes may still be drained within their bounds, but missing status never becomes an assumed zero exit. Losing reap evidence does not refund the already observed launch count.

At a stream or total input ceiling, at most one extra byte is read per affected pipe to distinguish exact EOF from overflow. Its two-digit hexadecimal value is retained in metadata, outside the admitted byte count and hash. Overflow locks failure and cannot be accepted as a shorter successful stream. After a failure the transport retains the other stream's obtainable bounded prefix, but it does not keep reading without limits to obtain a preferred result.

Each planned call has at most one launch attempt. The first failed call stops subsequent launches. Spawn failure records an unknown return code, no reaped child and no actual pipe EOF. Nonzero exit, missing EOF, timeout, failed termination/reap, unavailable wait status, source or byte mismatch, and I/O failure all remain failures. No retry or replacement call is scheduled.

## Receipts and the PR251 handoff

The transport receipt records the admitted bytes before writing them, so a short spool write cannot shrink the receipt into agreement with a shorter saved file.

| Transport receipt field | Meaning |
|---|---|
| `byte_count` | Number of admitted original pipe bytes |
| `sha256` | SHA-256 of those admitted bytes |
| `lf_count` | Number of LF bytes in that admitted prefix |
| `ends_with_lf` | Whether the admitted prefix ends in LF; false for empty input |
| `pipe_eof` | Whether the original pipe was actually read to EOF |
| `capture_error` | The stream's recorded capture error, or null |
| `overflow_probe_hex` | Empty string or the single overflow probe byte in hexadecimal |

The driver starts the corresponding PR251 call before entering transport. After supervision and termination/reap, transport attempts to flush, synchronize and close its spool writers. The driver then reads each saved spool with bounded regular-file checks, compares its receipt and fixed-output identity, and supplies those bytes through `io.BytesIO` to `preserve_stream`, one role at a time. PR251 retains its single-writer contract; the driver does not call it concurrently for the two live pipes.

Reading a truncated or interrupted spool to its own EOF cannot establish EOF on the original subprocess pipe. The transport's `pipe_eof` and lifecycle record remain independent of PR251's source `eof`. The driver carries a transport failure into capture completion even if both saved prefixes are readable to the end. Files and prefixes actually created are retained for diagnosis; a failed creation or later damage can leave only a partial invalid bundle.

The first failure remains latched. Transport and driver diagnostics retain bounded messages, up to 32 entries of at most 256 characters at each layer; the receipt keeps the stream's first capture error. This does not promise a lossless record of every secondary exception. A saved prefix or reaped child cannot remove the first failure.

This design stores a third copy and adds bounded in-memory spool reads, parsing and synchronization. Those costs are not assumed free and must be accounted for before any later real measurement protocol chooses its observation window.

## Bundle layout and external publication

The driver creates a fresh evidence root exclusively. The root must not already exist; its parent and the external seal's parent must exist. The seal path must be outside the audited bundle and must not already exist. Conflicts are rejected without overwriting existing evidence. A later failure can leave a partial bundle, which is retained.

| Evidence | Purpose |
|---|---|
| `protocol.json` | Frozen canonical protocol bytes |
| `spool/000000.stdout.bin` and matching stderr | Bounded bytes read from the original pipes |
| `calls/000000.json` | Per-call transport receipts and process lifecycle |
| `capture/manifest.json` and `capture/journal.jsonl` | PR251's separate frozen plan and capture lifecycle |
| `capture/raw/` and `capture/backup/` | PR251's byte-preserving original/copy pair for each spool input |
| `capture/result.json` | PR251's final capture judgment and the bytes bound by its seal |
| `driver-result.json` | Driver judgment binding the protocol, call records and capture result |
| External `seal_path` | Exclusively saved driver-result SHA-256 retained outside the bundle |

Indices start at zero and use six decimal digits. Only `capture/` is passed as the PR251 root. Each completed call record is written exclusively. Its `index`, exact `sha256`, observed `launch_attempted`, `launched`, `reaped` and `admitted_bytes` are retained as an in-memory anchor, then bound by the driver seal. Expected directory members are inspected incrementally with `os.scandir`, stopping at an unexpected member, rather than allocating a list of every entry in a damaged directory.

If a call record cannot be published, the driver attempts to retain the current capture and raises `DriverError`; it does not return a verdict implying that the missing record consumed zero launches. The absence of a valid result or external seal is never evidence of zero execution. Missing evidence cannot refund a launch budget. Acceptance launch accounting also uses the independent `Popen` ledger.

| Driver result fields | Accounting meaning |
|---|---|
| `lifecycle_counts_available` | Whether trusted, structurally valid observation anchors are available |
| `launch_attempts`, `launched_calls`, `reaped_calls`, `admitted_bytes` | Observations bound by those anchors, preserved even if a later audit finds a damaged call record |
| `verified_launch_attempts`, `verified_launched_calls`, `verified_reaped_calls`, `verified_admitted_bytes` | Counts reconstructed from the current call records whose hashes, shapes and observations match their anchors |

If the driver seal cannot be trusted, `lifecycle_counts_available` is false and all four observed counts are null (unknown), not zero. The `verified_*` counts concern call-record integrity; they do not by themselves assert intact spool/raw/backup bytes or successful transport. Those independent checks still determine the full integrity verdict.

Before driver sealing, the implementation freshly checks the protocol, expected layout, all original spools, transport records and PR251 evidence. The driver result binds the capture result's exact SHA-256 as well as the protocol and call-record anchors. Result publication and the external seal use exclusive writes, synchronization, closure and readback. The implementation then repeats the full read-only driver audit before returning success. Publication failure raises; it does not return a successful summary or overwrite an earlier seal.

An external sibling seal is a separately supplied expectation, not authenticated storage or an independent failure domain. The caller must define how its protocol and seal anchors are retained and trusted.

## Fresh audit and success rule

The caller must check both `complete` and `integrity_status`. Successful evidence requires `complete == True` and `integrity_status == "VERIFIED"`. A returned SHA-256 by itself is not a successful judgment; invalid evidence may also have a published seal.

Success requires all planned calls in order, successful and reaped direct children, actual EOF on both original pipes, exact frozen fixed-output identities, intact original spools and lifecycle records, and all unchanged PR251 integrity gates. A complete backup cannot rescue a damaged raw or spool file. An earlier in-memory count, cached `complete` or capture `execution_finished` cannot override a failed fresh read.

`audit_driver_evidence` uses the caller's frozen protocol and externally retained protocol/driver seal hashes. It launches nothing, re-reads the saved bundle and PR251 evidence, and reports the current validity. It checks the stored identities against the supplied protocol; it does not rerun the producer or independently attest loaded module code. It does not restore backups, rewrite metadata, delete failed prefixes or promote a previously invalid result to success.

`VERIFIED` describes the final sequential read interval. It does not establish one atomic multi-file snapshot, permanent integrity, unchanged bytes at publication/return, authenticated producers, a permanent writer lease or power-loss recovery. Later use of retained evidence requires a new audit with the externally retained anchors. Linux results do not establish Windows filesystem or process qualification.

## Local verification and CI scope

The two driver test modules exercise real bundled substitute processes and the unchanged PR251 helper. Fault tests alter synthetic fixtures or inject transport/publication failures; they do not edit or rerun the historical 001/002 studies. The accepted matrix includes normal and large dual streams, nonzero stop, timeout/escalation, closed-pipe hang, input limits, immutable protocol/source identities, target conflicts, short writes, final corruption and failed publication. Additional process-boundary contracts cover no signaling after reap, unavailable or invalid wait status, direct-waitpid use and the frozen/default-SIGCHLD prerequisite.

In retained `boundary-red-001`, the alias contract's preflight failed with `file_limit`: the actual Python executable was 30,894,944 bytes and exceeded the initial shared 16 MiB identity bound. This failure establishes the prelaunch file-bound problem, not an alias-regression RED. The same invocation separately demonstrated the late-final-reap deadline regression using a fake child; neither case launched an actual child. The subsequent explicit source/executable split retains the source bound and adds bounded streaming identification for the existing interpreter, without changing real-measurement budgets.

The complete local analysis command is:

```sh
PYTHONDONTWRITEBYTECODE=1 python -B -m unittest discover -s tests/analysis -p 'test_*.py' -v
```

The registered acceptance budget limits each full driver test invocation to 64 direct substitute launch attempts, at most 8 per protocol, with at most 20 seconds of active supervision per protocol. Named diagnostic invocations use the same per-protocol ceilings. Retained verification records must state the exact command, source hashes before/after, stdout/stderr, actual launch counts and all first failures. The command's presence in documentation does not establish that a run passed. Imported repository code and substitute children use the no-bytecode policy so verification does not leave `__pycache__` in the worktree.

Existing CI does not automatically execute these new Python contracts. The unchanged [Native workflow](../.github/workflows/native-ci.yml) determines impact through its existing classifier; the accepted analysis-only scope expects classifier/gate processing with real native builds/shards skipped. The unchanged [Documentation workflow](../.github/workflows/docs-ci.yml) checks its original 22 pairs; this pair requires separate review. A non-`perf:` PR title leaves the [Performance Evidence](../.github/workflows/performance-evidence.yml) comparison skipped. Actual job outcomes and exact checked-out identities must be inspected after submission; none is inferred merely from a green check or a branch name.

No new workflow, manual dispatch, retry of unrelated work or changed performance admission is part of this slice. Local Linux substitute acceptance is recorded separately from those existing CI outcomes.

## Deferred real-driver qualification and next handoff

This slice qualifies restricted transport/capture wiring. A real cost driver still needs a separately reviewed protocol defining exact candidate/base/source/binary/input identities, output-value semantics, observation windows, storage/copy/parse/sync costs, a decision purpose, a full new budget, stop rules and final acceptance. If real measurements are needed, that concrete protocol must be separately registered before execution.

This work allocates no third study or five replacement trials, does not reopen 001/002, and does not approve PR250 integration. The existing HOLD decisions, adverse samples, dependency gaps, default-path qualification limits and writer/atomicity/reparse/concurrency/recovery/Rium/#164 limitations remain attached to their original evidence.

Product producer identity, current content, complete inventory and deletion authority remain false. This slice does not add default CLI integration, automatic persistence, clean/prune or #248 integration, and does not publish a release. VERSION remains 5.6.0 and v5.7.0 remains unreleased. The next handoff must retain these boundaries while deciding the separately reviewable real-driver protocol; it cannot treat a verified substitute bundle as real measurement evidence.
