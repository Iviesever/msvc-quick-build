# Source-specific resource proof 015 — reviewed / preactual-ready

Status: **REVIEWED / PREACTUAL-READY; ACTUAL ADMISSION PENDING**.
This proof describes explicitly accounted application data and named file
content. It is not a parent RSS/AS bound, a host filesystem sandbox, a native
benchmark or a claim that operating-system calls always return by a deadline.
The sealed source/resource review identifies exact source/manifest hashes,
retained tests and the separate preparation/actual/independent/outer/transport
lifetimes. Its open critical/important/minor findings are 0/0/0.

## Reviewed evidence and completed preparation

Evidence names below are relative to the fixed directory
`/workspace/scratch/a8b3faf101c5/evidence-015/`.

| Reviewed evidence | Bytes | SHA256 |
|---|---:|---|
| `source-resource-review-015.json` | 25071 | `cbe7b1d41b5016fcc47b898bee51581581db8e122e314d721e481eec95d91dfe` |
| `independent-verifier-source-review-015.json` | 20214 | `6f12f7ce7512065a9fc6b8f7b55e789e8e054b7b11d64c8785c8e900772486ff` |
| `preparation-contract-snapshot-015.json` | 2008 | `07430e091c71538f94b02c62d98386ea504b330d02910baba96e65753e3c690e` |

The review binds the five immutable documents under
`preparation-contract-snapshot-015/` and records their repository origins.
Those original review inputs remain unchanged. This publication revision adds
completed preparation evidence without rewriting that historical context.

