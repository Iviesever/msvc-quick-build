# Implementation plan: actual producer-to-cleaner handoff

Status: `IPC_STARTUP_DIAGNOSTIC_SOURCE_PREPARATION`.
Prepared against main `87f86770cfc7e1941603074ace914e0c7faf10d3`.
No product implementation has been performed. The bounded prerequisite fixture
ran in attempt 005 and disqualified the metadata-seal dependency below. See
[the exact result, limits and next gate](native_probe_result_005.md).
This plan preserves PR256's accepted scope and does not reclassify its
unexecuted C01-C28 scenarios as passed.
The [006 IPC prerequisite closed before its first case](native_delegation_result_006.md).
The fixture compiled, but the report pipe broke before any child report and
outer Job total 3 exceeded 2. The actual child exit was not observed.
The next source is [007 startup/exit diagnostics](native_delegation_plan_007.md),
pending independent source review and immutable one-shot registration. The
candidate remains unqualified; the diagnostic preserves its original gates.

## 1. Decision required before product changes

The next engineering operation is a concrete native writer-boundary design.
It must account for the interval from the first allowed effect to the final
read-only handoff, including writers outside the directly launched process
tree. The current source/API review rules out treating Job completion plus
H1/H2 pins as that full authority.

| Alternative | Useful capability | Remaining obligation / disposition |
| --- | --- | --- |
| Existing cooperative marker plus per-launch Job | Serialize participating claimants; account exact Job members | Older/nonparticipating writers and services remain reachable. Insufficient for the accepted positive handoff. |
| Continuously retained H1/H2 output | Maintain object identity and tighten sharing without dropping H1 | Does not distinguish admitted and external writers during allowed writes. LINK compatibility is unqualified. Useful A4 prerequisite only. |
| A real participant/isolation owner | Potentially restrict the write boundary before any producer runs and own native completion | Revision 003 below selects a conditional-ACE candidate and a bounded prerequisite probe. The full access/IPC, lifetime and completion argument remains unqualified. |

A newly created directory name alone is not isolation. AppContainer, ACL,
restricted token, dedicated logon or service broker names alone are also not
proof. A candidate must show which concrete OS identity/access check prevents
a legacy writer and an out-of-Job service from altering each admitted
destination, while allowing the existing owners to perform their real writes.
Do not change machine accounts, grant broader rights, terminate shared
services, or install a kernel component as an implicit consequence of this plan.

The outcome is not that a future implementation is impossible. It is that no
qualified A2 owner exists at the reviewed base and a specific enforcement
design remains necessary.

### Historical selected candidate, revision 003: disqualified dependency

The candidate selected for qualification used existing OS token security
attributes (`TSA://ProcUnique`) in conditional allow ACEs. It does not issue or
modify a token attribute. A fresh existing-layout `.mqb` directory is created
with its protected DACL from the outset; existing output roots are preserved.
The actual MQB owner has the necessary management access. Each directly
launched native process is created suspended with protected process/thread
objects and a strict inherited-handle list, its actual token is queried, its
exact claim is admitted, and its owned Job is established before resume.
`OWNER RIGHTS = READ_CONTROL` suppresses implicit owner DACL changes. Native
grants exclude `WRITE_DAC` and `WRITE_OWNER`.

The host creates expected parents. Native directory grants exclude attribute/EA,
ownership and DACL changes, and admit only demonstrated namespace operations.
Separate file grants supply the permissions actually needed by cl/link. A
shared service impersonating an admitted token is a reachable writer, not an
automatically excluded actor. At closure, revoke native admission on every
object which can affect the selected boundary, retain all ancestry pins, acquire
final data-read/no-WRITE/no-DELETE parent and EXE pins, then request an exclusive
RWH oplock on the same final EXE handle. Old write/mapping/attribute and namespace
capabilities must cause refusal or be proved unable to affect that boundary.

The composite claim is disqualified by the old-WRITE_ATTRIBUTES counterexample
in attempt 005; the conditional ACL itself was not tested. Chromium supplies concrete attribute-query and
conditional-ACL implementations, while Microsoft documents the ACL, sharing
and oplock primitives. Those sources do not establish the composite MQB
completion guarantee. In particular, attribute-only opens do not break an
existing oplock. The proposed only-open check at oplock grant time must first
be tested. Directory oplocks cannot substitute for this leaf gate: they support
R/RH only, and content changes may be advisory.

The contract covers ordinary same-user file writers and actual native IPC
delegation. It does not add arbitrary malicious token theft, code injection,
administrator privileges or kernel compromise as new acceptance requirements.
The existing MQB owner may participate if its ordinary path has not delegated
its write identity outside the accountable owners.

The necessary-primitive falsification probe in `native_probe_plan.md` is now
closed. Its granted pending RWH did not prevent a preverified old attribute
writer from modifying the file. Keep the original plan and raw failure records;
do not count a repeated identical probe as progress toward a receipt.

### Replacement design gate after attempt 005

