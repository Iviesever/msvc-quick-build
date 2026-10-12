# Native startup 016: complete CHAININFO range result

## Decision and delivered boundary

The single registered offline observation `016-chain-static-1` completed. Root, independent byte/semantic review, and independent runtime/resource review admit exactly 83 bytes and 18 instructions. Open findings for this 016 static observation are C0 / I0 / M0. The target is runtime-function record 252, section 0 `.text`, file/RVA `[0xDB3C,0xDB8F)`, preferred VMA `[0x18000DB3C,0x18000DB8F)`, SHA256 `ff762eb73a125811551bfd8cfeb3b7cf68f26607d045d78311f50d247250df32`.

This closes the four previously unresolved incoming instruction starts from 015 and the visible connection back to its status-return epilogue. Calls outside the observed ranges retain their unknown behavior. The failed API and the cause of the recorded native `0xC0000142` remain unresolved.

Immutable observation source: `51bbce58ae37b36521be6425de55e784e8ceee72`; tree `6cd03acc902a260fb24e9a91414943df035b26f4`; sole parent/final015 baseline `ff30a122e92b1b8fef645111ddf277aae065c275`. Exact pre-execution registration: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6129084740 . The later closing commit and final branch identity are recorded by the closing ledger and outside package receipts; this document cannot contain its own future commit hash.

## Selection and CHAININFO identity

The two preparation slots each completed once. Each independently verified its complete original-image identity before using its own retained snapshot. The selected 83 bytes matched in full and were jointly admitted before the actual manifest was constructed.

The child `.pdata` record at `0x22BD0` points to unwind record `0x1E61C`, flags 4, depth 2. Its exact 16-byte unwind material is `210000004c580000f358000078dd0100`. The embedded parent tuple at `0x1E620` is `(0x584C,0x58F3,0x1DD78)`. It matches runtime-function 105 at `.pdata` `0x224EC`; that parent's 12-byte unwind material at `0x1DD78` is `010a04000a3406000a320670`, flags 0, depth 1. The relation compares the logical tuple and mappings: the embedded record and parent table row have different physical offsets. The two preparations each bound these 52 metadata bytes without a partial image read.

## Complete observed instructions

The GNU binary-input pseudo-section label does not replace the admitted original PE section mapping. File offset, RVA and preferred VMA below are separately checked for every instruction. Concatenating every exact byte field gives the full 83-byte target hash above; no gaps, overlap, undecoded tail or neighboring bytes are admitted.

| RVA / file offset | Preferred VMA | Length | Exact bytes | GNU Intel text |
|---|---|---:|---|---|
| `0xDB3C` | `0x18000DB3C` | 6 | `41b988070000` | `mov    r9d,0x788` |
| `0xDB42` | `0x18000DB42` | 7 | `4c8d0597e80000` | `lea    r8,[rip+0xe897]        # 0x18001c3e0` |
| `0xDB49` | `0x18000DB49` | 7 | `488d15c8e80000` | `lea    rdx,[rip+0xe8c8]        # 0x18001c418` |
| `0xDB50` | `0x18000DB50` | 2 | `8bcf` | `mov    ecx,edi` |
| `0xDB52` | `0x18000DB52` | 5 | `e8297bffff` | `call   0x180005680` |
| `0xDB57` | `0x18000DB57` | 2 | `8bdf` | `mov    ebx,edi` |
| `0xDB59` | `0x18000DB59` | 6 | `41b97c000000` | `mov    r9d,0x7c` |
| `0xDB5F` | `0x18000DB5F` | 2 | `eb0e` | `jmp    0x18000db6f` |
| `0xDB61` | `0x18000DB61` | 6 | `41b985000000` | `mov    r9d,0x85` |
| `0xDB67` | `0x18000DB67` | 2 | `eb06` | `jmp    0x18000db6f` |
| `0xDB69` | `0x18000DB69` | 6 | `41b98e000000` | `mov    r9d,0x8e` |
| `0xDB6F` | `0x18000DB6F` | 7 | `488d1546e40000` | `lea    rdx,[rip+0xe446]        # 0x18001bfbc` |
| `0xDB76` | `0x18000DB76` | 2 | `8bcf` | `mov    ecx,edi` |
| `0xDB78` | `0x18000DB78` | 7 | `4c8d0591e70000` | `lea    r8,[rip+0xe791]        # 0x18001c310` |
| `0xDB7F` | `0x18000DB7F` | 5 | `e8fc7affff` | `call   0x180005680` |
| `0xDB84` | `0x18000DB84` | 5 | `e8f3350000` | `call   0x18001117c` |
| `0xDB89` | `0x18000DB89` | 1 | `90` | `nop` |
| `0xDB8A` | `0x18000DB8A` | 5 | `e9567dffff` | `jmp    0x1800058e5` |

