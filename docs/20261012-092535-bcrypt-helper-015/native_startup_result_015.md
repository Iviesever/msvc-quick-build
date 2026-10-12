# Native startup result 015 — bounded helper observation

## Admitted result

The single registered `015-helper-static-1` observation completed and is now
jointly admitted as **bounded offline static evidence**. It establishes exactly
43 instruction records covering 167 B of runtime-function 105, together with
their raw bytes and three address domains. It does not establish a native
execution path, the failed API, or the cause of the original startup failure.

The main new finding is that four conditional branches leave the selected
helper extent and all land in one indexed 83 B range, runtime-function 252.
Its retained CHAININFO metadata links back to runtime 105. This provides a
single concrete candidate for 016. Those 83 B have not been selected, hashed
or decoded in this round; their four incoming instruction boundaries remain
unverified.

## Exact source, registration and target

Source commit: `62addcd32bb7ec376719d5b830fa7fef0a2be0fc`.
Source tree: `d98939bacd199a0f9345a5e6d2bd2ab5283aabe2`.
Sole parent / previous closure: `68d5a913399a64e8c99e8ecbfa073b668f603430`.

The source publication adds exactly 17 files / 177,328 B, preserving all 815
old blobs. The complete source tree contains 832 blobs / 8,553,438 blob-content
bytes and 936 recursive entries. Every new source blob, full tree and commit
was read back through Connector, and independently verified before activation.

The exact 18,026 B registration body was fully read back at
[Issue 198, comment 6123019350](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6123019350).
It embeds the entire 8,513 B canonical invocation manifest, SHA-256
`6d8ceb25ab8e3fd550fb01bd6136565b103de51c8554be30db1c60f8c9640ddb`.
The feature was then advanced by expected-old, nonforced Connector update.
Local HEAD/index and every one of the 832 working files matched the source
tree before the actual execution admission.

