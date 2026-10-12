# 014 result: bounded static init-function observation

Status: `COMPLETE_STATIC_DISASSEMBLY_INDEPENDENTLY_REVIEWED`.
The single registered `014-init-static-1` attempt is consumed. The retained GNU
listing and owner report cover **85 instruction records and exactly 391 B**.
The independent byte/interpretation and resource/lifecycle reviews, both
separate-process full readbacks and root's joint result admission have passed
with zero open Critical, Important or Minor findings. Final evidence identities
are in Section 6. Git closure publication and final remote scope/readback
remain root's pending completion steps.

## 1. Immutable source, registration and closure identity

The actual invocation source is commit
`4beac41f341f4cc8f3ddf02471b17d113ecb39f8`, tree
`7a49f6c4d34566863ac4d2030121315f64c651c6`, with sole parent
`02b6fae17ee36f0011b7bae0f9102478b76ba42c`. It introduced fourteen files:
five implementation modules, five tests and four 014 documents, 149,769 B.
All 799 prior blobs and modes were preserved; the source tree has 813 blobs.
The implementation's frozen design and test evidence are in
[validation_014.md](validation_014.md),
[resource_contract_014.md](resource_contract_014.md) and
[implementation_plan.md](implementation_plan.md).

[The complete registration in #198](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6120286848)
is 20,243 B, SHA256
`5bc516d10c7ba6c23f06d124dd91c3b2e03b88b32311e395ebb1b0351a0ccc9e`.
It contains the complete canonical 6,149 B manifest, whose SHA256 is
`0f14b621af601f26fec3024d7e10fe4a79523033e1d9b44c5d0cb10ce780b288`.
The complete comment and manifest were read back before the expected-old,
nonforced feature transition from `02b6fae17ee36f0011b7bae0f9102478b76ba42c`
to the invocation source. The pre-execution joint gate R2 binds that sequence;
R7 preserves the transition responses. Historical `PENDING` fields in earlier
snapshots are not rewritten when a later joint record closes their gates.

This result and [the 015 handoff](native_startup_handoff_015.md) are the only
planned closure additions. Their identity is the **containing closure commit**,
resolved from Git and the subsequent #198 closure anchor. A commit cannot
include its own final SHA in these files. The invocation source above remains
the identity for the executed code, even after the documentation-only closure
advances the feature head.

## 2. Exactly what was observed

| Property | Value |
|---|---|
| Runtime-function record | 169 |
| Original PE section | index 0, `.text` |
| Original-file interval, end exclusive | `[0xA590, 0xA717)` |
| RVA interval, end exclusive | `[0xA590, 0xA717)` |
| Preferred ImageBase | `0x180000000` |
| Display/preferred VMA interval | `[0x18000A590, 0x18000A717)` |
| Target length and SHA256 | 391 B; `e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44` |
| Decoded records | 85, complete continuous coverage in all three address domains |
| Calls | 14 sites: 10 direct sites to 9 distinct targets, 4 RIP-relative indirect sites |
| Branches | 11 sites: 9 conditional, 2 unconditional; all destinations are instruction starts inside this interval |
| Largest instruction / raw line including LF / assembly | 11 B / 120 B / 56 printable ASCII characters |

GNU received only the derived `target.bin`. It received no original PE pathname
and followed no outgoing code or data target. Its binary-backend heading
`Disassembly of section .data` is a raw-input display label. It does not rename
or reclassify original PE section 0 `.text`. File offsets and RVAs happen to
coincide here; the verified mapping, not a general equality rule, supplies the
addresses. Preferred VMA is not the relocated native process address.

The full runtime extent includes `ret` at RVA `0xA715` and the final byte `cc`,
decoded as `int3`, at `0xA716`. Both are included in the 391-byte coverage. The
byte after the return does not prove a breakpoint was executed. All static
observations in this document come from R4/R5 and the exact retained output
identities in Section 7.

## 3. Two explicit zero-return routes in the observed body

These are **static control-flow interpretations under normal-return
assumptions**. Calls on the described paths must return normally, the shown
instructions must execute with their ordinary semantics, and no unobserved
exception or control transfer may bypass the route. None of those runtime
conditions was measured. The bodies of outgoing helpers and runtime IAT targets
remain unexamined.

Both routes reach `xor eax,eax` at `0xA636`, then `jmp 0xA70B` at `0xA638` and
the shared epilogue ending in `ret` at `0xA715`. The other explicit return-value
assignment in the observed body is `mov eax,0x1` at `0xA706`.

| Route | Static guard and continuation | Limit of the conclusion |
|---|---|---|
| Direct-helper result | On the `edx == 1` arm selected at `0xA5A7`, the direct call at `0xA612` targets RVA `0x584C`. A nonzero returned EAX makes `test eax,eax` at `0xA617` and `je` at `0xA619` fall through. After argument setup, the call at `0xA631` targets `0x5680`; if it returns normally, `0xA636` assigns zero and the common tail returns. | The value actually returned by `0x584C`, its internal behavior and whether this route ran are unknown. The helper at `0x5680` is not given an inferred function name. |
| Indirect-call result | On the same arm, after the `0x584C` call returned zero and the intervening path returned normally, the indirect call at `0xA6A0` uses static IAT slot `0x1B820`. A zero EAX makes the `jne` at `0xA6AE` fall through to the call at `0xA6B0`, static slot `0x1B7F0`. Setup at `0xA6BC`/`0xA6C2` and the jump at `0xA6C9` lead to `0xA628`, then the same `0x5680` call and explicit zero assignment. | Existing import metadata names the slots `GetModuleHandleExW` and `GetLastError`; their live bindings, results and any error value are unobserved. The error value's magnitude does not change the displayed explicit zero assignment under the stated assumptions. |

This establishes two visible routes to the common zero assignment. It does not
identify the route taken by 011, prove that either route caused initialization
failure, or establish the failed API behind `0xC0000142`.

## 4. Four static import-slot associations

R5 matches the displayed RIP-relative operand addresses to the already admitted
013 `iat_index`. No raw IAT slot, external instruction, import-name string or
new DLL was followed or loaded for this interpretation.

| Call-site RVA | Static IAT RVA | Existing 013 import metadata |
|---|---|---|
| `0xA5B0` | `0x1BA60` | `ntdll.dll!LdrDisableThreadCalloutsForDll` |
| `0xA659` | `0x1B810` | `api-ms-win-core-libraryloader-l1-2-0.dll!GetModuleFileNameW` |
| `0xA6A0` | `0x1B820` | `api-ms-win-core-libraryloader-l1-2-0.dll!GetModuleHandleExW` |
| `0xA6B0` | `0x1B7F0` | `api-ms-win-core-errorhandling-l1-1-0.dll!GetLastError` |

All four `resolved_runtime_target` values remain null. Static associations do
not show API-set forwarding, live pointer values, a native call, or an API
failure. The nine distinct direct targets are RVA `0xA1B4`, `0x3780`, `0x3840`,
`0x3990`, `0x584C`, `0x5680`, `0x111EC`, `0x3934` and `0x39D0`; `0x111EC`
appears at two call sites. Their instructions were not decoded in 014.

## 5. Lifecycle, retained output and resource interpretation

R3 records one outer submission and clean controller CLI exit 0. The outer
report records owner exit 0, owner reaped, no controller fault and no captured
owner stdout/stderr. The owner report records one GNU launch reservation,
child exit 0, child reaped, stable captures, complete pre/post identity checks
and a complete-disassembly candidate. GNU stderr is empty. Both clean CLI
outcomes are required alongside the reports; stored candidate/status fields
alone never admit completion. Independent result reviews and root's joint admission have passed;
the complete final identities are in Section 6.

| Retained content | Observed bytes | Registered maximum bytes |
|---|---:|---:|
| Five owner files | 21,084 | 787,847 |
| Four outer files | 1,379 | 9,283 |
| Nine controlled files | 22,463 | 797,130 |
| Independent manifest | 6,149 | 32,768 |
| Controlled files plus manifest | **28,612** | **829,898** |

The 65 B owner diagnostic allowance is already contained in the outer stderr
capture's 66 B limit, including its overflow detector. It is not added twice.
The controller's external banner is outside these nine files and manifest.
GNU raw stdout is 7,801 B; the complete owner report is 12,608 B.

The frozen producer limits are child FSIZE 131,072 B per capture, AS 268,435,456 B,
CPU 2 s, CORE 0 and NOFILE 64; reaching a capture cap refuses even with exit 0.
Owner and controller admission deadlines are 12 s and 20 s. The successful
exit contract includes persistence, complete readback and cleanup checks; it
does not promise universally bounded OS syscall return. These are process and
logical-content limits, not a host-wide disk/process sandbox or a measured
memory profile.

The reports' `elapsed_ns_before_persistence` values are **177,620,508 ns** for
the owner and **220,809,186 ns** for the outer controller. They explicitly stop
before persistence and final cleanup, so neither is a complete invocation
duration or native startup measurement. R3's tool/wall timestamps describe
another observation interface and are not substituted for those clocks.

The reviewed 78,721,129 B bound is conservative owner application/temporary
payload accounting. It excludes interpreter/compiler/library internals,
allocator/OS overhead and the separate outer controller, and is not a parent
RSS/AS hard limit. Child AS is a separate 256 MiB limit. Actual short output
does not replace the prior producer, serialization or failure-path proofs.

## 6. Final result admission

<!-- FINAL_RESULT_ADMISSION_014_BEGIN -->
Status: `COMPLETE_STATIC_DISASSEMBLY_INDEPENDENTLY_REVIEWED`.
Root joint decision: `ADMIT_COMPLETE_STATIC_DISASSEMBLY_014`.
The independent byte/interpretation and resource/lifecycle reviews, their
separate-process full readbacks, and root's joint result admission have passed.
Open Critical / Important / Minor findings: **0 / 0 / 0**.

| Final result record | Bytes | SHA256 |
|---|---:|---|
| `runtime-byte-review-014.json` | 59,571 | `d4e7e824ff5280f371e40230e71abd7a5943d8aa7ee080bd091b0cb7457250a9` |
| `runtime-byte-review-014-readback.json` | 6,788 | `0b96fb3e5ade17accdd3eca88705ab56f676fc3a794a8084551353da258a8c8b` |
| `runtime-budget-review-014.json` | 49,512 | `7267b01abca8a0cca7899e083b3a6ed44e74a4e2f54ae2e39652a24ad031ffe9` |
| `runtime-budget-review-014-readback.json` | 11,409 | `c4c9692556345dc4d7451f94c375d88dd08a781e970e955d088d5cd9d3ce3595` |
| `runtime-review-admission-014.json` | 13,826 | `909fe6ba18ff15e9b7971d341c5c31412cb62faaf3e62eec3453b089a988e0cd` |

The byte-review readback covers its 15 fixed direct references; the budget-review
readback covers its 47 fixed direct references. Root fully read and hash-checked
both review/reference sets and jointly admitted the complete 85-record,
391-byte result, both clean CLI exits, GNU exit/reaping and the preserved
resource/scope limits. The normal-return static interpretation remains subject
to the assumptions in Sections 3 and 4.

R3, R4, R5 and earlier review records preserve their historical pending status
fields. The separate joint decision above closes the result gate without
rewriting those records. The actual attempt remains permanently consumed and
no 015 observation is admitted. Git closure publication and the final remote
scope/readback remain root's pending completion steps.
<!-- FINAL_RESULT_ADMISSION_014_END -->

## 7. Exact raw evidence identities

The audit root is
`/workspace/scratch/a8b3faf101c5/evidence-014`.
Paths in the following evidence tables are relative to that audit root.
All nine retained files were fully read and SHA256-matched while preparing this
document, without importing the owner/parser, invoking GNU or reading the
original image. The separate 013 index was read only as retained JSON metadata.

| Retained file | Bytes | Cap | SHA256 |
|---|---:|---:|---|
| `offline-014/consumed.json` | 284 | 1,024 | `365b25a39e2ffc25e745ce4e11d86f4e48a6bf3e35469cf946b40e2618fffea8` |
| `offline-014/target.bin` | 391 | 391 | `e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44` |
| `offline-014/tool-stdout.bin` | 7,801 | 131,072 | `ec02f2bca27a4e550afba17ea319eeeb9b04afa167cc0ee6b5e2862011b2d59e` |
| `offline-014/tool-stderr.bin` | 0 | 131,072 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `offline-014/report.json` | 12,608 | 524,288 | `2969185ddf6368bc8251208964715a371761d5f42ba5d1c9ce5830e5db578ccb` |
| `cli-actual-014/consumed.json` | 369 | 1,024 | `9cce65af24d622da214a5f989e54aa897d7a06cd1481a34a881ee4a98faca183` |
| `cli-actual-014/tool-stdout.bin` | 0 | 1 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `cli-actual-014/tool-stderr.bin` | 0 | 66 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `cli-actual-014/report.json` | 1,010 | 8,192 | `bb681231bcde82e134fc9fced2086063b0b34ceda64370957d478ad8406c0606` |

## 8. Frozen invocation source identities

Source paths below are relative to `tests/native/` at invocation commit
`4beac41f341f4cc8f3ddf02471b17d113ecb39f8`.

| Source | Bytes | SHA256 |
|---|---:|---|
| `bcrypt_disassembly_io_014.py` | 10,976 | `18596e673d8f352a7b49c1955e608e931d69c7dd01d7b9ded47ee402fe90e72e` |
| `bcrypt_disassembly_listing_014.py` | 5,164 | `f21cd65291e7a079885b6c7dc76b65f309ca311d06f3904cd1b145968b8e8e6a` |
| `bcrypt_disassembly_model_014.py` | 16,716 | `3b355306adfdc1ac13c2b193663bc36fb702784779b34891bf4b1a585dc55f12` |
| `bcrypt_disassembly_owner_014.py` | 8,228 | `e0ac62273ebc5fa9c71b53f1e79e7ffeaa6aa7d055fd3ff847188f812ceea1b5` |
| `run_bcrypt_disassembly_014.py` | 4,997 | `22e8820fdf397ad342dd3130324d97e140765fb026f62baa7db17054f251ec3a` |

The separately registered outer controller is R9; it is audit evidence rather
than one of the fourteen source additions. The complete manifest R1 fixes the
fourteen non-source roles, all nineteen source/identity commitments,
environment, owner output path and GNU argv. R2/R11 separately bind the
controller and complete outer/inner/tool interfaces, with 7 / 6 / 18 argv
elements respectively. The manifest does not gain an extra controller role.

## 9. Control and review evidence references

| Ref | Audit record | Bytes | SHA256 |
|---|---|---:|---|
| R1 | `invocation-manifest-014.json` | 6,149 | `0f14b621af601f26fec3024d7e10fe4a79523033e1d9b44c5d0cb10ce780b288` |
| R2 | `actual-execution-admission-014.json` | 6,018 | `6922d1c100b740f695f687eae30bb07c8f3e8e271f22fe7c91347f6d3d00c417` |
| R3 | `actual-execution-014.json` | 1,627 | `1ebaeca2ae3304ae0481decdfae3c9ff7294b0dfd86ef7b2459f4c1cc6dc17a4` |
| R4 | `actual-root-readback-014.json` | 6,354 | `72ad881aaa922712f6c24dedf8e188bfeab4976bd2e1fe81bd9f2ef4a3bde239` |
| R5 | `actual-static-interpretation-014.json` | 7,746 | `b43dc17e9187a6f1903ee9fb2a0c386d2e3fc550ebbb0b9ebb17786c7c2988f9` |
| R6 | `registration-readback-014.json` | 25,773 | `9a71d1384b491d56b3966550857793a2854a5377279ecec905768ccc47a09fb0` |
| R7 | `source-activation-readback-014.json` | 335,697 | `777d46925359ab3968d2e208b4141e1aa83554eb63711434826e7632a6292862` |
| R8 | `source-publication-readback-014.json` | 8,308 | `e10e6354e359ea7d3f6fcc95e2d46ed6a381501716ed0b085f40016108945bf7` |
| R9 | `run_bounded_cli_014.py` | 8,227 | `0c795fbb4549d7c229d4b49a21102bedf9349434fc22064c2427c192619c24cd` |
| R10 | `registration-review-014.json` | 6,532 | `07d3d3a7cb0a76cf3da55f58819a12c0788af54b41e5be4889a1bef558630782` |
| R11 | `manifest-review-014.json` | 41,408 | `4d916b344d32171f7b98bc8e1cf8fedf4977cee6cf6212ea8f084886b3a0dc19` |
| R12 | `manifest-review-014-readback.json` | 11,807 | `a505317aa96b95d28615b3ed1efa341059f66ca9634504ec05ace76acb8adc43` |
| R13 | `synthetic-runtime-admission-014.json` | 1,034 | `418024215d113f9dbd4075e8fe3f8a0517451e78013331311cc6b332b118a2c0` |

The retained original image identity remains 170,984 B / SHA256
`663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`.
The admitted 013 index is 271,920 B / SHA256
`ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e`;
its root joint admission is 20,232 B / SHA256
`c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b`.
014 does not regenerate the index or alter any consumed 011/012/013 evidence.

## 10. Counts, limits and next work

014 has spent one actual Linux GNU observation, seven finite GNU synthetic
observations and three bounded synthetic Python cap children. Baseline tests
remain 275 PASS; final new integration was 121 PASS with three already-spent
process cases explicitly skipped. Overlapping independent reviews are not
added to these counts. Drafting these closure documents added no subject,
GNU, native or test invocation and followed no new target.

At the recorded source activation/pre-execution scope checkpoint, main and the
three open draft/HOLD PRs were unchanged, the feature had no open PR, and the
same eleven attempt-1 completed failure workflow runs remained. The feature
was the registered invocation source. A fresh closure and final remote-scope
readback is still required; this historical checkpoint is not a lease. The
exact two closure paths remain within the previously reviewed path set and
create no authority for a new PR, main update or workflow dispatch.

011 remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`: outer Job total 3
instead of 2, no admitted native_summary, failed API/cause of `0xC0000142`
unknown. Native history remains **11 runs / 10 compiler drivers / compiler Job
TotalProcesses sum 23 / 7 native fixture invocations / 0 ordinary MQB runs**.
These Linux static observations do not increase those Windows fixture counts.

Consumer/cost DEFER remains **+3.0963 ms / +26.70%**; VERSION **5.6.0**;
M1b/service disabled; A1-A5, deletion and ordinary qualification remain false,
and `canonical_evidence_sealed=false`. Main remains
`87f86770cfc7e1941603074ace914e0c7faf10d3`; PR250/248/207 remain draft/HOLD.
IPC capability lifetime and a qualified producer writer owner remain unresolved.
No native/build/workflow activation, DLL reacquisition/load/execution, symbol or
dependency download, injection/hook/process-memory write, permission expansion,
product/VERSION/cost change, merge or release follows from this result.

The concrete next candidate is runtime-function 105 at
`[0x584C, 0x58F3)`, 167 B, referenced by the direct-call instruction at
`0xA612`. It was not separately selected, retained, slice-hashed or decoded
as a 015 target; its slice digest is still null. The 014 full-image identity
hashing covered these file offsets but did not interpret or retain this
candidate as a separate slice. Follow the independent 015
selection/identity/resource gates
in [native_startup_handoff_015.md](native_startup_handoff_015.md); no 014 call,
manifest, output directory or synthetic allowance transfers to it.
