# Selection and execution resource contract 016

## Authority and exact candidate

This new contract follows the user's continuation of the published 016
handoff. Baseline ff30a122e92b1b8fef645111ddf277aae065c275;
tree c7fc8dc750996797238a11f68654720adb752cb1. All 015 slots remain consumed.

Target: runtime252 / section0 .text, file/RVA [0xDB3C,0xDB8F), exactly83B,
preferred VMA [0x18000DB3C,0x18000DB8F), digest initially null.
Child pdata file0x22BD0, unwind RVA/file0x1E61C,16B:
210000004c580000f358000078dd0100.
Version1 flags4 prolog0 slots0 frame0/0, no handler, chain depth2.
Embedded tuple at file0x1E620: begin0x584C,end0x58F3,unwind0x1DD78.
Parent runtime105 pdata file0x224EC, unwind RVA/file0x1DD78,12B:
010a04000a3406000a320670; version1 flags0 prolog10 slots4 frame0/0,
no handler, no next chain, depth1.

Validate logical tuple and file/RVA/section mappings independently; the
embedded table offset and parent pdata offset must differ as specified.
Reject duplicate/missing rows, wrong tuple/flags/handler/depth/cycle/extra
level/frame/code fields, wrong section/range and unmapped/truncated metadata.
From the same verified snapshot bind child pdata12 + child unwind16 +
parent pdata12 + parent unwind12 =52B, then select exactly83B.
There is no additional original-image content open for these memory slices.

015 caller report is6740B /
5c55e875110e732068f3695d35504f40e7ae7f4e7bf32de4a7bf10b18f3a1e9b.
Verify source RVA/raw signed-rel32 pairs:
5883/0f88b3820000 -> DB3C,
5897/0f85bc820000 -> DB59,
58B4/0f85a7820000 -> DB61,
58CC/0f8597820000 -> DB69.
A full actual83B listing must establish all offsets0,29,37,45 as starts.

## Immutable data and provenance

Original image170984B /
663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb.
Index271920B /
ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e.
Index admission20232B /
c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b.
All under /workspace/scratch/a8b3faf101c5 at inherited exact paths.

Runtime provenance has exactly5 roles:
manifest0158513B, runtime01524444B, closure01514819B,
result01515931B, handoff01613792B; total77499B.
Each immutable size/SHA/path is pinned in new policy. Outside package receipts
belong to the prerequisite gate and are not oversized runtime provenance.
Preserve canonical Python/GNU/eight dependencies/loader-cache identities,
the9 actual aliases and5 absent paths from the exact015 manifest.
ENV exactly LANG=C, LC_ALL=C, TZ=UTC, DEBUGINFOD_URLS=empty.
No download or version/help probe is allocated.

## Separately consumed slots

Primary016-selection-primary-1: one complete image snapshot, one83B target
file under evidence-016/selection-016; output names/caps consumed.json1024,
target.bin83, report.json8192 =>9299B.
Independent016-selection-independent-1: one complete image snapshot, zero
additional target files; output evidence-016/selection-independent-016,
consumed.json1024 and review.json<=16384 =>17408B.
Each computes its own digest. The admitted candidate digest remains null until
both real clean containing completions and the independent result are joined.
The independent verifier compares all83B to the retained primary target and
does not invoke or import the subject selector for verification.

Actual016-chain-static-1: at most one GNU child and one root submission.
Before/after phases each verify the complete original image once.
Thus this new contract's maximum is primary1 + independent1 + actual2 =
4 full image passes /683936 declared image-content bytes.
Root/reviewer ad hoc original-image reads=0, neighboring byte reads=0,
new constructed GNU=0, new real cap children=0. Original-image slots cannot
be replaced, retried at another path or reused after setup/persistence failure.
Read retained target bytes only within separately bounded preparation
readback, actual verification and result review; that is not another image
content pass. Do not recursively follow any outgoing target.

## Source, IO, process and application-payload gate

