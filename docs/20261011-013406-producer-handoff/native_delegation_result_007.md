# Closed prerequisite 007: actual DLL-initialization failure observed

Status: `CLOSED_INCONCLUSIVE_DLL_INIT_FAILED`.
The diagnostics compiled and obtained the actual child process termination
status before any owned cleanup: `0xC0000142 / STATUS_DLL_INIT_FAILED`.
No startup snapshot or IPC case completed. The specific DLL and the underlying
access dependency remain unidentified; the candidate remains unqualified.

## Exact execution

- Commit: `76789bb89c5922cade3094277414bf91dcc26ff5`;
  tree: `a48a8d6e4993f585161161796ad1761e12f4dbe4`.
- [Registration comment 6105494779](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105494779),
  written and fully read back before branch activation.
- [Closure comment 6105610155](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105610155),
  written and fully read back after the independent runtime review.
- [Run 38112190244](https://github.com/Iviesever/msvc-quick-build/actions/runs/38112190244):
  workflow `380958963`, run number 7, attempt 1, job `114389871133`.
- Original artifact `11692485424`,
  `producer-delegation-007-38112190244-1.zip`: 664,113 bytes,
  SHA-256 `8c9744be24b71115b07bfe4834d9ee8341070c24bce016b75f6d40bfea6e9f65`.
- Complete archive readback and CRC verification: 29 files. All 17 archived
  repository files match the activating Git commit byte-for-byte.
- `artifact-member-manifest-007.json`: 9,292 bytes,
  SHA-256 `5110ab1f7533566f0cb5128613fc9a1429e7939bf0efcc372c2245b1e25c0435`.
- `runtime-closure-007.json`: 28,606 bytes,
  SHA-256 `78fa9d720644e817060c7480c63fff2b9833067478613a901d2dceef3290779f`.
- Independent `runtime-review-007.json`: 15,387 bytes,
  SHA-256 `1b4a98dea4f3de1d7c75f0ca595348f00c64f2b98433e3ceb7e725304a404b61`.

Source review remains distinct from execution: the independent final source
review is `pre-execution-review-007-agent.json`, 10,920 bytes,
SHA-256 `49ac2da0701a0fcf56a5ec9cc1149662b1ccf42fff97d23bd82949040658d4be`;
root review is `pre-execution-review-007.json`, 7,705 bytes,
SHA-256 `a99fbaedf655adf67b2991baab3c687358013311a38bb27bf7b6aebda85505ce`.

## Measured result

Windows Server 2022 build 20348, image `win22/20261004.326.1`,
MSVC `14.44.35207`, SDK `10.0.26100.0`.

| Observation | Actual result |
| --- | --- |
| Compiler | One driver, exit 0; Job total 2 / active 0; no timeout, capture errors or requested termination |
| Native outer capture | One fixture invocation, root exit 3; Job total 3 / active 0; no outer timeout, capture errors or requested termination |
| Raw native stdout | 35,034 bytes, 332 consecutive JSONL rows; empty stderr |
| Final fixture | One zero-data file; all source/tool and retained-file readbacks succeeded |
| Wrapper | `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`, because outer Job total 3 still violates required total 2 |
| IPC result | Zero observed startup snapshots, first-command snapshots, child reports or test cases |

The original token/ACL/handle predicates and fixed-case gates were not
relaxed. The new diagnostic code successfully exposed the failure status.
That is an observation result, not a successful writer-boundary qualification.

## Actual child evidence, before cleanup

| Raw sequence | Observed value |
| --- | --- |
| 311 | Child-only Job query before ResumeThread: total 1 / active 1 / limit-terminated 0 |
| 316 | First startup transport ReadFile failed with error 109, 0 bytes |
| 318 | One bounded wait on actual child process handle `328` returned WAIT_OBJECT_0; the child was signaled |
| 319 | GetExitCodeProcess succeeded: decimal `3221225794`, hex `0xC0000142` |
| 320 | GetProcessTimes succeeded, creation `134361670868470068`, exit `134361670868524913`; both times are lossless strings in the raw JSON |
| 321 | Same child-only Job queried before cleanup: total 2 / active 0 / limit-terminated 0 |
| 322 | Actual child process handle closed only after those observations |
| 325 | Child-only Job handle closed afterward |
| 332 | Final native summary remains INCONCLUSIVE, errors 2, cases 0 |

This child had already terminated before inner Job close. Its actual status
is not one of the source's ordinary `return 3` rejection paths. The first
startup frame is absent, so the child's handle flags were not observed and
must not be filled from the parent or inferred to be zero. Legacy summary
child Job 0/0 remains an unqueried default; the new successful Job queries
above are the actual measurements.

[Microsoft's NTSTATUS table](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)
maps this value to DLL initialization failure. The
[CreateProcessAsUserW documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw)
explicitly states that successful process creation can return before required
DLL initialization completes, and directs the caller to GetExitCodeProcess
for later initialization failure. Neither source identifies the DLL in this run.

The additional inner/outer Job count has no observed image identity. It must
not be labelled WerFault, a console host or a successfully executing third
fixture process. Its exact attribution remains open; the original count
gate remains in force. The process times above are identity/lifetime evidence,
not a performance sample.

## Next handoff

Locate the concrete DLL-initialization access dependency. A specific change
must be justified by the actual startup construction and primary API/source
evidence, then tested as one separately reviewed and registered hypothesis.
Do not remove the inheritance check, accept outer total 3, or change token
default DACL, process/thread DACL and desktop access simultaneously.

The selected next [008 hypothesis](native_delegation_plan_008.md) adds only
the fresh restricting SID grant to the new C token's default DACL, with
complete readbacks. Its primary-source precedent does not establish the
cause of this closed run; process/thread and desktop access are not changed.

Before any further attempt, preserve the original 007 failure and this exact
diagnostic chain. Attempts 001-007 now comprise seven workflow runs, six
compiler-driver invocations, compiler Job TotalProcesses summing to fifteen,
and three native fixture invocations. None is ordinary MQB production.

A1/A2/A4 and the actual positive consumer handoff remain pending. A3/A5 and
deletion remain disabled. 005's attribute-writer counterexample remains
valid; 006 and 007 supply no tested IPC-route conclusion. Frozen cost
preparation, PR250 HOLD, consumer/cost DEFER and disabled M1b remain separate.
