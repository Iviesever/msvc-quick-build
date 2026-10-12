# Native startup handoff 016 — one CHAININFO-associated 83 B range

## Current completed checkpoint

015 has jointly admitted one bounded offline disassembly: helper runtime 105,
file/RVA `[0x584C,0x58F3)`, 167 B, SHA-256
`c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1`,
43 instructions. Read `native_startup_result_015.md` and its exact referenced
joint/independent reviews before implementing this handoff.

Source commit is `62addcd32bb7ec376719d5b830fa7fef0a2be0fc`, with parent
`68d5a913399a64e8c99e8ecbfa073b668f603430`. The final 015 closure adds exactly
the two closing documents to that source. Obtain the final closure SHA/tree,
closing-comment ID, final ledger count, scope receipt and outside archive
checks from the sealed final closure admission and the Issue 198 closure.
Use that **final closure** as 016 baseline and expected-old feature; source
commit 62addcd32 is an immutable implementation anchor, not the final handoff
HEAD. This document cannot embed the hash of the commit containing itself.

All 015 slots are permanently consumed: primary preparation 1, independent
preparation 1, constructed GNU 5, actual GNU / actual root submission 1 / 1.
015 made four original-image full reads / 683,936 B in total; ad hoc root or
reviewer original-image reads were zero. No 016 preparation, target retention,
digest establishment, decode or actual submission has occurred.

## The next question and exact metadata-only candidate

Determine what the four observed external conditions from helper 105 do in
the single CHAININFO-associated range below, including how any visible
EAX/EDI/EBX values are preserved, changed or routed toward already observed
015 instructions. Establish only what those exact bytes and explicit joins
support. Keep every outgoing destination outside this new range as an address
unless it can be joined directly to already admitted instruction records.
Such a join does not authorize opening any new target range.

The reason for choosing this range is concrete: it is the unique smallest
complete indexed runtime interval containing all four observed external
conditional targets, and its chain triple links back to helper 105. Metadata
alone does not establish executed order, failure behavior or instruction starts.

| Candidate field | Exact value |
|---|---|
| Runtime / section | 252 / section 0 `.text` |
| File range | `[0xDB3C, 0xDB8F)` |
| RVA range | `[0xDB3C, 0xDB8F)` |
| Preferred VMA range | `[0x18000DB3C, 0x18000DB8F)` |
| Selected size | **83 B** |
| Selected SHA-256 | **Unknown / null; not yet established** |
| `.pdata` row file offset | `0x22BD0` |
| Child unwind RVA / file | `0x1E61C` / `0x1E61C` |
| Child unwind bytes | 16 |
| Child unwind exact metadata | `210000004c580000f358000078dd0100` |
| Child version / flags / prolog / code slots | 1 / 4 / 0 / 0 |
| Child frame register / offset / handler | 0 / 0 / none |
| Child chain depth | 2 |
| Embedded chain triple | begin `0x584C`, end `0x58F3`, unwind `0x1DD78` |
| Embedded triple file offset | `0x1E620` |
| Parent runtime / `.pdata` row | 105 / `0x224EC` |
| Parent unwind RVA / file / bytes | `0x1DD78` / `0x1DD78` / 12 |
| Parent unwind metadata | `010a04000a3406000a320670` |
| Parent version / flags / depth | 1 / 0 / 1, no handler and no next chain |

The child chain object's `table_entry_file_offset` is the embedded triple
location `0x1E620`; the parent's actual `.pdata` row is `0x224EC`. Validate
these two locations independently. Compare the logical begin/end/unwind
triple, section mapping and file extents; do not compare the entire two
record dictionaries for equality.

| Admitted 015 source RVA | Exact branch bytes | Destination RVA | Offset in candidate |
|---|---|---|---:|
| `0x5883` | `0f88b3820000` | `0xDB3C` | 0 |
| `0x5897` | `0f85bc820000` | `0xDB59` | 29 |
| `0x58B4` | `0f85a7820000` | `0xDB61` | 37 |
| `0x58CC` | `0f8597820000` | `0xDB69` | 45 |

After a valid full 83 B listing, **all four offsets 0, 29, 37 and 45 must be
verified instruction starts**. They are not verified starts at this checkpoint.
A mismatch is a refusing result, not permission to decode again from the
individual offset, widen the slice or try another tool.

