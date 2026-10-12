# Pre-execution validation 015

Status: **pure integration, both preparation completions and all five finite
GNU outcomes passed their fixed checks**. Full independent runtime reviews
are identified in the publication checkpoint below. Actual call
`015-helper-static-1` has not run at this source-publication checkpoint.

## Scope and final pure integration

The final integration loads exactly the six new test modules, plus the new
external transport's constructed process tests. It uses constructed buffers,
temporary regular files and fake Popen/selector objects. Result: **80 tests,
0 failures, 0 errors**, source bytes identical before and after. This is not
native or compiler validation and spends no GNU/real cap-test allowance.

Evidence root is `/workspace/scratch/a8b3faf101c5/evidence-015/`.

| Record | Bytes | SHA256 |
|---|---:|---|
| `pure-integration-015.json` | 3976 | `20696cd4e60e5f093a7698c764aae152db961b6428bcbf2183fcfcd7feba86d3` |
| `pure-integration-015.log` | 12766 | `8539f0c516efe5bd38547627605563c45f35e70d76548105140fe72d3def53b3` |
| `tool-identity-precheck-015.json` | 2756 | `35c547ea960751f718e8c8c77f25e6eb360ca376dff601470cd2e244b00a8f14` |

The 17 source/test refs in the integration receipt include all six new source
units, six tests, the three unchanged 014 generic modules and external
transport/source test. The checked boundaries cover helper-not-entrypoint,
file/RVA/VMA separation, exact selected end, input identity before decoding,
malformed runtime/unwind/caller metadata, primary/independent/source/manifest
association, full source verification before core execution, malformed JSON,
exclusive output reuse, every image-read gate, byte/capture limits, tool
nonzero/crash/timeout/unreaped states, SIGCHLD ownership, durability/readback,
late close/deadline and bounded diagnostic failures.

## Retained negative evidence and fixes

All earlier failing evidence is preserved, including failures that exposed a
test defect or an incorrect interface assumption. No original slice or spent
observation was used to discover these corrections.

| Evidence group | Finding and resolution |
|---|---|
| Model/selection initial RED | Missing new policy/selector, then genuine behavior failures; a `len(None)` test error was retained and changed to assert bytes before length. |
| Bootstrap/preparation RED | Missing distinct phase/source and one-image lifecycle; final phase tests passed. |
| Owner/outer RED | Lifecycle/cap/reaping completion gates and fresh child limit installation; all verified with constructed processes. |
| Manifest/source join RED | Primary manifest SHA and common source/provenance refs were insufficiently joined; exact association checks added. |
| Transport/outer report path RED | External transport receipt and owner-containing outer report are separate artifacts; the erroneous identity equivalence was removed and correct path associations tested. |
| Diagnostic review RED | Failed diagnostic writes could escape as traceback; entrypoints now contain writes and return nonzero on failure/short write. |
| Strict decode review RED | Outer now strict-decodes and canonical-checks the manifest before core compilation. |
| Transport resource review RED | Encoding now measures bounded graph/bytes before allocation; output readback uses its own bounded FD instead of unbounded path reopen. |
| Transport SIGCHLD RED | Ignored SIGCHLD now prevents Popen, preserving reliable wait status. |

## Finite constructed GNU plan and consumed outcomes

The prepared plan `gnu-synthetic-plan-015.json` is 31221 B, SHA256
`802c9017c8a058edebe083c5491cdba92c2889e35ec7716a6dd60a7c06cd1d2e`.
It freezes every case manifest, target byte string, synthetic input/selection
receipt, source, argv, output directory, deadline and expected outcome before
the first tool launch. Prepared status alone is not execution admission.

| Case | Bytes | Observed records | Owner / outer / GNU exits | Observed disposition |
|---|---:|---:|---|---|
| 167 NOP instructions | 167 | 167 | 0 / 0 / 0 | Complete |
| 166 zero bytes then ret | 167 | 84 | 0 / 0 / 0 | Complete, no zero skipping |
| Known 15-byte instruction | 15 | 1 | 0 / 0 / 0 | Complete |
| Separated-domain branch and RIP-relative call | 9 | 3 | 0 / 0 / 0 | Complete, outgoing references not followed |
| Partial instruction `488b` | 2 | null | 1 / 1 / 0 | Stage listing, `LISTING_ASSEMBLY` refusal |

