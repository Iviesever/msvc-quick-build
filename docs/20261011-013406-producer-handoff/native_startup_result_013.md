# Offline startup result 013: complete versioned PE index

Status: `COMPLETE_INDEX_INDEPENDENTLY_REVIEWED`.

The separately registered 013 observation returned a complete structural
index of the exact retained 011 bcrypt image. The owner and its sole worker
both exited zero; the worker was reaped. The selected version-1 SET_FPREG
consumer-compatibility change passed the earlier 012 guard without changing
the original bytes. The actual reservation is consumed and must never be
retried. Independent byte, schema, budget and scope reviews passed, and the
root joint admission accepts this complete versioned static index.

The resulting index places the independently established init RVA `0xA590`
at the beginning of runtime-function record 169, in `.text`, with range
`[0xA590, 0xA717)` and length **391 bytes**. This supplies the concrete static
target for [handoff 014](native_startup_handoff_014.md). No disassembler was
invoked in 013, including a version query.

## Exact source, registration and actual artifacts

| Item | Identity |
|---|---|
| Invocation source commit | `0172607ad74802a0ae74495f62cc0f07aed3ae12` |
| Source tree | `42f78e4932800aa212d922639095594b5f1d0ee8` |
| Source parent / 012 closure | `7622aa4d2757c082c4db12140cb71129b3e3511b` |
| Registration | [#198 comment 6117168744](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6117168744) |
| Registration body | 13,447 B; SHA256 `b420cb94b7a1317065ef4a40a171ae049c12fea34c1c28439cd6d8cf89a368f9` |
| Call ID | `013-complete-index-1`, consumed |
| Canonical manifest | 3,155 B; SHA256 `eca8c4d91bdd7fa00e029abda3b5d0d0cb9f52246773fd83e830a262dc8dde8d` |
| Retained original image | 170,984 B; SHA256 `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Actual consumed marker | 205 B; SHA256 `05ddd8ee92ae4b536a0b33ad4d72cac51a73d1319395ecb582463db3f55fb29c` |
| Actual complete report | 271,920 B; SHA256 `ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e` |
| Owner CLI evidence | 2,382 B; SHA256 `0335eb5b78a470f649a491467ab1596fcf066c9161591948f80f50eadfe4bddf` |
| Submission evidence | 949 B; SHA256 `c6d73f31e2116f5df6960a6422189472c71628fc2bf5d6f9852dcdd93a148a72` |

The source commit added nine source/test files and three issue/plan/resource
documents. Full Connector readback matched every new byte and preserved all
785 baseline blobs/modes. The exact manifest and registration body were read
back before a `force=false` expected-old-HEAD feature update. Local HEAD was
then synchronized to the same verified commit and the worktree was clean.
Source and invocation artifacts remain immutable after observation.

The five execution-source identities, all codec profiles and all owner limits
are fixed in the [013 resource contract](../20261012-021948-bcrypt-compat-013/resource_contract_013.md).
Python is the pinned 64-bit CPython 3.12.14 executable, 30,894,944 B, SHA256
`fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7`.

## Actual outcome and completion boundary

| Observation | Actual value |
|---|---|
| Owner report / core | `complete_index` / `indexed` |
| Actual parser calls | 1 |
| Owner / worker exit | 0 / 0 |
| Worker reaped | true |
| READY / GO | one matching READY, one GO |
| Kill requested | false |
| Manifest post-verification | true |
| Input, five sources and Python post-verification | all seven true |
| Failure code / failure list | null / empty |
| Owner stdout / stderr | 0 B / 0 B |
| Worker framed stream | 268,733 B |
| Stream SHA256 | `3e071533c5649bac2db15f31b03ef17399383bde7f5a6cf06a338d53c65ce7e4` |
| Complete core | 268,695 B |
| Report plus marker | 272,125 B |
| Manifest plus report and marker | 275,280 B |
| Parser work | 264,523 units under 4,194,304 |

The owner clock sampled **259.931345 ms** from its pre-Popen start to its
postidentity finish check. That sample precedes final report persistence.
The external CLI observation took **377.345810 ms**, including bootstrap and
the owner process exit; it is a different clock scope. Completion requires
the complete report jointly with owner exit zero, because the reviewed owner
checks the unchanged five-second deadline again after file/directory fsync
and full report readback. Neither timestamp alone is substituted for that
joint gate. There was no outer timeout or retry.

The fixed caps remain 67,108,864 B core, 67,108,902 B worker stream plus one
separate overflow-probe byte, 67,125,248 B report and 1,024 B marker. Runtime
report/marker cap is 67,126,272 B; adding the 32,768 B manifest cap gives
67,159,040 B. Complete evidence was retained without truncation.

## What the complete index establishes

| Indexed structure | Count |
|---|---:|
| Sections | 7 |
| Data-directory entries | 16 |
| Ordinary import descriptors | 15 |
| Delay-import descriptors | 1 |
| IAT symbol records | 87 |
| Unknown IAT slots | 0 |
| Runtime-function records | 390 |
| Unique unwind records | 318 |
| Certificate records | 1 |

The index includes normal and delay imports, reported IAT-slot associations,
runtime-function records and bounded raw unwind envelopes. Zero unknown IAT
slots means those slots have structural associations in this static index;
it does not verify live import binding or successful calls. Certificate
presence does not verify trust.

The compatibility metadata is:

```json
{"id":"AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED","nonzero_opinfo_ignored":true,"notice":"CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED"}
```

The boolean proves this parser instance used the selected compatibility rule.
The original frozen parser and the consumed 012 refusal remain valid historical
evidence. Their bytes were not revised to make 013 succeed.

In particular, old root 11 still has table offset `0x22084`, function range
`[0x1830, 0x19BE)` and unwind RVA/file offset `0x1D958`. Its original 36-byte
envelope remains:

```text
19300e6522741a001e6419001a3418001663110112000af008e006d004c00250d8a70000
```

Its SHA256 remains
`31930bd7175248a6377f6ecde5d9e88a28ba3eb7ded19b14c96ca57ebca7be1c`.
Slot 6 still contains `16 63`, after slot progression `0 -> 2 -> 4 -> 6`.
The operation byte was not normalized to `03`. This old root is outside the
init function; neither its former parser refusal nor its handler identifies
the native failed instruction.

## Concrete init association

| Field | Value |
|---|---|
| Header entrypoint and independently retained loader init RVA | `0xA590` |
| Runtime-function index | 169 |
| Runtime table file offset | `0x227EC` |
| Raw table triple | `90a5000017a7000088ec0100` |
| Raw table triple SHA256 | `c8a2190fa83215b5ca2fc8a060f7e98f95a8be00eb4502007a314ba93d9a832e` |
| `.text` RVA and file interval | `[0xA590, 0xA717)` |
| Interval length | 391 B |
| Interval raw-byte SHA256 | `e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44` |
| Unwind RVA / file offset | `0x1EC88` |
| Unwind envelope | 12 B; `010a04000a3409000a520670` |
| Unwind envelope SHA256 | `cff4d61e7f88c087c11e26c322a81decf2f0e7b7515fb78b17f2d867e224a26c` |
| Preferred PE ImageBase | `0x180000000` |
| Corresponding preferred VMA interval | `[0x18000A590, 0x18000A717)` |

This identifies a bounded static interval only. It does not establish the
instructions actually executed, the failed API, a failing branch, register
values or runtime unwind behavior.

## Validation and retained uncertainty

Development retained genuine synthetic failing expectations before the new
behavior, followed by passing checks. The final root integration run passed
**275 tests**. A separate full-source reviewer ran **143 tests plus six
supplemental checks**; the independent codec reviewer added 17 bounded codec
checks. These overlapping runs provide separate verification evidence.

Full-source, numerical-budget, resource-contract and exact-manifest reviews
had zero open Critical/Important/Minor findings before registration. One Minor
documentation issue was fixed: initial hash-bound bootstrap reads are now
distinguished from subsequent regular-file/NOFOLLOW IO checks. No Python
source changed during that correction.

The independent runtime byte review checked all 390 runtime roots, all 318
unique raw unwind envelopes, all 87 IAT records and 371 disjoint metadata
claims. Its 18,371 bounded assertions passed. A separate schema reviewer
checked all 1,108 objects, 27 arrays and 26,721 nodes including keys. The
budget/scope review reconstructed the complete canonical core and framed
stream, matched every byte-count/hash, verified the single-call lifecycle
and jointly admitted the complete report with clean owner exit. Both reviews
have zero Critical/Important/Minor findings.

| Sealed runtime evidence | Identity |
|---|---|
| `runtime-byte-review-013.json` | 88,458 B; SHA256 `d0ba5e1da43a2ecaf7e26603c1b2b5d29f7380f1100f792bc97b9a0b7b83d755` |
| `runtime-budget-scope-013.json` | 32,289 B; SHA256 `00f9a6b703fd961534afcdf9ec91ce2c13e904c23f0bf76b5c77fda77fd5b161` |
| `runtime-review-admission-013.json` | 20,232 B; SHA256 `c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b` |

The root joint admission rechecked the full identities of 61 referenced paths,
matched every reported unwind-envelope hash and the aggregate against the
independent byte review, and bound the two runtime reviews to the immutable
manifest, source commit, registration and actual CLI/report. Review-time
verification made no additional parser, owner, native or objdump invocation.
The original CLI record's pending-review field remains an accurate historical
state at return time; the distinct joint-admission record supplies the later
decision. These evidence records are included in the delivered audit package.

The index explicitly retains these six limitations:

- Actual execution and failure cause are unknown.
- Certificate trust is not verified.
- Checksum is not verified.
- Language-specific unwind data is not interpreted.
- Other directory contents are not interpreted.
- Unwind stack effects are not verified.

The original 011 result stays `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`:
outer Job total 3 instead of 2, no admitted native_summary, and loader
initialization failure `0xC0000142` without an identified failing API.
There was no new native run, build, image acquisition, symbol download or
objdump operation in 013.

Main `87f86770cfc7e1941603074ace914e0c7faf10d3`, draft/HOLD PR250/248/207,
VERSION 5.6.0, consumer/cost DEFER, the +3.0963 ms / +26.70% excess and disabled
M1b/service work stay unchanged. A1-A5, deletion, ordinary qualification and
canonical_evidence_sealed remain false. Native history remains 11 runs,
10 compiler drivers, summed compiler Job TotalProcesses 23, seven native
fixture invocations and zero ordinary MQB runs. No merge or release is selected.
The original IPC capability-lifetime prerequisite and a qualified producer
writer owner remain ahead of the startup investigation.