The count is 7 MOV, 4 LEA, 3 direct CALL, 3 unconditional JMP and 1 NOP. This range contains no conditional jump or RET. The four LEAs materialize addresses `0x1C3E0`, `0x1C418`, `0x1BFBC` and `0x1C310`; their contents were not read and no string or API meaning is assigned to them.

## Four incoming paths and their common tail

| Already admitted 015 edge | 016 entry offset | Visible distinguishing operations |
|---|---:|---|
| `0x5883: 0f88b3820000` -> `0xDB3C` | 0 | R9D=`0x788`, two LEA addresses, ECX=EDI, CALL `0x5680`, then EBX=EDI and continuation at `0xDB59` |
| `0x5897: 0f85bc8200000` -> `0xDB59` | 29 | R9D=`0x7C`; short JMP to `0xDB6F` |
| `0x58B4: 0f85a7820000` -> `0xDB61` | 37 | R9D=`0x85`; short JMP to `0xDB6F` |
| `0x58CC: 0f8597820000` -> `0xDB69` | 45 | R9D=`0x8E`; falls through to `0xDB6F` |

All four destinations are independently verified instruction starts. The common tail materializes RDX=`0x18001BFBC` and R8=`0x18001C310`, copies EDI to ECX, calls `0x5680`, calls `0x1117C`, executes NOP, then jumps from `0xDB8A` to already admitted `0x58E5`. No argument reset occurs between the two common calls, so the argument values visible before CALL `0x5680` cannot be assigned to CALL `0x1117C`: the volatile argument registers may have changed.

| Direct control transfer in 016 | Signed displacement | Destination |
|---|---:|---|
| CALL at `0xDB52`, next `0xDB57` | -34007 | `0x5680` |
| JMP at `0xDB5F`, next `0xDB61` | 14 | `0xDB6F` |
| JMP at `0xDB67`, next `0xDB69` | 6 | `0xDB6F` |
| CALL at `0xDB7F`, next `0xDB84` | -34052 | `0x5680` |
| CALL at `0xDB84`, next `0xDB89` | 13811 | `0x1117C` |
| JMP at `0xDB8A`, next `0xDB8F` | -33450 | `0x58E5` |

### Conditional status-return interpretation

At 015 `0x587F`, EDI receives EAX from CALL `0x8A98`; TEST EAX at `0x5881` precedes JS `0x5883`. Thus the DB3C entry is not an unconditional zero-EDI path. Its visible MOV EBX,EDI follows the first external CALL. The `0xDB59` entry is also used for the separate nonzero EDI test at `0x5895`; that path has copied EDI to EBX at `0x5893`. The `0xDB61` and `0xDB69` entries follow nonzero EAX results copied into both EBX and EDI after calls at `0x58A9` and `0x58C1`.

If the external calls return normally, preserve RDI/RBX as required, and preserve the parent frame and stack, the visible jump reaches `0x58E5: MOV EAX,EBX` and the already observed restore/RET sequence. This connection bypasses the `0x58D9` write of 1 to `0x21740` and the `0x58E3` XOR EBX,EBX. It does not prove that all paths actually return, what the outside callees do, which path the earlier native run took, or which API failed. On that qualified path, the old 014 caller at A617 tests the returned nonzero EAX and takes its nonzero continuation; only if its subsequent CALL to 5680 returns normally does its visible XOR EAX,EAX and epilogue return zero. This remains a conditional static relation, as detailed in the independent byte review.

## Actual execution and finite resource closure