Primary and independent selection both completed with one reserved and one
verified full image read, and real root CLI exit 0. Their combined original
image content is 341968 B across two reads. Both established the same 167 B
target SHA256:
`c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1`.
The primary retained one target file; independent retained no additional
target file. Exact manifests, reports and external completion receipts are
listed in [the selection contract](selection_resource_contract_015.md#completed-preparation-evidence).

The five named finite GNU calls are consumed. Their full independent runtime
review and root joint admission passed; exact references are recorded in
[validation](validation_015.md#publication-checkpoint-evidence). Actual `015-helper-static-1` has
not been admitted or executed. Its eventual canonical manifest must bind the
immutable source publication commit and non-null selected digest; the
historical preparation manifest keeps its original null fields.

## Phase structure and reads

The new bootstrap independently checks its six preparation or seven actual
source records before compiling any core module, then compiles retained bytes.
Root first hashes the executing outer/bootstrap and full loading set. The outer
also checks its pinned bootstrap, strict-decodes and canonical-checks the
manifest before core compilation. Runtime self-hashing is not substituted for
the external pre-execution identity gate.

Preparation owner identity passes: before 5 identities + 5 provenance +
6 sources + manifest = 17; after 16, skipping image content. The image is read
once, retained and hashed as a single verified snapshot; later path/stat
verification includes exact integer nanoseconds. Other identities have two
passes. Actual owner passes: 17 identities + 5 provenance + 7 sources +
manifest = 30 per phase, 60 total. Selection/independent evidence is verified
before image in both phases. Actual reads image content twice, retaining only
the first snapshot. Rehashing retained bytes is additional memory work without
an extra file read. Bootstrap, outer, root preflight and independent reads are
separate entries; they do not silently share these allowances.

Fixed provenance is exactly five roles:

| Role | Bytes |
|---|---:|
| 014 invocation manifest | 6149 |
| 014 runtime admission | 13826 |
| 014 final closure admission | 14757 |
| 014 result document | 19123 |
| Published 015 handoff document | 18113 |
| **Total** | **71968** |

All roles have full canonical path/size/SHA pins in the new policy and manifest;
individual cap 32768 B, aggregate cap 163840 B. Core identity files individually
and in aggregate are capped at 67108864 B. Hash chunks are 65536 B.

## Owner application-data envelope

Use `G(N,C)=192N+4C`, where N counts JSON nodes and C string characters under
the strict codec. Node ceiling is 32768 and depth ceiling 16. Numbers are
integers; duplicate keys, floats, nonfinite values and oversized integer tokens
are rejected. Retained input caps: image 262144, index 327680, admission and
caller 32768 each, selection report 8192 and selection review 16384 B. Each
source and manifest is at most 32768 B.

The figures deliberately retain a generic index graph so the same envelope
also covers constructed validation. The admitted index's exact 26721 nodes /
171892 characters would permit 5818000 B instead of the 7602176 B generic row;
this proof does not take that reduction.

| Accounted item | Preparation B | Actual B |
|---|---:|---:|
| Two manifest graphs | 12845056 | 12845056 |
| Index graph | 7602176 | 7602176 |
| 013 admission graph | 6422528 | 6422528 |
| Caller report graph | 6422528 | 6422528 |
| Selection report graph allowance | 6324224 | 6324224 |
| Independent selection review graph | 0 | 6356992 |
| Listing and fixed report graph | 0 | 766408 |
| Wrapper shallow copy | 0 | 40960 |
| Decoder text and pairs temporaries | 10223616 | 10321920 |
| Two possibly coexisting encoder temporaries | 17825792 | 25165824 |
| Bootstrap source/manifest/join buffers | 294928 | 327696 |
| Owner manifest and retained input raw bytes | 688128 | 712704 |
| All expected output content | 9383 | 525479 |
| First failed report raw bytes | 8192 | 262144 |
| IO read/join/stream temporaries | 1114132 | 1114132 |
| Listing line/regex/word work | 0 | 131072 |
| Popen private error pipe and fixed arguments | 0 | 2097152 |
| Bounded control dict/set/path/frame allowance | 262144 | 262144 |
| **Reviewed total** | **70042827** | **87701139** |

Decoder temporary allowance is `4 * sum(decoded-text caps) + 256 * 32768`.
Only one active decoder pairs workspace exists at a time; previously decoded
graphs are included separately. Encoder allowance is
`2 * (16 * max(report_cap, manifest_cap) + 256 * 32768)`, including manifest
canonicalization in the new `validate_manifest`. The preparation maximum is
32768 B; using its smaller 8192 B report cap would undercount 786432 B and was
corrected during independent review before execution. Failure tracebacks may retain
the first encoder/report while refusal persistence is attempted; those terms
are retained even when the preparation's `report_attempted` guard prevents a
second report attempt. A caught exception cannot silently erase that charge.
IO work is `3 * largest retained input/report cap + 2 * 65536 + 20`.

Actual listing/report graph is `G(128 + 13*167, 4096 + 462*167)`. Positive
instruction lengths, exact raw byte equality, contiguous addresses, and an
end-bound check before append imply at most 167 records. Each encoded record
is at most 937 B; commas/brackets are explicit:
`167*937 + 166 + 2 + 16384 = 173031 B`, below 262144 B report cap. The wrapper
is measured and encoded separately at a 16384 B cap before full persistence.

These sums exclude interpreter/compiler/stdlib baseline, executable module and
code-object implementation overhead, native allocator behavior, kernel page
cache, and separate processes. They are not inferred from measured memory use.
Independent verifier, outer and root transport have their own reviewed
numerical envelopes below; no owner's payload is presented as their sum.

## Separate verifier, outer and transport envelopes

The independent verifier uses the same conservative graph rule for each
simultaneously retained graph; even tiny receipts receive the full node ceiling.
This is deliberate over-accounting, separate from the preparation owner.

| Independent verifier item | B |
|---|---:|
| Three manifest graphs | 19267584 |
| Index graph | 7602176 |
| Admission and caller graphs | 12845056 |
| Two primary marker graphs | 12591104 |
| Two primary report graphs | 12648448 |
| Transport receipt graph | 6324224 |
| Submission graph | 6299648 |
| Transport completion graph | 6324224 |
| Independent review graph | 6356992 |
| Independent marker graph | 6295552 |
| Decoder temporaries | 10502144 |
| Two encoder temporaries | 17825792 |
| Bootstrap source and raw buffers | 294928 |
| Input and primary evidence raw buffers | 791851 |
| Owned output content | 17408 |
| First failed review bytes | 16384 |
| Compared slice | 167 |
| IO temporaries | 1114132 |
| Control allowance | 262144 |
| **Independent total** | **127379958** |

Independent bootstrap performs 10 full-hash reads, then the observation has
33 before and 32 after full-hash reads: 75 total. The sole original image
content pass contributes 170984 B; its after check uses exact saved state.
The fixed identities and maximum small receipt sizes bound declared content
read by 94131134 B. In addition, each of the 75 full identity passes may issue
one EOF detection request of at most 1 B. Consistent declared-size inputs
return 0 content bytes for those requests; a nonzero result refuses the pass
and consumes the slot. These requests are explicit in the allowance and do
not authorize a second image snapshot or image-content pass.

| Outer item | B |
|---|---:|
| Two manifest graphs | 12845056 |
| Fixed marker/result/argv/summary graphs | 163840 |
| Decoder temporaries | 8650752 |
| Two encoder temporaries | 23068672 |
| Source/bootstrap/manifest/canonical raw buffers | 557076 |
| Owned content | 9283 |
| Pipe capture | 134 |
| IO temporaries | 229396 |
| Popen error pipe and fixed arguments | 2097152 |
| Control allowance | 262144 |
| **Outer total** | **47883505** |

The outer canonicalizes before the model's premeasurement. An ASCII DEL may
expand to six encoded bytes, so its encoder allowance uses six times the
32768 B manifest cap. Fixed outer control graphs are bounded by 512 nodes
and 16384 characters; they are not arbitrary caller inputs.

| Root transport item | B |
|---|---:|
| Three bounded graphs | 688128 |
| Encoder temporaries | 786432 |
| Owned content | 11331 |
| Pipe and conversion work | 4364 |
| Same-FD readback | 8193 |
| Popen error pipe and fixed arguments | 2097152 |
| Source buffer | 32768 |
| Control allowance | 262144 |
| **Transport total** | **3890512** |

Each transport graph has at most 1024 nodes and 8192 characters. The exact
metadata is frozen in the execution plan as only `schema`, `call_id` and
`manifest_sha256`, each a bounded ASCII string. This premise matters because
`submit` copies caller metadata before encoding; the numerical envelope is
not asserted for arbitrary caller dictionaries. Independent transport uses
its lower stdout cap and still fits this conservative outer-transport sum.
Any subsequent GNU/actual admission must bind the same exact three-field
metadata premise and its concrete manifest SHA.
All three sums retain the same exclusions as the owner table and are separate
application-data envelopes, not a combined process-tree RSS assertion.

## Owned content and process limits

| Named content | Preparation B | Actual B |
|---|---:|---:|
| Owner files | 9383 | 525479 |
| Outer files (1024 + 1 + 66 + 8192) | 9283 | 9283 |
| Both directories | 18666 | 534762 |
| Including maximum manifest | **51434** | **567530** |

The independent verifier owns only marker 1024 plus review 16384 = 17408 B.
The external transport owns marker 2048, stdout detection file 1025 (1 for
independent), stderr detection file 66, and receipt 8192: at most 11331 B per
outer submission, or 10307 B for the independent CLI. These are file content
bounds, excluding directory allocation, external tool transport and archive
copies. Each directory/path is single-use; root submission consumes its call
even if exclusive creation or marker writing fails.

Preparation owner/outer/transport deadlines are 8/12/15 s. Actual owner/outer/
transport are 12/20/23 s. Independent verifier/transport are 12/15 s. Success
requires deadlines after persistence, complete readback and final close, plus
real containing CLI zero exits. Failure cleanup wait is at most one additional
second per controller; blocking OS behavior cannot be converted to success.

The five named synthetic GNU launch reservations have been consumed, one each.
The distinct actual scope permits at most one further GNU launch only after
its own final root admission. Each uses fresh hard=soft CPU 2 s,
AS 268435456 B, FSIZE 131072 B, CORE 0, NOFILE 64 before exec; the callback
reads installed pairs back. AS
provides a child virtual-address refusal envelope and file size limits the
capture producer. Empty stderr and captures strictly below cap are completion
conditions. These limits do not prove a host-wide write or process boundary.

Selection launches no GNU child; independent verifier launches no child.
Every outer and external transport launches only its one exact reviewed CLI,
with canonical Python and `LANG=C, LC_ALL=C, TZ=UTC, DEBUGINFOD_URLS=''` replacing
the environment. All source, binary, dependency/cache/alias/absence checks are
identities of existing files; no discovery process, version/help probe, install
or download is authorized. New real Python cap-test children are zero.
