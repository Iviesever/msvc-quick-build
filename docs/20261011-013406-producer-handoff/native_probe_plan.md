# Native prerequisite: selected-boundary seal primitives

Status: `PREPARED_NOT_EXECUTED`. Attempt identifier: `producer-seal-001`.
Base: `87f86770cfc7e1941603074ace914e0c7faf10d3`.

## Purpose and gate

Before implementing the selected conditional-ACL enforcer, falsify its two
smallest necessary assumptions: the proposed exclusive RWH grant must detect
preexisting attribute-only file handles, and a final directory data-read pin
must not silently be described as excluding every old namespace handle.
Independent source review found this concrete candidate testable. No additional
user permission to continue engineering is required.

This is a single-process Windows primitive qualification fixture. Independent
opens create distinct native file objects in the same process; the test does
not pretend that these handles belong to a service or to actual MSVC. Such a
case can disprove a universal uniqueness claim. Passing it proves neither the
ACL boundary nor service completion, and is not C01 or A1/A2/A4 completion.

## Scope and fixed budget

| Item | Maximum for attempt 001 |
| --- | --- |
| GitHub workflow | One new branch-triggered run, run number 1 and attempt 1 only |
| Fixture build | One cl driver invocation and its one link invocation; at most two owned Job processes, no retry |
| Fixture execution | One executable invocation; no child helper processes |
| OS / architecture | `windows-2022` runner, native x64, actual OS/image recorded |
| Fixture wall time | 30 seconds; wrapper timeout 40 seconds |
| Fixture content | At most 1 MiB, only newly created local NTFS objects |
| Compiler timeout | 90 seconds |
| MQB / ordinary producer / service study / ETW / cost calls | Zero |
| Old fixture cleanup, old output takeover or evidence overwrite | Zero |

Fixture compilation is test infrastructure, not a qualification run of the
ordinary MSVC producer. It uses the installed VS 2022 Enterprise x64 compiler
selected by its existing default-tools-version file and the newest complete
installed Windows 10 SDK include/lib version, without installation or registry
changes. Before the sole compiler launch, record the exact resolved compiler,
linker, compiler DLLs and runtime library identities, source hashes, ordered
arguments and relevant include/lib paths. Bind the built fixture by full hash
before its sole launch. Recheck source/tool identities after the attempt.
Do not report the runner label as an immutable OS/toolchain identity.

The new workflow is restricted to the isolated branch and has both run-number
and attempt guards. The script also requires the expected repository, ref,
event, run and attempt and refuses an existing output root. No workflow_dispatch,
PR trigger, matrix or automatic rerun is introduced. A failed attempt stays
closed. Any later revised attempt needs a separately reviewed source/budget
record and keeps this attempt's original failure and streams.

The fixture runner redirects original stdout/stderr into new staging files
before process creation. The compiler and probe each receive a private
kill-on-close Job; their root starts suspended and joins the Job before resume.
Only explicit standard handles are inherited. Timeout/remaining members are
terminated within that owned Job, with a bounded member drain. This bounds test
infrastructure; it does not assert any service or selected-output isolation.
Partial streams survive a capture failure. Final readback errors retain the
original native result and invalidate the attempt instead of masking the error.

## Ordered observations

1. Create the new fixture root and confirm the actual volume is local NTFS.
   Create a small ordinary file. With no earlier file handle remaining, open a
   real data-read handle sharing READ only and request RWH on that same handle.
   If the baseline cannot obtain and retain the oplock, report inconclusive;
   do not reinterpret unsupported behavior as exclusion.
2. On separate new files, keep a `FILE_WRITE_ATTRIBUTES`-only handle, then a
   `FILE_WRITE_EA`-only handle, each opened before the final data-read/share-READ
   handle. Observe exact share compatibility and RWH grant/refusal. Attempt
   the corresponding harmless fixture-only metadata/EA mutation. A grant with a
   verified mutation completed while the final pin is still held and the RWH
   request remains pending disqualifies the proposed seal. Handle coexistence
   alone disproves the only-open subclaim; a notified break or mutation completed
   only after release remains inconclusive for this probe.
3. On separate new directories, retain exact `FILE_ADD_FILE`,
   `FILE_DELETE_CHILD`, and `FILE_TRAVERSE` handles separately, rather than
   combining them into GENERIC_WRITE. Observe the final directory
   `FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES` / share-READ pin result.
   An admitted old namespace capability which coexists is an unresolved
   primitive obligation; it cannot be called fully excluded without a later
   exact ACL-revocation/relative-operation test.
4. Emit exact call results, immediately captured native errors, access/share
   flags, separate handle identities, oplock return and break states, mutation
   result, and explicit candidate-disqualified/inconclusive/necessary-checks-only
   classification. A test-program zero exit is not a completion certificate.

All mutations are to the new probe objects. No deletion/disposition call,
recursive cleanup, account/privilege change, output-root takeover, or preserved
source/tool ACL change is permitted. Keep pending OVERLAPPED storage alive until
its operation completes or process shutdown; cancel and drain deliberately.

## Review and evidence

Before any branch ref makes this workflow runnable, independently review the
actual fixture, runner, workflow and plan hashes. Bind the exact Connector
commit and the review in #198. The workflow's raw stdout/stderr, preflight and
result records, fixture sources, binary and retained files are captured as
complete files with exclusive creation and flush. They are staging captures;
the final canonical evidence is sealed on receipt with exclusive complete
writes, file and directory fsync, and independent full readback. Preserve native
failures even if parsing or capture later fails. No missing result means pass.

The PR250 cost preparation stays byte-identical. M1b default-endpoint/service
research remains guarded false; this fixture neither calls those collectors nor
allocates another 70/42-case matrix. Applicable existing automatic CI is a
separate gate if a later PR or product change triggers it.

## Next gate after this attempt

If an old mutation succeeds through the proposed held seal, reject that seal
and preserve the exact counterexample. A failed only-open or directory-handle
exclusion subclaim alone leaves a precise unresolved obligation; it does not
prove that every composite design is impossible. The next mechanism must
explicitly address the observed capability, without a caller bool or relabelled
native return. If the observations support the necessary assumptions, next
qualify full conditional-ACE identity,
old-handle and named-pipe delegation/revocation behavior, then integrate and test
the actual single-source ordinary compile/LINK path. No positive product
capability, A3/A5, or deletion is enabled by this probe.

Primary references: Microsoft CreateFileW; Types of Opportunistic Locks;
FSCTL_REQUEST_OPLOCK; Checking the Oplock State of an IRP_MJ_CREATE Operation;
and MS-FSA FileRenameInformation. The implementation plan records the selected
composite mechanism and its remaining obligations.