The real root CLI completed with `02ca7b / exit 0`. Its complete canonical five-field summary is retained. The subsequent completion observer itself completed with `c68b54 / exit 0`. GNU, owner, outer and root CLI all have strict integer-zero completion and direct-child reaping; no controller or transport fault is present. Final close, persistence, full readback and deadline obligations are joined to the exact reviewed successful source paths.

| Quantity | Admitted actual value / bound |
|---|---|
| Preparation slots | Primary 1, independent 1; both consumed |
| Actual slot / GNU children | 1 / 1; permanently consumed; retries 0 |
| Constructed GNU or real cap-test children added in 016 | 0 / 0 |
| Original image complete passes | 2 preparation + 2 actual = 4; 683,936 declared-content bytes |
| Actual runtime identity work | 77 complete passes / 75,890,258 declared-content bytes |
| Root adapter preflight | 14 complete passes / 31,040,136 bytes |
| Final gate | 13,982 B; N565 / C12084 / depth3; graph payload156,816 B |
| Owner / outer / transport / adapter deadline | 12 / 20 / 23 / 28 seconds |
| GNU CPU / AS / FSIZE / CORE / NOFILE | 2 s / 268,435,456 B / 131,072 B / 0 / 64; hard=soft |
| Actual owner5 / outer4 / transport4 named contents | 5,603 / 1,415 / 2,260 B; 13 files / 9,278 B |
| GNU stdout / stderr | 1,635 B / 0 B |
| Owner stdout / stderr | 0 B / 0 B |
| Transport stdout / stderr; root stdout | 106 B / 0 B; 154 B |
| Completion observer | 26 inputs154,725 B + one7,706 B readback = 27 / 162,431 B |

Elapsed values recorded before persistence were owner196,573,408 ns, outer242,711,250 ns and transport271,031,041 ns. They are stage observations; the SDK wait-time field is not a native startup benchmark. Application-data bounds remain owner87,368,927 B, outer47,883,505 B, transport3,890,512 B and root adapter plus coexisting transport37,509,514 B. These bounds exclude allocator/interpreter/OS baseline and are not RSS measurements.

The tighter fixed-report/maximum-manifest runtime ceiling is75,987,274 B; the broader schema ceiling is76,015,464 B. Actual values remain below both. Independent byte review opened the new target and listing exactly once each and reconstructed them from retained memory thereafter. Independent runtime review used target stat and the byte review's digest, without opening the target or original image. No outgoing code or LEA destination was opened for this result.

## Validation and retained corrections

The current 016 record contains186 distinct pure test methods:177 in the frozen source-stage validation,4 in the actual-manifest builder,2 in the publication metadata boundary fix, and3 in the completion observer. RED runs and repeat GREEN invocations are not counted as new methods. The actual observation and completion observer are real execution evidence, separate from pure tests.

Historical source/metadata corrections and failed audit or collector invocations remain in the package. The earlier local collector stopped on four extra EOF blank lines after creating local blobs and a private index; the original failed index and original document bodies remain retained. The corrected collector and complete publication readback passed before registration. The metadata-only corrections did not reopen either preparation slot or the actual slot.

## Preserved native and product state

