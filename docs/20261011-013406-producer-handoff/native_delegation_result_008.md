# Closed prerequisite 008: default-DACL grant was not sufficient

Status: `CLOSED_INCONCLUSIVE_DEFAULT_DACL_HYPOTHESIS_NOT_SUFFICIENT`.
The exact additive R/GENERIC_ALL grant reached the actual suspended C primary
token and S retained its original default DACL. C nevertheless exited with
`0xC0000142 / STATUS_DLL_INIT_FAILED` before any startup report. The particular
DLL or failed access remains unidentified; there is no IPC-route result.

## Exact execution and evidence

- Commit `31a064f164df5224a0c9a111650564a117f8b4fa`,
  tree `ad57e04d568fc7c6b6fffbec956ae8095bde35d4`.
- [Registration 6105676104](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105676104)
  was fully read back before activation.
- [Run 38113661316](https://github.com/Iviesever/msvc-quick-build/actions/runs/38113661316):
  workflow `380958963`, number 8 / attempt 1, job `114394168408`.
- [Closure 6105774964](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6105774964)
  was written and fully read back after independent runtime review.
- Original artifact `11691884852`,
  `producer-delegation-008-38113661316-1.zip`: 700,815 bytes,
  SHA-256 `ee7dd664329d0d020ccd0a586424e0122e31514c6dc50d26ccd5d8d870f4df38`.
  All 31 members were read completely and CRC verified; all 19 repository
  files match the activating commit byte-for-byte.
- Member manifest: 9,870 bytes,
  SHA-256 `5a1e8eb770095496e768613cfb7ac3e517ce6a333d4cfee646664ef5fea5cccc`.
- Root `runtime-closure-008.json`: 37,639 bytes,
  SHA-256 `ce47b9f757f270600282165eeac6172ec33791ff2422e80f94533eac2877b77e`.
- Accounting clarification `runtime-closure-008-accounting-note.json`: 1,005 bytes,
  SHA-256 `e8079d7ba2b33ad0f02df4cd1d986c6904fa64f548fd33861850348d7adc28de`.
- Independent `runtime-review-008.json`: 41,555 bytes,
  SHA-256 `c0445bf3d49d2fe25f19e32fc3cddf4af4e5bf6da04ee1fe91fceee8d4fff8ca`.

The source review was separate: independent `pre-execution-review-008-agent.json`,
19,665 bytes, SHA-256
`31423e81f67c23a1fca59d55b3acc7ddf11d71252d1f515e8a1986d22be75d1d`;
root `pre-execution-review-008.json`, 11,016 bytes, SHA-256
`8e54a83ef52fe07f4d8307a8ba093b7d293851bb6f4d24d495fa2f3389cae2ec`.
Complete source and evidence readbacks preceded their respective claims.

## Actual default-DACL change

All six raw ACL hex records were independently decoded, including every ACE
size, mask, flag and SID. The fresh R was
`S-1-5-21-0-2170001-3680-1048577`.

| Raw sequence | Snapshot | Bytes / ACEs | Actual result |
| --- | --- | --- | --- |
| 163 | S before filtering | 80 / 3 | Administrators GA, SYSTEM GA, logon SID GR/GX |
| 219 | New restricted token before setting | 80 / 3 | Exact original ACL; no R |
| 249 | Constructed expected ACL | 116 / 4 | Original ACEs plus one flags-0 R/GA ACE of 36 bytes |
| 288 | Immediate new-token readback | 116 / 4 | Exact expected ACL |
| 401 | Actual suspended C primary token | 116 / 4 | Exact expected ACL |
| 503 | S before C resume | 80 / 3 | Exact original S ACL |

SetTokenInformation succeeded at sequence 254 on the new restricted handle
`172`; the S handle was `244`, and actual C's query-only token handle was
`336`. All original ACE bytes and their order remained unchanged. This run's
expected and readback allocations happened to be byte-identical; the source
comparison intentionally uses validated ACE content rather than allocation
capacity or address-dependent PACL values.

## Actual execution and failure

Windows Server 2022 build 20348, image `win22/20261004.326.1`,
MSVC `14.44.35207`, SDK `10.0.26100.0`.

| Observation | Measured result |
| --- | --- |
| Compiler | One driver, exit 0, Job total 2 / active 0, no capture errors, timeout or requested termination; PDB absent |
| Native outer capture | One fixture invocation, root exit 3, Job total 3 / active 0, no outer errors, timeout or requested termination |
| Native trace | 58,659 bytes, 528 strict consecutive JSONL records; empty stderr |
| Retention | One zero-data fixture file; source/tool/retained-file readbacks reported no changes or errors |
| Wrapper | `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`; outer total 3 fails the exact total-2 gate before summary parsing |
| IPC | Zero startup frames, first-command frames, child reports or cases |

The actual C process was PID `7932`; S was PID `3680`. The token user,
authentication context, ProcUnique distinction, single restricting SID and
privilege predicates passed. No parent-configured handle flag was substituted
for missing child-side observations.

| Raw sequence | Actual child/Job evidence |
| --- | --- |
| 507 | Inner Job before resume: total 1 / active 1 / limit-terminated 0 |
| 512 | First startup transport ReadFile: error 109, 0 bytes |
| 514 | Single bounded wait on actual C process handle `328`: signaled |
| 515 | Actual C exit obtained successfully: `3221225794 / 0xC0000142` |
| 516 | Lossless creation/exit FILETIME strings: `134361687423923431` / `134361687424112312` |
| 517 | Same inner Job before cleanup: total 2 / active 1 / limit-terminated 0 |
| 518 | Actual C process handle closed after these observations |
| 521 | Inner kill-on-close Job handle closed |
| 528 | Native summary INCONCLUSIVE, errors 2, cases 0 |

The actual C had already exited before cleanup. The particular DLL and
access dependency are not identified by the status code. Summary child Job
0/0 and the incomplete false fields are defaults, not observed Job results
or IPC refusals.

The `2/1/0` observation must not be changed to 007's `2/0/0`.
[Microsoft's Job accounting documentation](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information)
allows temporary active counts for failed associations until exit/reference
release. Active 1 alone does not prove an additional executing process, and
no PID/image listing exists here. The outer helper's lack of a termination
request does not prove natural inner completion or exclude inner Job-close
effects. No particular additional process or specific kill is asserted.

## Closed hypothesis and next handoff

The exact single R grant was not sufficient to remove this startup failure.
It does not follow that default-DACL grants are always irrelevant or that
another particular ACL is the cause. Preserve 008 and do not repeat its
missing-R hypothesis or combine speculative permission changes.

The next [009 source](native_startup_plan_009.md) is a separate, explicitly
non-qualifying startup debug-event capture of actual C. It retains the token
and object ACL construction, observes actual process/thread DACLs, mapped
module identities, bounded debug strings, exceptions and exit. A mapped DLL
list alone cannot identify a failed initializer. The diagnostic must have its
own exact review, immutable registration and bounded one-shot activation.

After 008 there have been eight prerequisite workflow runs, seven compiler
drivers, compiler Job TotalProcesses summing to seventeen, and four native
fixture invocations. None is ordinary MQB production. A1/A2/A4 and the positive
live consumer handoff remain pending. A3/A5/deletion remain disabled; the 005
counterexample, frozen cost preparation, PR250 HOLD, consumer/cost DEFER and
disabled M1b remain unchanged.
