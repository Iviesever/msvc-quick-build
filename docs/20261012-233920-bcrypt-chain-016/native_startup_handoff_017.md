# Native startup 017 handoff from completed 016

## Takeover point

016 has completed its one registered actual observation and independent result admission:83 B /18 instructions at `[0xDB3C,0xDB8F)`. Its source commit is `51bbce58ae37b36521be6425de55e784e8ceee72` and source tree `6cd03acc902a260fb24e9a91414943df035b26f4`; its previous completed baseline was `ff30a122e92b1b8fef645111ddf277aae065c275`. Use the later final016 closing commit, established by the exact closing Issue198 ledger and the independent/root outside package receipts, as the next expected-old feature and local baseline. This source commit identifies the executed code; the final closing commit also includes both016 result documents.

The four incoming destinations at relative offsets0,29,37,45 are exact instruction starts. They converge at `0xDB6F`; the common path calls `0x5680`, then `0x1117C`, and its visible final JMP goes to already observed015 `0x58E5`. The status-return connection is conditional on the outside calls returning, preserving RDI/RBX as required, and preserving the parent frame and stack. Their unobserved behavior is the remaining question that motivates the next candidate.

## Exact metadata candidate

The following is a proposal derived from admitted instructions and the fixed013 metadata. It is not a selected-byte or execution admission. Its full proposal evidence is included among the direct identities below.

```json
{
  "bytes": 103,
  "candidate_bytes_read": 0,
  "decision": "PROPOSE_017_METADATA_CANDIDATE_ONLY",
  "execution_admitted": false,
  "final016_baseline_binding": "Use final closing commit from final-closure-admission-016.json; not the source-only commit.",
  "full_index_metadata_ref": {
    "bytes": 271920,
    "path": "/workspace/scratch/a8b3faf101c5/evidence-013/offline-013/report.json",
    "sha256": "ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e"
  },
  "preferred_vma_end": 6442521059,
  "proposal_ref": {
    "bytes": 8180,
    "path": "/workspace/scratch/a8b3faf101c5/evidence-016/next-target-candidate-017.json",
    "sha256": "39ba3c6010c14777508edea602c3f4186e730127565e12dcaba2de713a3911bb"
  },
  "qualifications": [
    "Candidate bytes and SHA256 are unknown.",
    "No instruction, string, symbol, import, API name, return guarantee or native startup result has been inferred for 1117C.",
    "No LEA-pointed data or unread callee body was opened.",
    "A complete indexed extent alone may still leave outgoing-call behavior unresolved after the next static review."
  ],
  "rationale": [
    "All four verified 016 entry paths converge on DB6F. If their preceding calls return, the common sequence calls 5680 at DB7F and then makes the final direct call to 1117C at DB84.",
    "The admitted NOP at DB89 and JMP at DB8A to the known 015 epilogue are reachable from this sequence only if 1117C returns. Its return behavior is an explicit uncertainty in the completed 016 byte review.",
    "013 metadata supplies one complete executable runtime-function extent beginning exactly at that call destination: row 302, 103 bytes, with one version-1 flags-0 depth-1 unwind record.",
    "Examining that one bounded extent in a separately admitted 017 round can narrow this specific remaining uncertainty and reveal any further unresolved edges. It does not establish that the other unread callee 5680 returns."
  ],
  "required_new_geometry_cap_bytes": 103,
  "runtime_function": {
    "begin_file_offset": 70012,
    "begin_rva": 70012,
    "end_file_offset": 70115,
    "end_rva": 70115,
    "index": 302,
    "section_index": 0,
    "section_name": ".text",
    "table_entry_file_offset": 142888,
    "unwind_info_rva": 122188
  },
  "selection_identity_admitted": false,
  "source_call": {
    "assembly": "call 0x18001117c",
    "destination_preferred_vma": 6442520956,
    "destination_rva": 70012,
    "file_offset": 56196,
    "length": 5,
    "next_rva": 56201,
    "preferred_vma": 6442507140,
    "raw_hex": "e8f3350000",
    "rva": 56196,
    "signed_rel32": 13811
  },
  "target": {
    "file_end": 70115,
    "file_start": 70012,
    "preferred_vma_start": 6442520956,
    "runtime_function_index": 302,
    "rva_end": 70115,
    "rva_start": 70012,
    "section_index": 0,
    "sha256": null
  },
  "unwind": {
    "bytes": 8,
    "chain": null,
    "chain_depth": 1,
    "code_slots": 1,
    "file_offset": 122188,
    "flags": 0,
    "frame_offset": 0,
    "frame_register": 0,
    "handler_file_offset": null,
    "handler_rva": null,
    "prolog_size": 4,
    "raw_hex": "0104010004420000",
    "rva": 122188,
    "version": 1
  }
}
```

The candidate SHA remains null. Neither its bytes nor any neighboring code or referenced data has been opened in016. The proposal's exact edge and range must be revalidated in017 under a new written acceptance/resource contract before any original-image access. If its cited metadata or range conditions fail, record the refusal and design a new bounded step; do not silently widen the interval or substitute an adjacent address.

## Required next sequence