| Target identity | Value |
|---|---|
| Original image | 170,984 B / `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Runtime / section | 105 / section 0 `.text` |
| File extent | `[0x584C, 0x58F3)` |
| RVA extent | `[0x584C, 0x58F3)` |
| Preferred VMA extent | `[0x18000584C, 0x1800058F3)` |
| Selected bytes | 167 |
| Selected SHA-256 | `c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1` |
| `.pdata` row | file `0x224EC` |
| Unwind | RVA/file `0x1DD78`, 12 B; `010a04000a3406000a320670` |

File offsets and RVAs happen to be equal for this image mapping. Preferred
VMAs use image base `0x180000000`; they are not measured loaded addresses.
The 014 caller at RVA `0xA612` contains `e835b2ffff`, signed displacement
-19,915, and therefore targets `0x584C`; its following instruction tests EAX.

## Runtime completion and finite consumption

The actual external execution returned **exit 0, no running session**, chunk
`65efc0`. Root transport, outer controller, owner and GNU were joined to their
real containing exits; all exit codes were 0, all direct children were reaped,
and no controller or transport fault was present. GNU stderr, owner stdout /
stderr and transport stderr were empty. The controller emitted only its
107 B canonical summary. GNU stdout is 3,831 B and the owner report 6,740 B.
The thirteen named runtime files total 14,721 B.

The owner reported successful complete identity verification before and after
the call. The immutable success paths require owned-file fsync and full
readback, handle closure and final deadline checks before clean return.
Stored candidate booleans alone were not treated as completion. Pre-persistence
elapsed values are 180,087,040 ns (owner), 233,142,025 ns (outer), and
271,037,476 ns (transport); these are internal observations, not new native
benchmark measurements.

| Allocation | Consumed / outcome |
|---|---|
| Primary selection preparation | 1; completed |
| Independent selection preparation | 1; completed without writing another target |
| Five constructed GNU cases | 5; all finite predicates admitted |
| Actual GNU / actual root submission | 1 / 1; completed |
| New real Python cap children | 0 |
| Original-image full reads in all of 015 | 4 / 683,936 B: primary 1, independent 1, actual before/after 2 |
| Root/reviewer ad hoc original-image content reads | 0 |
| 016 target content reads / decodes | 0 / 0 |

Every attempted slot remains permanently consumed. No retry or replacement
output path is available. The five constructed results were 167 NOP records,
84 zero-run records, one 15 B record, three separated-domain branch/RIP records,
and the expected partial-instruction listing refusal. GNU was reaped with
exit 0 in all five; the refusal's owner/outer exits of 1 were expected.
The completed pure integration had 80 passing tests. Full retained RED,
adverse-case and review evidence remains in the delivery package.

## Static control and data findings

There are five direct relative calls and seven conditional branches. Three
conditional destinations are verified instruction starts within the 167 B.
The other four are outside this selected extent:

| Source RVA | Raw bytes | Condition after the visible test | Destination RVA | Candidate offset |
|---|---|---|---|---:|
| `0x5883` | `0f88b3820000` | SF = 1 (`js`) after testing EAX | `0xDB3C` | 0 |
| `0x5897` | `0f85bc820000` | ZF = 0 (`jne`) after testing EDI | `0xDB59` | 29 |
| `0x58B4` | `0f85a7820000` | ZF = 0 (`jne`) after testing EAX | `0xDB61` | 37 |
| `0x58CC` | `0f8597820000` | ZF = 0 (`jne`) after testing EAX | `0xDB69` | 45 |

The five call targets, kept as destinations only, are `0x8AE4`, `0x8A98`,
`0x5A18`, `0x8A50`, and `0x58FC`. Their bodies and return contracts were not
examined. The eight RIP-relative accesses map to five `.data` cells at RVAs
`0x21638`, `0x21740`, `0x21750`, `0x21754`, and `0x21778`; none overlaps an
indexed IAT slot. Cell contents and symbol names were not read or inferred.

EAX first receives the dword at `0x21754`. A zero value causes the call at
`0x5860`, followed by `test al,0x18`. A nonzero result of that mask test
branches to the store at `0x58D9`. The bit meanings and the callee behavior are
unknown.

EDI is cleared. If dword `0x21778` is zero, qword `0x21638` is ANDed with zero
and the call at `0x587A` occurs. EAX is copied to EDI. A negative tested EAX
takes the external branch at `0x5883`. The subsequent `0x21778 = 1` store is
therefore reached on a **nonnegative** result; a separate later EDI test still
branches on **nonzero**. The store must not be described as proof of a
strictly zero result.

The OR masks 2, 4 and 8 all address dword `0x21750`. Mask 2 is set before the
calls at `0x58A4` and `0x58A9`. Masks 4 and 8 occur after the respective zero
checks at `0x58B4` and `0x58CC`. No algorithm, thread-safety, idempotence or
failure-cleanup meaning is assigned to these bits.

The visible completion route stores dword `0x21740 = 1`, clears EBX, moves EBX
to EAX, restores the saved state and returns zero. The four external branches
prevent this limited observation from establishing every possible return
value or all error handling. The final `CC` byte at `0x58F2` decodes as INT3
after RET; no in-range branch targets it, and its presence does not show that
a breakpoint executed.

## Complete admitted 167 B listing

The assembly below is the retained GNU text, checked independently against
each selected encoding. Common preferred VMA base is `0x180000000`; file
offset equals RVA for every row under the verified section mapping.

| RVA | Raw bytes | Assembly |
|---|---|---|
| `0x584C` | `48895c2408` | `mov    QWORD PTR [rsp+0x8],rbx` |
| `0x5851` | `57` | `push   rdi` |
| `0x5852` | `4883ec20` | `sub    rsp,0x20` |
| `0x5856` | `8b05f8be0100` | `mov    eax,DWORD PTR [rip+0x1bef8]        # 0x180021754` |
| `0x585C` | `85c0` | `test   eax,eax` |
| `0x585E` | `7505` | `jne    0x180005865` |
| `0x5860` | `e87f320000` | `call   0x180008ae4` |
| `0x5865` | `a818` | `test   al,0x18` |
| `0x5867` | `7570` | `jne    0x1800058d9` |
| `0x5869` | `33ff` | `xor    edi,edi` |
| `0x586B` | `393d07bf0100` | `cmp    DWORD PTR [rip+0x1bf07],edi        # 0x180021778` |
| `0x5871` | `7520` | `jne    0x180005893` |
| `0x5873` | `48213dbebd0100` | `and    QWORD PTR [rip+0x1bdbe],rdi        # 0x180021638` |
| `0x587A` | `e819320000` | `call   0x180008a98` |
| `0x587F` | `8bf8` | `mov    edi,eax` |
| `0x5881` | `85c0` | `test   eax,eax` |
| `0x5883` | `0f88b3820000` | `js     0x18000db3c` |
| `0x5889` | `c705e5be010001000000` | `mov    DWORD PTR [rip+0x1bee5],0x1        # 0x180021778` |
| `0x5893` | `8bdf` | `mov    ebx,edi` |
| `0x5895` | `85ff` | `test   edi,edi` |
| `0x5897` | `0f85bc820000` | `jne    0x18000db59` |
| `0x589D` | `830dacbe010002` | `or     DWORD PTR [rip+0x1beac],0x2        # 0x180021750` |
| `0x58A4` | `e86f010000` | `call   0x180005a18` |
| `0x58A9` | `e8a2310000` | `call   0x180008a50` |
| `0x58AE` | `8bd8` | `mov    ebx,eax` |
| `0x58B0` | `8bf8` | `mov    edi,eax` |
| `0x58B2` | `85c0` | `test   eax,eax` |
| `0x58B4` | `0f85a7820000` | `jne    0x18000db61` |
| `0x58BA` | `830d8fbe010004` | `or     DWORD PTR [rip+0x1be8f],0x4        # 0x180021750` |
| `0x58C1` | `e836000000` | `call   0x1800058fc` |
| `0x58C6` | `8bd8` | `mov    ebx,eax` |
| `0x58C8` | `8bf8` | `mov    edi,eax` |
| `0x58CA` | `85c0` | `test   eax,eax` |
| `0x58CC` | `0f8597820000` | `jne    0x18000db69` |
| `0x58D2` | `830d77be010008` | `or     DWORD PTR [rip+0x1be77],0x8        # 0x180021750` |
| `0x58D9` | `c7055dbe010001000000` | `mov    DWORD PTR [rip+0x1be5d],0x1        # 0x180021740` |
| `0x58E3` | `33db` | `xor    ebx,ebx` |
| `0x58E5` | `8bc3` | `mov    eax,ebx` |
| `0x58E7` | `488b5c2430` | `mov    rbx,QWORD PTR [rsp+0x30]` |
| `0x58EC` | `4883c420` | `add    rsp,0x20` |
| `0x58F0` | `5f` | `pop    rdi` |
| `0x58F1` | `c3` | `ret` |
| `0x58F2` | `cc` | `int3` |