## Exact inherited inputs

Original image: 170,984 B / SHA-256
`663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`.
013 complete index: 271,920 B /
`ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e`.
013 runtime admission: 20,232 B /
`c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b`.
015 report: 6,740 B /
`5c55e875110e732068f3695d35504f40e7ae7f4e7bf32de4a7bf10b18f3a1e9b`.
The complete original index and admission are retained previous evidence;
their serialization must remain byte-for-byte unchanged.

Follow the closed 015 manifest and result chains for the canonical Python,
GNU binary, eight dependencies, loader cache, nine aliases and five absent
paths. Preserve the exact tool identity and four environment fields. Existing
version evidence is sufficient; no version/help probe or download is allocated.

## Required new selection and source contract

The old 015 selector requires a flags-0, nonchained, depth-1 unwind record and
is intentionally incompatible with runtime 252. Keep it, its actual manifest,
source publication and all consumed outputs frozen. Implement new 016 policy,
selection and phase entrypoints rather than changing the accepted target of
015 or invoking old 013/014/015 owner/run permits.

Before opening original-image content, write and independently review the new
finite preparation contract, source identities and resource proof. Verify the
already admitted index/image association, the exact 015 caller branches, both
runtime rows, the child's 16 B unwind and the parent's 12 B unwind. The new
selector must accept only this exact two-level relation: child flags 4/depth 2,
parent flags 0/depth 1. Reject missing records, changed triples, handler flags,
unexpected frame/code fields, a cycle or extra chain level, unmapped metadata,
wrong section/range, duplicates and truncation.

During each newly admitted preparation use one complete verified image
snapshot. Bind the candidate `.pdata` 12 B, child unwind 16 B, parent `.pdata`
12 B, and parent unwind 12 B to their indexed bytes in that same snapshot,
then select exactly the candidate's 83 B. A separately allocated independent
preparation must establish the same range and digest without calling the
subject selector or writing another target. Each preparation records its
computed candidate digest in its own result. Keep the admitted candidate
identity/digest unset until both real preparation completions and the
independent result are jointly admitted.
Any attempted slot, including setup or persistence failure, is consumed.

Pure tests should directly cover the CHAININFO change: the positive case in
which embedded and parent table offsets differ while the logical triple
matches; wrong flags/handler, wrong parent/tuple, extra level/cycle, bad range,
wrong section/digest, metadata truncation, source-set mismatch and consumed
outputs. Preserve separate file/RVA/preferred-VMA address-domain examples.
Use isolated tests that cannot discover native fixtures. Do not rerun 015's
five real constructed calls or any 013/014/015 observation. Any required new
constructed GNU validation must have its exact finite plan, predicates and
resource review sealed before its first new child. New real cap children
remain zero unless a concrete uncovered risk is explicitly allocated in that
new unconsumed plan and justified before execution.

## Resource ceilings and execution order

The new contract must recompute actual source/identity/provenance counts,
declared-content passes, object-graph payloads, listing bound, named-file totals
and cleanup/deadline paths. The 015 77-pass proof is evidence to consult; it is
not an automatic numeric proof for changed 016 inputs and code. Keep the new
target exactly 83 B, per-instruction length 1–15 B and instruction-count ceiling
83. No range concatenation, neighboring bytes or target-specific extra read
is allocated.

Use the existing 015 ceilings as maximum design constraints to re-justify:
manifest and each source 32,768 B; report 262,144 B; each GNU capture 131,072 B
with strict-below-cap success; owner/outer/transport deadlines 12/20/23 s;
GNU hard=soft CPU 2 s, AS 268,435,456 B, FSIZE 131,072 B, CORE 0, NOFILE 64.
Keep owned stdout/diagnostic caps, exclusive creation, full source-set checking
before any core compile, pinned entrypoints/interpreter, final fsync/readback/
close/deadline completion and three-field transport metadata explicit.
Application payload proofs must continue to exclude interpreter/compiler/
stdlib/native allocator and OS baseline; do not describe them as RSS bounds.

