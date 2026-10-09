# Fixed-substitute driver verification walkthrough

## Delivered behavior

This slice connects real Linux fixed-substitute pipes to the unchanged PR251 capture helper. A selector concurrently drains both streams into bounded exclusive spools; the driver then hands their preserved bytes to CaptureSession in order. The independently frozen protocol pins source/Python identities, exact fixture outputs, argv, environment, limits and zero real-measurement budgets. Final success also requires original pipe EOF, known successful child status, complete order, intact spools/raws/backups, bounded lifecycle records and a fresh externally anchored audit.

The implementation adds three source modules, two contract-test modules, an English/Chinese contract pair, and the issue/plan/walkthrough in this directory. Every one of the 688 previously tracked blob/mode pairs, including 51 workflow files, remains unchanged from development base `d4c0d5f143ea52862329180512caa1cf5ffd0244` (tree `e4cbc65ea1db62d3735397417656b155eff89bf7`). VERSION remains 5.6.0. Remote commit, PR, CI and merge identities are later recorded by the Connector handoff, not predicted in this precommit document.

## Final frozen local verification

The retained wrapper command was:

```text
python3 -B mqb-work/evidence/analysis-driver-integration/execute_stage.py integration-full-002
```

The wrapper used unittest discovery over all `tests/analysis/test_*.py` files. It retained the exact input command, UTC interval, all eight source hashes before/after, unmodified stdout/stderr, evidence fixtures and a separate closed-argv Popen ledger. This is the actual final test-log excerpt:

```text
test_reaped_nonzero_child_drains_both_pipes_without_signalling_old_group (test_analysis_substitute_driver.SubstituteDriverTests.test_reaped_nonzero_child_drains_both_pipes_without_signalling_old_group) ... ok
test_sigchld_ignored_rejects_freeze_run_and_low_level_launch (test_analysis_substitute_driver_faults.DriverFaultTests.test_sigchld_ignored_rejects_freeze_run_and_low_level_launch) ... ok
test_unavailable_wait_status_is_not_synthesized_as_success (test_analysis_substitute_driver_faults.DriverFaultTests.test_unavailable_wait_status_is_not_synthesized_as_success) ... ok
Ran 59 tests in 5.259s
OK
```

Result: **59 tests = 32 unchanged capture contracts + 27 new driver contracts; 0 failures, 0 errors, 0 skips, exit 0.** The run started `2026-10-09T12:21:41.950103+00:00` and finished `2026-10-09T12:21:47.262848+00:00`. All eight source files were identical before and after, and their current bytes were compared again before writing this walkthrough. The recorded test duration is harness execution time, not a codec performance observation.

The final invocation started **19 actual fixed-substitute children**, all reaped by the driver; no launch-budget violation and no test-cleanup termination occurred. The simulated child/PID contracts consumed zero actual process launches. Codec, MQB, MSVC, ETW and performance-study executions remained zero. The full first-failure history below consumed **42 actual fixed-substitute launches** in total, without production measurements.

| Retained final stream | Bytes | SHA-256 |
|---|---:|---|
| stdout | 3109 | `e50848b49edb8b029f785d65d72e3e463e209427f12d8c9a2f53d44a5e0b342c` |
| stderr | 10586 | `3376786173c3459abdf9d2b1085790ee46f38237025a259e3e994117e3093322` |

## First failures retained

The preimplementation `core-red-001` failed because the required driver did not exist; it launched no child. The initial unchanged 32-test baseline passed. Neither output was replaced.

| Stage | Tests | Failures / errors | Actual substitute launches |
|---|---:|---:|---:|
| `boundary-red-001` | 2 | 1 / 1 | 0 |
| `identity-red-001` | 1 | 1 / 0 | 0 |
| `identity-green-001` | 1 | 0 / 0 | 0 |
| `boundary-red-002` | 3 | 4 / 0 | 1 |
| `alias-green-001` | 1 | 0 / 0 | 0 |
| `layout-green-001` | 1 | 0 / 0 | 0 |
| `deadline-green-001` | 1 | 0 / 0 | 0 |
| `call-record-green-001` | 1 | 0 / 0 | 1 |
| `lifecycle-red-001` | 1 | 2 / 0 | 1 |
| `lifecycle-green-001` | 1 | 0 / 0 | 1 |
| `integration-full-001` | 56 | 0 / 0 | 19 |
| `reaped-signal-red-001` | 1 | 2 / 0 | 0 |
| `reaped-signal-green-001` | 1 | 0 / 0 | 0 |
| `wait-status-red-001` | 2 | 5 / 0 | 0 |
| `wait-status-green-001` | 4 | 0 / 0 | 0 |
| `integration-full-002` | 59 | 0 / 0 | 19 |