No replacement owner is selected. The next implementation decision must name
the exact access checks which permit actual cl/link and host parent/cache writes
while preventing unaccounted actors from obtaining or retaining relevant write
capabilities. The argument must include initial CREATE_NEW handles, later opens,
necessary metadata rights, process/handle inheritance and legitimate IPC
impersonation or delegation. Later ACL revocation cannot simply stand in for
completion of already-open handles.

CreateRestrictedToken(WRITE_RESTRICTED) with a unique restricting SID is selected
only for the concrete 006 prerequisite. It is not adopted as an owner: a
write-only restriction is not yet a demonstrated IPC boundary. The new plan
fixes the desired access, effective protected DACL, actual child token, static
and dynamic SQOS, true IPC thread-token query, natural child Job completion and
retained-handle use after revocation. An explicitly R-admitted duplex route
provides an end-to-end positive control; ordinary write and read-only routes do
not grant R. NULL-DACL endpoints and relevant ALPC mapping are outside this
finite test. A refusal of its fixed routes cannot qualify a complete owner.
006 did not reach any IPC case, so it supplies no result for these routes.
007 adds only fixed startup/first-command observations and actual child/Job
failure snapshots before cleanup. Independent source review and immutable
registration still precede the new activation.

Do not replace this work with a generic owner class, another PID census, a
candidate-recipe collector, an always-refusing CLI, or a repetition of consumer
or cost studies. The existing engineering authorization remains sufficient to
continue a concrete in-scope design; the missing authority is technical.

## 2. Preserve the actual production chain

Once section 1 has a defensible owner, use the existing production composition.

| Existing owner | Required change / evidence |
| --- | --- |
| `Application.cpp::build` | Select the supported explicit-source/already-selected portable profile before discovery/VS/PCH side effects; retain resolved target/config/architecture/options and a live session. The ordinary branch currently calls `target_coordinator.run(request)`. |
| `MsvcIncrementalTargetCoordinator::run_impl` and `TargetCompileWave` | Carry the same gateway/epoch through direct scheduling, inspect/miss scheduling and the late-freshness second wave. Preserve source-order errors and actual per-source cwd. |
| `MsvcIncrementalCompileCoordinator::execute_inspected_impl` | Admission covers parent preparation, native submission, dependency sealing and complete cache save/failure cleanup. Cache warnings preserve their existing meaning. |
| `MsvcIncrementalLinkCoordinator::inspect_link/run_impl` | Keep object/library routing and freshness as the authority. Bind the final invocation after these decisions; finish the whole LINK owner only after side-output/input observations and cache save conclude. |
| `MsvcLinker::link/build_recipe` | Execute and capture the same final recipe object. Do not derive a receipt from a prewrite candidate or maintain another recipe model. Preserve validation, parent preparation, native result and diagnostics. |
| `WindowsProcessRunner::build_environment_block/run` | Only this owner sees the actual effective inherited-plus-overlay environment. Preserve raw values privately, including fully inherited/null-environment semantics. A recipe overlay is not the complete environment. |
| New concrete Windows writer-boundary owner | Privately issue a move-only completion object only after its real native scope and all admitted owners are accounted for. A public constructible `ProcessResult` cannot mint it. |
| Existing storage identity primitives with an owning lifetime | Retain full ancestors, exact parent and leaf with no reopen/upgrade gap; use exact component-relative opens and preserve refusal of ambiguous/reparse/multiple-link/delete-pending states. |

Only a successful actual terminal LINK may issue the first current EXE
receipt. A no-op remains an ordinary successful build without that capability.
The in-session consumer must actually receive and retain the capability; an
unused collector or an always-refusing CLI is not the planned deliverable.

## 3. A4 candidate and its precise limits

The preferred sequencing once an enforcing writer owner exists is to keep
that owner active from the first effect, complete the admitted native and host
writes, then acquire the exact ancestor/parent/leaf pins and content seal while
the same exclusion still holds. Transfer the enforcer and retained objects
together. This also handles a newly created output without pinning a leaf that
did not exist. The contract does not require a pre-LINK H1 when this continuous
producer-to-seal exclusion is independently established.

The H1/H2 alternative below is an optional candidate, not a universal
prerequisite. It cannot supply the missing enforcer by itself.

Unexecuted read-only candidate:

- H1, established after the final LINK decision but before native output writes:
  data-read plus necessary metadata access; share READ|WRITE, not DELETE.
- With H1 and every required ancestor still held, H2 opens the exact same leaf
  relative to the retained parent: data-read access, share READ.
- Move the complete chain and live state; do not rebuild authority from a path,
  exported ID/hash, caller boolean or historical receipt.

Before adopting it, qualify the exact LINK mode's output sharing, truncation,
supersede and replacement behavior. Do not precreate a missing EXE before
`inspect_link` decides `missing_output`; that would change freshness.

The candidate does not close A2: another writer may participate in H1's allowed
write interval and finish before H2. Preserve that controlled negative case.
The final hash binds bytes but cannot retroactively prove exclusive provenance.

