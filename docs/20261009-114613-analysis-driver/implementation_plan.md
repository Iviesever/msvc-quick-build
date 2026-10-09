# Fixed-substitute driver implementation and acceptance plan

## Decision and alternatives

Use a Linux selector to drain stdout and stderr concurrently into exclusive bounded spool files. After supervision and bounded termination/reap, close both writers and feed their preserved bytes sequentially into the unchanged PR251 CaptureSession. Retain the original pipe EOF, receipt and lifecycle separately. The final driver seal binds the frozen protocol, per-call transport records and capture seal; a fresh audit must pass before the caller receives success.

Direct sequential reads from live pipes can deadlock behind a full second pipe. Concurrent calls to CaptureSession would require changing its single-writer contract. An all-memory buffer loses the durable received prefix when the controller stops before handoff. The selected spool adapter keeps that prefix and avoids either change, at the explicit cost of a third stored copy, extra reads, parsing and synchronization.

## Files and API

- `tests/analysis/analysis_fixed_substitute.py`: a closed set of deterministic fixture modes; no arbitrary command or real workload inputs.
- `tests/analysis/analysis_substitute_transport.py`: concurrent nonblocking pipe reads, exclusive spool writes, monotonic deadlines, process-group TERM/KILL and bounded direct-child reap. Shared bounded evidence-file primitives live here.
- `tests/analysis/analysis_substitute_driver.py`: protocol freezing, source checks, ordered scheduling, PR251 handoff, final driver seal and read-only audit.
- Two independent driver test modules, plus corresponding English/Chinese contract documents.

Public API:

```python
freeze_protocol(*, protocol_id, calls, limits=DriverLimits()) -> FrozenProtocol
run_frozen_substitutes(root, *, protocol, expected_protocol_sha256, seal_path) -> dict
audit_driver_evidence(root, *, protocol, expected_protocol_sha256,
                      expected_driver_seal_sha256) -> dict
```

`calls` is a nonempty ordered list of exact `{call_id, mode}` objects. Every mode is a bundled fixed substitute. FrozenProtocol stores canonical bytes and exposes `sha256` and `document()` (a fresh decoded copy). Source SHA identities include driver, transport, fixture, unchanged capture helper and resolved Python executable. The protocol records source-file facts and this controller's development base, not an attestation that a whole interpreter/shared-library/runtime stack is trusted or that uncommitted bytes belong to a commit.

The complete fixed outputs, record identities, executable, argv, cwd, explicit environment, capture limits, lifecycle limits and zero real-measurement budgets are frozen before launch. Runtime policy also freezes `reaper_policy='exclusive-driver-waitpid-sigchld-default'`: the host must keep SIGCHLD at its default disposition and allow no external reaper for these children throughout execution. Signal-disposition checks at freeze, run/source checks and before Popen observe that prerequisite; they do not prove exclusive ownership or establish a lease. Input object mutation cannot change this snapshot. A local source mismatch is rejected before launch; identities are checked again between calls and before sealing. Offline audit uses the caller's externally retained protocol and driver-seal hashes and launches nothing.

## Bounded lifecycle

Driver limits are strict positive integers; bool and over-ceiling values fail before output creation. Default values are also hard ceilings: max_calls 8; max_stream_bytes 1 MiB; max_total_bytes 8 MiB; chunk_bytes 32 KiB; call_timeout_ms 3000; terminate_grace_ms 200; kill_wait_ms 1000; poll_ms 10; overall_timeout_ms 20000. Driver metadata files have a separate 256 KiB ceiling. Source-file identity reads retain a 16 MiB ceiling; the resolved Python executable has a separate 64 MiB ceiling and is hashed in chunks of at most 64 KiB without retaining the whole binary in memory. These identity limits are explicit frozen protocol fields. Capture limits are explicitly derived from this frozen policy within PR251's hard ceilings; its whole-journal limit remains 8 MiB.

This identity-bound refinement follows the alias contract's preflight in retained `boundary-red-001`: the actual resolved Python executable is 30,894,944 bytes, so the original shared 16 MiB identity ceiling rejected it with `file_limit`. That failure is retained as a prelaunch file-bound failure, not an alias-regression RED. The same invocation separately demonstrated the late-final-reap deadline regression using a fake child; no actual child launched in either case. Separating source and executable bounds permits the existing runtime to be identified without raising source-file limits, creating an unbounded whole-binary read, or changing any real-measurement budget.

