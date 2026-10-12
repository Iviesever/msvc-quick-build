# Next handoff 015: bounded static helper candidate at RVA 0x584C

Status: `CANDIDATE_METADATA_ONLY_NOT_REGISTERED_OR_OBSERVED`.
Separate 015 target selection/extraction operations: **0**.
Candidate bytes retained as a separate 015 slice: **0**.
Candidate instructions decoded: **0**.
Slice SHA256: **null / not yet established**.

The 014 full-image identity hashing covered these file offsets. It did not
interpret or retain this candidate as a separate slice. The zero counts here
refer to separate 015 target operations.

The next concrete task is to design and independently review a 015-only
selection, identity and resource contract for runtime-function **105**, the
**167-byte** interval **`[0x584C, 0x58F3)`**. Only after that contract permits a
bounded selection step may its exact slice digest be established. A separately
reviewed implementation, finite synthetic checks, immutable publication and
complete registration must precede one new actual observation.

This candidate comes from the direct-call instruction at RVA `0xA612` in the
consumed 014 static listing. It is not a follow-on call authorized by 014. The frozen 014 owner, manifest, actual output
directories and consumed synthetic budgets must not be reused or altered.
The [014 result](native_startup_result_014.md) documents the available static
facts and their interpretation limits.

## 1. Close and verify 014 before starting 015

<!-- FINAL_RESULT_ADMISSION_014_BEGIN -->
014 is `COMPLETE_STATIC_DISASSEMBLY_INDEPENDENTLY_REVIEWED`.
Root joint decision: `ADMIT_COMPLETE_STATIC_DISASSEMBLY_014`.
Both independent runtime reviews and their separate-process complete readbacks
have passed, and root has jointly admitted the result. Open Critical /
Important / Minor findings: **0 / 0 / 0**.

| Final 014 prerequisite record | Bytes | SHA256 |
|---|---:|---|
| `runtime-byte-review-014.json` | 59,571 | `d4e7e824ff5280f371e40230e71abd7a5943d8aa7ee080bd091b0cb7457250a9` |
| `runtime-byte-review-014-readback.json` | 6,788 | `0b96fb3e5ade17accdd3eca88705ab56f676fc3a794a8084551353da258a8c8b` |
| `runtime-budget-review-014.json` | 49,512 | `7267b01abca8a0cca7899e083b3a6ed44e74a4e2f54ae2e39652a24ad031ffe9` |
| `runtime-budget-review-014-readback.json` | 11,409 | `c4c9692556345dc4d7451f94c375d88dd08a781e970e955d088d5cd9d3ce3595` |
| `runtime-review-admission-014.json` | 13,826 | `909fe6ba18ff15e9b7971d341c5c31412cb62faaf3e62eec3453b089a988e0cd` |

Root fully read back these records and hash-checked the byte review's 15 and
budget review's 47 fixed direct references. The earlier execution/root-readback/
interpretation records retain their historical pending fields; the separate
joint decision closes the result gate. 014's single actual attempt remains
consumed and no 015 actual observation is admitted. Git closure publication
and final remote scope/readback remain root's pending completion steps; resolve
the containing closure commit and subsequent #198 closure anchor before the
next session starts from that baseline.
<!-- FINAL_RESULT_ADMISSION_014_END -->

Then use Connector to read the latest feature/main heads, complete #198
registration and closure anchor, three draft/HOLD PRs, feature PR inventory and
workflow-run inventory. Read the final 014 reviews and all required retained
evidence fully; compare complete bytes/hashes and clean CLI/reaping evidence.
The expected working baseline is the **containing closure commit**, whose SHA
is resolved from Git and the subsequent #198 closure anchor. Do not substitute
the older invocation source SHA for a later closure HEAD.

014 invocation source is
`4beac41f341f4cc8f3ddf02471b17d113ecb39f8`, tree
`7a49f6c4d34566863ac4d2030121315f64c651c6`; its sole source parent is
`02b6fae17ee36f0011b7bae0f9102478b76ba42c`.
[014 registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6120286848)
binds `014-init-static-1`. That one actual observation is consumed, regardless
of how useful a later investigator finds its retained result. The historical
source activation passed expected-old/nonforced checks; it does not establish
fresh scope for a future 015 branch transition.

