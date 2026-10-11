# Closed 012: the retained image's exact unwind refusal is located

Status: `CLOSED_LOCATED_REFUSAL_NO_STATIC_INDEX_HANDOFF_013`.

The one separately registered offline observation completed with owner exit 0.
It located the frozen parser's `UNWIND_OPCODE_INVALID` at line 438:
version-1 `UWOP_SET_FPREG` has **OpInfo 6**, while its frame-register field is
**5 (RBP)**. The failing predicate is `info == 0 and frame & 15 != 0`; only the
zero-OpInfo operand is false. Independent raw-byte and invocation-budget reviews
admit this failure context. A full PE index, disassembly and the internal
bcrypt initialization failure remain unadmitted.

This completes the first stage of the historical
[012 handoff](native_startup_handoff_012.md). The original 011 acquisition,
execution result, PE refusal and source remain unchanged. The next slice is
the separately versioned [013 compatibility design](native_startup_handoff_013.md).

## 1. Immutable invocation and source

| Item | Exact identity / result |
| --- | --- |
| Source commit | [`4b0e0154790a7b3d4ef2b06920819377aa077fd9`](https://github.com/Iviesever/msvc-quick-build/commit/4b0e0154790a7b3d4ef2b06920819377aa077fd9), tree `8c2187b61694b661e9d7bb607233fd50a19f3c54` |
| Registration | [#198 comment 6110066078](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6110066078), created before ref activation and actual invocation; full body 9,330 B, SHA-256 `0bbb4a383e5c34a726a032fcd9b2bb6ad83031aeca3f09c3febcf3dc565cc258` |
| Manifest | `invocation-manifest-012.json`, 2,446 B, SHA-256 `1320f9bee7c1e9ed82872dab1fead985713bd70b1a73db9aad0491eb1e3f9805` |
| Call | `012-refusal-context-1`, stage `refusal_context`, exactly one reservation / GO / actual parser call; no automatic retry |
| Retained image | 170,984 B, SHA-256 `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`; original 011 event object, init RVA `0xA590` / 42384 |
| Frozen parser | `tests/native/producer_bcrypt_pe.py`, 31,555 B, SHA-256 `941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba` |
| Runtime | Exact manifest-pinned CPython 3.12.14 executable, isolated `-I -S -B` owner/worker; no DLL loaded or executed |
| Owner exit | 0, with empty stdout; the complete execution receipt is retained |
| Completion | `located_refusal`, no failure codes, all six post-identity checks and the manifest postcheck true |

The source commit adds the context extractor, invocation owner, IO helper,
their 55 synthetic tests and the task's original issue/plan. All 774 paths
tracked at the prior feature HEAD were byte/mode-identical before publication,
including production C++, old tests and all 52 workflows. The manifest fixes
the precise source paths/hashes, Python identity, argv, output path and limits.
The consumed marker is permanent; it must not be removed, replaced or reused.

## 2. Exact located bytes

| Field | Observed value |
| --- | --- |
| Root exception entry | Index **11**, RVA and file offset **`0x22084` / 139396**, 12 B |
| Root function | Begin **`0x1830` / 6192**, exclusive end **`0x19BE` / 6590** |
| Leaf unwind | RVA and file offset **`0x1D958` / 121176**, 36 B; chain contains only this RVA |
| Header | Version **1**, flags **3**, prolog **48**, code slots **14**, frame byte **`0x65`** |
| Offending operation | Slot **6**, code offset **`0x16` / 22**, operation byte **`0x63`**, opcode **3**, OpInfo **6** |
| Width | **1**, assigned in the current iteration; not a previous iteration's value |
| Guard binding | `_Image.unwind_info:438`, terminal `_require:64`; exception-table call 483 and inspect call 543; five traceback nodes |
| Views | Header at envelope offset 0, length 4; slot at offset 16, length 2; no extra raw copy |

The complete envelope is:

```text
19300e6522741a001e6419001a3418001663110112000af008e006d004c00250d8a70000
```

Its SHA-256 is `31930bd7175248a6377f6ecde5d9e88a28ba3eb7ded19b14c96ca57ebca7be1c`.
The root entry is `30180000be19000058d90100`, SHA-256
`ba99b79b830b4591fe5555471d02cde74fbe404584f6cc0cb46cce398871bd04`.

The preceding operations at slots 0, 2 and 4 are opcode 4, each width 2.
They advance **0 -> 2 -> 4 -> 6**; the next original two bytes are `16 63`.
The frame byte has low nibble 5 and high nibble 6. The equality of that high
nibble with the operation's OpInfo does not make OpInfo the frame-offset field.
This observation is a whole-image structural scan refusal. Init RVA `0xA590`
is outside `[0x1830, 0x19BE)`, so this root record is not the init function and
the refusal is not a located internal failed instruction.

The independent reviewer matched the full image hash and both raw ranges.
To check only their RVA/file mapping and index arithmetic, it sampled another
158 bytes of header/section fields in 17 requests, within its 1,024-byte cap.
`.pdata` starts at `0x22000`; adding `11 * 12` gives `0x22084`. The leaf maps
uniquely into `.rdata`. This review neither read other runtime entries nor
continued the parser; the extra header fields were not added to raw context.

## 3. Primary rule evidence and its limit

Microsoft's [version-1 x64 format description](https://github.com/MicrosoftDocs/cpp-docs/blob/e61067551f92a1c7beaad20c04b9f5ccfd2b5dd6/docs/build/exception-handling-x64.md#L156-L158)
defines opcode 3 as one node and says: "The operation info field is reserved
and shouldn't be used." The operation uses the header's frame register and
frame offset, scaled by 16. This wording does not expressly certify all
nonzero encodings as valid.

Microsoft's current pinned [.NET unwind consumer](https://github.com/dotnet/runtime/blob/ed84dd37d6728e9d0d50a37a323eddcc5c57cd71/src/coreclr/unwinder/amd64/unwinder.cpp#L814-L820)
sets Rsp from `IntegerRegister[FrameRegister]` and subtracts `FrameOffset * 16`
for `UWOP_SET_FPREG`; it does not use OpInfo. The [earlier pinned revision](https://github.com/dotnet/runtime/blob/421be955e4b70cddf583b10f5ad99814b713fb87/src/coreclr/unwinder/amd64/unwinder.cpp#L793-L799)
has the same branch. These are public Microsoft consumer implementations,
not validation of the exact Windows build used by 011.

The supported inference is narrow: the frozen zero-only predicate rejects a
reserved value that this public consumer ignores. That supports reviewing a
new version-1/opcode-3 compatibility indexer. It does not establish full image
validity, justify ignoring other OpInfo fields, or explain DLL_PROCESS_ATTACH.
No compatibility parser was implemented or invoked in 012.

## 4. Actual invocation budget and durability

| Resource | Actual | Admitted maximum |
| --- | ---: | ---: |
| 012 actual-image parser calls | 1 | 1 |
| Consumed marker | 210 B | 1,024 B |
| Complete owner report | 3,485 B | 15,360 B |
| Marker + report | 3,695 B | 16,384 B |
| Canonical core | 1,289 B | 8,192 B |
| Worker stream | 1,327 B | 8,230 B accepted; one overflow probe always fails |
| Context work units | 3,965 | 16,384 |
| Traceback nodes / chain RVAs | 5 / 1 | 64 / 32 |
| Raw envelope / root entry | 36 B / 12 B | 528 B / 12 B |
| Owner observation interval | 105.950332 ms | 5 seconds to admit completion |
| Native compile / fixture / Actions activation / objdump | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |

The worker stream was independently reconstructed as a 33-byte READY record
plus 5-byte result header plus the 1,289-byte core. Its exact SHA-256 and
128-byte prefix match. READY and GO are true; worker return code is 0,
reaped is true, and kill was not requested. PID 2 / 5 are recorded namespace
values, not host-global process claims.

The recorded interval ends before final report persistence. A complete report
and clean owner exit **together** support the post-save/fsync/readback deadline
check. The tool's separate `wall_time_seconds=0.071729954` is retained without
equating it to the owner's monotonic interval. The owner does not promise that
OS creation, termination or fsync will return within an absolute wall time.
Source/manifest/data identities remained equal after the call. Output creation
was exclusive, with file/directory fsync and complete readback.

## 5. Regression and independent admission

The context/owner work retained real RED/GREEN evidence for missing feature
behavior, DEL escaping, bounded exception codes and manifest postchecks.
Stale locals, exact lineage, call counts and budget boundaries were also
independently verified; later added acceptance checks are not claimed as RED.
Before registration, all **174** integrated pure tests passed;
an independent focused review ran **97** relevant tests successfully.

After observing the actual refusal, the closure adds only
`tests/native/test_bcrypt_refusal_regression_012.py`: 140 lines, 7,760 B,
SHA-256 `ad985f21c424997b27de853f189354e78a7e5e1492d9664ab3e3dc362e015b3f`.
Its four tests preserve the exact raw anchors, reproduce the smallest info-6
refusal, trace the original multi-slot prefix, and verify explicitly synthetic
info-0 neighbors. The full neighbor relocates its handler only in the synthetic
copy; the original 36-byte anchor and actual image never change. These tests
describe the frozen parser's acceptance boundary, not Windows semantic validity.

The final seven-module suite passed **178 tests, 0 failures, 0 errors** in
2.625 seconds. All eight source/test fingerprints were identical before and
afterward. No actual image was parsed by those tests.

The independently reviewed fixed parser scope has no open Critical/Important
findings. One source-review Minor note (`R012-M01`) concerns hypothetical
arbitrary private `_finish` fallback metadata, outside reachable whitelisted
metadata for the frozen parser; it is retained, not represented as a generic
serializer guarantee.

Key evidence files in the delivered package:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `offline-012/consumed.json` | 210 | `6b5c2864db796a5da05e85e6a86e187be07b8bb1ac9364602373b427a70e49f5` |
| `offline-012/report.json` | 3,485 | `d747b6dc008b8191e0226282879d3258ee11a645d0066b91f8757cb5af357e86` |
| `runtime-context-review-012.json` | 12,808 | `712ff26820c24a4b6984712225202adbf9998767ee7d6beb46ec355dbad45b78` |
| `runtime-budget-scope-012.json` | 34,538 | `fc597ae586bb85365f2ddd6c65929e8771ce6180e5d3cbe912a47bdf12b47e60` |
| `runtime-review-admission-012.json` | 3,595 | `496b443b3fdb1fb66adf9b44bc7f5e347766fe88aaae3396d3f4d343369f1327` |
| `primary-rule-review-012.json` | 4,689 | `530d1eafaa3a3d8e6652a39fc347f9452e4edcd6d8fa9e765f5bbdfd4689865c` |
| `regression-source-review-012.json` | 7,759 | `44b4d292f7689b396f9a296bace2882fc2e90924963abee28632add6c9b97082` |
| `final-integration-validation-012.json` | 3,010 | `d7db1402556a4b1c3d89a8c4b1de4751243f2bbf7d528b4ec21d27d25cccc6d4` |

The package also retains full registration, source/manifest admission, exact
execution receipt, synthetic input identities and all test/review logs. Its
evidence packaging size is separate from the invocation's 16 KiB output cap.

## 6. Preserved state and next handoff

The original native result is still `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`:
outer Job total 3 fails required 2, with no admitted native_summary. Actual C
exited `0xC0000142`; its internal failed API/access remains unknown. Main,
draft/HOLD PR250/248/207, VERSION 5.6.0, consumer/cost DEFER and the historical
cost excess remain unchanged. Native totals stay at eleven workflow runs,
ten compiler drivers, summed compiler Job TotalProcesses twenty-three and
seven native fixture invocations. Ordinary MQB executions remain zero.

A1-A5, deletion, ordinary qualification and canonical_evidence_sealed remain
false. The original 012 handoff and its consumed outputs are immutable.
013 must first implement/review the exact version-1 compatibility delta and
its new complete-index resource contract; its separately registered one-call
observation must stop at any next refusal. Startup understanding would still
leave the original IPC prerequisite and producer owner qualification ahead.
