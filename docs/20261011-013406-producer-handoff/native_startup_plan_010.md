# One-shot child-image loader-snaps diagnostic 010

Status: `IMPLEMENTATION_AND_REVIEW_IN_PROGRESS_NOT_REGISTERED_NOT_EXECUTED`.
This implements the [010 handoff](native_startup_handoff_010.md) under the
user's continuation instruction. Exact source review and #198 registration
must precede the only activating branch update. Diagnostic results cannot
qualify a replacement producer owner; A1-A5 and deletion remain false.

## Acceptance contract and chosen slice

The question is whether bounded loader-origin output explicitly associates an
initializer or startup stage with the actual restricted child's failed result.
009 observed the initial breakpoint and actual 0xC0000142 exit without strings.
Preserve all 001-009 source and evidence. Do not infer a cause from module order.

Create `producer_loader_snaps_probe.cpp` as an independent 009-derived TU that
includes the frozen 008 client. Preserve the token, fresh restricting SID,
default/kernel DACLs, atomic child Job, two inherited anonymous channels,
unchanged client protocol and owned cleanup. Only the parent selects a fresh
child image, binds it by handle, and captures the expanded bounded diagnostic.
No client command, IPC qualification case or fixture data leaf is admitted.

New Python modules are `producer_loader_snaps_pe.py` (pure byte validation)
and `producer_loader_snaps_image.py` (checked Windows file operations). The
new runner is `run_producer_loader_snaps_probe.py`; old runners/common Job
helpers are frozen. The pure PE helper has meaningful synthetic malformed-PE
and single-DWORD-contract tests before implementation. No additional native
fixture run is allocated for tests. Read-only compatibility inspection of the
archived 009 EXE is permitted, without writing a modified copy or executing it.

One new compiler driver creates `build/producer_loader_snaps_probe.exe` and
its object. One fresh `build/producer_loader_snaps_child.exe` is created from
that parent. Native argv is exactly:

```text
parent.exe --parent fixture_root child_image_path volume_decimal file_id_32_lower_hex size_decimal
```

Parent argc is 7. The frozen client remains argc 6 with `--client`. Volume
serial is a decimal string and FileIdInfo.FileId is the 16 bytes in storage
order rendered as 32 lowercase hex digits; numeric JSON precision is not used
for either 64-bit identity or pointers. Image size is also passed as decimal.

## PE and physical-file contract

Parse the newly compiled parent each time: complete MZ/PE/COFF/AMD64/PE32+
headers, declared optional-header/directory/section bounds, checked RVA/file
ranges, no overlapping or ambiguous section mapping, and a complete existing
Load Config uniquely mapped into read-only initialized `.rdata`. Require
GlobalFlagsClear=0, GlobalFlagsSet=0, checksum=0 and absent certificate table.
Reject missing, truncated, virtual-only, raw-padding or ambiguous flag fields.
Never use the observed 009 offsets as the new patch offset.
The four patch bytes must not also lie in another declared data-directory
range. This is a field-alias refusal, not a blanket ban on directory nesting.

The parent is opened non-inheritable with GENERIC_READ / FILE_SHARE_READ /
OPEN_EXISTING / FILE_FLAG_OPEN_REPARSE_POINT. Check actual FileIdInfo,
FileStandardInfo and FileAttributeTagInfo: bounded positive size, link count 1,
not a directory, not delete pending, and no reparse point/tag. Read every byte
and verify EOF through this handle. Keep it until native capture and readback
finish. No parent write handle is requested.

Create the child exactly once with CREATE_NEW and GENERIC_READ|GENERIC_WRITE,
FILE_SHARE_READ|FILE_SHARE_WRITE, no delete sharing, non-inheritable, and
FILE_FLAG_OPEN_REPARSE_POINT. This is exclusive name creation, not a claim of
exclusive writer admission. Record its distinct physical identity, copy the
bounded parent bytes once, completely read back equality, then seek to the
parsed field and issue one checked four-byte WriteFile of `02 00 00 00`.
FlushFileBuffers and complete readback must prove that every byte outside the
four-byte field is unchanged, with identical length and preserved PE fields.

Before closing the writer, ReOpenFile that exact object for FILE_READ_ATTRIBUTES
with FILE_SHARE_READ|FILE_SHARE_WRITE and no delete sharing. Verify the same
identity, close the writer with checked CloseHandle, then ReOpenFile the
attributes anchor for GENERIC_READ / FILE_SHARE_READ. Check the same identity
and complete content again before releasing the attributes anchor. ReOpenFile
flags are FILE_FLAG_OPEN_REPARSE_POINT only, with no FILE_ATTRIBUTE flags.
The final parent/child read handles remain held through native capture and
complete final readback, then are explicitly closed. There is no pathname
retry, overwrite, deletion, executable fallback or flag/budget expansion.

