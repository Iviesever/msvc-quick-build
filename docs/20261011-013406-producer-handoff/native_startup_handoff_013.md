# Next handoff 013: version-1 SET_FPREG consumer compatibility

Status: `DESIGNED_NOT_IMPLEMENTED_NOT_REGISTERED_NOT_EXECUTED`.

The [closed 012 observation](native_startup_result_012.md) supplies the exact
guard, raw bytes and independent reviews missing after 011. The next concrete
step is a separately versioned offline PE indexer whose only acceptance change
is **version-1 UWOP_SET_FPREG's reserved OpInfo field**. First implement and
review that small delta and its synthetic cases, then prove a new complete-index
output budget and invocation owner. Only a separately registered single actual
call may follow. No new Windows/native work is needed for this slice.

## 1. Read and verify the actual starting point

| Input / evidence | Required exact value |
| --- | --- |
| Original image | 170,984 B; SHA-256 `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Original parser | `tests/native/producer_bcrypt_pe.py`, SHA-256 `941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba` |
| 012 source commit | `4b0e0154790a7b3d4ef2b06920819377aa077fd9` |
| Consumed 012 call | `012-refusal-context-1`; manifest SHA-256 `1320f9bee7c1e9ed82872dab1fead985713bd70b1a73db9aad0491eb1e3f9805` |
| Actual 012 report | 3,485 B; SHA-256 `d747b6dc008b8191e0226282879d3258ee11a645d0066b91f8757cb5af357e86`; owner exit 0 |
| Joint context admission | `runtime-review-admission-012.json`, SHA-256 `496b443b3fdb1fb66adf9b44bc7f5e347766fe88aaae3396d3f4d343369f1327` |
| Observed record | Root index 11 at `0x22084`; function `[0x1830, 0x19BE)`; leaf `0x1D958`; version 1, flags 3, count 14, frame byte `0x65` |
| Observed operation | Slot 6 after `0 -> 2 -> 4 -> 6`; bytes `16 63`; opcode 3, OpInfo 6, current width 1 |
| Original envelope | 36 B; SHA-256 `31930bd7175248a6377f6ecde5d9e88a28ba3eb7ded19b14c96ca57ebca7be1c` |
| Independent init association | RVA `0xA590`; outside the observed root function |
| Synthetic checkpoint | Original 174 tests plus four 012 regression tests: final **178 PASS** |

Read the complete result, manifest, source review, actual context/budget reviews,
primary rule review and synthetic input identities from the evidence package.
Verify their hashes and fetch the current feature HEAD through Connector;
the later 012 documentation/test closure is separate from the pinned invocation
source commit above. Preserve the existing isolated worktree and confirm it is
clean before beginning. Do not replace the retained image with another DLL.

## 2. Primary evidence and selected behavior

The [Microsoft version-1 format source](https://github.com/MicrosoftDocs/cpp-docs/blob/e61067551f92a1c7beaad20c04b9f5ccfd2b5dd6/docs/build/exception-handling-x64.md#L156-L158)
defines opcode 3 as one node with unused reserved OpInfo. Its frame register
and scaled offset come from the UNWIND_INFO header. The current pinned
[Microsoft .NET implementation](https://github.com/dotnet/runtime/blob/ed84dd37d6728e9d0d50a37a323eddcc5c57cd71/src/coreclr/unwinder/amd64/unwinder.cpp#L814-L820)
does not use OpInfo in that branch; an [earlier pinned revision](https://github.com/dotnet/runtime/blob/421be955e4b70cddf583b10f5ad99814b713fb87/src/coreclr/unwinder/amd64/unwinder.cpp#L793-L799)
agrees. Primary decoded-source hashes and short excerpts are retained in
`primary-rule-review-012.json`.

This is a consumer-compatibility basis. It is not an assertion that the format
specification expressly authorizes every nonzero reserved encoding or that the
exact Windows build used in 011 has been verified. Keep that distinction in
the new result. The scope remains static indexing, not execution of unwind
semantics or certification of the full PE/runtime behavior.

Create a new versioned parser, for example `producer_bcrypt_pe_013.py`, from
the exact frozen parser. Keep the original byte-identical. Within only the
`opcode == 3` branch, the selected acceptance predicate is:

```python
(frame & 15) != 0 and (version == 1 or info == 0)
```

The preceding supported-version guard remains unchanged. Thus version 1 may
retain any four-bit OpInfo in this opcode, while version 2 still requires zero.
The nonzero frame-register condition remains mandatory. Do not use this rule
for ALLOC_LARGE, PUSH_MACHFRAME or any other opcode. Do not skip a record,
patch `_require`, bypass map/read validation, or alter the actual image.

Preserve every raw byte, including operation `0x63`; do not normalize it to
`0x03`. A versioned result must explicitly name the compatibility policy and
record that nonzero version-1 SET_FPREG OpInfo was ignored by this indexer.
The existing raw envelope is sufficient to retain each original OpInfo;
prefer a fixed-size policy notice over an unbounded redundant event list.
Apart from this acceptance delta and explicit version/policy reporting, retain
the original mapping, allocation, import, unwind, chain, overlap and identity
behavior. Review the complete source diff against the frozen file.

## 3. Required synthetic RED/GREEN evidence

Use the existing synthetic PE fixture and the immutable 012 regression anchors.
Do not load the actual image while designing or testing the change.

| Case | Required behavior |
| --- | --- |
| Version 1, opcode 3, frame register nonzero, OpInfo 0..15 | One slot each; new parser retains the original OpInfo/raw bytes and records the compatibility policy when nonzero |
| Same info-6 fixture through the frozen parser | Original line-438 refusal remains; all four 012 regressions remain valid |
| Version 2, opcode 3 | Info 0 retains its old result; every nonzero info retains the old refusal |
| Zero frame register | Rejected for zero and nonzero info; a nonzero frame-offset nibble with zero register still fails its original header guard |
| Original three width-2 operations before slot 6 | Original slot progression and envelope preserved; the new rule affects only that operation's reserved info |
| Invalid ALLOC_LARGE / PUSH_MACHFRAME info | Original refusals unchanged |
| Unsupported versions/opcodes, truncated operand slots, code order | Original refusals unchanged |
| Chain depth/cycle/frame mismatch, range/overlap/ambiguous mapping | Original refusals unchanged |
| Existing accepted synthetic PE | Same full structural result except the explicitly versioned policy metadata |

Retain an actual failing new expectation before implementing the selected
predicate, then the passing result. The unchanged historical info-6 refusal is
not itself evidence that an unimplemented compatibility feature passed.
The full-envelope accepted synthetic neighbor may relocate its handler to the
fixture's mapped executable section, but that relocation must remain explicit
and must never be applied to the original image or immutable evidence anchor.
Run the affected existing pure suites; do not manufacture broad native test work.

## 4. New complete-index resource contract is a required gate

012 admitted only a failure context: 8 KiB core, 15 KiB report and a 16 KiB
marker/report pair. Those figures do **not** admit a complete PE index. The
old parser can repeat import symbols inside descriptor tables and `iat_index`;
JSON serializes shared objects twice. Escaped names, raw-hex unwind records,
runtime entries, directory metadata and owner identities must all be counted.
Do not bound output from input size alone or from the size of the 012 sample.

Before creating a 013 invocation manifest, independently derive and review:

1. The complete successful and failure-result schemas, including fixed policy
   notice, input/source identities and any retained bounded refusal details.
2. Maximum parser work and in-memory structures from every source-level cap;
   output duplication and ASCII/control-character escaping before serialization.
3. Explicit numerical limits for core, transport, stderr/diagnostics, manifest,
   report, marker and combined persistent output. Account any protocol header
   and overflow-probe byte separately. A cap failure is a consumed refusal,
   never a truncated report labelled a complete index.
4. A proved pre-serialization/streaming bound which avoids first allocating an
   unrestricted result encoding and only then checking its size.
5. A reviewed absolute five-second completion-admission deadline and bounded
   process ownership. If the required proof cannot fit that contract, stop at
   the synthetic design checkpoint before registering or using actual bytes.

The numerical full-index output allocation is deliberately unassigned here:
there is no implemented serializer or reviewed upper-bound derivation yet.
Completing that derivation is part of the next concrete implementation task,
not permission to inherit the 012 allocation or make a trial actual call.

The 012 owner/context/helper are frozen and hard-bind their schema/source
semantics. Use a separately versioned owner and, if needed, a small new helper;
do not make the old owner permissive or reuse its consumed output directory.
Retain source-before-import verification, exact resolved Python identity,
isolated worker startup, one exclusive/fsynced marker before launch, at most
one worker and GO, full pre/post identities, bounded protocol, owned-process
reaping, exclusive output plus file/directory fsync and full readback. A late
save/readback must produce a nonzero owner exit. Result admission requires
the complete report jointly with the clean owner exit.

Any refusal-context handling must be part of that same one failed call with
separately reviewed bounded extraction. Do not call the frozen 012 wrapper
after a failure as an unregistered second parser invocation.

## 5. Execution sequence and stop conditions

1. Confirm latest feature/main/draft heads, 11 native workflow runs and unchanged
   original inputs. Read task instructions and the full 012 evidence.
2. Implement only the versioned compatibility delta and synthetic cases above.
   Preserve the old parser, 012 sources/tests/outputs and original native files.
3. Implement and prove the new schema, serializer, resource budget and owner.
   Independently review exact code, source identities, caps, failure paths and
   test evidence. Resolve Critical/Important findings before actual admission.
4. Review all workflow events/path filters for the exact new filenames. Create
   immutable blobs/tree/commit through GitHub Connector. Fully read back the
   new exact-input manifest and #198 registration, including all numeric caps.
   Activate only the feature ref using the expected old HEAD and force=false.
5. Invoke at most **one** separately registered 013 actual-image parser call.
   It consumes its reservation on timeout, crash, unavailable result or any
   refusal. No new ID, automatic retry, record skip or alternate input follows.
6. Independently validate the complete result and original-byte relationships.
   An accepted versioned structural index still does not admit native success
   or runtime root cause. Another refusal becomes the next precise handoff;
   stop and retain it instead of stacking unreviewed compatibility changes.

If a complete index is admitted, use only its bounded static facts to define a
later analysis target around the separately established init RVA. **No objdump
call, including version query, is included in 013's selected scope.** Any later
disassembly needs its own exact tool/input/range/output-budget review and
registration; the unused 011 disassembler allowance is not transferable.

## 6. Permanent boundaries

The original 011 result remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`:
outer Job total 3 instead of 2, no admitted native_summary. The 012 actual
refusal and spent marker remain immutable. Do not compile or run Windows/native
fixtures, reacquire the DLL, load/execute it, download symbols, inject/hook,
write process memory, expand permissions, relax token/ACL/Job gates or activate
a native workflow as part of this offline slice.

Main `87f86770cfc7e1941603074ace914e0c7faf10d3`, draft/HOLD PR250/248/207,
consumer/cost DEFER, VERSION 5.6.0, disabled M1b/service work and the cost
excess remain unchanged unless independently authorized and reviewed later.
Production C++, native runners and workflows remain unchanged. A1-A5, deletion,
ordinary qualification and canonical_evidence_sealed stay false. No merge or
release is selected. Understanding startup still leaves the original IPC
capability-lifetime prerequisite and a qualified producer writer owner ahead.