If the same two-preparation/two-actual-pass architecture is chosen and freshly
proved, explicitly allocate primary 1 + independent 1 + actual before/after 2
full image passes, for a maximum four full reads / 683,936 declared image
content bytes in 016. No root/reviewer ad hoc original-image content reads
are available. This is a contract ceiling to prove and register, not a current
execution admission. Later reconstruction from retained 83 B is separate from
opening the original image.

Proceed in this order under the user's continuation authorization:

1. Reconcile the exact final 015 closure, full references, archive checks,
   current feature/main/three PRs/feature PR count/all runs and ledger anchors.
2. Use an isolated worktree at that closure. Write the new selection contract,
   meaningful failing pure tests and small 016 implementation changes. Close
   independent source/resource findings before the first original-image read.
3. Complete the separately allocated primary and independent preparations;
   admit the non-null 83 B identity only after real clean containing exits.
4. Close the exact finite pure/constructed validation and recomputed resource
   proof. Check every intended publication path against all workflow rules.
5. Publish immutable source through Connector only; full-read every new blob,
   the complete tree and single-parent commit while the feature stays old.
6. Freeze and independently review the entire canonical actual manifest,
   exact GNU argv, ENV, tool/dependency/alias/absence set and unused outputs.
   Post/read back its exact Issue 198 registration. Use a fresh expected-old,
   nonforced feature activation and read back full scope and local state.
7. Issue a final one-slot admission and submit the new outer once. Failure,
   timeout, cap, refusal, interruption or late persistence consumes the slot.
8. Independently validate all 83 B, every address domain and the four incoming
   starts, then join the full runtime/resource completion. Root admits only
   supported conclusions, closes result/next handoff through the same guarded
   publication chain, and delivers a verified evidence package.

Keep direct callees, other runtime ranges and data-cell contents as explicit
unknowns. Do not load or execute the image as a Windows module, reproduce
native startup, broaden to a native fixture or use workflow dispatch/rerun
to answer this static question. Remote
writes remain Connector-only; no git push, main/PR update, merge, release,
version/product change or deletion is part of this next handoff.

## Continuing native and product boundaries

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

The metadata-associated 83 B candidate is a narrower investigation step.
It is not a diagnosed failing path or a native execution result. Preserve
these distinctions when later joining its branches to admitted 014/015 text.

## Direct evidence identities for the next gate

| Evidence path under evidence-015 | Bytes | SHA-256 |
|---|---:|---|
| runtime-review-admission-015.json | 24444 | `6cff1daa185450717d2c08aa23a89f6bd50608471eb647b37a64ca9ce106f437` |
| actual-byte-semantic-review-015.json | 18653 | `6a92d67cb679c01bfe1b2a91ee608e3ac7121461ab7bb5f63f81750ce73e4d97` |
| actual-runtime-resource-review-015.json | 20488 | `847face1159c83d39d8a4d61665bc0561c0b89a1b8de15fd6583fc468ed9f5c3` |
| actual-transport-completion-015.json | 6229 | `78421129bef61995ce46f24b1a4f4bf8ff0d2b72c1c0e2961d000fb652699e22` |
| actual-external-exec-result-015.json | 3514 | `808599f826134cd29abba5fdbe3f7d3754d819f6a8ea10109952fb8a6bdeded6` |
| actual-root-byte-readback-015.json | 6250 | `6f8d0cb381aa4a009f2b54f0545384c8a0a1ef92c542387dd94256b4adfa6dbf` |
| invocation-manifest-015.json | 8513 | `6d8ceb25ab8e3fd550fb01bd6136565b103de51c8554be30db1c60f8c9640ddb` |
| source-publication-readback-015.json | 5103 | `1150d3a07fc1eb69cc6523c7c00880f59663f7cd502665bebf96c51539b1268a` |
| source-publication-independent-review-015.json | 9795 | `5fa7b9bca5598a2aa5691f796eaeccb3f07dec1edb9d38b22fb21f9e8489b070` |
| selection-identity-admission-015.json | 2493 | `7d51bb380949cd3c386fd5b28a1f7641a6a7cf9f1a70e2382c30eea55870abaa` |
| synthetic-results-admission-015.json | 3278 | `171f20962e53c61031f84e38e06d885ebe301e74b196989ccfd917f61d7c476f` |
