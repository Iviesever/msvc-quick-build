# Next handoff 012: locate the frozen PE refusal on retained 011 bytes

Status: `OFFLINE_DIAGNOSIS_DESIGNED_NOT_IMPLEMENTED_NOT_REGISTERED_NOT_EXECUTED`.

This follows [closed diagnostic 011](native_startup_result_011.md). Its native
acquisition is complete for a separately reviewed raw snapshot, while its
original execution remains INCONCLUSIVE and its pure PE index was refused.
The immediate next step is a small **offline failure-context wrapper around the
unchanged parser**, using the already acquired image. No Windows compilation,
native fixture, new child, DLL reacquisition or workflow allocation is needed.
Do not rerun 011 or transfer its unused disassembler budget into a new native
attempt. The current feature branch and its isolated worktree remain available.

## Verified starting point

| Input / gate | Exact retained evidence |
| --- | --- |
| Native execution | Commit `2d0f48ce551e0a2acdb1cf34628264458fa30ca7`, tree `a2aa09ae66e2af43609622a12dbbd487abd43abf`, run [38136004405](https://github.com/Iviesever/msvc-quick-build/actions/runs/38136004405), number 11 / attempt 1 |
| Registration | [#198 comment 6108577968](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6108577968), fully read back before activation |
| Original result | `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`, no `native_summary`; outer Job total **3**, required **2** |
| Raw acquisition | C PID 7896 / initial TID 3052; unique bcrypt LOAD_DLL event 428; event hFile 264 -> retained pin 252; five matching identities, two complete reads and EOF |
| Exact offline image | **170,984 B**, SHA-256 `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Same-run init association | Loader init/failure at `0x7FF96AC5A590`, selected base `0x7FF96AC50000`, derived RVA **`0xA590` / 42384** |
| Frozen PE source | `tests/native/producer_bcrypt_pe.py`, 31,555 B, SHA-256 `941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba` |
| Exact invocation and result | One `inspect_bcrypt_pe(image_bytes, 42384)`; `PeValidationError: UNWIND_OPCODE_INVALID`; no complete result returned |
| Downstream gate | `DENY_DISASSEMBLY_PE_VALIDATION_REJECTED`; zero objdump calls, including version query |

Read the full original artifact and the independently sealed reviews before
working from a short handoff. The evidence package includes the original ZIP,
all members, source admission and tests, native/image reviews, immutable raw
snapshot admission, independently read-back `offline-011/bcrypt-image.bytes`,
init-address evidence, and the PE refusal/admission reports. Confirm each
input's length and SHA-256 rather than locating another same-named DLL.

This image belongs to 011's event, not retrospectively to 010. The observed
loader address is not an internal failed instruction or API. A complete PE
entrypoint/runtime/IAT index has not been admitted, even though the arithmetic
RVA association is independently supported by the raw loader records.

## The precise missing evidence

The first PE invocation only serialized exception type, reason and message.
The process exited without retaining traceback locals. The failing unwind
record, file offset, table index, slot and opcode/OpInfo are unknown. The
frozen source uses the same reason for three conditions:

| Guard in `_Image.unwind_info` | Failure condition |
| --- | --- |
| `opcode == 1` (`UWOP_ALLOC_LARGE`) | `info` is outside `(0, 1)` |
| `opcode == 3` (`UWOP_SET_FPREG`) | `info != 0` or the frame-register nibble is zero |
| `opcode == 10` (`UWOP_PUSH_MACHFRAME`) | `info` is outside `(0, 1)` |

Do not choose one from the reason string. A prior operation's slot consumption
could also affect which bytes are interpreted as the next operation; that is
a question to test against the located bytes and a primary format source,
not a finding about this image. Neither parser defect nor image malformation
is established by the retained refusal. It also supplies no bcrypt runtime
root-cause evidence.

Microsoft's [x64 exception-handling format](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64)
describes slot counts, variable-width operations, frame fields and chained
records. Check version-specific evidence before adopting any compatibility
change; a version-1 description alone cannot justify accepting a different
version's encoding. Keep language-specific unwind semantics and actual stack
effects outside this static indexing contract.

## Selected first implementation: retain context within the same failed call

Add a new offline-only wrapper, for example
`tests/native/run_bcrypt_pe_diagnosis_012.py`, plus its pure tests. Keep the
frozen 011 parser, native collector, snapshot validator, runner and workflow
byte-identical. Do not copy the parser merely to add logging, monkeypatch its
guards, skip unwind records or weaken an acceptance condition.

The wrapper should verify exact source and image bytes before import/use,
invoke the unchanged parser once, and catch `PeValidationError` while its
traceback still exists. Bind `f_code` by identity to the imported frozen
`_Image.unwind_info.__code__`, require `tb_lineno` to be one of its exact
guard locations 427 / 438 / 440, and require the adjacent failing `_require`
frame's `f_code is frozen_module._require.__code__`. Bind the owning exception
table frame to `frozen_module._Image.exceptions.__code__`. Select that unique innermost
guard frame, not the first frame with a matching name. Retain outer table/chain
ownership separately; do not confuse a parent
chain frame with the current failing record. From those matching source frames,
retain an explicit whitelist of scalars: guard location/function,
exception-table index and 12-byte entry when present, unwind RVA and file
offset, header version and flags, prolog/count/frame fields, slot, code offset,
opcode, OpInfo, justified current width and bounded chain path. Missing values
must stay explicitly unavailable.

In particular, the ALLOC_LARGE OpInfo guard runs before the current iteration
assigns `width`. A `width` key in that frame may still contain the previous
iteration's value. At that guard, always report `width: null` with
`not_assigned_for_current_slot`; existence in `f_locals` is insufficient.
The SET_FPREG and PUSH_MACHFRAME guards occur after assigning current width 1.
Test these stages with a preceding multi-slot operation so stale values cannot
masquerade as current evidence. Never manufacture a width after refusal.

Retain only already-bounded bytes for the relevant unwind envelope and the
adjacent offending slot, with offsets and hashes. A one-record envelope has
at most 4 header bytes + 512 aligned code-array bytes + 12 chained bytes =
528 bytes under the existing unsigned-byte count field, plus at most the
12-byte owning root exception-table entry. Header/slot views must reference
subranges of that envelope rather than expand the raw read. Use the current
record's already validated bounds; do not call `map`, `read`, `unwind_info`
or `inspect_bcrypt_pe` again to collect context. Never perform an unchecked
context read when mapping itself failed. Record unavailable bytes as
unavailable. Walk at most 64 traceback nodes and emit at most 32 chain RVAs;
stop on an unexpected identity, lineage or cap. Do not dump `self`, arbitrary
frame locals, full parser maps, the whole image or process state. Serialize
only whitelisted bounded values and enforce the output cap before writing.
Discard the traceback reference after serializing the small diagnostic result.

The wrapper changes error observability only. It must not continue parsing
after refusal or expose partial maps as a successful full PE index. The exact
011 input is expected to retain its original reason. An unexpected success,
different reason, missing/ambiguous context or output/time bound ends this
step with that observation, without a second automatic invocation.

### First-stage acceptance and fixed budget

Before using the real image, meaningful synthetic tests must cover all three
same-reason guards with distinct expected context, one- and multi-slot
progression, missing optional locals, chained context, out-of-bounds/truncated
mapping, and oversized/unserializable context refusal. Verify that the frozen
parser source is unchanged and that its prior accept/reject outcomes remain
unchanged on the existing suite. Preserve actual failing tests before the
wrapper implementation and passing results afterward.

Independently review the final exact wrapper and test bytes, source import
binding, output schema and bounds. Before the first real-input call, write
and fully read back an immutable offline invocation manifest containing the
exact input/source hashes, argv, output paths, maximum call count and budget.
Record it in the existing issue audit trail through the GitHub Connector.
This is an offline operation, not a new Actions run or a revision of the 011
registration. Before launching its one parser call, exclusively create and
fsync a durable call marker keyed by that manifest hash. An existing marker
refuses re-entry. A crash, timeout or missing final report still consumes that
call; do not delete the marker, choose another call ID or reinterpret the
missing report as permission to repeat it.

| First-stage item | Maximum / requirement |
| --- | --- |
| Native compilations / invocations / workflow activations | **0 / 0 / 0** |
| Real-image diagnostic parser calls | **1** |
| Real input | The retained 170,984-byte image, within the unchanged 1 MiB parser limit |
| Real-input call time | 5 seconds, measured and bounded by its owner |
| Persisted marker + refusal report | 16 KiB combined: marker <=1,024 B, report <=15,360 B, including metadata/raw hex |
| Raw byte context | One <=528-byte unwind envelope + one <=12-byte root entry, only where prior bounds permit |
| Traceback walk / emitted chain | <=64 nodes / <=32 RVAs |
| Chain depth / parser work | Existing depth 32 / 4,194,304 work-unit limits; new context work explicitly counted |
| Output ownership | Exclusive create, file/directory fsync, independent full readback |
| PE/objdump admission from a refusal | None |

Hash the image and source before and after the call, preserve the complete
failure report and manifest, and require an independent byte-level context
review. This first stage's deliverable is a uniquely located refusal with a
reproducible source predicate and a bounded minimal regression input, or an
explicitly retained inability to obtain that context. It is not a completed
compatibility correction or a static root-cause diagnosis.

## Evidence-dependent continuation within the offline work

Use the located bytes to decide whether the existing reader mishandles a
documented valid format or correctly refuses unsupported/invalid input.
Construct the smallest synthetic fixture preserving the relevant version,
slot widths/order, record bounds and failing field. Preserve the exact raw
record separately so minimization does not replace provenance. Obtain a
primary specification or maintained platform/toolchain source for the exact
rule and pin that source revision when its behavior matters.

Only a demonstrated valid encoding can justify a compatibility correction.
Create it as a separately versioned reviewed source slice; leave the frozen
011 code and original refusal intact. Start with the minimal failing
regression, keep malformed neighbors and all original range, role-alias,
chain, truncation and work-budget refusals, then obtain independent review of
both correctness and bounded work. Do not simply ignore `UNWIND_OPCODE_INVALID`,
accept all OpInfo values, skip the exception directory, switch off structural
validation or identify a Windows basename as proof of validity.

Applying such a corrected slice to the real image requires its own exact
source/input invocation record, a single declared verification call and full
readback. It is a new versioned offline result; it cannot overwrite or turn
the original 011 refusal into success. An unresolved rule, changed input,
remaining ambiguity or further refusal stops progression. There is no blind
edit-and-rerun loop or automatic cap growth.

Only after a complete PE result is independently admitted may the retained
static-analysis proposal proceed. Freeze the exact resolved tool identity,
version-verification call, argv, declared RVA/file intervals and output files
before the first disassembler call. Keep one DLL, at most 64 KiB unique code,
256 basic blocks, 32 direct in-module callees, 30 seconds total tool time and
8 MiB combined output, counting the version query. Byte-check every reported
instruction's RVA/file mapping. Stop at unresolved indirect targets,
cross-module boundaries, ambiguity or a cap. No DLL execution, symbols,
other build, software breakpoint, IAT hook, injection, memory write,
thread-context rewrite, ETW or permission expansion follows from this plan.

Any later candidate init call/return test/FALSE path remains static evidence.
An internal runtime observation would need a separate concrete design after
that analysis; a later loader event's volatile return register or GetLastError
is not proof of the earlier operation's result.

## Preserved product and history boundaries

This handoff has not been implemented, registered or executed. Begin from a
fresh read of the retained feature head after the 011 documentation closure,
not by assuming the execution commit is still the branch tip. All remote
mutations use the GitHub Connector, exact reviewed trees and expected-head
non-force updates. Preserve historical plans, tests, failures and spent counts.

Main remains `87f86770cfc7e1941603074ace914e0c7faf10d3`; PR250 / PR248 / PR207
remain their separate draft/HOLD states. Consumer/cost DEFER, the byte-exact
21,000-byte cost preparation, disabled M1b/service, production C++, VERSION,
all unrelated workflows and earlier native evidence remain unchanged. Outer
Job total-2 admission still precedes native_summary parsing. All A1-A5,
deletion, ordinary qualification and canonical_evidence_sealed remain false.
This offline prerequisite cannot qualify IPC delegation, exclusive writers,
native completion or the ordinary producer-to-cleaner handoff.