011 remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`: observed outer JobTotalProcesses3 versus expected2, native_summary unadmitted, failed API unknown, cause of0xC0000142 unknown. Native counters remain11 workflow /10 compiler-driver /23 summed Job /7 fixture /0 ordinary MQB. Cost remains DEFER (+3.0963 ms /+26.70%); VERSION5.6.0; M1b and service remain disabled; A1-A5, deletion and ordinary qualification remain HOLD; canonical_evidence_sealed=false. IPC capability lifetime and qualified producer/writer ownership remain unresolved.

Main and existing PR250/248/207 retain their admitted state. The two authorized feature activations and two Issue198 comments are recorded by the final closure receipt. There was no Windows module loading, native rerun, workflow dispatch, new PR, merge, release, product edit or deletion in this 016 result.

## 017 handoff

Continue with the separately documented metadata candidate and the fresh final016 baseline in `native_startup_handoff_017.md`. Candidate bytes remain unread, candidate SHA256 is null, and the next actual execution has not been admitted.

## Complete direct evidence identities

| Complete local evidence path | Bytes | SHA256 |
|---|---:|---|
| `/workspace/scratch/a8b3faf101c5/evidence-016/runtime-review-admission-016.json` | 17743 | `c5ce6f84cefa7963d36e0cb835b6e8c19e10421239ae5af0e6f8b23f6e3f52ae` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-byte-semantic-review-016.json` | 27568 | `33acc0ff6c5f94d5c6698fc1e04da4fffc7a978cc84386cc56f45b583f675650` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-runtime-resource-review-016.json` | 14986 | `0357313ebff2dde0fa82521d0ca49334461c5415f731f95dfbd9a502378577b5` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/next-target-candidate-017.json` | 8180 | `39ba3c6010c14777508edea602c3f4186e730127565e12dcaba2de713a3911bb` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/invocation-manifest-016.json` | 8514 | `c484d5269c8805834fd542751e637266ae18ca9e5978a28f92a84d834b733a65` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-execution-plan-016.json` | 12261 | `da9148d1e7e04661b382eb099f681d18e82a1aff424acd69b96970286689c447` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-execution-admission-016.json` | 13982 | `0e68239c48c8a99ac8e7b24bcf8200e10a651824ab2ccbfb8b256233ab00f1fa` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-final-gate-parameter-review-016.json` | 5873 | `365dd32ca6856e08bb953f60ab45e421dd788c702c5e0b85a18a5c25dc747f44` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/root-final-execution-admission-source-review-016.json` | 7261 | `0dc7d3a4d2fe633af213ca602ee8089122cc210c465dc6d6d6a5065e80494093` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-external-exec-result-016.json` | 338 | `3feb2a41d8161e49c13c95e0f3c174006e5353be320537c65f5460db86fdefa9` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-transport-completion-016.json` | 7706 | `6f406a95ccb1306ed0f1fcd81d9f89cc28ed780148a82ec67f5b17fe827b709e` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/actual-completion-observer-external-exec-016.json` | 510 | `0007d3510d77485bd8b199dcaefe207c453bfd029738c1da8ea6518cf94dc3a0` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/offline-016/report.json` | 3583 | `8a8513ad908a2db62f816639c0a08ed6b6d2413fde181ea8aab39937faec951c` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/selection-identity-admission-016.json` | 10097 | `ae3b9063b6e26c424802b897f733fdb1816f9afc6d897db81af03a1fedf5302f` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/source-publication-readback-016.json` | 5102 | `f1d1e70d009850624bfc3fb55685c12fa2c1b0134c7248d59debaf64d4cf0d64` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/source-publication-independent-review-016.json` | 4510 | `d7718de73783869950083e9dabad2f133dc8afbcdddfcd3d493c54f753f1fc47` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/registration-readback-016.json` | 2091 | `8d1d5d9e4fd037d089f14fe18f91060058e6a6bda6feb8450f75040f734baba5` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/source-activation-readback-016.json` | 194894 | `773e99b05590e63e4e49751c93208d839908fcc54b3b280a1ccc6af59e25ac38` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/registration-activation-scope-independent-review-016.json` | 7084 | `82aef5dbc65eb41822d43da063f9e65128ea2b2773ad895405be28bfca2fa08a` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/independent-design-actual-validation-016.json` | 30057 | `721964ece55d8a6b78937633a18ae1ff48d383e0b2d8b3e110e1b486f2f2428a` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/publication-input-fix-validation-016.json` | 1894 | `3566e956a17a810aabe7f2750b35902e21601b0171ae8ec8c14216eaf9747f90` |
| `/workspace/scratch/a8b3faf101c5/evidence-016/independent-design-actual-completion-validation-016.json` | 14090 | `af1b213a7bec15c69ca754a93b7e3bd83d4f1e7485a90f6af30cb3b9491b5ea5` |
| `/workspace/scratch/3d56575ba9f2/mqb-work/producer-clean-handoff/docs/20261012-233920-bcrypt-chain-016/validation_016.md` | 13272 | `06bbfae80efcccff076080e39d119de47d2dbbf5a659d6ea7df82318358d9af5` |