## Resource and review closure

The concrete actual runtime is 77 full identity passes / 75,857,272 declared
content bytes, plus at most 77 one-byte EOF-detection requests that return
zero extra content for consistent inputs. The original image accounts for
two actual passes / 341,968 B. Owner / outer / transport deadlines remain
12 / 20 / 23 seconds. GNU hard and soft limits are equal: CPU 2 seconds,
address space 268,435,456 B, file size 131,072 B, core 0, and open files 64.
The 18 exact GNU arguments and four exact environment fields remain bound
to the registered manifest and execution plan.

Owner, outer and transport named-content ceilings are 525,479 B, 9,283 B and
11,331 B. Owner plus outer plus maximum manifest is 567,530 B. The actual
external execution record and completion record have separate 8,192 B caps;
their actual sizes are 3,514 B and 6,229 B. The application-payload envelopes
remain 70,042,827 / 87,701,139 / 127,379,958 / 47,883,505 / 3,890,512 B for
preparation / actual owner / independent preparation / outer / transport.
These exclude interpreter/compiler/stdlib/native allocator and OS baseline;
they do not bound process-tree RSS.

Independent byte/semantic and runtime/resource reviews both have C/I/M
0/0/0. Root jointly admits their exact evidence below. Two read-only observer
format assumptions were corrected and retained: owner/outer outputs use the
frozen IO default 0600 while transport uses 0400; older 013 evidence keeps its
own frozen serialization instead of the 015 canonical-output format.
No source or runtime result was changed and no actual observation was rerun.

## Preserved scope and next handoff

011 remains **INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE**. Its outer
Job TotalProcesses was 3 rather than the expected 2, and native_summary was not
admitted. The failed API and cause of `0xC0000142` remain unknown.

The counters remain **11 native workflow runs / 10 compiler drivers / summed
compiler Job TotalProcesses 23 / 7 native fixtures / 0 ordinary MQB**. These
Windows/native counters are separate from the bounded offline GNU calls.
Cost remains **DEFER**, +3.0963 ms / +26.70%; VERSION remains **5.6.0**.
M1b/service remain disabled. A1–A5, deletion and ordinary qualification remain
**HOLD**; `canonical_evidence_sealed=false`. IPC capability lifetime and
qualified producer/writer ownership remain unresolved.

