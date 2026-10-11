# Current-session producer handoff: implementation entry gate

Status: `BCRYPT_SNAPSHOT_011_CLOSED_PE_REFUSED_OFFLINE_HANDOFF_012`. Attempt 005 ran
the necessary native primitive checks and demonstrated an attribute-writer
counterexample. See [the closed result and next gate](native_probe_result_005.md).
Product implementation, full native qualification, and C01 positive handoff
are not complete.

[Prerequisite 006 closed before IPC](native_delegation_result_006.md): its
compiler passed, the first child-report ReadFile failed with error 109, and
the outer Job total was 3 instead of 2. The actual child exit was not queried.
[007 startup diagnostics completed](native_delegation_result_007.md) and
observed actual C exit `0xC0000142 / STATUS_DLL_INIT_FAILED` before owned
cleanup. [008 applied the single default-DACL grant](native_delegation_result_008.md)
and verified it on actual C while S stayed unchanged, but C again exited
DLL_INIT_FAILED before startup. No IPC case was observed. Those 008 observations
did not identify the specific DLL or failed access. [009 startup event diagnosis is closed](native_startup_result_009.md):
all 15 observed debug events were continued successfully, but C still exited
`0xC0000142` before any startup frame or debug string. The sole exception was
the initial breakpoint. Raw diagnostic capture was complete; outer Job total
3 still fails the exact-total-2 gate before the runner parses that summary.
[010 is now closed](native_startup_result_010.md): its reviewed fresh child
copy changed only the existing Load Config GlobalFlagsSet DWORD from 0 to 2.
All image bindings and 646 event continuations were verified. Actual C's
loader explicitly reported bcrypt.dll initialization failure during
DLL_PROCESS_ATTACH, followed by actual exit 0xC0000142. Its internal failed API
or access remains unidentified. Outer Job total 3 still fails the unchanged
exact-total-2 gate; no native summary is admitted into result.json.
[011 is closed](native_startup_result_011.md): its one registered run 11 /
attempt 1 acquired the exact file from its actual C's bcrypt LOAD_DLL event.
Both independent raw reviews admit 170,984 bytes, two complete reads with EOF
and five matching identities; the original outer Job total 3 still fails the
required 2, leaving INCONCLUSIVE with no native_summary in result.json. The
frozen pure PE parser then refused the image with UNWIND_OPCODE_INVALID. The
offline phase made no objdump call, loaded or executed no DLL, and performed
no parser retry or alternate-image substitution. The [012 handoff](native_startup_handoff_012.md) reuses these exact
bytes for a bounded offline refusal-context and parser-compatibility diagnosis;
no new Windows/native run is needed. All 119 final source unit tests passed,
but that does not turn the real-input PE refusal into an accepted index.
The historical 010/011 proposals and execution plans retain their original
pre-execution status and are superseded by their closed results and issue
registrations/closures. All token/ACL restrictions and qualification limits
remain. Completed 001-011 records are not rerun or reclassified.

## Intended outcome

Continue PR256 and issue #198 comment 6095001403 on actual main
`87f86770cfc7e1941603074ace914e0c7faf10d3`, tree
`7c8a684a98ab7e714ca563e46440ec33aee487b3`.
The accepted outcome remains an actual ordinary terminal-LINK producer-owned
current EXE receipt, participating writer completion, and continuously held
object handoff in one live session. A3, A5 and deletion remain disabled.

## Verified current state

PR256 is merged. The initial current-source read found no later main commit and
no new PR that implements this handoff. PR250 and PR248 remain separate drafts.
A clean sibling worktree was prepared from the exact actual main object without
moving prior worktrees or making any remote Git mutation.

Three separate initial source audits examined the actual production path, Windows
completion/physical boundaries, and verification scope. No product, project
test, analyzer, compiler, MSVC, MQB or workload was executed in those initial
audits. Subsequent attempts 001-011 are distinct: eleven prerequisite workflow
runs, ten compiler-driver invocations, compiler Job TotalProcesses summing to
twenty-three, and seven native fixture invocations. The 006-011 native outer
Job totals of 3 are incomplete results with no additional process identity.
No ordinary MQB producer ran.

## Concrete implementation blocker

No existing owner enforces that only the admitted producer can modify the
selected EXE while native production is allowed to write it. Job membership
completion also excludes service/WMI/non-descendant work. The documented
contract requires a demonstrated participating or physically isolated boundary;
the source contains neither such a boundary nor qualification that closes it.

A same-object pin can protect identity while still sharing write access. An
unaccounted writer can use that interval, modify the same file, and close before
the final restrictive pin. The final pin, file ID and content hash then bind
the resulting bytes without identifying their exclusive producer. This is a
source/API-based counterexample, not a newly executed native experiment.

## Why the entry gate matters

The existing source already acknowledges these limits:

- `MsvcWriteInventory.cpp::native_gap` retains native, temporary and shared-service gaps.
- `WindowsWriteDomain` reserves a cooperative marker; after writes begin its
  deliberate lack of completion/recovery does not establish writer completion.
- `WindowsProcessRunner` establishes its Job/member boundary only for a
  stoppable request; ordinary compiler/LINK requests currently use the default.
- `StorageFileObservation` releases its leaf and ancestors on return.
  `WindowsWriteInventory` retains exact parents, not the full output lifetime.
- `PROCESS_LIFETIME.md` retains the unresolved M1b dependency for managed MSVC
  cancellation or write-lease transfer.

## Acceptance to unlock implementation

The previously selected conditional-ACE / retained-object candidate depended
on excluding old metadata writers at its final seal. Attempt 005 disproved that
dependency: a preverified FILE_WRITE_ATTRIBUTES handle completed SetFileTime
while the final data-read/share-READ handle and pending RWH remained held.
The original candidate must not issue a receipt. Its conditional ACL and actual
service delegation were not exercised by this necessary-primitive fixture.

The replacement gate is concrete control of write capabilities from their
creation through their complete lifetime, including legitimate IPC delegation.
WRITE_RESTRICTED plus a unique restricting SID is selected only for the bounded
006 read-only IPC falsification, not as a qualified producer owner. The IPC
cases remain unobserved through the separately scoped 011 diagnosis. Write
restrictions alone do not yet close NULL-DACL endpoints, read-only IPC that
can delegate identity, or the relevant ALPC access mapping. Do not rerun
the disproved RWH dependency or promote a token type name to completion.

A proposed native/physical owner must demonstrate admission before effects,
exclusive or fully participating writers for the exact selected boundary,
accountable native completion, and continuous retention through handoff.
An API label, process success, empty unresolved vector, marker reset, PID
snapshot, portable selection or Z7 flag cannot establish this authority.

See implementation_plan.md for exact code owners, design alternatives,
qualification obligations and preserved budget boundaries.
