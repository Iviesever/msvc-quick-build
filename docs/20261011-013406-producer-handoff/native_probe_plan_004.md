# Native prerequisite attempt 004: identify the remaining owned Job member

Status: `SOURCE_READY_PENDING_INDEPENDENT_REVIEW_AND_REGISTRATION`.
Parent: `3de84ca1b3ceba3b6180911d2d671e5be91581d4`.
No 004 compiler or native execution has occurred at preparation time.

## Observed reason for this separate attempt

001 is closed after exact-byte checkout admission failure; no compiler or native
fixture ran. 002 is closed after the compiler root returned 0 without timeout,
but the owned Job recorded three processes and root exit with live members.
The wrapper refused and terminated its own Job; no native fixture ran.

003 added `/DEBUG:NONE`. Its exact reviewed source passed admission and its
compiler again returned 0 without timeout. The artifact retained the EXE and
OBJ without a LINK PDB. Nevertheless, it again recorded Job total 3 and
`root-exit-live-members`, followed by owned Job termination. The PDB absence
check in the runner was not reached because the earlier gate refused. This
rules out the tested no-PDB-only recipe as a sufficient correction. The actual
image of the third process is still unknown. Neither a PDB nor its absence
identifies a process or establishes a service lifetime.

003 run: `38106060921`, run number 3, attempt 1, commit shown above.
Artifact: `producer-seal-003-38106060921-1.zip`, 357234 bytes, SHA-256
`7d853db2c787d17f387a4bb423cb2a2b343303f24ca1c0dc762c5069e790add3`.
Closure: `attempt-003-closure.json`, 49515 bytes, SHA-256
`2b39a4e582063871325fef3d48fedfcacb81cf5741e78c5f5d3eae6484645e5c`.
All 17 members and ten exact reviewed source files were completely read back.

## One diagnostic change, original rejection preserved

The C++ fixture and the actual compiler/linker recipe remain byte-for-byte or
argument-for-argument unchanged from 003. The Job helper adds a diagnostic only
after it has recorded the existing `root-exit-live-members` error and before
its existing `TerminateJobObject` call. It does not extend the wait or change
the process count, success, error or completion rules.

The diagnostic calls `QueryInformationJobObject(JobObjectBasicProcessIdList)`
once on the exact private owned Job, with capacity eight PIDs. Incomplete,
over-cap or failed results are retained as errors, without resize or retry.
For at most eight listed PIDs it opens a noninheritable process handle with
`PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`, verifies membership in that
exact Job with `IsProcessInJob`, then queries the handle's PID, creation time,
full image name and zero-time signal state. One held process handle supplies
all of those observations. It closes every acquired diagnostic handle and
retains query and close errors. Failed membership prevents identity queries.
An elapsed cleanup deadline stops further diagnostic entries; the existing
termination remains unconditional for the already-observed live Job.

This is a bounded snapshot, not a complete historical process census. PID
reuse before OpenProcess is not erased by a name: membership is checked on
the actual opened object, and its actual PID and creation time are retained.
An exited, inaccessible, reused or missing entry can remain unidentified.
The image name is not a cryptographic identity of the executable file. No
out-of-Job process is enumerated, inspected for identity, terminated or certified.
The snapshot cannot provide producer A2 or external-service qualification.

The concrete question is which owned process is still represented at the
rejection boundary, if the same failure recurs. The root may refuse again;
usable diagnostic evidence does not relabel that result as a successful build.
No recipe suppression option will be selected from a guessed process name.

## Registration and budget

Register the independently accepted exact tree, immutable Connector commit,
review identity and this allocation before advancing the existing branch.

- Existing branch `feat/producer-clean-handoff-20261011`; existing workflow
  `.github/workflows/producer-boundary-seal-probe.yml`; push only.
- Required new workflow run number **4**, run attempt **1**, exact activating
  commit. No manual dispatch, PR trigger, matrix or automatic retry.
- One compiler driver call at most. Accepted compiler Job total remains **1
  or 2**. These are observed acceptance caps, not an OS process-creation cap;
  every actual process, including an unexpected third, remains in spending.
- Any existing error, timeout, nonzero exit or incomplete completion refuses.
  In particular, `root-exit-live-members` still refuses after cleanup reaches
  zero, regardless of any diagnostic identity.
- At most one native invocation only if every original compiler/Job/PDB gate
  passes. Native Job total must equal one; zero native child processes and at
  most two auxiliary threads. No C++ fixture case or classification changes.
- At most one diagnostic PID-list call per capture and eight process opens;
  no process launch, separate helper, service lookup, file-image read or wait
  is added. The diagnostic is called only at the original root-exit-live case.
- Compiler root wait 90 seconds, native wrapper root wait 40 seconds, native
  watchdog 30 seconds, the same ten-second cleanup deadline, five-minute
  workflow timeout. OS calls retain the prior no-hard-real-time caveat.
- At most 1 MiB fixture content in new fixed-NTFS objects; compilation and
  capture artifacts retained separately. Fresh output/artifact prefix
  `producer-seal-004-<run_id>-1`; existing output refuses.
- No MQB, ordinary producer, cost, ETW, service study, deletion, disposition,
  old-root takeover or recursive cleanup calls. No evidence overwrite.

Before 004, three new prerequisite workflow runs, two compiler driver calls,
six observed compiler Job processes and zero native fixture calls have been
consumed. Allocate one new bounded attempt; further retry allocation is zero.
001, 002 and 003 remain closed with all their original bytes and statuses.

## Evidence and qualification

Keep complete original source, tool identities, stdout, stderr, partial and
failure records. Windows records are staging only. After artifact transport,
canonical evidence uses exclusive complete writes, file and directory fsync,
and separate-process full archive/member readback.

If the compiler gate refuses, classify the attempt as closed build-boundary
rejection, even when a useful process name is captured. If a native invocation
is admitted, the unchanged `native_probe_plan.md` controls the necessary-case
classification. A name, Job completion or primitive result cannot qualify the
full conditional-ACL and delegated-writer candidate.

A1, A2, A4, C01 and real ordinary terminal LINK positive consumer handoff remain
pending. A3, A5 and deletion remain disabled. PR250 HOLD, the frozen cost
preparation marker, old cost-study failures, ordinary-generation consumer/cost
DEFER, and job-level-disabled M1b research jobs remain unchanged.

Primary API contracts: Microsoft JOBOBJECT_BASIC_PROCESS_ID_LIST, IsProcessInJob,
GetProcessId, GetProcessTimes and QueryFullProcessImageNameW. These support the
limited actual-handle diagnostic; they do not turn a snapshot into completion.
