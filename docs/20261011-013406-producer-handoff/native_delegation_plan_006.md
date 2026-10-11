# Prerequisite 006: actual WRITE_RESTRICTED read-only IPC delegation

Status: `SOURCE_READY_PENDING_INDEPENDENT_REVIEW_AND_REGISTRATION`.
Parent: `8d4d25fb2866b1f267ce4a948d9e9273cb62853c`.
This is one new necessary-boundary falsification, not a product implementation
or a rerun of 005. No 006 compiler/native invocation existed at preparation.

## Why this is the next concrete test

005 disproved the claim that a final data-read/share-READ handle plus a pending
RWH excludes all older attribute writers. A preverified FILE_WRITE_ATTRIBUTES
handle changed LastWriteTime under that gate. Its raw evidence and failure stay
unchanged. The replacement design must prevent or account for those capabilities
before their creation; later ACL revocation cannot supply that missing lifetime.

WRITE_RESTRICTED with a fresh restricting SID is a concrete attempt to preserve
ordinary input/tool reads while checking a separate restricting SID for writes.
Its specific unresolved assumption is whether a read-only service connection
can delegate an admitted token and leave an unaccounted write handle. This
probe fixes the transport, access masks, DACL and SQOS instead of adopting an
unqualified token label.

Primary Chromium source at `50da56df411ff9bffaf219f1bcf5e83d6d91e028`
reads actual token security attributes and formats multi-value UInt64 conditions
as `(TSA://ProcUnique == {u0,u1})`. Its restricted-token tests perform actual
OpenProcess/OpenThread access checks under copied impersonation tokens. Those
source assertions distinguish filtering/impersonation from new process birth;
they are not this machine's runtime result and do not test named-pipe propagation.
No fabricated TSA attribute, token theft or code injection is used here.

The read-only silo investigation did not select a replacement. A bare application
silo and its private Object Manager root are separate operations; ordinary-user
creation of the former does not establish the required service namespace. No
privileged silo initializer, account change, driver, VM or tool copy is added.

## Actual owners, birth and permission controls

S is the fixture server process. C is its only child, created suspended through
CreateProcessAsUser with a restricted version of S's actual primary token:
`DISABLE_MAX_PRIVILEGE | WRITE_RESTRICTED`, one fresh restricting SID R, no
added normal SID and no disabled-SID list. AllocateLocallyUniqueId supplies the
fresh discriminator; reject collision with S's actual user/groups/restrictions.
The restricting SID changes the additional access check; it creates no identity
claim. The input and actual C token are separately queried and recorded.

C enters an owned unnamed nested Job atomically at CreateProcessAsUser via
PROC_THREAD_ATTRIBUTE_JOB_LIST, before its first ResumeThread. Its active
process limit is one and KILL_ON_JOB_CLOSE applies to abnormal cleanup. Verify C
is in that Job and S is not. The outer Python capture Job contains both solely
to bound fixture cleanup; it is never called a producer completion owner.
Process/thread object DACLs are protected, with the actual S condition and
OWNER RIGHTS=READ_CONTROL. The two startup attributes are JOB_LIST and
HANDLE_LIST; exactly two anonymous protocol handles are inherited.
No token, leaf, server-endpoint, Job or process handle enters the child.

Read C's actual primary ProcUnique after process birth while it is still
suspended. Verify it differs from S and the pre-birth filtered token, C and S
have the same authenticated user, and C has exactly R as its restricting SID.
Compare actual TokenStatistics.AuthenticationId as well as user SID, and record
the token/modified IDs. Any enabled C privilege must be SeChangeNotifyPrivilege.
Close both pre-birth and actual-child token handles before native resume.

Create one fresh zero-data file with its complete protected DACL at birth:

- exact user: FILE_GENERIC_READ, allowing independent readback and artifact capture;
- actual S ProcUnique condition: WRITE_DAC only, for the explicit later revocation;
- actual C ProcUnique condition: FILE_WRITE_ATTRIBUTES;
- R: FILE_WRITE_ATTRIBUTES, to satisfy C's separate write restriction;
- OWNER RIGHTS: READ_CONTROL, suppressing implicit owner WRITE_DAC.

Record the complete effective DACL and exact physical file identity. Close the
creation handle before any fresh-open control. S's fresh WA open must be denied.
C's fresh WA open must succeed, write a fixed exact FILETIME, and close. Verify
that time independently through a new read-only handle to the same physical leaf.