Preserve all original 011/012/013 artifacts, fourteen frozen 014 source/test/
design files, actual manifest, both consumed output directories and independent
reviews. Preserve any isolated work already present. Do not regenerate the
013 PE index or rerun 014 to obtain another interpretation.

## 2. Why this one helper is the next candidate

The 014 listing contains a direct call at RVA `0xA612` with raw bytes
`e835b2ffff`, displaying preferred target `0x18000584C`. Its EAX result is tested
at `0xA617`. On the observed `edx == 1` arm, a nonzero value takes the fallthrough
route through the unexamined call at `0xA631` to RVA `0x5680`, then the explicit
`xor eax,eax` at `0xA636` and common return at `0xA715`, provided calls on the
route return normally. This is the first explicit zero-return guard visible
in the 014 body, so the small containing runtime extent is a concrete next
static target.

Neither that helper's instructions nor its actual return value are known.
Its name, purpose, side effects and responsibility for 011 must not be inferred
from the caller alone. The second visible zero-return route in 014 uses static
IAT `0x1B820` (`GetModuleHandleExW` in prior metadata) and then static IAT
`0x1B7F0` (`GetLastError`); no live binding or actual failing API was observed.
The proposed 015 range does not include either external target or the common
helper at `0x5680`.

## 3. Exact candidate metadata, still without a slice digest

These values are retained metadata from the complete admitted 013 index,
corroborated by 014's static direct-call operand. They are not a new byte
acquisition or disassembly result.

| Property | Exact candidate |
|---|---|
| Runtime-function index | 105 |
| Original PE section | index 0, `.text` |
| Original-file interval, end exclusive | `[0x584C, 0x58F3)` |
| RVA interval, end exclusive | `[0x584C, 0x58F3)` |
| Exact extent | 167 B |
| Preferred ImageBase | `0x180000000` |
| Preferred VMA interval, end exclusive | `[0x18000584C, 0x1800058F3)` |
| Runtime table entry file offset | `0x224EC` |
| Unwind RVA / file offset | `0x1DD78` |
| Admitted unwind envelope | 12 B, raw `010a04000a3406000a320670` |
| Unwind metadata | version 1, flags 0, prolog 10 B, four slots, no chain or handler in the retained record |
| Slice SHA256 | **null; must be established under the separate reviewed 015 selection contract** |
| Separate 015 slice bytes retained / instructions decoded / actual disassembler calls | **0 / 0 / 0** |

Use the original section mapping to derive all three address domains. Equality
of file offset and RVA here must not become a general assumption. Preferred
VMA remains a file-derived address rather than an observed process load address.
An instruction straddling the exclusive end must cause refusal; it does not
permit extending the range into a neighbor.

## 4. Next executable work: design the selection and resource contract

The first deliverable is a new 015 design/selection/identity/resource contract,
reviewed before a preparation step opens original image content or a new tool
process is launched. It must name the current closure/source baseline, the
fixed original image and 013 index/admission identities, candidate record 105,
exact section mapping, target length and every allowed preparation read/write.

The contract must explicitly allocate the bounded selection-only operation
needed to establish the currently unknown slice digest. Define how the full
original-image identity will be checked, how exactly the declared 167 bytes
will be selected and hashed, what bounded selection record is persisted, and
how identity/range failures stop before disassembler registration. Do not
use a trial disassembly, a new PE parse or adjacent-byte inspection to discover
that digest. This handoff performs none of those reads and assigns no implicit
preparation invocation budget.

A non-null, independently verified exact slice SHA256 is a required input to
the final 015 manifest. Reconcile it with the original-image commitment and
admitted index mapping; retain the full selection evidence. If the chosen
selection method cannot prove those associations within its reviewed resource
contract, stop with the candidate still unobserved.

## 5. Separately versioned owner and finite validation

The frozen 014 actual mode binds runtime-function 169, 391 B, the 014 digest,
call ID and source identities. It is not a configurable 015 execution permit.
If the chosen 015 implementation requires different constants, selection or
budgets, create new versioned source and tests. Do not edit 014, disguise the
new actual target as synthetic, or rewrite a consumed manifest.