Selection source set6, actual set7, old014 utilities frozen. New outer is
separately pinned; every source and manifest<=32768B. All sources are verified
before any core compile and executed from those bytes. Independent verifier
is separately pinned with all its dependency bytes verified before compile.
Any necessary independent helper must be declared, source-set checked and
recounted before either preparation slot; no implicit source is permitted.

Hash/read chunk<=65536B; include coexisting old/new chunks131072B.
Each full declared-size pass permits one EOF-detection request<=1B;
consistent content returns0 extra bytes. Unexpected content refuses/consumes.
Use canonical O_NOFOLLOW regular files with exact size/hash and stable
before/after FD and matching path snapshots. Save exact integer stat fields.
No original-image reopen for selecting from a previously hashed identity.

Selection owner/outer/transport ceilings8/12/15s; actual12/20/23s;
independent verifier12s, containing transport15s. All include success
persistence, complete readback, close and final deadline. Cleanup has finite
wait/reaping and cannot turn late completion into success. Owner stdout0,
diagnostic<=65B; outer transport stdout<=1024B/stderr<=65B, separate detection.
Root transport input metadata contains exactly schema, call_id and
manifest_sha256, each a bounded ASCII string. The external SDK result with
chunk/exit/timing fields is separate. Root retains exact stdout/stderr and
the real clean containing exit before any admission.

Actual outputs: consumed1024,target83,GNU stdout131072,stderr131072,
report262144 =>525395B. GNU captures must be strictly below cap and successful
stderr empty. Each instruction1..15B, count<=83. Six-field record<=937B,
wrapper<=16384: 83*937+82+2+16384=94239B maximum encoded listing/report bound.
Producer hard=soft limits CPU2s,AS268435456,FSIZE131072,CORE0,NOFILE64,
each read back before exec; inherited default SIGCHLD required.

Numerical proof before image read must account for each source, identity,
provenance, manifest, decoded object graph, retained input, listing,
serialization buffer, wrappers/diagnostics/readback and output namespace.
The admitted index has26721 nodes/171892 characters =>5818000B at
192B/node+4B/character. Recompute every other graph and all complete content
passes from new sources; do not inherit a previous total without proof.
Application payload terms exclude interpreter/compiler/stdlib/native allocator
and OS baseline. They are not RSS caps or host-wide sandbox claims.

For the unchanged6/7 source architecture only, selection owner33 full identity
passes plus bootstrap7 and outer8 =48; actual owner60 plus bootstrap8 and
outer9 =77. These count templates require final source inspection and exact
byte substitution before admission. Independent verifier and root preflight
have their own separate finite full-pass inventory and final numerical proof.

## Pure validation and publication gates

Write meaningful failing pure tests before implementing changed behavior.
No pure invocation may discover native fixtures. Cover all chain/snapshot/
caller/source/consumption/entry-start changes and affected lifecycle gates.
The unchanged GNU decoding/listing mechanism has retained015 constructed
evidence; no new constructed GNU child is required for metadata-policy and
entry-start checks. Independent source/resource reviewers must confirm this.

Before primary or independent launch, freeze each exact canonical manifest,
argv, executing source identities, full bounded output/transport set, finite
read-count proof and actual source review. A report alone is not a completion.
Before actual launch additionally require immutable published source, full
blob/tree/commit readback, canonical manifest independent admission, exact
Issue198 registration readback, fresh nonforced expected-old activation and
final unused-output/source/environment checks.

Native011 remains INCONCLUSIVE/NATIVE_EXECUTION_INCOMPLETE, counters
11/10/23/7/0, cause0xC0000142 and failed API unknown. Cost DEFER
+3.0963ms/+26.70%, VERSION5.6.0, M1b/service disabled, A1-A5/deletion/ordinary
qualification HOLD, canonical_evidence_sealed=false. No Windows module load/
execution, native fixture, workflow dispatch/rerun or product change is allowed.