Each planned call has at most one launch attempt, no retry and at most one direct child. stdin is DEVNULL, shell is false, close_fds is true, and the child starts a new POSIX session. Both pipes are read in one selector loop; deadlines are checked even while data stays ready and after both pipes close. A first error or active deadline locks failure and, only while the child remains waitable, starts TERM, then bounded grace and KILL if still needed. Signals stop after reap or loss of wait-status ownership, including when buffered pipes remain. Direct `os.waitpid(pid, os.WNOHANG)` supplies the exact child's terminating status; only that sets exit code and reaped. ECHILD or invalid status stays unknown/INVALID, preserves consumed launch counts and stops later calls. Popen.poll/wait are not used by the supervisor or its cleanup. Drain and reap remain bounded at controller checkpoints. No CPU/AS or OS-wide wall limit is claimed. Popen, regular-file I/O and fsync can block in the kernel, so these are supervision deadlines rather than an absolute function-return guarantee.

For each stream the transport records admitted bytes/hash/LF/final LF before writing, actual pipe EOF, a one-byte overflow probe, and capture error. A short write cannot shrink the receipt. Hitting a limit permits at most one probe byte per affected pipe, preserves it in metadata and latches failure. Failure keeps the other stream's obtainable bounded prefix. Termination, missing pipe EOF, nonzero exit, unreaped child, byte/source mismatch or any I/O failure remains INVALID when the spool is later read to its own EOF.

## Evidence and success rule

An exclusive bundle contains `protocol.json`, `spool/`, `calls/`, `capture/` and `driver-result.json`; only the capture subdirectory is passed to PR251. Each completed transport record is saved exclusively and its byte hash retained as an in-memory anchor before later verification. All original spools, capture raws/backups, lifecycle records, layout and protocol are freshly checked before sealing. The driver result pins the capture result hash. A sibling/external seal file is exclusively saved and read back, then the full driver audit is repeated. Publication failure raises and never returns success.

Only complete planned order, all successful/reaped children, actual two-pipe EOF, exact fixed output bytes, intact transport records and all PR251 gates permit complete=true/VERIFIED. An intact backup cannot rescue damaged raw or spool. Cached complete or execution_finished cannot override a later audit. VERIFIED covers the final sequential read interval, not an atomic multi-file snapshot, permanent integrity, authenticated producer or Windows qualification.

## Test-first tasks and recorded budgets

1. Retain the baseline 32-test output and source hashes. Add driver contracts first and save an expected missing-feature RED before implementation.
2. Implement the closed fixture and transport independently from the protocol/integration layer. Test the real unchanged helper, with faults at transport or publication boundaries.
3. Verify fixed normal/large dual-stream bytes; nonzero stop; timeout, ignored TERM and closed-pipe hang; stream/total limits; no subsequent launch; protocol/source identity rejection; pre-existing roots/seals; post-last-call and post-seal corruption; short writes; immutable limits and expected hash mismatch. Do not repeat PR251's full low-level fault matrix.
4. Every full driver test invocation is limited to 64 direct substitute launch attempts, at most 8 per protocol; at most 20 seconds of active supervision per protocol. Targeted diagnostic invocations use only their named contract with the same per-protocol ceilings. Record source before/after, exact command, stdout/stderr, actual counts and all first failures. These are synthetic lifecycle tests; real codec/MQB/MSVC/ETW/performance budgets are all zero. No sample selection or measurement reruns are involved.
5. Independently review final implementation, fault contracts and the bilingual pair. Freeze final inputs and run the complete analysis suite once after issues are fixed; broaden only for a concrete remaining risk.
6. Compare all pre-existing tracked blobs/modes and the 51 workflows to exact base. Submit with GitHub Connector Git Data APIs, exact base/head/tree and no local git push. Review actual applicable classifier/gate, Documentation and skipped Performance records; do not dispatch/retry unrelated measurements. Normally merge after acceptance, check actual main results and publish a precise next handoff with retained evidence.

## Deferred qualification

This is the transport/capture wiring qualification. A real cost driver requires its own output-value semantics, exact candidate/base/binary/input identities, observation windows, copy/sync cost accounting, decision purpose, full new budget and separate reviewed registration. No third study, five replacement trials, PR250 integration, automatic persistence, clean/prune, #248 integration or v5.7.0 release is part of this slice. All prior HOLD decisions and limitations remain.