`boundary-red-001` includes two distinct outcomes: one fake-child late-reap deadline assertion failed, and the alias test stopped in preflight because the installed Python executable exceeded the original shared 16 MiB identity cap. The latter is a retained environment/preflight error, not proof of an alias defect. The later named alias RED/GREEN and separate 16 MiB source / 64 MiB streamed Python identity limits address the respective contracts.

The first full run's 56 passing tests did not close independent review. Review subsequently found two Important lifecycle defects: signalling a process-group ID after the direct child had already been reaped, and trusting Popen's synthetic zero when wait status is unavailable. The retained zero-child REDs reproduce those paths. The final implementation guards every signal entry, takes actual `os.waitpid(..., os.WNOHANG)` status, and retains unknown status as `returncode=None`, `reaped=False`, `wait_status_unavailable=True`, INVALID and no next call. Both original stream prefixes can still be preserved. It freezes an exclusive-driver/SIGCHLD-default policy and checks the observable signal disposition at freeze, run/source checks and immediately before launch. The host must retain exclusive child-reap ownership and that disposition throughout the call; the check is not proof of concurrent ownership.

Other retained regressions cover dangling output/seal aliases, eager directory enumeration, unpublished call records, and observed launch counts disappearing when a later audit loses a record. Failure cannot refund known consumed launches; an untrusted seal makes observed counts unknown, not zero.

## Exact tested source identities

| File | SHA-256 |
|---|---|
| `tests/analysis/analysis_evidence_capture.py` | `cdbf21a3a79f3646c6417565f59aa9af662cb9cacca730e7e2a90159495f1458` |
| `tests/analysis/analysis_fixed_substitute.py` | `23857077226a0049d0ec1c7712412909115e1fa7b067d58b366397e8f52b410f` |
| `tests/analysis/analysis_substitute_driver.py` | `9f9a9870d0718e69384e6a9c1fddc1a8c03b4240c0882bfca15e03fdd520d55b` |
| `tests/analysis/analysis_substitute_transport.py` | `b114322e0e84056def0b9be577f19073b421c83aa5df9843d34a2b88e10cd837` |
| `tests/analysis/test_analysis_evidence_capture.py` | `02e598e9d4160e004c22eddbf5e9311978a965d474a6a9efac31c083aa3203f8` |
| `tests/analysis/test_analysis_evidence_capture_faults.py` | `c9000fb4fffd4c37458d96b5ac11b685b6fe8f9be83eb8454d4699fd883fa2d6` |
| `tests/analysis/test_analysis_substitute_driver.py` | `c34016a0853f0e59f50912e812e49b4ad5c9d9d3a06884f3bd004a1ac581aae5` |
| `tests/analysis/test_analysis_substitute_driver_faults.py` | `d5fe2ef8769c86c7e2820dc231c4f14ea5a5edde5dabc1ea5ccbd4f301551e3e` |

## Coverage and limits

Fixed normal, empty-stderr and large dual-stream outputs validate exact bytes. Failure contracts cover nonzero exit, active-output/closed-pipe timeouts, ignored TERM with KILL/reap, stream/total ceilings and overflow probes, short writes, no later launch, immutable protocol, source drift, publication failures, post-call damage, fresh offline audit, signal/reap ownership and unknown exit status. No real codec, general descendant tree, Windows producer or real cost study is qualified.

Active process/pipe supervision has frozen deadlines; final flush/fsync/close, capture sealing, driver publication and sequential audits are not an absolute wall-time guarantee. Disk hashes do not attest loaded modules or a whole interpreter stack. Three local copies have read/write/parse/sync costs and a shared fault domain. VERIFIED describes the final sequential verification interval, not an atomic snapshot or permanent integrity.

The unchanged Native workflow can classify these paths and pass its gate while skipping native builds/shards. Documentation checks its old 22 registered pairs. The non-perf PR is not a request to run Performance. Actual event, attempt, job logs, checkout parents/tree and artifacts must be read before integration; these workflows do not automatically execute the new 59-test local suite.

## Next handoff

After this slice is accepted and normally merged, prepare the real-cost driver's output semantics adapter and a separate frozen decision protocol, initially with static checks and fixed prepared/observation substitutes. Specify exact candidate/base/binary/input identities, value validation, observation windows, copy/sync costs, full budgets and stopping/acceptance rules. Any actual measurement needs its own independent review and registration. This slice does not open study 003, restore missing study-002 observations, change PR250 HOLD, reopen 001/002, integrate PR248, change product authority or release v5.7.0.
