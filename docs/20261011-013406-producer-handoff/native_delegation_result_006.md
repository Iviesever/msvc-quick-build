# Closed prerequisite 006: child startup was not observed

Status: `CLOSED_INCONCLUSIVE_BEFORE_IPC`.
The fixture compiled, but none of the four IPC cases started. This result
neither qualifies nor disqualifies WRITE_RESTRICTED as the proposed boundary.
The actual child exit status is missing from this attempt; do not infer it
from the server's exit code or the broken report pipe.

## Exact execution and retained evidence

- Source commit: `a6f3557f27f9b061551f1e2f8351f16be499cee5`;
  tree: `50fd5f46b39ac524518842e7f170ad6e525ad627`.
- Registration: [issue #198 comment 6105352596](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105352596).
- Execution: [run 38111081193](https://github.com/Iviesever/msvc-quick-build/actions/runs/38111081193),
  workflow `380958963`, run number 6, attempt 1, job `114386602394`.
- Original artifact: `11692085275`,
  `producer-delegation-006-38111081193-1.zip`, 641,957 bytes,
  SHA-256 `cb04a016cf1b6fd40c67360a0965b70e869e07714f679fe2efda58466770c401`.
- Independent complete ZIP readback, CRC verification and extraction retained
  27 files. `artifact-member-manifest-006.json`: 5,555 bytes,
  SHA-256 `47b59f508dd7788413cc901e6dc911fc293821628b873dd5e3e9de4c4ed0fc69`.
- Root closure: `runtime-closure-006.json`, 29,351 bytes,
  SHA-256 `bb2cb7c219391c413dfa94395bcb7e27f456edd218d1a9b236ea1362690d888a`.
- Independent review: `delegation-006-runtime-review-001.json`, 25,841 bytes,
  SHA-256 `87053a002488d1527b11d801695c6673839134f0dff7f5312fb279d0a8e2efc6`.
- Closed result recorded in [issue #198 comment 6105447231](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105447231)
  and read back byte-for-byte before preparing the next registration.

The six preflight source files exactly match the registered commit, as do
14 of the 15 archived repository files. The old `native_probe_result_005.md`
was checked out with CRLF: 6,969 bytes, SHA-256
`c07fdd8c605ce7198305ddd2a5bf6cd007f618e287fabe136c38a0c002a089e0`.
Its committed LF bytes remain 6,856 bytes, SHA-256
`d34d53fbed41663f0806e6ca5be3f6c5bba22624bec53a00a1f07029d5836dda`.
The only difference is the 113 CR bytes. This historical document was not
one of the six execution inputs. Both identities and the original archive
are retained; it is not silently normalized in the evidence.

## What actually ran

Environment: Windows Server 2022 build 20348, hosted image
`win22/20261004.326.1`, MSVC `14.44.35207`, SDK `10.0.26100.0`.

| Observation | Actual result |
| --- | --- |
| Compiler driver | One invocation, exit 0, no timeout |
| Compiler Job | Total 2, active 0, completed; no requested termination or capture errors |
| Native fixture | One invocation, root exit 3, no outer timeout |
| Native outer Job | Total 3, active 0, completed; no outer requested termination or capture errors |
| Raw native output | 34,236 bytes, 328 ordered JSONL rows, empty stderr |
| Native summary | `INCONCLUSIVE`, errors 2, cases completed 0, positive control false |
| Wrapper classification | `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE` |
| Final retained fixture | One zero-byte file; no identity/readback errors |

The compiler telemetry image pin and its before/held/after execution-open
controls completed. All six sources and fourteen selected tool identities
were unchanged on final full readback. This is a fixture build, not an
ordinary MQB compile or LINK and not a cost/ETW experiment.

## Exact failure boundary

Before the first client resume, the server successfully queried the actual
child token: its ProcUnique values differed from the server's, its user and
authentication context matched, and its single restricting SID and enabled
privilege bound matched the plan. The protected file DACL was read back
exactly and a fresh FILE_WRITE_ATTRIBUTES open under the server primary
token was refused with ERROR_ACCESS_DENIED. All server privileges were
disabled and the server thread had no impersonation token.

The decisive raw sequence is:

| Sequence | Observation |
| --- | --- |
| 311 | ResumeThread returned previous suspend count 1, error 0 |
| 315-316 | The original 16-byte leaf command was written completely |
| 317 | First bounded child-report ReadFile failed with error 109, ERROR_BROKEN_PIPE |
| 318 | The server closed its actual child process handle |
| 321 | The server closed the child-only Job handle |
| 323 | Exception recorded as `bounded child report` |
| 328 | Final `INCONCLUSIVE` summary |

There are no child-report, named-pipe-case, impersonation, child-exit-wait or
child-Job-completion records. Summary `client_job_total=0` and
`client_job_active=0` are unqueried default values, not a measured empty Job.
Root exit 3 is not evidence of child exit 3. The absence of an outer
termination request does not exclude the inner KILL_ON_JOB_CLOSE cleanup.

The outer total of 3 exceeds the registered count of 2 and independently
fails the runner's completeness gate. It does not identify a third process
image or prove that it ran user code. Microsoft documents that even a Job
association failing due to a limit violation increments TotalProcesses:
[JOBOBJECT_BASIC_ACCOUNTING_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information).
No WerFault, console host, handle-inheritance or CRT root cause is asserted.

## Next engineering operation

Preserve this failed attempt. In separately reviewed and registered
[prerequisite 007](native_delegation_plan_007.md), add only observations of
client startup and the first command, plus actual child-process exit/times
and child-Job accounting before owned handles are cleaned up. Keep the
existing access checks, process limit, fixed routes and conclusive gates.
Do not loosen a predicate based on error 109 or rerun 006 with changed bytes.

A1, A2, A4 and the positive live consumer handoff remain unimplemented.
A3, A5 and deletion remain disabled. Attempt 005's old attribute-writer
counterexample remains valid and unchanged. PR250 HOLD and the frozen
cost/consumer controls remain separate.
