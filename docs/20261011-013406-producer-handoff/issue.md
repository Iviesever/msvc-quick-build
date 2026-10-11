# Current-session producer handoff: implementation entry gate

Status: `DEFAULT_DACL_HYPOTHESIS_SOURCE_PREPARATION`. Attempt 005 ran
the necessary native primitive checks and demonstrated an attribute-writer
counterexample. See [the closed result and next gate](native_probe_result_005.md).
Product implementation, full native qualification, and C01 positive handoff
are not complete.

[Prerequisite 006 closed before IPC](native_delegation_result_006.md): its
compiler passed, the first child-report ReadFile failed with error 109, and
the outer Job total was 3 instead of 2. The actual child exit was not queried.
[007 startup diagnostics completed](native_delegation_result_007.md) and
observed actual C exit `0xC0000142 / STATUS_DLL_INIT_FAILED` before owned
cleanup. Its specific DLL/access dependency remains unidentified. The next
operation is the single-variable [008 default-DACL hypothesis](native_delegation_plan_008.md),
adding only R's default grant to the new C token and reading it back.
Original IPC conditions remain in force. Completed 001-007 records are not
rerun or reclassified.

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
audits. Subsequent attempts 001-007 are distinct: seven prerequisite workflow
runs, six compiler-driver invocations, compiler Job TotalProcesses summing to
fifteen, and three native fixture invocations. Both 006 and 007 native outer
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
006 read-only IPC falsification, not as a qualified producer owner. Write
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