Native S opens its own non-inheritable child read pin with the same access,
share and reparse policy, verifies the supplied FileIdInfo/size and retains it
through event draining and cleanup. The actual CREATE_PROCESS_DEBUG_EVENT
hFile must yield the same identity and properties while the pin is held.
The event's path is descriptive only. Missing hFile, a failed query or mismatch
prevents complete capture. The pin is never in the two-entry HANDLE_LIST.
These diagnostic bindings do not assert A2 or A4 producer authority.

## Fixed operation and admission limits

| Resource | Fixed limit |
| --- | --- |
| Workflow | Existing ID 380958963; next number 10, attempt 1 only |
| Host | GitHub-hosted windows-2022, expected repository/branch/push |
| Compiler drivers / native invocations | 1 / 1; no helper executable |
| Compiler root wait / accepted total Job processes | 90 s / 1 or 2 |
| Native outer wait / cleanup | 40 s / 10 s; exact total Job processes 2 |
| S / actual C | 2 fixture processes, 1 watchdog thread; owned C Job active limit 1 |
| Parent or child image | 8 MiB each; exactly one additional child build file |
| Image copy writes | At most 128 chunks of 65,536 bytes, no short-write retry |
| Image patch writes / flushes | Exactly 1 DWORD write / 1 successful flush before admission |
| Held-image complete reads | 6 passes maximum, each <=8 MiB plus one-byte EOF check |
| Final image inventory reads | 2 additional passes, each <=8 MiB plus one byte to detect excess |
| Debug events | 1,024 total; 960 main, 64 reserved for cleanup |
| WaitForDebugEventEx | 2,048 calls total; 1,792 main, 256 cleanup reserve; <=50 ms/call |
| Main / cleanup / watchdog | 20 s / <=5 s and absolute entry+25 s / 30 s |
| Debug strings | <=4,096 reported bytes per event, <=1 MiB total requested |
| Image path query | <=4,096 wchar buffer per image event; no path reopening |
| Path text emitted | <=1,048,576 wchar units aggregate, including exception matches |
| Native JSONL acceptance | <=32 MiB and <=16,384 rows; strict consecutive JSON |
| Startup protocol / anonymous channels | <=56 bytes / exactly 2 channels |
| Named test pipes / fixture leaves / IPC cases | 0 / 0 / 0 |

JSONL byte/row limits are post-capture admission checks, not a claim of output
file write-side truncation. Native event/wait/string/path counters bound the
operations and emitted variable data. Worst-case emitted paths contribute
<=6 MiB after JSON escaping, raw string hex <=2 MiB; fixed rows, event metadata
and bounded timeout records fit the 32 MiB / 16,384-row acceptance envelope.
No counter resets or automatic limit increases are permitted.

Every received event gets exactly one bounded continuation attempt. Retain raw
fUnicode, the reported low-16-bit byte length (never multiply it for Unicode),
requested/read bytes and raw hex. A budget-truncated prefix must explicitly
prevent capture_complete, while later bounded exit observations may continue.
Reported-prefix completeness and terminal NUL observations do not establish
the unknown original logical string length. Optional observation failures
remain incomplete; structural errors trigger the owned bounded cleanup.

## Review, one-shot registration and closure

Before activation, verify source hashes/byte lengths, frozen-source equality,
meaningful pure-Python red/green results, Win32 ABI/handle/share semantics,
event/row budgets and all failure/cleanup paths. Obtain independent exact-source
review. Pin all new/included modules, test source, old helpers, workflow and
this plan. The workflow hash-binds every executable/imported source before
import and compares checkout HEAD with GITHUB_SHA.

Register the actual immutable commit/tree/parent, complete hashes, fixed
budgets and one-shot identity in #198; read it back fully. Freshly verify branch,
main, nine completed prior runs and unchanged draft PR states before the
expected-head branch update. No rerun is admitted. Retain original ZIP,
complete members, source bindings, all raw streams and independent closure.

The exact-total-2 outer gate remains before native summary admission. If its
actual accounting is 3 again, result.json remains NATIVE_EXECUTION_INCOMPLETE;
separate raw review does not insert or retrofit an admitted native summary.
The diagnostic always returns INCONCLUSIVE even if its raw capture completes.

Only explicit, untruncated loader failure association can narrow the module or
stage. Last-load/last-call order, absent strings, invalid bindings or exhaustion
stay inconclusive. Tracing-induced startup advancement is a perturbation of
009, not its retroactive success. A named failed initializer does not identify
its internal failed API without further evidence. Actual IPC/lifetime and
ordinary producer A1/A2/A4 remain separate pending work.

Frozen scope: main, PR250 HOLD, PR248 draft, consumer/cost DEFER, the 21,000-byte
cost preparation file, disabled M1b/service jobs, product cpp/VERSION and all
001-009 evidence. No ACL grant change, registry/global debugger setting,
symbol download, process-memory write, thread-context rewrite, injection,
ordinary MQB, cost/ETW/M1b run, cleanup or deletion.

Sources: [PE Load Config](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#load-configuration-layout),
[loader snaps](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/show-loader-snaps),
[ReOpenFile](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile),
[FileIdInfo](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info),
[CREATE_PROCESS hFile](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-create_process_debug_info),
[debug-string byte length](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-output_debug_string_info).