The 014 installed GNU identity and source-reviewed IO design are useful design
references. Reuse requires a fresh 015 identity and behavior review: canonical
tool/interpreter/source paths, selected dependencies and loader metadata,
exact options, environment, argv, working directory and exclusive output paths.
An unavailable executable, changed dependency or unsupported option is a stop
point, not authority for a download, alternative tool or version/help probe.
This handoff allocates no version/help probe; any proposal is separate scope,
never an implicit prerequisite.

Derive new numerical bounds from the actual proposed implementation. Account
for producer stdout/stderr before capture, manifest and strict JSON graphs,
full input identities, all retained/temporary buffers, instruction records,
serialization/wrappers/diagnostics, persistence/readback, owner and outer
processes, child limits and cleanup. The 167-byte input alone does not bound
textual output. 014's 12/20 s deadlines, capture limits and 78,721,129 B owner
payload proof do not transfer automatically. Any reused number needs an
independent 015 justification. Do not label application payload as parent RSS
or AS, or regular-file FSIZE as a host-wide write/process sandbox.

For changed behavior, keep real failing assertions before the minimal source
change and retained RED/GREEN evidence. Use pure fake-object lifecycle tests
when adequate and register a finite constructed tool/process set before its
first execution. Include the meaningful boundary cases: separated address
domains; the exact 167-byte exclusive range; partial final instruction; byte
mismatch/extra lines; identity failure; stdout/stderr caps; consumed directory;
nonzero/crash/timeout/unreaped child; and late persistence/close/readback.
Reap every constructed child and record its exact process count. Neither the
three spent 014 cap children nor the seven spent GNU fixtures can be rerun by
reference to their old allowances. Run only the affected appropriate tests;
no Windows/native test expansion is selected.

## 6. Publication, registration and the one actual 015 observation

After independent source, resource, synthetic and exact-path/workflow review
has closed the required findings, perform these steps in order:

1. Recheck the latest complete remote scope and the exact source path set.
   Preserve main and draft/HOLD PRs; do not assume a previous path-filter review
   covers new 015 files or a PR/main/dispatch event.
2. Publish the reviewed immutable 015 source and completely read back every
   changed blob, the complete tree and commit/parent. Keep the feature at the
   expected old closure head until the full registration gate is closed.
3. Freeze the complete canonical 015 manifest: verified non-null target digest,
   source/tool/interpreter/input identities, environment, exact range/argv,
   both new exclusive output locations, full producer/owner/outer allocations
   and one nonreusable actual attempt. Independently read back all manifest
   bytes and exact invocation interfaces.
4. Post the exact complete #198 registration, then fully read back its body and
   embedded manifest. A comment response alone is insufficient. Reconcile the
   expected ledger change with a fresh whole-scope check.
5. Perform only the registered expected-old, nonforced feature transition.
   Completely read back the new ref and required remote scope. A moved head or
   unexpected scope change stops the transition; it does not permit force.
6. Recheck initial executable/source/manifest identities and unused paths;
   submit the exact new outer argv once. The owner supplies only the fixed
   167-byte target to the selected tool and follows no outgoing destination.
   A setup refusal, timeout, cap, crash, identity change, interrupted cleanup
   or persistence failure consumes the attempt. No automatic retry, alternate
   output directory, wider range or replacement call is allowed.
7. Independently reconstruct every retained instruction's bytes and all three
   address domains; review complete captures, CLI exits, reaping, identities,
   deadlines and durable outputs. Root must jointly admit the result before
   an interpreted result/next handoff is published. Preserve negative evidence.

## 7. Stop gates the next owner must keep explicit