Before C resumes and before any IPC test, S disables all privileges in its own
ephemeral fixture primary token and verifies that no privilege remains enabled.
Its actual ProcUnique must remain unchanged. This is no account/machine change
or privilege grant. It prevents a successful same-user impersonation result
from depending on the hosted runner's enabled SeImpersonatePrivilege.

## Four fixed routes, with independent real-open baselines

All named pipes have one instance, byte mode, blocking I/O, reject remote clients,
64-byte buffers, and FILE_FLAG_FIRST_PIPE_INSTANCE. Each has a new unrelated
baseline object, opened by S using exactly the specified client access and SQOS;
the baseline connection and instance close before the actual object is created.
Never reuse its security context as C's context.

| Case | Server mode | C desired access | C SQOS flags | Pipe DACL grants R | Purpose |
| --- | --- | --- | --- | --- | --- |
| 0 | duplex | GENERIC_WRITE | SQOS_PRESENT + IMPERSONATION, static | no | ordinary write-connection restriction control |
| 1 | outbound | GENERIC_READ | SQOS_PRESENT + IMPERSONATION, static | no | read-only connection with captured static security context |
| 2 | outbound | GENERIC_READ | SQOS_PRESENT + IMPERSONATION + CONTEXT_TRACKING | no | distinct dynamic-context read-only control |
| 3 | duplex | GENERIC_WRITE | SQOS_PRESENT + IMPERSONATION, static | yes | explicitly admitted positive delegation/control route |

The ordinary pipe DACL is protected and grants the exact user FILE_GENERIC_READ
and FILE_GENERIC_WRITE, plus OWNER RIGHTS=READ_CONTROL. Only case 3 adds that
same combined file mask for R. The actual DACL is roundtrip-compared in every
case. A GENERIC_WRITE attempt on an outbound-only pipe is deliberately absent;
that would confound write restriction with direction mismatch.

C reports its actual CreateFile result through the separately admitted protocol
channel, then keeps a successful connection alive. S acknowledges that already
connected client with ConnectNamedPipe. Microsoft documents ERROR_PIPE_CONNECTED
for this ordering. Each successful route exchanges exactly one byte: S writes
in cases 1/2, and S reads C's message in cases 0/3. The read-only routes never
perform a C-to-S payload write or a server ReadFile on the tested endpoint.
Neither side changes pipe mode after opening. No reconnect or retry occurs.

Each case starts without an existing thread token. S calls
ImpersonateNamedPipeClient on that exact endpoint once. A refusal records its
specific code and does not fall through to privileged server-context work.
On success, S obtains only TOKEN_QUERY on the actual thread token and records
its type, impersonation level, user, exact restricting SID and complete
ProcUnique type/flags/UInt64 values, and compare the same actual AuthenticationId.
Values and flags are compared separately;
the conditional expression binds values, not an assumed flag equality.

S then attempts a fresh FILE_WRITE_ATTRIBUTES open under that actual IPC token.
A successful open must bind the actual C condition and usable impersonation
level. Close the query-only token, revert, and independently verify no thread
token remains. A failed normal RevertToSelf directly stops the fixture; it is
not retried by the separate early-exception guard. Retain a granted WA handle
across connection teardown. A refusal
of the dynamic outbound route cannot close the distinct static route, nor can
these four cases qualify all transports or actual cl/link.

## Completion, revocation and retained-capability test

After all cases C closes its handles and exits naturally. Wait on the retained
actual process handle, require exit 0, and query the exact child Job: total 1,
active 0, no limit-caused termination. No timeout, forced Job cleanup or census
substitutes for natural completion. The server remains outside that child Job.

Under S's original primary token, replace the exact leaf DACL, removing both
C's conditional grant and R's grant. Read back the complete protected DACL,
same file ID and unchanged baseline time. S's fresh WA open must still fail.
Close the DACL-management handle before attempting retained-writer operations.

For each WA acquired through IPC, S now calls SetFileTime on that same retained
handle, under its own nonimpersonating primary token, after C exit and admission
revocation. Each case gets a distinct exact time. Independent read-only opens
must verify the exact requested time on the same physical leaf. Close every
retained handle after its observation. There is no final RWH rerun here: 005
already established that primitive's missing guarantee.

## Classification fixed before execution

- `WRITE_RESTRICTED_IPC_DISQUALIFIED`, native exit 2: the full positive control
  and natural completion/revocation controls pass, and at least one pipe whose
  DACL does not grant R permits the complete retained-writer mutation chain.
- `FIXED_ROUTES_REFUSED_ONLY`, native exit 0: all four cases complete and the
  R-admitted positive control passes; none of the three non-R routes produces
  that chain, and case 0's ordinary write-connection control is actually denied.
  If that connection opens but its write chain is incomplete, classify the
  failed negative control as INCONCLUSIVE. This is a finite-route result, never
  positive A2 qualification.
