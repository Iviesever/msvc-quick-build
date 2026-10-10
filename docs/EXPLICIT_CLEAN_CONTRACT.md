# Explicit clean: positive eligibility and refusal contract

**English | [简体中文](EXPLICIT_CLEAN_CONTRACT_ZH.md)**

## 1. Decision and exact boundary

**Decision: `CONTRACT_DEFINED`. Initial profile: `CURRENT_SESSION_EXE`.
Current implementation: `EXECUTION_BLOCKED`.** These are separate statements:
this document completes PR255's finite contract handoff; it does not add a clean
command, make an existing file deletable, or complete #198.

The checked main is `c96c75750bcdebb1e8e31f69841d33ec4752e175`, tree
`d049d9fc95071796e5232f79655f6735e7322f89`. The authoritative request is
[#198 comment6091785630](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6091785630).
The [decision/source index](EXPLICIT_CLEAN_CONTRACT.json) binds exact source
identities, the independent audits, acceptance scenarios and missing authorities.
Normative requirements below describe future behavior; facts explicitly marked
**current** describe this base. Proposed records are not existing C++ APIs.

The first positive path is one user-selected ordinary executable target's current
main EXE, actually produced in the same controlled writer/cleaner session. It
retains PDB, ILK, MAP, manifests, import/export files, object/PCH/IFC/HU state,
dependency data and all caches. It neither selects an old generation nor infers
that any other file is obsolete. EXE-only is a minimum correctness path, not the
PDB-heavy space benefit or complete Rium acceptance requested by #198.

## 2. Alternatives and selected use

| Alternative | Product consequence | Decision |
| --- | --- | --- |
| Current EXE from an accountable producer in one controlled session | A concrete success path with no on-disk ownership migration; requires real writer completion and held-object handoff | Select `CURRENT_SESSION_EXE` |
| A later standalone CLI reopens current/legacy files by target name, scan or saved report | Needs durable authenticity, replacement detection, recovery and participant coverage not supplied by current observations | Do not admit in this profile |
| Whole-target historical clean/prune, including shared intermediates and symbols | Needs wider producer/use relationships, independent symbol/ILK handling and historical retention policy | Separate later extension |

The selected user action is explicit discard of a target's known-rebuildable
current EXE. A managed caller can offer it after a qualifying build, without
launching the EXE, after a read-only preview and an explicit execute choice.
Building a target must never itself request cleanup. No CLI spelling or persisted
plan format is introduced. A report exported from this session is descriptive
data and cannot be reimported as an execution capability.

## 3. What the current source establishes

| Current mechanism | Fact it supplies | Fact it does not supply |
| --- | --- | --- |
| `ProjectArtifactLayout::for_target` | Intended `.mqb/bin/<target>.exe` and target cache paths | Configuration/architecture identity: neither is a parameter here; the same path can be overwritten by another configuration |
| Source and PCH layouts | Source-keyed object/dependency/IFC/cache paths; PCH target/configuration/architecture paths | Exclusive use by the selected target; source intermediates may be shared |
| Storage inventory / storage association | Finite file observations and associations to supplied unverified claims | Current producer ownership, complete dependency use, retirement or deletion permission |
| Recorded ordinary target results | LINK completion/cache facts and per-source compile cache evidence, including exact-entry capture when supplied | Main-EXE post-completion physical/content seal or a complete actual terminal recipe |
| Pure known-write collection | Typed possible writes, actual owner-selected cache paths and explicit unresolved effects | Executed final argv, closed native effect set, or an admission token |
| `WindowsWriteDomain` | Held local NTFS directory identity; cooperative persistent marker reservation | Production caller integration, recursive exclusion, writer completion/reset or recovery |
| `WindowsWriteInventory` / `observe_storage_file` | The inventory retains private exact-parent pins; the single-file observer holds leaf/ancestors only during its call | A continuous producer-to-cleaner leaf/full-ancestry capability with content and execution authority |
| Root process return / foreground scope exit | The documented result of that execution boundary | Completion of untracked children, shared services, older builders or running products |

Do not erase capabilities which now exist: ordinary `RecordedTargetResult`
contains `TargetCompileCacheEvidence.compiles`, and compile cache evidence can
carry `exact_cache_entry_captured=true`. Its remaining authority flags stay
separate. Conversely, LINK's successful process exit does not seal the current
main EXE. Known LINK writes explicitly retain ILK/LTCG/object-directive/native
gaps. Conditional side-output records are not a license to delete those files.

The independent source audits document that `collect_prewrite_inventory`,
`try_reserve` and `begin_writes` have no production adoption at this base.
`begin_writes` changes reserved to writing; it does not validate a producer or
wait for a child. The deliberate lack of completion/reset must not be bypassed
by marker deletion, normal destruction, PID checks, elapsed time or process exit.

## 4. Initial positive profile

All of the following are required together. Failing to establish any condition
produces a named refusal or unavailable result before a delete request.

1. The user selects one exact ordinary `TargetKind::executable` target and its
   resolved configuration, architecture, MQB/toolchain identities, source set,
   ordered effective options, resolved inputs and execution directories. The
   target must use the supported ordinary path, without PCH/modules/HU, additional
   object producers, archive/DLL targets, arbitrary output overrides or run-after-
   build in this initial profile.
2. The selected operation uses an explicit source set and an already selected
   portable toolchain. This avoids implicitly admitting discovery and automatic
   VS/bootstrap stages; it does not prove that cl/link have no private temporary,
   directive, service or environmental effects. Those remaining effects require
   the writer authority in section 7.
3. This exact session's real terminal LINK operation succeeded. Its responsible
   owner records the final effective invocation after object/library routing and
   late freshness decisions, then binds the actual main EXE while its writer
   exclusion remains continuous. A no-op/cache hit, candidate recipe, caller
   label, process exit alone or historical recorded result cannot mint the first
   current-output receipt. No-op builds can continue normally without this new
   cleanup capability.
4. The result is a regular local-NTFS leaf with one link, within the verified
   typed output boundary, matching the produced object and content. It has no
   reparse/ADS/namespace ambiguity or protected/shared/runtime-use veto.
5. The retained request can still resolve the required source/tool inputs and a
   supported reconstruction path. A known missing or unavailable dependency is
   not called rebuildable. Rebuildable means the supported target can be built
   again; it does not promise identical future bytes or permanently available
   tools. Changed request/input bindings between preview and execute invalidate
   the plan rather than silently changing the selected build.
6. A real participating writer/launcher gateway has stopped new admissions,
   accounted for admitted work, and transferred a still-live, nonforgeable
   completion capability to the cleaner. The native backend has separately
   qualified retained-object deletion and running/mapped-image refusal.

**Current outcome for this profile is blocked by A1–A5 in section 10.** Its
specified successful transition is intentional and testable; no always-refusing
CLI should be shipped while those implementation authorities are missing.

## 5. Producer-owned current-output receipt

The receipt is an owning, noncopyable capability bound to its live session,
never an arbitrary caller-populated structure or a collection of true booleans.
Its conceptual fields are:

| Binding | Required content and responsible owner |
| --- | --- |
| Target request | Target kind/name, configuration, architecture, ordered resolved options, input/use bindings and their accountable resolver |
| Actual execution | MQB and compiler/linker/toolchain identities, final effective recipe/argv, response-file expansion/binding where applicable, effective environment/cwd and terminal success evidence from their existing owners |
| Physical object | Held authority-directory/ancestor identities, exact relative EXE leaf, retained leaf identity, content binding and supported attributes/link count from the Windows boundary |
| Producer lifetime | Exact gateway instance, writer epoch, admissions closure and completion record; no unknown outstanding writer affecting the selected boundary |
| Product use | Selected outputs are not retained inputs/tools of another target, released/protected assets or a launched/mapped product; the accountable use boundary is explicit |
| Consent and consumption | Exact preview selection, immutable plan identity and single-use execute choice; move/consume invalidates other uses |

Effective recipe evidence belongs to its producer. Existing LINK cache fields,
signatures or the prewrite candidate cannot be relabeled as the actual terminal
command. The current source does not capture all these fields. A changed source
configuration or an EXE replaced in place must not survive merely because its
target name, file ID, size or mtime is unchanged. Retained pins, content binding
and the writer epoch cover different failure modes and are all required.

Raw environment values and secrets in command arguments or response files remain
with their in-memory owners. Preview/exported reports may contain only permitted
non-sensitive identities or redacted descriptive summaries. Such summaries
cannot reconstruct the receipt or grant execution authority.

The first receipt is scoped to the current process/session and continuous
object lifetime. A new process, released pin, abandoned owner, changed epoch or
replaced authority root invalidates it. No ordinary-generation wire/archive,
cryptographic signing scheme, persistence transaction or recovery claim is
introduced by this contract.

## 6. Eligibility and mandatory exclusions

| Classification | Initial-profile handling |
| --- | --- |
| Selected current EXE with all producer, input/use, writer and physical conditions | Eligible for the guarded execute transition only |
| Intended EXE path with no current producer receipt; legacy or externally copied EXE | Unknown ownership; retain |
| Debug/Release or architecture/options request different from the receipt | Stale or mismatched request; retain, even when the pathname matches |
| EXE used as a tool/input by another target, a retained dependency, release asset, or active product | Shared/protected/in-use veto; retain regardless of target kind |
| LINK PDB, compiler PDB, ILK, MAP, manifests, DLL/import/export files | Outside the initial selected role set; retain; PDB types and ILK are distinct |
| Object, PCH, IFC, HU, scan/dependency state and any compile/link/archive/discovery/toolchain cache | Retain; no recursive or sibling deletion |
| Source/configuration/external inputs, generated inputs, logs/failure samples, release ZIP/checksums/manifests, VS state, unknown directories or `.mqb/tmp` contents | Protected or unknown; retain |
| Multiple hard links, unknown link count, directory/reparse leaf, reparse/changed ancestor, ambiguous case/namespace, unsupported filesystem | Physical refusal; no alias unlink or best-effort fallback |
| Missing exact leaf under a proven unchanged parent | Report already absent; no deletion and no credited bytes; uncertainty/case conflict is not absence |
| Unknown dependency/use coverage, unaccounted writer/service, running/mapped-image status not established | Unavailable authority; retain |

Physical classification and current content do not establish ownership or
exclusive use. Conversely, a valid logical receipt cannot waive a physical
refusal. No age, access time, extension, byte size, scan membership, empty gap
vector, expired marker or absence from one invocation overrides these rules.

## 7. Participating writers and running products

The new gateway must be an opt-in composition of existing owners, not another
parallel build planner. Before any admitted stage creates a parent directory,
writes a cache or submits native work, it must be within the declared session
and physical authority. Existing collection-only APIs can supply declarations;
unresolved entries remain unresolved until their actual owner qualifies them.

| Actor | Required participation or refusal |
| --- | --- |
| Ordinary compile and LINK, plus their output-parent/cache owners | Enter before first side effect; preserve late freshness, routing and original errors; completion includes submitted work and all admitted writes |
| Discovery and automatic VS/bootstrap | Excluded from the initial explicit-source/already-selected-toolchain profile; a caller which invokes them is refused until their producers join |
| PCH, module/HU/provider, librarian, additional-object or external output producers | Excluded; do not silently treat their inputs as owned outputs |
| Native descendants/private or shared services | Use a demonstrated boundary covering the actual producer effects; root-only success or a root handle closing is insufficient |
| Managed foreground launch, debugger or product use | Block launch throughout the clean interval; any active or unknown use refuses clean. The initial path does not run the produced EXE |
| Older MQB, other build tools or uncooperative writers able to reach the boundary | No compatibility claim. Refuse unless a separately demonstrated physical isolation/participant boundary excludes them; installing a new binary or adding a cleaner marker is insufficient |

Current cancellable `WindowsProcessRunner` has a stronger job/member completion
boundary only when the relevant token is used. Current ordinary compiler/LINK
and foreground calls do not thereby acquire it. Even a qualified job boundary
does not cover arbitrary service/WMI/non-descendant writers. `/Z7`, portable
toolchain selection, an idle snapshot and a surviving mspdbsrv PID each leave
different questions; none is a universal completion certificate.

The future gateway must close admissions, account for every admitted operation,
and transfer completion plus object pins without an unlocked gap. The next
writer epoch may open only after the cleaner finishes or explicitly abandons
without mutation under a proven release protocol. Any owner death, incomplete
drain or uncertain mutation leaves unresolved state. Existing unresolved markers
are not imported, reset or deleted by this profile. No killing global services,
lock-age takeover, automatic recovery or new whole-project transaction is added.

## 8. Preview, execution and physical continuity

Preview is read-only with respect to build files: it neither creates output
directories nor sets deletion disposition, changes attributes or removes a
marker. It reports exact selected entries, classifications, refusal reasons,
logical bytes and available allocation observations. It is not a disk-space
reclamation estimate. The live caller owns the plan and consent boundary.

For an eligible execute transition, keep the writer/launcher exclusion and
validated ancestor/parent/leaf handles alive across the preview-to-execute
interval. Do not drop a read pin, reopen a pathname with delete rights and assume
identity survived. The native design must obtain compatible delete-capable
handles without an unprotected upgrade gap, or refuse/restart the entire preview
with fresh consent before any mutation. Pin failure must not fall back to an
attribute-only handle or follow a reparse point.

Immediately before each mutation, revalidate plan/gateway/epoch, request/use and
content bindings, exact ancestry/leaf identities, type, link count, deletion
state and supported access. Order is fixed by the preview and never expands to
siblings or a directory tree. Once validation fails, retain the affected file
and stop subsequent delete requests. Report the entries not attempted.

The Windows backend must request disposition on the same held leaf, not use a
later path-based `remove`/`DeleteFile` fallback. It must keep running/mapped-image
and incompatible-access cases in the refusal set. Advanced POSIX/replacement
flags, forced attribute changes and mapping bypasses are not selected here.
Any unproved native behavior remains A5-blocked pending explicit Windows
qualification; these documentation decisions do not execute a native experiment.

Microsoft's API contracts support only the primitive facts used here: access and
sharing compatibility, file identity comparison for held handles, and handle-based
disposition with suitable rights. They do not certify MQB ownership, completed
writers or immediate reclaimed space. See [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[FILE_ID_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info),
[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
and [FILE_DISPOSITION_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_disposition_info).

## 9. Results, interruption and rebuild behavior

Separate the stages `not_attempted`, `refused`, `already_absent`,
`delete_requested`, `closed_name_absent`, `failed` and `outcome_unknown`.
Record actual native errors, first failure, path and physical identity, action
attempted, disposition/close/postcondition observations and every later entry
not attempted. A successful disposition call alone is only `delete_requested`.
Only a verified close and same-parent postcondition can support the corresponding
name-absence statement. An uncertain postcondition stays unknown; do not retry
against a reopened pathname or credit its planned size as reclaimed space.

Keep preview logical bytes, observed allocation bytes, successfully removed
names and actually measured volume-space change separate. Unsupported or
unmeasured allocation/reclamation remains null/unavailable. Hard-link alias
accounting never counts the same physical file twice. This EXE-only profile
refuses multiple links rather than making an unlink/reclamation promise.

Acquire the ability to retain the operation record before the first mutation.
If result recording becomes unavailable, stop later mutations; expose already
known results through the surviving channel and retain an unresolved operation.
Do not delete original logs to make room. Process interruption/power loss can
leave the final attempted result unknown; no cross-file atomic deletion, rollback,
durable journal or automatic recovery is claimed. A later session cannot resume
an old plan as authority. It must use an independently admitted recovery or new
producer path, with old incomplete evidence preserved.

The current LINK decision includes `missing_output`. Keeping a cache record
does not make a missing EXE a cache hit. Future end-to-end qualification must
prove that the selected target rebuilds and retained targets keep their original
freshness/behavior. Do not erase caches or pretend all cache hits are invalid;
do not claim those future clean/rebuild tests have already run.

## 10. Exact missing authorities and next implementation

| ID | Missing authority at the checked base | Concrete implementation owner / proof required |
| --- | --- | --- |
| A1 | No producer-owned current main-EXE receipt with actual terminal recipe and post-completion physical/content binding | Terminal LINK/coordinator plus existing target request and Windows identity owners; a successful real production path must mint the receipt, with mutation/staleness refusals |
| A2 | No ordinary production gateway that closes admissions, accounts for admitted native/cache/parent writes and transfers verified completion | Existing ordinary target/compile/LINK/cache owners and process boundary; explicit participating subset, failure/drain/abandonment semantics, no caller bool or marker reset |
| A3 | No accounted shared-use and runtime-launch boundary for clean | Target/input/use and foreground/launcher owners; actual other-use veto and launch exclusion, with old/external actors explicitly refused or isolated |
| A4 | Single-file observation releases leaf/ancestors; the write inventory retains private exact-parent pins but releases observed leaves and lacks the full continuous handoff | A move-only Windows retained-object capability, reusing existing ownership boundaries, joining producer completion, preview and execution without a pathname gap |
| A5 | No qualified native clean executor and outcome accounting | Same-handle disposition, close/postcondition and record-failure handling; real NTFS alias/mapping/access/race/partial-failure and subsequent rebuild evidence |

**The sole next implementation handoff is the producer-owned current-output
receipt and ordinary-target writer gateway for the selected subset, with a
non-destructive cleaner handoff.** Implement A1/A2 and the retained A4 handoff
together where their authority cannot be separated. Hook the actual selected
production call path, close new admission, preserve original outcomes, and prove
that valid completion can hand over the current EXE while incomplete work,
wrong epochs, changed output and unaccounted writers cannot. Use existing layout,
recipe, cache, process and Windows ownership boundaries; do not introduce a
parallel recipe model or populate an opaque token from synthetic truth flags.

This next implementation must deliver a real positive handoff, not another
necessity review, an unused collection API, an always-refusing CLI or a bare
`complete()` which removes the persistent marker. A2's exact native completion
dependency remains a blocker if it cannot be demonstrated. A3/A5 and actual
deletion stay disabled until their independent contracts and applicable evidence
are satisfied. Do not enlarge this into full #164 recovery or treat it as a new
ordinary-generation consumer/cost proposal. The next implementation's tests and
Windows budget require its own concrete plan; none is registered or launched here.

## 11. Acceptance specification and verification boundary

The JSON index defines 28 named scenarios (`C01`–`C28`) covering the positive
handoff, no-op/no-receipt, missing output, configuration/content change, wrong
epoch/consent, shared/protected roles, legacy claims, namespace/alias/reparse/
hard-link failures, running products, uncontrolled/native writers, abandoned
markers, failed disposition/close/postcondition, record failure, interruption,
rebuild and retained-target behavior. Expected outcomes are requirements for
future implementation, not executed results or an allocated workload matrix.

For this documentation slice, verification consists of exact-source/static
contract and bilingual review, JSON/source identity consistency, unchanged old
tree entries, and the actually applicable automatic documentation/classifier CI.
The existing bilingual verifier enumerates its maintained pairs; this new pair
receives direct independent semantic review. Skipped native/performance jobs
remain skipped and supply no execution/cleanup/cost qualification. Actual PR,
merge/main identities and CI readbacks belong to the final #198 handoff and
sealed evidence; this source does not predeclare their success.

## 12. Preserved decisions

The [ordinary-generation consumer decision](GENERATION_CONSUMER_DECISION.md)
remains DEFER and ends that measurement path. Frozen
`ANALYSIS_COST_DECISION_PREPARATION.json` remains 21000 bytes / SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`, with
15 strict-int zero values in `authorized_real_budgets`, 10 null values in
`future_selection`, 7 blockers and 4 false authorities. PR250 remains
Draft/unmerged, product-cost/integration HOLD and real execution BLOCKED; #248
remains independently Draft/unmerged. No old failure, adverse sample, missing
stream, spent budget or capability gap is erased or reinterpreted.

No product code, tests, workflow, VERSION, old document or release changes in
this slice. No local project import/test/analyzer/runner, fixed substitute,
codec/compiler/MQB/MSVC/ETW or real workload is run. The actual automatic CI
scope is reported separately. VERSION remains 5.6.0. #198's safe reclamation,
symbol/ILK extensions, Windows/Rium acceptance and release work remain open;
#164 retains its wider session, service and recovery roadmap.