| Trigger | Required stopping point |
|---|---|
| Missing/failed 014 independent or joint result admission | Finish the evidence review and closure; do not begin an actual 015 observation. |
| Unexpected closure HEAD, main/PR/run/ledger delta or conflicting worktree change | Preserve existing work and reconcile full scope before a mutation. |
| Candidate mapping, extent or original identity differs | Stop before digest preparation or tool launch; do not select a replacement target implicitly. |
| Slice digest remains null or selection evidence is incomplete | No final actual manifest or actual registration. |
| Tool/dependency/source/environment unavailable or changed | Refuse; no automatic installation, alternate argv or probe. |
| Producer/parent/output/deadline proof incomplete, synthetic failure or required finding open | Keep the actual attempt unsubmitted while the preflight gate is unresolved. |
| Registration/immutable source/ref readback incomplete | No actual tool invocation. |
| A 015 actual directory/reservation already exists, or an actual call has already been attempted | Consumed state; preserve it and do not choose a new identifier for a retry. |
| Nonzero/overflow/partial listing/unreaped child/late or incomplete persistence | Preserve a refused result; no success inferred from one stored field. |
| Interpretation needs an out-of-range code/data target or live API behavior | Record a later bounded question; do not extend the current observation. |

## 8. Fixed evidence for the next session

The complete 014 source/raw/control SHA256 tables are in the sibling result.
The audit root is `/workspace/scratch/a8b3faf101c5/evidence-014`.
The following records identify the current handoff's starting evidence; the
final independent/root result identities in Section 1 must also be present.

| Evidence | Bytes | SHA256 |
|---|---:|---|
| `invocation-manifest-014.json` | 6,149 | `0f14b621af601f26fec3024d7e10fe4a79523033e1d9b44c5d0cb10ce780b288` |
| `offline-014/target.bin` | 391 | `e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44` |
| `offline-014/tool-stdout.bin` | 7,801 | `ec02f2bca27a4e550afba17ea319eeeb9b04afa167cc0ee6b5e2862011b2d59e` |
| `offline-014/report.json` | 12,608 | `2969185ddf6368bc8251208964715a371761d5f42ba5d1c9ce5830e5db578ccb` |
| `actual-execution-014.json` | 1,627 | `1ebaeca2ae3304ae0481decdfae3c9ff7294b0dfd86ef7b2459f4c1cc6dc17a4` |
| `actual-root-readback-014.json` | 6,354 | `72ad881aaa922712f6c24dedf8e188bfeab4976bd2e1fe81bd9f2ef4a3bde239` |
| `actual-static-interpretation-014.json` | 7,746 | `b43dc17e9187a6f1903ee9fb2a0c386d2e3fc550ebbb0b9ebb17786c7c2988f9` |
| `registration-readback-014.json` | 25,773 | `9a71d1384b491d56b3966550857793a2854a5377279ecec905768ccc47a09fb0` |
| Original retained 011 image, identity only here | 170,984 | `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Admitted 013 full index | 271,920 | `ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e` |
| 013 root joint admission | 20,232 | `c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b` |

The candidate unwind bytes in Section 3 are already retained 013 metadata;
they do not supply the unknown 167-byte instruction-slice digest. There is no
015 manifest, registered tool argv, output directory reservation or executed
015 source identity in this handoff.

## 9. Permanent native, product and IPC limits

011 remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`: outer Job total 3
instead of 2 and no admitted native_summary. 014 observes static bytes and
references; actual control flow, live IAT binding, failed API and the cause
of `0xC0000142` remain unknown. The `ret` at `0xA715` and included `int3` byte
at `0xA716` do not demonstrate either instruction executed. GNU's binary
`.data` label does not change the original `.text` section.

Native history stays **11 runs / 10 compiler drivers / compiler Job
TotalProcesses sum 23 / 7 native fixture invocations / 0 ordinary MQB runs**.
The consumed Linux static calls do not increment those Windows counts. Main
`87f86770cfc7e1941603074ace914e0c7faf10d3` and PR250/248/207 draft/HOLD remain
protected; the recorded feature had zero open PRs and the same eleven
attempt-1 completed failure workflow runs. Fresh scope checks are required.

Consumer/cost DEFER stays **+3.0963 ms / +26.70%**; VERSION **5.6.0**;
M1b/service disabled; A1-A5, deletion and ordinary qualification remain false,
and `canonical_evidence_sealed=false`. Do not build/run native fixtures,
reacquire/load/execute DLLs, download symbols/dependencies, inject/hook/write
process memory, relax token/ACL/Job or permission gates, activate native
workflows, change product/VERSION/cost, merge or release through this handoff.

The original IPC capability-lifetime prerequisite and a qualified producer
writer owner are still unresolved. A later static helper listing does not
satisfy either prerequisite or establish native qualification.
