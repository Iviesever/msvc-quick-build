# Current-session producer handoff: implementation entry gate

Status: `CANDIDATE_SELECTED_PENDING_NATIVE_PRIMITIVE_CHECK`. This is an implementation
preparation record. Product implementation, native qualification, and C01
positive handoff are not complete.

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

Three separate source audits examined the actual production path, Windows
completion/physical boundaries, and verification scope. No product, project
test, analyzer, compiler, MSVC, MQB or workload was executed in this review.

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

Independent review has now selected a concrete conditional-ACE / retained-object
candidate for bounded qualification. It distinguishes normal same-user file
writers using actual token attributes, accounts real IPC delegation, and tests
the final seal rather than inferring completion from a root exit. The first
necessary-primitive fixture is being written under `native_probe_plan.md`.
This is engineering progress beyond the initial audit; it is not a positive
producer receipt and does not establish that the candidate will qualify.

A proposed native/physical owner must demonstrate admission before effects,
exclusive or fully participating writers for the exact selected boundary,
accountable native completion, and continuous retention through handoff.
An API label, process success, empty unresolved vector, marker reset, PID
snapshot, portable selection or Z7 flag cannot establish this authority.

See implementation_plan.md for exact code owners, design alternatives,
qualification obligations and preserved budget boundaries.
