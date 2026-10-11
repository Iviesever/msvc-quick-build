# Closed startup diagnostic 009: capture complete, initializer unidentified

Status: `CLOSED_INCONCLUSIVE_DIAGNOSTIC_CAPTURE_COMPLETE_DLL_CAUSE_UNIDENTIFIED`.
The actual C debug-event chain completed, and its exit was independently
queried through the retained original process handle. C still exited
`0xC0000142 / STATUS_DLL_INIT_FAILED` before any startup frame or debug string.
No particular DLL initializer or failed access was identified. The unchanged
outer Job gate rejects this attempt before parsing the native summary.

## Exact execution and independently checked evidence

- Execution commit `59179a95739ef12b358c462470e84943d730a6e4`,
  tree `3191836534e4588fb3f1bcadd1eb7fae3d58288c`; parent
  `31a064f164df5224a0c9a111650564a117f8b4fa`.
- [Registration 6105984404](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105984404)
  was completely read back before activation.
- [Run 38116128743](https://github.com/Iviesever/msvc-quick-build/actions/runs/38116128743):
  workflow `380958963`, number 9 / attempt 1, job `114401261202`.
  The run completed with failure; the original evidence upload succeeded.
- [Closure 6106121868](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6106121868)
  was written and fully read back after the two runtime reviews.
- Original artifact `11693610310`,
  `producer-startup-009-38116128743-1.zip`: 824,616 bytes,
  SHA-256 `34f7c42ac9d3ea493b04c16d10d587526e40a7d9cf4659613f520cd3ebbba10e`.
- `artifact-member-manifest-009.json`: 10,162 bytes,
  SHA-256 `13dfdf49072183df313c7579021d89bc90ba0658cf77f3283afb92ce172bd896`.
- Root `runtime-closure-009.json`: 64,727 bytes,
  SHA-256 `3949e690ee3fb7257848baba36aec40af7c2e6292fbfc7883d1eb0988ab89db3`.
- Independent `runtime-review-009.json`: 61,423 bytes,
  SHA-256 `3fa6d609015c4027f4c034dd97eb0f694980cd7fb3116c89dea11b9966f220e2`.
- `closure-comment-readback-009.json`: 12,226 bytes,
  SHA-256 `d07e677faeb2605fbdc6d50d772ff1b700a99522fd61749ce2125d0e31a4d709`.

Both reviews fully read and checked all 34 ZIP members, their CRC and SHA-256,
all 23 repository members against the activating commit, seven preflight
source identities and ten retained-file records. Fourteen tool identities
were recorded before compilation, with no later reported changes. The
reviewers did not independently reopen the live Windows tool binaries after
the run. Original bytes and integer/string representations remain preserved.

Source review was separate: `pre-execution-review-009-agent.json`, 27,066 bytes,
SHA-256 `6ef09ebef323a55f4a829135f16859d736d13bb37417876b4333eeaa7d19c5b3`;
root `pre-execution-review-009.json`, 14,075 bytes,
SHA-256 `66655acb8ffd2215691a86cc4b97df141c1e65927340fee2869dcf2af393e374`.
The final compiled translation unit was 50,262 bytes, SHA-256
`1007705524ae2e4d041aa5d64a734cb9b9d16c01ebc63b1a9811a3faec328dd8`;
its runner was 16,670 bytes, SHA-256
`c9f8b72c8cfebbe3067628f49e2cadabb4b2f0004c45f6e51f95305db351807d`.

## The three distinct result layers

Windows Server 2022 build 20348, image `win22/20261004.326.1`,
MSVC `14.44.35207`, SDK `10.0.26100.0`.

| Layer | Observed result | Meaning |
| --- | --- | --- |
| Fixture compilation | One driver, exit 0 under /W4 /WX; Job total 2 / active 0; PDB absent | This diagnostic source compiled and passed its compiler capture gate. |
| Native debug capture | 639 strict consecutive JSONL records, 15 events, 15 successful continuations, errors 0, capture_complete=true | The recorded diagnostic event chain completed under its own bounds. |
| Outer execution admission | Native root exit 3; outer Job total 3 / active 0; exact-total-2 gate fails | Runner remains INCONCLUSIVE/NATIVE_EXECUTION_INCOMPLETE before native summary parsing. No production qualification follows. |

Neither capture had an outer timeout, error or requested Job termination.
The raw stdout was 74,527 bytes, SHA-256
`326f4215c71232f07eb7d2704af5da2e299c909a704bd012fc77f3a06a18bafd`;
native stderr was empty. `result.json` contains no parsed `native_summary`.
The external raw review does not insert one retroactively or relax the gate.

All 326 assertion records and 94 api records succeeded. There were also 23
expected size-query failures with error 122 and one expected no-thread-token
error 1008. Thus errors 0 in the native summary does not mean every recorded
API-shaped outcome has ok=true/error=0. The source-defined expected probes
were checked separately from unexpected errors.

## Actual token and object construction

S was PID 7848. Actual C was PID 2916, initial TID 5124. The child was born
suspended with flags 134742022, the original two inherited anonymous-pipe
handles, and the original atomic Job-list assignment. The added diagnostic
flag was DEBUG_ONLY_THIS_PROCESS. No new ACL grant was introduced by 009.

| Raw sequence | Default-DACL snapshot | Result |
| --- | --- | --- |
| 163 | S before filtering | 80 bytes / 3 ACEs |
| 219 | Restricted token before update | Exact original ACL |
| 249 | Expected additive R/GA ACL | 116 bytes / original 3 ACEs plus one R ACE |
| 288 | Immediate restricted-token readback | Exact expected ACL |
| 402 | Actual C before resume | Exact expected ACL |
| 484 | S before C resume | Exact original S ACL |

The fresh R was `S-1-5-21-0-2374062-7848-1048577`. Both actual primary tokens
had authentication ID `689147`; S ProcUnique was `{7,2373897}` and C's was
`{18,2374109}`. C retained only enabled SeChangeNotifyPrivilege, LUID 23;
S had zero enabled privileges and no thread token. C is not described as
having zero enabled privileges.

At sequences 499 and 511 the actual suspended C process/thread DACLs were
read through handles 344 and 340. Each had a protected 136-byte, two-ACE DACL:
OWNER RIGHTS READ_CONTROL and the H ProcUnique conditional full-access grant.
The complete raw ACL and SDDL are in both reviews. These observations verify
that the requested descriptors reached the objects. They do not establish
that a missing SYSTEM ACE, desktop right or any other access caused the exit.

## Event, exit and cleanup evidence

All 15 debug events identified actual C PID 2916. The sequence comprised one
CREATE_PROCESS, eight LOAD_DLL, two CREATE_THREAD, one initial breakpoint,
two EXIT_THREAD and one EXIT_PROCESS. All nine supplied image hFile handles
were successfully closed before their corresponding ContinueDebugEvent.

The observed DLL paths were ntdll, kernel32, KernelBase, advapi32, msvcrt,
sechost, bcrypt and rpcrt4 beneath System32. These were bounded path/base
observations, not separate file-hash, version, PDB or symbol observations.
A module's position in this list does not identify an initialization failure.

| Raw sequence | Observation |
| --- | --- |
| 512 | Inner Job before resume: total 1 / active 1 / terminated 0 |
| 513 | ResumeThread returned previous suspend count 1 |
| 515 | Original PROCESS_INFORMATION thread handle 340 closed |
| 600 / 602 | First-chance initial breakpoint 0x80000003, address 140708988011844 |
| 603 | One successful VirtualQueryEx associated that address with observed ntdll MEM_IMAGE, RVA 869700 |
| 619 / 620 | EXIT_PROCESS reported 0xC0000142; its Continue succeeded |
| 622 | Diagnostic cleanup phase entered because the exit event had been continued; no child command sent |
| 624 / 625 | Original C process handle 344 signaled; actual exit query returned the same 0xC0000142 |
| 626 | Creation/exit FILETIME strings 134361714855409663 / 134361714855636637 |
| 628 | Inner Job before close: total 2 / active 1 / terminated 0 |
| 629 | Inner kill-on-close Job closed |
| 630 / 631 / 632 | Parent command writer, report reader and actual C process handle closed, in that order |
| 639 | Native summary: capture complete, no diagnostic termination request, startup absent, all authorities false |

Debug-event process/thread handles were borrowed. Their numeric values can
repeat values already closed in an earlier lifetime: token handle 352 closed
at 408 before borrowed debug process 352 arrived at 520; original thread 340
closed at 515 before borrowed debug thread 340 arrived at 520. Image handle
248 was last closed at 563 before borrowed thread 248 appeared at 568.
No later explicit close of those borrowed generations was recorded.

All 16 PeekNamedPipe observations succeeded with zero available bytes and no
ReadFile attempt. No 56-byte startup frame, first command, debug string,
named test pipe, IPC case or fixture data file was observed. A path string in
the client arguments is not evidence that its file or named pipe was created.

The actual C had already exited before the owned Job close, and this execution
did not request TerminateProcess on C. Nevertheless, inner accounting 2/1/0
does not prove whole-inner-Job natural completion or identify an additional
executing process. The outer helper's lack of a termination request does not
exclude inner Job-close effects. Preserve this exact observation, including
its difference from 007's 2/0/0.

## Closed conclusion and next handoff

The initial breakpoint's ntdll association is not a DLL_INIT_FAILED exception
location. No failing initializer, DLL or access operation has been identified.
The debugger captured no strings that could provide more specific evidence.
Optional-query failures, structural failure, timeout cleanup and deadline
overrun branches remain source-reviewed only; this run did not cover them.

The next [010 handoff](native_startup_handoff_010.md) specifies a concrete
proposal to enable loader snaps using only the documented GlobalFlagsSet
DWORD in a fresh child EXE copy. No 010 implementation, registration or run
exists. Its own bounds and exact identities must be reviewed before activation.

After 009: nine prerequisite workflow runs, eight compiler drivers, compiler
Job TotalProcesses summing to nineteen, five native fixture invocations and
zero ordinary MQB executions. Those counters do not prove that every counted
helper executed successfully. The 005 metadata-writer counterexample remains
disqualifying; 006-008 remain closed with their original observations.

A1/A2/A4, an ordinary terminal-LINK receipt and the positive same-session
consumer handoff remain pending. A3/A5/deletion stay disabled. Main remains
`87f86770cfc7e1941603074ace914e0c7faf10d3`; PR250 and PR248 remain separate
open draft PRs. PR250 HOLD, consumer/cost DEFER, the byte-identical cost
preparation and disabled M1b/service jobs are unchanged. This closeout changes
documentation only; no native rerun is selected.