Main remains `87f86770cfc7e1941603074ace914e0c7faf10d3`. PRs #250, #248 and
#207 remain open, draft and unmerged with unchanged branch heads and bodies.
The feature has no PR. Registration increased Issue 198 comments from 352
to 353; source activation changed only the expected feature ref plus shared
repository pushed_at metadata embedded in the three PR responses. The full
eleven run records and old ledger anchors remained unchanged. These are
retained point-in-time readbacks, not an atomic lock across GitHub resources.

The closing publication adds only this result and `native_startup_handoff_016.md`.
The resulting closing commit and exact closing-comment identity are recorded
in the later closure readback / final admission and Issue 198 closure, avoiding
a self-referential commit hash in these documents. A following session must
start from that final closure, not from source commit `62addcd32...`.

## Evidence identities

Rows refer to the `evidence-015/` directory in the supplied package. JSON refs
also preserve original absolute locations. The package retains the exact 014
ZIP once, opaquely, for the previous complete evidence chain.

| Evidence path under evidence-015 | Bytes | SHA-256 |
|---|---:|---|
| source-publication-readback-015.json | 5103 | `1150d3a07fc1eb69cc6523c7c00880f59663f7cd502665bebf96c51539b1268a` |
| source-publication-independent-review-015.json | 9795 | `5fa7b9bca5598a2aa5691f796eaeccb3f07dec1edb9d38b22fb21f9e8489b070` |
| selection-identity-admission-015.json | 2493 | `7d51bb380949cd3c386fd5b28a1f7641a6a7cf9f1a70e2382c30eea55870abaa` |
| synthetic-results-admission-015.json | 3278 | `171f20962e53c61031f84e38e06d885ebe301e74b196989ccfd917f61d7c476f` |
| pure-integration-015.json | 3976 | `20696cd4e60e5f093a7698c764aae152db961b6428bcbf2183fcfcd7feba86d3` |
| source-resource-review-015.json | 25071 | `cbe7b1d41b5016fcc47b898bee51581581db8e122e314d721e481eec95d91dfe` |
| actual-manifest-independent-review-015.json | 23600 | `f32e42cc2cd08aef4ae359cac242b618d2de30a43f78b8d5a731e7bced876fab` |
| actual-parameter-resource-review-015.json | 13279 | `262b730be0bc2da9263cb06554a9b87730203452f6232d1ac7c50bb652ca8f8c` |
| registration-readback-015.json | 2171 | `abb32e83c4e650173bbcb514c57d5c795023a16eba66eb6b3f7a111fc54b24a5` |
| source-activation-readback-015.json | 197361 | `ad8f3378e568727b5e2a7904c018ed5a8d56e721f785cd71ba33259f6aa87737` |
| registration-activation-scope-independent-review-015.json | 10374 | `758885ca46f01949ecff3061477e6a6f8bbdd56d3fa113ac00772a8e9e1a3fb8` |
| invocation-manifest-015.json | 8513 | `6d8ceb25ab8e3fd550fb01bd6136565b103de51c8554be30db1c60f8c9640ddb` |
| actual-execution-admission-015.json | 18192 | `4158653228a132928cd9ab4db882efcfcebf21ba157e1e3b598be53f55b231d8` |
| actual-external-exec-result-015.json | 3514 | `808599f826134cd29abba5fdbe3f7d3754d819f6a8ea10109952fb8a6bdeded6` |
| actual-transport-completion-015.json | 6229 | `78421129bef61995ce46f24b1a4f4bf8ff0d2b72c1c0e2961d000fb652699e22` |
| actual-byte-semantic-review-015.json | 18653 | `6a92d67cb679c01bfe1b2a91ee608e3ac7121461ab7bb5f63f81750ce73e4d97` |
| actual-runtime-resource-review-015.json | 20488 | `847face1159c83d39d8a4d61665bc0561c0b89a1b8de15fd6583fc468ed9f5c3` |
| actual-root-byte-readback-015.json | 6250 | `6f8d0cb381aa4a009f2b54f0545384c8a0a1ef92c542387dd94256b4adfa6dbf` |
| runtime-review-admission-015.json | 24444 | `6cff1daa185450717d2c08aa23a89f6bd50608471eb647b37a64ca9ce106f437` |
| offline-015/report.json | 6740 | `5c55e875110e732068f3695d35504f40e7ae7f4e7bf32de4a7bf10b18f3a1e9b` |
| offline-015/tool-stdout.bin | 3831 | `7c9f20993d0daf69c414bdb12d706d754ce8e1415f2b2948eec5cdb2b2302043` |
