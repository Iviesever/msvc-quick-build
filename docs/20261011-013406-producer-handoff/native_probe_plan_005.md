# Prerequisite 005: temporary read-only pin on the observed telemetry image

Status: `SOURCE_READY_PENDING_INDEPENDENT_REVIEW_AND_REGISTRATION`.
Parent: `db68c06a6af861b8653fe4f69fb1ed6586ea2d93`.
No 005 compiler or native execution at preparation time.

## Evidence selecting this mechanism

004 again returned compiler root exit 0 without timeout, then refused Job
total 3 and root exit with live members. Its new bounded diagnostic opened an
actual member handle in that exact Job: PID 2600, creation FILETIME decimal
`134361607508425601`, image in the selected MSVC 14.44.35207 x64 directory
`vctip.exe`, zero-time wait 258. It then performed the existing owned Job
termination. No native fixture ran. This identifies that 004 snapshot member;
it does not identify every historical member or backfill the unknown images
in 002 and 003. The original raw failure remains intact.

004 ZIP: `producer-seal-004-38106543918-1.zip`, 362085 bytes, SHA-256
`b0433d8ba436f8d76a19a3da0e1fb513691e4c3e9e16fba725bc8cff89eb7c7e`.
All 18 members were independently read back. The initial closure's parsed
FILETIME projection rounded its last digit through a JavaScript Number;
`attempt-004-closure-correction-001.json` (1409 bytes, SHA-256
`6ea49139ca6032397722498adce3cc4bf7468e9b965abb149fb1b31d6e78683d`)
records the exact original decimal above. Raw JSON and ZIP bytes never changed.
Future large integer transport uses exact decimal strings.

No verified Microsoft compiler switch was found for the proposed `/d2:-notip`
or `VCTIP_DISABLE` names. Neither is selected. Microsoft VSCEIP documentation
does not promise that an optional-telemetry opt-out prevents process creation.

## Concrete bounded control

On this new `github-hosted` Windows 2022 runner only, keep the actual selected
`vctip.exe` file open with `GENERIC_READ`, share mode zero, `OPEN_EXISTING`,
`FILE_FLAG_OPEN_REPARSE_POINT`, and noninheritance. Reject a directory, reparse
leaf, multiple links, non-fixed/non-NTFS volume or content larger than 16 MiB.
Read and hash the same held object, requiring an observed zero-byte EOF read;
match its complete bytes to the extra preflight tool identity. The new image's
preflight/postflight reads each cap at 16 MiB plus one over-limit observation
byte and reject over-limit data. Record volume serial and file ID as exact values.

Three bounded controls use `GENERIC_EXECUTE`, share READ|WRITE|DELETE and
`OPEN_EXISTING`, without executing the file. Before the pin, that open must
succeed; during the pin it must fail specifically with sharing violation 32;
after checked pin close it must succeed again. The successful controls and pin
must identify the same physical file. All control handles are checked-closed.
The controls record their operation and close errors separately, including when
both fail. Unexpected access, errors or close failures refuse and remain recorded.

The pin surrounds exactly the existing compiler capture and its owned Job
cleanup. Release it on every path. If release verification fails, refuse even
if the compiler otherwise succeeded. The C++ fixture, Job helper, compiler
arguments, effective environment recipe and original Job gates are unchanged.
The only tool-selection addition is the observed telemetry image's identity.

This is a temporary file-sharing mechanism, not a telemetry opt-out assertion,
binary modification, file/ACL/registry deletion or product writer isolation.
It makes no assertion about earlier mappings, other image paths or external
services. Microsoft documents share-none access conflicts; the execute-open
controls qualify the relevant access on the actual object. Whether cl handles
the denied optional image open and satisfies the original Job boundary remains
the experiment. A new unexpected process or error still refuses, with the 004
diagnostic retained. No process cap or completion condition is relaxed.

## One new allocation

Register exact reviewed tree, immutable commit and review identity before
activation. Existing branch/workflow, push only, new run number **5**, attempt
**1** and that exact commit. No old attempt rerun, dispatch, PR trigger, matrix
or automatic retry. 001–004 remain closed.

- At most one compiler driver; accepted compiler Job totals still {1,2}.
  Any existing root-live error, timeout, nonzero exit or incomplete accounting
  refuses. Post-termination zero never becomes accepted natural completion.
- At most one native fixture only after all compiler, pin-release and expected
  PDB-absence gates pass. Native Job total exactly 1, zero fixture children,
  at most two auxiliary threads. Unchanged original primitive classifications.
- Four additional CreateFile calls at most: one read pin and three execute-open
  controls. Zero added process launches or waits. Same-handle hash reads at most
  16 MiB plus one EOF observation. The extra image's preflight/postflight hashes
  each have a separate 16 MiB plus one-byte cap; all older tool hashes are unchanged.
- Compiler90s, native-wrapper40s, native-watchdog30s, each existing cleanup10s,
  workflow5min. No hard-real-time guarantee for OS calls or evidence fsync.
- At most1MiB fixture content, build/capture artifacts separate. Fresh output
  `producer-seal-005-<run_id>-1`; existing output refuses.
- Before005: four prerequisite workflow runs, three compiler driver calls,
  nine observed compiler Job processes, zero native fixture invocations.
  These are observed acceptance caps, not kernel creation limits. Record every
  actual unexpected member. Allocate one new attempt; further retry0.
- No MQB, ordinary producer, cost, ETW, service study, deletion, disposition,
  old-root takeover, recursive cleanup, installation or machine-setting edits.

Keep full original streams, pin controls/errors, source and tool identities,
and partial records. Canonical artifact transfer requires exclusive complete
writes, file/directory fsync and independent full archive/member readback.

A1/A2/A4/C01 and real ordinary terminal LINK positive handoff remain pending;
conditional-ACL, external-service and full-candidate qualification remain false.
A3/A5/deletion disabled. All frozen cost preparation, old studies, PR250 HOLD,
ordinary consumer/cost DEFER and job-level-disabled M1b research stay unchanged.

Primary contracts: Microsoft CreateFileW (sharing, OPEN_EXISTING, noninheritance,
reparse semantics), GetFileInformationByHandle and ReadFile. The executable
access conflict is checked directly without a CreateProcess control call.