All five use constructed file start 32, RVA `0x2000`, preferred VMA
`0x90002000`. Their synthetic selection receipts are explicitly constructed
fixtures and cannot admit actual mode. Exactly five owner/tool calls were
used once each, with all GNU children reaped. All five real external collector
CLI exits were 0; the partial case correctly reported its enclosed refusal.
The 65 owned files (74359 B) were fully read, hashed and joined with manifests,
markers, all capture streams, reports and actual external tool results. No
version/help probes or real Python cap children were added. All five slots are
consumed and cannot be retried. The branch specimen independently checks VMA
`0x90002081` and RIP slot `0x90002018`, without opening either destination.

## Original selection and completion

The primary manifest is 4887 B, SHA256
`89e8d0faa6c02156ac7869b5f477e205bd81c0ba98a5d2ef84beda70661413c6`.
Its source commit and target SHA remain null because these immutable bytes
were admitted before preparation. It binds the new six-file selection source
set, fixed baseline, five original identities and five provenance records.
The primary owner made one verified original-image read and retained the exact
167 B target. Its owner, outer and real external transport CLI exited 0 and
were reaped; the full protocol and all output files passed readback.

The independent manifest is 4233 B, SHA256
`f0b8cc5cc976dc00002c7fe32d304a005ee4a4ea5681a6a98ddd27f052b3f2a5`.
The independent verifier rebuilt the mapping and raw caller displacement from
one separately verified full image snapshot, without invoking the subject
selector, decoding the target, launching a child or writing a second target.
Its original-image reservation and verified-read counts both equal 1. Its real
external CLI and verifier exits were 0, verifier reaping was confirmed, and
both captured streams are empty. Root completion was recorded only after the
actual tool returned cleanly.

Both results identify runtime105, section0 `.text`, original file/RVA
`[0x584C,0x58F3)`, preferred VMA `[0x18000584C,0x1800058F3)`, and SHA256
`c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1`.
The owner report is 1681 B /
`8af5d4639b4bc85de43b7485d5f586afae43dbff0b2d46981a57af57e66e0628`;
the independent report is 8598 B /
`69e922779bc4e094e01f0f3102724acfd3f5546e0b9e89d50c0d9e744ce0c850`.
Two preparation slots are now consumed, accounting for two original-image
content reads in total. Outside the two admitted selection programs, ad hoc
root/reviewer original-image content reads were zero. Actual target
disassembly and actual GNU calls remain zero.

The independent verifier has a separate 16-test pure suite, all passed. The
fixture builder has three pure tests covering five subcases, all passed. These
are separately reported, not added to the 80-test integration count. Complete
source/resource review closes C/I/M findings and bounds all five application
data envelopes; compiler/interpreter/native allocator baseline is outside
those payload calculations.

Read-only audit corrections are retained: the primary parameter review listed
short outer capture names but checked the containing directory; root pinned
the exact `tool-stdout.bin` / `tool-stderr.bin` names before execution. The
independent manifest reviewer changed only its raw tool-envelope JSON parser
to accept the tool's floating wall-time field. The synthetic readback first
expected `retry_allowed` on an owner marker; the frozen owner schema correctly
uses `attempt_reuse_allowed`. The corrected observer joined each layer's own
field. None changed subject bytes, runtime outputs or consumed-call counts.

## Publication and unchanged boundaries

The path review covers exactly 17 source-publication paths and two later
closure documents. All 52 workflow files were completely read and matched
baseline Git blobs; 37 have no push event, 14 admit only main, and one admits
this feature with exact unrelated paths. These declared feature pushes match
none. This is not permission to create a PR, modify main, dispatch/rerun a
workflow or change the reviewed path set.