- `INCONCLUSIVE`, native exit 3: malformed source/protocol, missing identity,
  unexpected API error, failing positive control, mismatched DACL/readback,
  incomplete cleanup, or any other unfulfilled prerequisite.
- Watchdog exit 124 or other unplanned termination is incomplete, never a pass.

The summary must be the final single JSONL summary. It has integer errors,
cases_completed, client_job_total/active; booleans positive_control,
client_natural_exit, acl_revoked and write_connection_refused; and all A1/A2/A4
authority values false.
Only the first two labels require errors=0, four cases, complete positive
control, natural C exit, Job1/0 and completed revocation. Python rejects duplicate
JSON keys, nonfinite numbers, wrong types, summary multiplicity/order and exit
mismatches. It also checks a disqualified summary against a non-R case's raw
final mutation record. Retained writers are explicitly closed before summary,
including failure paths, so close errors and final record order remain complete.
A negative native exit remains a failing workflow with preserved
evidence, not a build failure or an automatic retry request.

## One new bounded allocation

Independent review must approve the exact changed source identities, immutable
tree/commit, claims and budgets before registration and branch activation.
Use the existing workflow ID `380958963`, run number **6**, attempt **1**, push
on the same feature branch at that exact commit. No dispatch, old-attempt rerun,
matrix, replacement sample or automatic retry is allocated.

- Exactly at most one fixture compiler-driver call, accepted compiler Job totals
  {1,2}, no root-live, timeout, error or requested termination; unchanged exact
  005 tool selection, recipe, telemetry-image read pin and three open controls.
- At most one fixture invocation, only after compiler, pin release, exact source
  and PDB-absence gates pass. It has S and at most one C, no grandchildren, one
  watchdog thread, one nested Job and two strictly inherited protocol channels.
- Four cases, at most eight explicit named pipe instances including baselines,
  plus two anonymous protocol channels. At most four payload bytes on tested
  connections and 1 KiB of fixed binary protocol; no retry loops or services.
- One fresh directory and one zero-data leaf. The outer content cap is 4 KiB;
  it excludes build binaries/debug objects and captured evidence streams.
  Existing root/leaf/pipe names refuse. No DELETE/disposition or recursive cleanup.
- Compiler root wait90s, native outer wait40s, native watchdog30s, child final
  wait5s, watchdog join5s, each existing capture cleanup10s, workflow5min.
  OS calls and evidence fsync do not have a hard-real-time guarantee.
- Token-information queries: size then one data call, each at most64KiB;
  at most128 SID/privilege entries,64 attributes,8 UInt64 ProcUnique values.
  All 64-bit identities, FILETIMEs and handle values are decimal strings.
- Before this allocation: five prerequisite runs, four compiler driver calls,
  eleven observed compiler Job processes, one native invocation. 001-005 remain
  closed. New MQB, ordinary producer, cost/ETW and service-study calls remain0.

Keep exact raw streams, source/tool bytes, partial records, original failure and
every unexpected process. Canonical transfer uses exclusive complete writes,
file/directory fsync and an independent full archive/member readback. The Linux
pre-activation checks only cover source/runner structure; they are not Windows
qualification. Do not change machine accounts, installed binaries, registry or
shared services to make the fixture pass.

## Product and frozen boundaries

A1/A2/A4 product delivery and P01-P12 remain pending; this test issues no receipt,
uses no ordinary terminal LINK owner and qualifies no compiler mode. A3/A5 and
actual deletion stay disabled. PR250 preparation stays byte-identical with its
zero/null budgets and false authorities; PR250 HOLD, ordinary consumer/cost
DEFER, M1b guarded-false jobs and the original failed studies stay unchanged.

Primary sources:

- [Microsoft CreateRestrictedToken](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken)
- [Microsoft CreateProcessAsUserW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw)
- [Microsoft CreateFileW, including SQOS](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
- [Microsoft ConnectNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe)
- [Microsoft ImpersonateNamedPipeClient](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient)
- [Chromium actual attribute reader and formatter](https://github.com/chromium/chromium/blob/50da56df411ff9bffaf219f1bcf5e83d6d91e028/base/win/access_token.cc)
- [Chromium restricted-token access tests](https://github.com/chromium/chromium/blob/50da56df411ff9bffaf219f1bcf5e83d6d91e028/sandbox/win/src/restricted_token_unittest.cc)
- [Project Zero original silo investigation](https://projectzero.google/2021/04/who-contains-containers.html)