A read-only H1 that denies DELETE sharing cannot later admit a DELETE-capable
handle without conflict. Future A5 must choose and qualify an appropriate
initial access combination or restart preview/consent under its separate
contract. This plan performs no delete request or rights upgrade.

Microsoft's CreateFileW contract explicitly says denial of FILE_SHARE_WRITE
also conflicts with an existing write-access mapping. CloseHandle does not
itself unmap surviving views. Qualification must check actual handle/mapping
lifetimes; it must not invent a claim that all mappings bypass sharing.

## 4. Concrete acceptance obligations for the new design

All product entries below remain `NOT_EXECUTED`. The 005 same-process primitive
counterexample is separate and does not execute these product scenarios.
These are obligations, not an allocated
run matrix. First select the exact enforcing owner and supported profile, then
register the smallest discriminating native attempt for that mechanism.

| ID | Evidence required |
| --- | --- |
| P01 | Real ordinary explicit-source build and actual terminal LINK issue a live move-only receipt and the in-session consumer receives it. Exact selected portable tool identities are recorded. |
| P02 | The actual submitted invocation, cwd and effective environment are bound; late library routing and force-full-link are preserved. Secrets remain private. |
| P03 | Native, parent and cache writes enter the same epoch before their first effect; closure denies new admissions and waits for every admitted owner. |
| P04 | Native error, compile/LINK failure, cancelled/unknown drain and host cache warning retain original outcomes; no success capability is fabricated. |
| P05 | No-op does not mint a first receipt; missing output still invokes the existing rebuild decision. |
| P06 | Wrong session, epoch, request, root, output and moved-from owner cannot be consumed as the current receipt. |
| P07 | Same-size/mtime content change is detected by actual content binding; held ancestry and leaf remain continuous across producer/consumer ownership. |
| P08 | A real external writer attempts to open/write during the producer phase. The enforcing mechanism denies it, or handoff refuses. H2 success alone cannot pass this case. |
| P09 | A real out-of-Job writer/service attempt cannot mutate an admitted destination after claimed completion. A PID census or absence of observed activity is insufficient. |
| P10 | Retained write handles and writable mappings produce the documented refusal or fully accounted completion; do not infer unmapping from a closed file handle. |
| P11 | If the design chooses prewrite leaf pins, qualify their compatibility with the exact LINK mode, including initially absent outputs. Otherwise prove continuous enforcer ownership through the post-completion seal. No attribute-only fallback, path retry or silent mode substitution. |
| P12 | The receipt consumer performs no deletion or attribute/disposition mutation; A3 and A5 are still unavailable. |

Any native attempt needs fixed source/tool identities, exact inputs/profile,
one-shot run/attempt admission, maximum tool/helper calls, original stream
retention, failure-stop policy and independently reviewed assertions before it
starts. Counts cannot be selected responsibly before the enforcing mechanism
and test program exist. No budget is allocated by this plan.

## 5. Applicable verification and frozen boundaries

The prior PR256 documentation slice's zero-local-execution rule is scoped to
that slice. Ordinary correctness tests for a concrete new implementation can
have their own plan. That does not reopen either frozen research program:

- PR250 cost preparation remains byte-identical: 21000 bytes, SHA-256
  `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`;
  15 named integer-zero budgets, 10 named null selections, 7 blockers and 4
  false authority values.
- M1b default-endpoint/service-ownership real-tool jobs remain guarded false.
  Any new study requires its separately reviewed #164 registration and source
  change; preserve the historical failures. Do not replay the 70/42 matrices.
- No cost/ETW study, failed-sample replacement, cleanup, codec adoption,
  VERSION or release change is selected here.

For future C++ changes, account for actual automatic Native Debug/Release,
write-domain, target-admission/work-batch, recovery and cross-stage gates.
Do not describe skipped or scope-only jobs as native qualification.
The repository uses `cpp/mqb.json` and PowerShell native scripts, not CMake.
Its exact 96 product TU / 96 test-program inventory guards and historical
observation-isolation contracts need deliberate review if changed.
Do not change a pinned hash solely to make a violated historical contract pass.

## 6. Completion and handoff status

The implementation remains pending. Attempt 005 has completed and independently
confirmed a native counterexample to the selected metadata-seal dependency.
This record provides exact integration locations and the replacement design
gate. It is not another consumer/cost necessity study, a delivered cleaner,
full native qualification, or a merged feature.

The next engineering handoff is: provide the concrete participant/isolation
owner described in section 1, review its actual access and completion argument,
then implement and qualify the A1/A2/A4 path above. Retain the current refusal
and all original evidence until that owner exists.

Sources: the unchanged repository at the pinned main commit; PR256 and #198
comment 6095001403; docs/EXPLICIT_CLEAN_CONTRACT.md; docs/PROCESS_LIFETIME.md;
the three current source audit reports; Microsoft CreateFileW and Closing a
File Mapping Object API contracts.