1. Reconcile the final016 source and closing commits, full Connector trees/blob bodies, exact registration and closing comments, all declared feature/main/PR/Actions/ledger API values, and local HEAD/index/complete working-file bytes and modes. Every snapshot is a point-in-time observation, not a cross-resource atomic transaction. Preserve any unexpected change for review.
2. Write the017 acceptance contract around the exact candidate above. Keep source/selection/execution gates separate. Bind the fixed image, original013 index, newly completed016 runtime result and final closure as provenance. Specify the target geometry, instruction-start/whole-range requirements and every unknown that remains outside the candidate.
3. Derive a fresh finite resource proof for that geometry and actual source set. Include declared content passes, identity/parser caps, object-graph payload, source verification, argv/environment, loader cache/dependencies, aliases and absence checks, output names/caps, complete persistence and containing-exit requirements. Recompute deltas rather than copying the01683-byte constants.
4. Retain pure distinguishing RED/GREEN evidence for changed selection, decoding or boundary behavior and get independent source/resource review. Reuse already frozen mechanisms only with their complete byte identities and explicit unchanged assumptions. Existing real GNU or cap-test evidence does not automatically allocate new probes.
5. Execute only the separately admitted primary and independent selection steps, each from its own single complete verified image snapshot. Obtain an exact full-byte target identity and independently join its metadata and incoming call/branch boundary. Do not derive a target digest by reading the current candidate in advance.
6. Publish the exact source additions through Connector and read every new blob body and the complete tree/commit back. Register the complete canonical actual manifest and exact bounded argv before source activation. Use force=false and the fresh expected-old feature; then verify full remote scope and guarded local activation.
7. Freeze and independently measure the final one-attempt execution gate. Use only its pinned interpreter/adapter/outer/owner/GNU chain. A setup error, timeout, cap, refusal, interruption, crash or late close consumes the root attempt. No renamed-output retry, alternate decoder or wider range follows automatically.
8. After the actual containing zero exit, independently admit complete target/listing bytes and runtime/resource evidence. Then publish the017 result and next handoff, close the exact ledger, activate the closing commit, and verify the new package before delivery.

## What016 establishes and what remains open

The complete18-instruction range contains7 MOV,4 LEA,3 CALL,3 JMP and1 NOP. Two CALL sites target `0x5680`; one targets `0x1117C`. The two internal short jumps reach `0xDB6F`; the external final jump reaches `0x58E5`. The four RIP-relative LEAs only establish address values. No names or contents are assigned to their targets.

The original015 negative path copies EAX to EDI before its sign test. Other incoming paths preserve the relevant nonzero result through EBX/EDI in the visible code. On the qualified normal-return path, the016 tail bypasses the015 completion-flag write at `0x58D9` and the zeroing at `0x58E3`, reaching MOV EAX,EBX and restore/RET. This still does not identify the earlier failed native API, prove any particular call returned, or connect a static path to the actual011 failure trace. Preserve the independent byte review's full qualifications when extending the argument.

The next contract must explicitly state whether it is checking an outside call's visible return behavior, a status transformation, an imported thunk or another admitted metadata fact. Structural metadata alone is insufficient to claim a callee's body, API name, runtime branch, exception behavior or status value.

## Exhausted016 resources and inherited controls

Both preparation slots and the single actual slot are permanently consumed. The current record has four original-image complete passes totaling683,936 declared-content bytes, one actual GNU invocation, zero new constructed GNU invocations and zero real cap-test children. The final actual root preflight was14 /31,040,136 B; runtime was77 /75,890,258 B. The13 named runtime contents total9,278 B. The two success SDKs are `02ca7b /0` and `c68b54 /0`. All subsequent017 resources require their own contract; the016 markers, manifests, target, captures and reviews remain immutable.

The186 distinct pure methods and complete RED/correction history are retained. Runtime files preserve their checked0600 or0400 modes. Package construction preserves all016 evidence, including failed local audit/collector records and private indexes, plus the exact19 published files. It includes the prior015 ZIP as one opaque member (4,482,736 B; SHA256 `ff57f86157e674d2dd9b06bf4da97b16a8c4dc35e249bfa1a6511dfe66993a78`), without opening its nested contents. Outside receipts authenticate the new package manifest and every member.

011 remains INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE. The failure API and0xC0000142 cause remain unknown; JobTotalProcesses3 versus expected2 remains unresolved and native_summary remains unadmitted. Preserve11 workflow /10 compiler-driver /23 summed Job /7 fixture /0 ordinary MQB; VERSION5.6.0; cost DEFER (+3.0963 ms /+26.70%); M1b/service disabled; A1-A5/deletion/ordinary qualification HOLD; canonical_evidence_sealed=false. IPC lifetime and qualified producer/writer ownership remain open.

The next static proposal grants no Windows module load, native fixture, workflow dispatch/rerun, new PR, merge, release, product change or deletion. Existing main and PR250/248/207 anchors must be reconciled from the final016 scope before any next write. Continue on the isolated `feat/producer-clean-handoff-20261011` worktree and use Connector for the authorized repository/ledger writes.

## Direct identities to verify at takeover

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
