# Next handoff 010: child-image loader snaps

Status: `NOT_IMPLEMENTED_NOT_REGISTERED_NOT_EXECUTED`.
This is a source-design handoff after [closed diagnostic 009](native_startup_result_009.md),
not an execution plan with an allocated native budget. No new source, binary,
workflow activation or run was created for 010. All A1-A5/deletion authorities
remain false; the replacement producer owner is unqualified.

## Specific question to resolve

009 preserved the full observed debug-event chain but saw only the initial
breakpoint and actual C exit 0xC0000142. There was no startup frame or debug
string. The next useful observation is a loader-origin record that explicitly
associates an initializer or initialization stage with a failed result.
Another plain module-event list would leave the same causal gap.

## Proposed narrow mechanism

After one newly reviewed fixture compilation, retain the parent executable
unchanged. Create one fresh byte-identical child EXE copy beneath the new
attempt's build directory. Parse that new PE and change only its existing
Load Config GlobalFlagsSet DWORD from the required zero baseline to 0x2.
Fully read it back, prove every byte outside that four-byte field is unchanged,
and bind the exact parent and final child identities before launch.

Launch that child once using the existing restricted token, explicit
process/thread DACLs, additive token default DACL, atomic owned-Job binding,
two inherited anonymous channels and original startup protocol. Capture
bounded raw debug events and loader strings without filtering away the
potential initialization failure. Keep the original parent image unmodified,
so only the diagnostic child receives the new image flag.

Microsoft's [PE Load Configuration Layout](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#load-configuration-layout)
defines the four-byte GlobalFlagsSet at relative offset 16 and says the loader
sets these flags for this process at startup. The
[loader-snaps documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/show-loader-snaps)
identifies 0x2 as FLG_SHOW_LDR_SNAPS. The
[64-bit structure definition](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_load_config_directory64)
also contains the existing security and loader fields, all of which must stay
intact. These contracts support the proposal; they do not prove this runner
will emit a particular failure message.

## Read-only feasibility evidence from 009

Root and the independent design audit only read the preserved 009 EXE:

| Field | Observed value |
| --- | --- |
| Size / SHA-256 | 482,816 / ae7b1935048d5003a5fdcef1073c466969e1a9a3e8cf8139ccd23af64b89ad9d |
| Format | AMD64, PE32+; six sections |
| Load Config RVA / directory size | 0x66bd0 / 320 bytes |
| Existing structure size | 320 bytes |
| Unique file-backed section / offset | .rdata / 0x65dd0 |
| GlobalFlagsClear / GlobalFlagsSet | Both zero |
| GlobalFlagsSet field file offset | 0x65de0 |
| Checksum / certificate directory | Both zero / absent |

The parsed DOS, COFF and optional headers, directory counts, section mapping,
file/image bounds and complete flag fields are recorded in
`next-diagnostic-proposal-010.json`: 22,627 bytes, SHA-256
`eeedac0977545438a901e79f63504a57602f32a18be16af30336bfa15d0af608`.
That proposal was exclusively created, fsynced and independently read back.
No hypothetical patched binary was written. The offsets above describe only
009; the new source must parse its own newly compiled image and reject any
missing, ambiguous, truncated or unexpected structure.

## Exact decisions still required before implementation and activation

1. Select the source slice and exact fresh parent/child paths. Retain one
   compiler driver, one actual child launch and the unchanged process gates;
   no helper executable is proposed.
2. Define the exclusive child-copy creation, physical file identity/link/reparse
   checks, bounded full reads, exact four-byte write, checked flush/close and
   complete readback. Reject nonzero GlobalFlagsClear or GlobalFlagsSet under
   this proposal. Preserve size, checksum, certificates, CFG, security cookie
   and every unrelated byte; never patch the archived 009 executable.
3. Select a hard maximum for the extra child image and account for its one
   additional build file. The measured 009 size is not a future budget.
4. Fix event, reserved-cleanup, per-string and total-string limits appropriate
   for loader snaps. The prior 128 events / 16 KiB string total have not been
   shown sufficient. Reconcile the selected limits with JSONL row/byte caps
   before execution; never grow the limits automatically in a live run.
5. Preserve the current absolute main/cleanup/watchdog model and original
   outer capture limits unless a concrete change is reviewed and registered.
   Exhaustion, truncation or missing evidence must prevent a complete-capture
   claim. Every acquired event still receives one bounded continuation attempt.
6. Bind the new compiled parent hash, copy-before-patch equality, exact field
   offset, final child hash and actual CREATE_PROCESS image observation. Include
   every changed/included source, runner, helper, workflow and plan in the new
   independent source review. Register the exact new commit, bounds and one-shot
   run identity before activation.

These are unresolved implementation decisions, not permission requests.
The existing work authorization does not substitute for concrete source and
reviewable evidence. This proposal makes no native-run allocation by itself.

## How to interpret the future observation

An explicit, untruncated loader record associating a DLL or initializer with
failure could identify a narrower module or stage. A specific stage/call/status
record could also narrow the investigation without naming a final module.
Identifying a failed initializer would still not identify its exact internal
failed Windows API without additional evidence.

No trace, only a last-load or last-call line, the same generic exit status,
an exhausted bound or an invalid source/PE/identity/Job/transport check remains
inconclusive. If startup advances only under loader tracing, report the
diagnostic perturbation; do not replace 009 with that different execution.

Do not infer a missing SYSTEM grant, desktop right, restricting SID defect or
other ACL cause from module order. No ACL expansion, registry setting, global
debugger setting, symbol download, installation, process-memory write, thread
context rewrite or injection is proposed. Zero startup/IPC cases cannot be
converted into successful IPC refusals.

After the startup blocker is understood, the original actual IPC delegation
and retained-capability lifetime question still needs its separately admitted
execution. Neither loader tracing nor successful fixture startup qualifies
ordinary cl/LINK writers, service completion, A1/A2/A4 or the live consumer.
