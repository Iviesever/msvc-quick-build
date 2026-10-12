# Selection and resource contract 015 — reviewed / preactual-ready

Status: **REVIEWED / PREACTUAL-READY; ACTUAL ADMISSION PENDING**.
Source/resource and independent verifier source reviews are sealed with no
open critical/important/minor findings. Both preparation slots have completed
and remain consumed; the 167 B digest is established below. The five finite
GNU calls are also consumed and admitted after full independent runtime
review and root joint readback. Exact final review/admission identities are in
[validation](validation_015.md#publication-checkpoint-evidence).
Actual `015-helper-static-1` has not been admitted or executed.
This publication revision records those results and the remaining finite
actual contract; it does not create another preparation or GNU allowance.

## Completed preparation evidence

Evidence names in this table are relative to
`/workspace/scratch/a8b3faf101c5/evidence-015/`.

| Evidence | Bytes | SHA256 |
|---|---:|---|
| Source/resource review `source-resource-review-015.json` | 25071 | `cbe7b1d41b5016fcc47b898bee51581581db8e122e314d721e481eec95d91dfe` |
| Independent source review `independent-verifier-source-review-015.json` | 20214 | `6f12f7ce7512065a9fc6b8f7b55e789e8e054b7b11d64c8785c8e900772486ff` |
| Frozen context index `preparation-contract-snapshot-015.json` | 2008 | `07430e091c71538f94b02c62d98386ea504b330d02910baba96e65753e3c690e` |
| Primary manifest `selection-manifest-015.json` | 4887 | `89e8d0faa6c02156ac7869b5f477e205bd81c0ba98a5d2ef84beda70661413c6` |
| Primary report `selection-015/report.json` | 1681 | `8af5d4639b4bc85de43b7485d5f586afae43dbff0b2d46981a57af57e66e0628` |
| Independent manifest `selection-independent-manifest-015.json` | 4233 | `f0b8cc5cc976dc00002c7fe32d304a005ee4a4ea5681a6a98ddd27f052b3f2a5` |
| Independent review `selection-independent-015/review.json` | 8598 | `69e922779bc4e094e01f0f3102724acfd3f5546e0b9e89d50c0d9e744ce0c850` |
| Primary external completion `primary-transport-completion-015.json` | 899 | `8493a37f883f34ee3d0048973d3e36f24f5428fcaeb3669b20b6aba806d2ceac` |
| Independent external completion `independent-transport-completion-015.json` | 3619 | `876de8087a1ac5e3f99c8937de19a34b627cbcc72244ba7d9bf82a0b6f01ebf4` |
| Finite GNU root readback `synthetic-root-readback-015.json` | 17828 | `0327b52f56ac70583ca0b7a557e3e93ea65a07410ea5a166c393dc63688f2d55` |

Both real containing root CLI executions exited 0, with exactly one submission
and one image-read reservation per slot. Each reports one verified full image
read. Independent review decision is `ADMIT_015_SELECTION_IDENTITY`, with
complete primary evidence verified, zero child processes and zero additional
target files. Both results bind the same 167 B digest:
`c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1`.

The frozen context index preserves all five original preparation documents and
their repository origins. Those snapshots and the sealed historical primary
manifest remain unchanged: the latter's `source_commit` and `target.sha256`
are still null. The separate actual canonical manifest must use the immutable
source publication commit and the non-null digest established by these
completed selections. A result does not retroactively change an input manifest.

## Fixed candidate and commitments

Baseline closure is `68d5a913399a64e8c99e8ecbfa073b668f603430`.
The admitted 014 call at RVA `0xA612`, raw `e835b2ffff`, points to preferred
VMA `0x18000584C`. Candidate: runtime 105, section 0 `.text`, file/RVA
`[0x584C,0x58F3)`, exactly 167 B; preferred ImageBase `0x180000000`.
Runtime entry file offset `0x224EC`; unwind RVA/file offset `0x1DD78`, 12 B,
raw retained metadata `010a04000a3406000a320670`, version 1, flags 0, prolog 10,
four code slots, no chain or handler in the admitted record.

| Fixed original input | Bytes | SHA256 |
|---|---:|---|
| `evidence/offline-011/bcrypt-image.bytes` | 170984 | `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| `evidence-013/offline-013/report.json` | 271920 | `ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e` |
| `evidence-013/runtime-review-admission-013.json` | 20232 | `c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b` |

Those paths are relative only in this table to
`/workspace/scratch/a8b3faf101c5/`; every executable manifest uses full canonical
paths. Bind the exact 014 manifest, owner report, runtime joint admission,
final closure admission and final result/handoff document identities as the
candidate provenance. These are retained bytes, not replacement observations.

013 admission proves the complete index was admitted. Its entrypoint and
concrete_next_static_target continue to identify runtime169/391 B. New code
checks section.raw_file_offset + RVA - section.virtual_address at both helper
endpoints and checks runtime_functions[105] and its retained unwind record.
No new PE parse, adjacent-byte interpretation or tool trial establishes a SHA.

## Two explicit preparation slots — completed and consumed

| Slot | Fixed logical identifier | Original image content allowance | New target retention |
|---|---|---:|---:|
| Primary | `015-selection-primary-1` | One full 170984 B read | One 167 B file |
| Independent | `015-selection-independent-1` | One full 170984 B read | Zero additional target files |

Each slot checks one canonical O_NOFOLLOW regular FD, exact size, complete
SHA256, read-before/after fstat and matching path identity. Keep the verified
small image in memory and select only the declared 167 B from that same
snapshot. Never hash one image then reopen another for selection. Across the
two slots, at most 341968 B of original image content and two logical 167 B
selections are allowed. This is a selection-only total. The separately
registered actual scope allows two complete image identity passes of its own.
The independent verifier compares every selected byte
with the primary retained file and never calls the subject selection function.

Each fixed slot is consumed at root submission/reservation, including mkdir or
marker failure. Failure has no retry at a replacement path. The independent
slot opens only after clean primary completion, not as a repair attempt.
Each also hashes the fixed index/admission/provenance/source identities listed
by its final manifest. The fixed image, index, index admission and caller report
are the only original data snapshots retained for selection. Bootstrap may
also retain the bounded source set before compilation. The independent
verifier additionally retains its fixed primary manifest, seven primary files,
bounded external captures and completion receipts; every control-evidence cap
belongs to its separate proof. Remaining provenance is stream-hashed. Each
full identity pass may issue one EOF detection request of at most 1 B. A
consistent declared-size file returns 0 content bytes; a nonzero result rejects
the pass and consumes the fixed slot.

Primary directory:
`/workspace/scratch/a8b3faf101c5/evidence-015/selection-015`.
Its three names/caps are `consumed.json` 1024 B, `target.bin` 167 B, and
`report.json` 8192 B: **9383 B maximum named content**.
The independent entrypoint is
`/workspace/scratch/a8b3faf101c5/evidence-015/verify_selection_independent_015.py`.
Its output directory is `evidence-015/selection-independent-015` with only
`consumed.json` up to 1024 B and `review.json` up to 16384 B: 17408 B total.
The exact source and manifest SHA are external prelaunch gates. Its separate
Python CLI deadline is 12 s, zero child processes, stdout zero, fixed diagnostic
at most 65 B. Root separately records final containing CLI status and captures.
Both require exclusive creation, file/directory fsync, complete byte readback,
all handles closed and a final deadline check. A stored success candidate alone
is insufficient: clean containing CLI exit and full retained evidence are
required for root admission.

## Runtime controls and source-specific proof gate

Hash/read chunk is at most 65536 B. Account for the previous and next chunk
coexisting (131072 B), retained chunks plus join, strict JSON bytes/graph,
all retained inputs, source buffers, instruction records, serializers,
wrappers, diagnostics and persistence readback. The exact admitted index has
26721 nodes and 171892 string characters: its 192 B/node + 4 B/character
application graph charge is 5818000 B. Other graphs need their own bounds.
Compiler/interpreter baseline and native allocations must be distinguished
from explicitly accounted application payload; do not claim a parent RSS cap.

Primary owner before-phase identity reads are 5 identities + 5 provenance +
6 sources + manifest = 17. The after phase skips image content and performs
16 full identity reads; image canonical/path stat is compared with exact
integer dev/inode/mode/nlink/size/mtime_ns/ctime_ns. Total is 33 file identity
passes and exactly one image content pass. Source/bootstrap/outer prechecks
are additional source reads, never additional image reads. The selector may
hash the same retained image bytes again in memory without reopening a file.

The reviewed preparation proof binds six source files at <=32768 B each,
manifest <=32768 B, image <=262144 B, index <=327680 B, index admission and
caller report <=32768 B each, fixed provenance role count and byte total.
Its graph/serialization terms and numerical sums were independently reviewed
before either original-image selection read. The unchanged application-data
envelopes are preparation owner **70042827 B**, actual owner **87701139 B**,
independent verifier **127379958 B**, outer **47883505 B** and transport
**3890512 B**; [the resource proof](resource_proof_015.md) gives every row.
These are separate accounted payloads, with compiler/stdlib/interpreter and
native allocator baselines excluded, and are not RSS bounds.

Independent verification has 10 bootstrap plus 33 before and 32 after full
identity passes: **75 passes**, with declared content bounded by **94131134 B**.
It additionally permits at most **75 EOF requests of at most 1 B each**;
consistent files return 0 bytes, while any nonzero return refuses and consumes
the slot. Its sole original-image full read is 170984 B; post verification uses
the saved exact path/stat identity without another content read.

Selection has **zero GNU or other target-tool child processes**. The new outer
may launch exactly its one registered Python preparation owner. It separately
bounds owner stdout/stderr, reaps that owner and records final CLI completion.

Reviewed deadlines are preparation owner 8 s / containing outer 12 s; actual
owner 12 s / containing outer 20 s. They are admission deadlines including
success persistence/readback/close, not promises that an OS call cannot block.
Failure cleanup has one bounded wait and cannot turn late completion green.
The exact outer CLI and external transport timeout must be separately reviewed.
For primary selection the fixed outer CLI is canonical Python `-I -B`,
`tests/native/run_bounded_cli_015.py`, selection manifest path, its full SHA256,
and `evidence-015/cli-selection-015`. Root transport deadline is 15 s, stdout
cap 1024 B and stderr cap 65 B, with one detection byte per stream. Its retained
raw captures and final receipt are separate transport evidence. The independent
CLI is canonical Python `-I -B`, the fixed independent entrypoint, independent
manifest path and SHA256; its root transport deadline is 15 s.

## Subsequent actual observation: distinct scope

The actual target digest is the independently established
`c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1`.
Actual source and manifest are new 015 artifacts and must bind both completed
selection records and the immutable published source commit.
Actual directory is `evidence-015/offline-015`; outer is `evidence-015/cli-actual-015`.
The registered actual call is `015-helper-static-1`, permanently at most once.
Preparation is not this call and does not authorize it.

Reviewed actual owned-file caps: consumed 1024, target 167, GNU stdout 131072,
GNU stderr 131072, report 262144 B; total **525479 B**. Each actual capture must
be strictly below its cap for success, and stderr must be empty. The three old
IO/JSON/listing files stay byte-for-byte unchanged. A new IO subclass overrides
_caps so inherited complete readback also uses the new 167/262144 limits.

New policy independently limits records to 167. With the unchanged six-field
listing representation, record encoding <=937 B and wrapper <=16384 B give
`167 * 937 + 166 + 2 + 16384 = 173031 B`, below the new report cap. The 166
commas and two list brackets are included. Full byte coverage,
positive instruction lengths and pre-append end checks establish this record
count even though the reused generic listing has a larger old ceiling.

The independently reviewed 015 child callback installs CPU 2 s,
AS 268435456 B, FSIZE 131072 B, CORE 0 and NOFILE 64 as fresh explicit 015
choices, hard=soft with each pair read back before exec. A default SIGCHLD
disposition is required before relying on child exit/reaping evidence.
014's allowance does not transfer. Producer limits precede capture, and file
limits are not a host-wide write/process sandbox. The owner payload, manifest,
outer streams/graphs and total named-file proof have been recomputed from the
frozen 015 call graph. Exact actual manifest/CLI review and root admission are
still required. The old 78721129 B payload is not a 015 budget.

## Validation, publication and interpretation boundaries

Pure constructed tests preceded changed behavior. Exactly five separately
reviewed GNU fixtures were executed under the fixed finite plan; all five
reservations are consumed and no replacement calls remain. Independent
runtime review of their retained outcomes and root joint admission passed.
New real cap-test children are zero. No 014 fixture budget, actual directory
or manifest is reused. All negative evidence is retained.

Independent code/resource/validation/path review, immutable source readback,
complete canonical actual manifest review, exact #198 registration readback,
fresh expected-old nonforced transition and final admission are required before
one actual call. A missing dependency or unexpected identity stops the phase;
there is no download, substitute tool, version/help probe or expanded target.

A helper listing establishes static bytes and references only. Runtime paths,
live bindings, failed native API and cause of 0xC0000142 remain unproved unless
separately established. Native 011 and all existing cost, product, IPC,
qualification and canonical-evidence HOLD boundaries remain unchanged.