Only a later full actual manifest review, exact ledger registration readback,
expected-old nonforced feature transition and root final execution admission
may open actual call `015-helper-static-1`. All 011 native incompleteness,
cost/product/IPC/canonical-evidence and qualification HOLD boundaries persist.
Native history remains 11 workflow runs / 10 compiler drivers / summed
compiler Job TotalProcesses 23 / 7 native fixtures / 0 ordinary MQB. Native
011 stays INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE: observed outer Job count
3 versus expected 2, no admitted native_summary, unknown failed API and unknown
`0xC0000142` cause. Cost DEFER remains +3.0963 ms / +26.70%; VERSION 5.6.0;
M1b/service disabled; A1–A5, deletion and ordinary qualification HOLD;
canonical_evidence_sealed=false; IPC capability lifetime and qualified
producer/writer ownership remain unresolved.

## Publication checkpoint evidence

Both completed selection slots and the exact five synthetic results are now
jointly admitted by root after their independent runtime reviews. All named
critical/important/minor counts are zero. This admission establishes the
non-null target identity and completed finite validation; it does not consume
or open the distinct actual observation.

| Record | Bytes | SHA256 |
|---|---:|---|
| `source-interface-review-015.json` | 12795 | `f0095b8dd48eea592406990969da79dde9b70a2c13995f197de1c8ed621a104c` |
| `source-resource-review-015.json` | 25071 | `cbe7b1d41b5016fcc47b898bee51581581db8e122e314d721e481eec95d91dfe` |
| `independent-verifier-source-review-015.json` | 20214 | `6f12f7ce7512065a9fc6b8f7b55e789e8e054b7b11d64c8785c8e900772486ff` |
| `independent-selection-pure-final-015.json` | 906 | `a260e833ea0e817fa025e347f24386a52067b16d8f3ae6053e48463df3beda8e` |
| `independent-selection-runtime-review-015.json` | 14720 | `c808a6b23179f8bdd06fa971874e560a4bb8c2c7a7a1cbad811067d820bf1d7b` |
| `selection-identity-admission-015.json` | 2493 | `7d51bb380949cd3c386fd5b28a1f7641a6a7cf9f1a70e2382c30eea55870abaa` |
| `gnu-synthetic-acceptance-predicates-015.json` | 19512 | `12deb762b4df533d64da877e5d9885c432a6dcea8b18a0efb13c906cc7dff719` |
| `synthetic-external-exec-results-015.json` | 16566 | `d7a291893f72bac8ea45d33a21f26cc253f060c4332c129075955698d7cafa07` |
| `synthetic-root-readback-015.json` | 17828 | `0327b52f56ac70583ca0b7a557e3e93ea65a07410ea5a166c393dc63688f2d55` |
| `synthetic-runtime-review-015.json` | 28621 | `96b2b713d65ff78cc05497b5fa870245a813eb833becc09ab59693bb52ab2ce6` |
| `synthetic-results-admission-015.json` | 3278 | `171f20962e53c61031f84e38e06d885ebe301e74b196989ccfd917f61d7c476f` |
| `workflow-path-review-015.json` | 64090 | `686f6a1fea7f2d77a380260390f16ee1236fddc1226f06e87a8499fad4aa03b0` |
| `workflow-path-independent-review-015.json` | 62497 | `7a0ebcfec541fb40d53e14950334d243cf0261fcc20fe50faecb8379795d6387` |
| `source-prepublication-scope-015.json` | 290557 | `eecd95817cf2fc489a77a91aa5bc65e9601d29ddcf15f2d383d741db069cd0b8` |
| `source-prepublication-scope-review-015.json` | 1779 | `9737d6a7c48cd03d0efb34656a73956814f0f5714e7d9d88886602eb037576be` |

The fresh prepublication scope exactly matches every retained initial-015
endpoint value: feature remains the 014 closure, main and the three open draft
PRs are unchanged, feature PR count is zero, all 11 recorded workflow runs
match and #198 has 352 comments. Those observations are point-in-time checks;
the registration and each later feature transition require their own fresh
guards and complete readbacks.

Independent synthetic review retains its initial SDK-envelope key-order
assumption failure and corrected boundary: external tool metadata keeps its
original insertion order and finite wall-time field; all subject protocols
retain their strict canonical rules. Root also retains an initial review-join
field-name error: resource review uses `open_findings` with full severity
names, and verifier source review uses `C_I_M` with `_open` suffixes. The final
readback checks each sealed schema explicitly. These were read-only observer
corrections, with no subject rerun, output rewrite or allowance change.
