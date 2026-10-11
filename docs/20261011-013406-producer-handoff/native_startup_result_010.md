# Closed loader-snaps diagnostic 010: bcrypt attach initialization failure

Status: `CLOSED_INCONCLUSIVE_BCRYPT_ATTACH_FAILURE_INTERNAL_CAUSE_UNKNOWN`.

The actual restricted child C's loader explicitly reported that the init
routine for `C:\Windows\System32\bcrypt.dll` failed during
`DLL_PROCESS_ATTACH`. This identifies a failed module and initialization stage
under 010's loader-snaps conditions. The internal failing API, object/access
mask and root cause remain unknown. Actual C exited `0xC0000142` before any
startup frame or IPC case. The unchanged outer Job gate still rejects the run
before native-summary parsing; this is not a qualified producer handoff.

## Exact execution and closure

| Item | Exact identity |
| --- | --- |
| Execution commit | `083d581af6822c04754799e2db92636f0c891fd8` |
| Tree / parent | `61abf70a6b38b4ac8651d814ba5b98abec31d29c` / `3dedbb8abbc8c346294a70abc1fcada37e1f21c1` |
| Registration | [6107842676](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6107842676), complete readback before activation |
| Run | [38130540974](https://github.com/Iviesever/msvc-quick-build/actions/runs/38130540974), workflow 380958963, number 10 / attempt 1 |
| Job | 114443756693; completed failure, original artifact upload succeeded |
| Closure | [6108033087](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6108033087), complete body readback matched |
| Artifact | 11698404468, `producer-loader-snaps-010-38130540974-1` |
| Original ZIP | 1,162,949 B / SHA-256 `91d6e5e88d10031e36e2711d1ff9bc75ccd75baeaee09e5eef7242e10445e018` |

All 45 ZIP members were fully read and CRC/hash checked, with unique safe names
and no links. All 32 repository members match the execution commit byte for
byte; the other 13 are runtime files. Total uncompressed size is 4,788,525 B.
Extracted bytes and twelve final retained-file inventory entries were checked.
The original ZIP and historical 001-009 source/evidence remain unchanged.

The [010 proposal](native_startup_handoff_010.md) and
[010 execution plan](native_startup_plan_010.md) remain immutable snapshots
of their pre-execution state. This result and the closure above supersede those
historical status lines; they do not modify the registered plan retrospectively.

## Independent evidence and verification

These evidence files are in the delivered 010 evidence package. SHA-256 binds
each complete file; sealing these diagnostic reviews does not change the
product result's `canonical_evidence_sealed=false` field.

| Evidence file | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifact-member-manifest-010.json` | 17358 | `bedfad0942f071560b0725e07f1f0defe14895029b081e2ba0b6c7d63f5f4596` |
| `runtime-closure-010.json` | 178723 | `bf3c70408e664608acb46526d61314d413852521c537fbe9c90e23239d57cf87` |
| `runtime-native-review-010-agent.json` | 748860 | `49981f04cc23af41bd0cdaeb6b5db4d8f396b3192922029b84d5bf3f924d7004` |
| `runtime-image-review-010-agent.json` | 70016 | `38c4c9c5c6f18d7109691847377bf8d3e8c9698bb86cc8fddf08cf03c3eca5f2` |
| `runtime-review-admission-010.json` | 6322 | `cd739f3c02d36ee2163c03733471a7e7b7c365008f30f20d416314b7eebdc689` |
| `loader-strings-decoded-010.jsonl` | 219788 | `62d53e647e0953e4c40bcc51e0c20eed0340ad679dbd2b1e2f305e8d4d75dd92` |
| `closure-comment-readback-010.json` | 20939 | `6fe578564fa8849b83e442a521bd9006d4e8005e3bc5ef7c7d526032b85d6c5c` |
| `source-static-checks-010.json` | 14050 | `b64ff44ec59fb56f89f9f0ce597d2a2ecdaaaa1ec42b75966dcd123853304d64` |
| `source-review-admission-010.json` | 7181 | `6fa9036b00b5e164e703a2c3400f0a02e7f83b46a48e5a627acb9f76e96e7f2d` |

Root fully checked the raw archive and runtime. Separate independent reviewers
checked the event/handle/token/loader-string chain and the PE/image-operation
chain. Their complete files were read back and reconciled. There was no extra
native run during review. Optional-query failures, structural errors, timeout
cleanup and deadline-exhaustion paths remain source-reviewed, not covered by
this successful raw-capture path.

The final source passed 37 meaningful pure-Python tests: 28 PE validation and
9 capture-admission tests. The actual command and output appear in the
[walkthrough](walkthrough.md). Tests exercised malformed or ambiguous PE
ranges, flag/directory aliasing, wrong or unrelated bytes, strict JSON and
counter/binding/authority refusal. Before activation, source review also found
and corrected nested-record sequence inversion, missing DLL hFile completeness,
the final-pin deadline gap and the PE-field alias. Rejected candidates were
never run natively. This is diagnostic-source verification, not the product
test suite or ordinary MQB qualification.

Final CPP: 65,047 B / SHA-256
`28e76ee784fbb66d678cf0a2806a61e2184d942b0a4fad4c5959a204c0c4b78a`.
Runner: 22,398 B /
`25953d401e8292787d75c7138b7f89102362ce0d91dfa20bf767bebaff07dd80`.
The ZIP contains the exact PE/image helpers, tests, workflow and all included
frozen sources. Twelve preflight source identities match; fourteen selected
host-tool identities were recorded with no reported changes. Tool binary bytes
were not archived, so their live Windows hashes cannot be independently
recomputed from this package.

## Three distinct result layers

Host: Windows Server 2022 build 20348, image `win22/20261004.326.1`,
MSVC `14.44.35207`, SDK `10.0.26100.0`.

| Layer | Observation | Consequence |
| --- | --- | --- |
| Compilation | One driver, exit 0; /MT /Z7 /Od /W4 /WX; Job total 2 / active 0; stdout 33 B, stderr empty, no PDB | Diagnostic fixture compiled and compiler capture gate passed |
| Raw native capture | 4472 strict consecutive JSONL rows; 646 events and successful continuations; errors 0; capture_complete=true | Bounded raw event/image/string observations are available for diagnosis |
| Outer admission | Native root exit 3; Job total 3 / active 0; exact total 2 required | Original result stays INCONCLUSIVE/NATIVE_EXECUTION_INCOMPLETE before summary parsing |

Neither outer capture timed out or requested termination. Raw native stdout is
923,510 B, SHA-256
`6ecd0bd534c6fd19d962a1f80c209fca27fb69359666337098b1bf08fdb5cf5e`;
stderr is empty. `result.json` is 5,957 B, SHA-256
`4bea0502173dd1fe43170b884a1a1ddb4d8151e9319b1c2b1c209235172c349a`.
It contains no `native_summary`. External raw review does not insert one or
relax the gate. The source deliberately classifies this diagnostic as
INCONCLUSIVE even if its own capture completes.

All 1,616 assertion records and 104 `api` records succeeded. Twenty-three
expected size probes returned error 122, and the expected no-thread-token
probe returned error 1008. Errors 0 therefore does not mean every separately
recorded API-shaped outcome had error 0. All 23 owned-handle closes succeeded.

## Exact child-image preparation and binding

The new parser derived its own AMD64 PE32+ Load Config: six sections, directory
RVA 436336 / file offset 431216 / size 320. `GlobalFlagsSet` lies at file offset
431232 / `0x69480`. The old 009 offset was not reused. GlobalFlagsClear,
checksum and certificate directory were zero; unrelated PE fields remain equal.

| Image | Bytes | SHA-256 |
| --- | ---: | --- |
| Parent | 498176 | `c1ea76588a97c47081f5d3fd8e2dccd093c34a588d045e81f5d83072984a3c76` |
| Fresh child | 498176 | `7793c1788324fce37e3dc0ca6e73fee34ecd5de7d67cbf8ef25b1e70f661b78b` |

There was one four-byte `WriteFile`, changing `00 00 00 00` to `02 00 00 00`.
Only the first byte actually differs; all bytes outside the four-byte field
are identical. Eight copy writes, one patch, one flush, six complete held
reads totaling 2,989,056 payload bytes plus six EOF checks and all 139 API
sequence records were verified. The writer/attributes-anchor/read-pin handoff
retained object continuity. Numeric handle 796 was reused after the writer
closed, not held as the same handle generation.

The two additional final EXE inventory passes have bounded runner reads and
complete hash records. They do not have additional per-Win32 API transcripts.
The fresh-copy shared-write interval is not proof of exclusive producer
provenance; these are diagnostic identity/content checks only.

The recorded volume FileIdInfo serial is `1791501536551043028`. Parent file ID
is `94040000000001000000000000000000`; child file ID is
`96040000000001000000000000000000`, in stored byte order. Python's held child
and native identities at sequences 24, 551 and 4457 match in volume, 128-bit
file ID, size and the required single-link/non-reparse/non-directory/non-delete
properties. The actual CREATE hFile was closed at 559; native's separate pin
remained through owned Job close and final identity readback, closing at 4466.
None of the pins was an added inherited child handle.

## Actual C token, events and explicit failure

Actual S was PID 1876; actual C was PID 6780, initial TID 1764. The original
PROCESS_INFORMATION process handle was 336, thread handle 332. C was created
suspended with flags 134742022, the original two inherited anonymous channels
(handles 252 and 264), and atomic Job-list assignment. No IPC command was sent.

Fresh restricting SID R was `S-1-5-21-0-2363348-1876-1048577`. S and C shared
authentication ID 575655; their ProcUnique values were `{162,2363183}` and
`{163,2363395}` respectively. At sequences 187/243/273/312/427/509, the default
DACLs preserve the original three ACEs in their original bytes/order, append
only the selected non-inheritable R/GA ACE, match expected/immediate/actual C,
and leave S unchanged. Sizes are 80 B / 3 ACEs before, 116 B / 4 ACEs after.

Actual suspended C process/thread DACLs at 524 and 536 match: protected 136 B
ACL with OWNER RIGHTS READ_CONTROL and the H ProcUnique conditional full
access grant. This verifies the descriptors reached those objects. It does not
identify a missing SYSTEM/desktop/other right as the initialization cause.

The event census is one CREATE_PROCESS, nine LOAD_DLL, 628 OUTPUT_DEBUG_STRING,
three CREATE_THREAD, one EXCEPTION, three EXIT_THREAD and one EXIT_PROCESS.
Each of the 646 received events identifies actual C and receives one successful
continuation. Main Wait calls were 646, cleanup Wait calls zero. All ten supplied
image hFiles were closed before their respective continuation. The observed
DLL paths were ntdll, kernel32, KernelBase, apphelp, advapi32, msvcrt, sechost,
bcrypt and rpcrt4. Presence or load order alone is not causal evidence.

All 628 string prefixes were fully read: 79,438 reported/requested/read bytes,
maximum 365 bytes per event. They are ANSI/ASCII with a single terminal NUL,
no internal NUL and no truncation. Text PID/TID agrees with the event. Levels
are 533 INFO, 43 ENTER, 48 RETURN and four ERROR; no WARNING. The raw low-16
byte-count contract still cannot prove an unknown original logical length.

| Raw sequence / event | Observation |
| --- | --- |
| 3133 / 428 | bcrypt LOAD_DLL at base `0x7ffa4a8f0000`; hFile 264; path at 3134; close at 3136, continuation at 3137 |
| 4396 / 638 | PID 6780 / TID 1764 loader line calls init routine `00007FFA4A8FA590` for `C:\Windows\System32\bcrypt.dll` |
| 4402 / 639 | Same PID/TID loader line explicitly says that init routine failed during `DLL_PROCESS_ATTACH` |
| 4408 / 640 | Static-import initialization fails with `0xc0000142` |
| 4414 / 641; 4420 / 642 | Process-initialization failure propagates the same status |
| 4441 / 646 | Actual C EXIT_PROCESS reports `0xc0000142`; continuation succeeds at 4442 |
| 4444 | Cleanup phase entered because actual exit was continued, without a child command |
| 4446 / 4447 / 4449 | Original C process handle signals, queried exit is `0xc0000142`, and event/query match |
| 4448 | Creation/exit FILETIME strings `134361865856600345` / `134361865857659950` |
| 4450 / 4451 | Owned Job total 2 / active 1 / terminated 0, followed by kill-on-close Job release |
| 4457 / 4466 | Final same-child image identity readback, then native pin release |
| 4472 | Raw summary: capture complete, startup absent, bound_hit=false, all authorities false |

The explicit 156-byte ERROR line at 4402 contains:

```text
1a7c:06e4 @ 01286781 - LdrpInitializeNode - ERROR: Init routine 00007FFA4A8FA590 for DLL "C:\Windows\System32\bcrypt.dll" failed during DLL_PROCESS_ATTACH
```

The archived raw bytes include its newline and terminal NUL. The init address
minus the observed base is 42384 / `0xA590`; that is only an arithmetic locator.
010 did not capture bcrypt's file bytes/hash, FileIdInfo or PE entrypoint.
Do not reuse that number as a verified entrypoint or next-run code offset.

The sole exception at 4383 is the expected first-chance initial breakpoint
`0x80000003`; the single VirtualQueryEx at 4384 associates it with ntdll MEM_IMAGE,
RVA 869700. It is not the failed bcrypt initializer's exception location.
Earlier loader lookup/manifest records do not establish a failing internal API.

All 647 successful pipe peeks reported zero bytes. There was no report ReadFile,
startup frame, first command, IPC case, named test pipe or fixture data leaf.
The additional process counted by the Jobs has no established identity. Inner
total 2 / active 1 after C's exit does not prove whole-Job natural completion;
later outer active 0 also does not exclude inner kill-on-close effects.

## Closed conclusion and next handoff

010 adds an explicit bcrypt DLL_PROCESS_ATTACH failure association that 009
did not provide. It does not retroactively identify 009's failed module or
prove the same root cause across differently instrumented runs. It does not
identify an internal failed API, access check or object. The original generic
status, startup absence and outer admission failure are preserved.

The next [011 handoff](native_startup_handoff_011.md) is a proposed actual-DLL
identity/byte collector followed by bounded offline analysis. Its first
unresolved task is a combined byte/row/operation/deadline proof. The candidate
8 MiB hex transport does not fit 010's conservative envelope; no 011 source,
registration or execution exists. The result must come from a future actual-C
event handle, not a pathname copy presented as 010's original file.

After 010: ten prerequisite workflows, nine compiler drivers, compiler Job
TotalProcesses sum 21, six native fixture invocations and zero ordinary MQB.
These counts do not prove every counted helper succeeded. A1/A2/A4, an ordinary
terminal-LINK producer receipt and positive same-session consumer handoff remain
pending; all A1-A5 and deletion authority fields remain false. The 005 old
FILE_WRITE_ATTRIBUTES counterexample remains disqualifying, and 006-010 have
no IPC result.

Main remains `87f86770cfc7e1941603074ace914e0c7faf10d3`. PR250 remains an open
draft/unmerged/HOLD at `a26b103685589ab8a5b4df60d1b760ec445efdf5`; PR248 remains
an open draft/unmerged at `e30f45ee5989d1a5e94a16a78ecb1d4f75a9443e`. Consumer/cost
DEFER, the 21,000-byte cost preparation, disabled M1b/service jobs, product
cpp/VERSION and the other workflows remain unchanged. The work branch and
isolated worktree are retained. This closeout changes documentation only and
does not select another native run.
