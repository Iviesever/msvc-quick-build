# 014 implementation validation and final resource accounting

## Publication checkpoint

This document records the reviewed implementation and finite synthetic validation
for the accepted 014 handoff. At this source-publication checkpoint the actual
call `014-init-static-1` has not been submitted. Immutable source readback, the
exact actual manifest, independent manifest admission, complete #198 registration
readback and an expected-old nonforced feature transition remain sequential gates
before that single call. A source commit alone is not actual-call admission.

The previous 013 observation is consumed and stays frozen. The fixed target is
runtime-function 169 in section 0 `.text`, original file/RVA
`[0xA590,0xA717)`, exactly 391 B, with SHA256
`e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44`.
Preferred VMA is `[0x18000A590,0x18000A717)` for ImageBase `0x180000000`.
Only a derived `target.bin` is supplied to the selected GNU tool. Outgoing
references remain observations in this interval, without following their targets.

## Frozen implementation

Five separately versioned modules, totaling 46,081 B, use normal imports into
the bootstrap's preverified retained module bytes. The bootstrap and selected
interpreter form the initial registered trust boundary; reading the bootstrap's
own identity does not retroactively authenticate its initial execution.

| Source under `tests/native` | Bytes | SHA256 |
|---|---:|---|
| `bcrypt_disassembly_model_014.py` | 16,716 | `3b355306adfdc1ac13c2b193663bc36fb702784779b34891bf4b1a585dc55f12` |
| `bcrypt_disassembly_listing_014.py` | 5,164 | `f21cd65291e7a079885b6c7dc76b65f309ca311d06f3904cd1b145968b8e8e6a` |
| `bcrypt_disassembly_io_014.py` | 10,976 | `18596e673d8f352a7b49c1955e608e931d69c7dd01d7b9ded47ee402fe90e72e` |
| `bcrypt_disassembly_owner_014.py` | 8,228 | `e0ac62273ebc5fa9c71b53f1e79e7ffeaa6aa7d055fd3ff847188f812ceea1b5` |
| `run_bcrypt_disassembly_014.py` | 4,997 | `22e8820fdf397ad342dd3130324d97e140765fb026f62baa7db17054f251ec3a` |

The complete ten source/test files total 119,382 B. The source publication adds
exactly these ten files and `issue.md`, `implementation_plan.md`,
`resource_contract_014.md`, and this document in this directory. All 799 prior
tracked files, modes and 8,189,105 bytes match baseline
`02b6fae17ee36f0011b7bae0f9102478b76ba42c`. The three preceding design documents
are preserved at their reviewed bytes; the final numerical addenda are here.

Before launch the owner validates the exact manifest, consumes its exclusive
output directory and writes the marker, verifies all pre-identities and selects
the target, then prepares the target and two capture files and durably reads
back all four files. It makes at most one GNU launch reservation. Child stdout
and stderr are owned regular files. Completion requires a reaped child, stable
captures, exit zero, empty stderr, both captures strictly below their cap,
complete verified instruction coverage, all post-identities, full durable
readback/close and a timely clean owner CLI exit. The owner rejects nondefault
SIGCHLD disposition. Its cleanup polls before signaling an already reaped PID.
`child_handle_obtained` states what is actually observed from Popen.

## Verified tests

| Evidence | Result | Meaning |
|---|---|---|
| Existing 013 integration baseline | 275 PASS | Existing pure/synthetic behavior preserved; no consumed actual call rerun |
| Final five-module 014 integration | 121 PASS, 3 explicit SKIP, zero failures/errors | The skipped cap cases were already consumed in their separate finite run |
| Model TDD | 26 assertion RED, then 26 GREEN | Strict manifest, bounded JSON and target selection |
| Listing TDD | 42 assertion RED, then 42 GREEN | Address domains, full grammar and byte/length coverage |
| IO final pure suite | 24 PASS | Identity/owned descriptors, exclusive paths and durability; initial-fstat leak closed with RED/GREEN |
| Owner/bootstrap review regression | 28 PASS plus one SIGCHLD regression PASS | Pure mocked lifecycle; no duplicate actual child |
| External controller regression | 12 PASS | Real RED for post-persistence cleanup deadline, followed by GREEN; zero real subprocess calls |

These runs overlap and are separate pieces of evidence, not additive counts of
unique tests. The final integration log is 18,567 B, SHA256
`c3408b42c6280c01b960118b6c05813fae363ba099856f1fb937e3db756baa84`.
Baseline log is 43,758 B, SHA256
`d552b5e019d500e9259e3d2ee0e664fa97213d203451c862bfd430a2f97f5562`.

Exactly three admitted constructed Python cap children were run once: stdout
overflow and stderr overflow both ended by SIGXFSZ (-25), retaining exactly
131,072 B in the respective capture; a partial writer exited zero after writing
exactly 131,072 B. All three were reaped and the parent's five resource limits
were unchanged. Reaching the cap remains refusal regardless of return code.
The complete 393,216 B of raw captures is retained. Do not rerun these cases.

### Finite GNU validation

Exactly seven constructed cases were executed sequentially through the reviewed
external controller. Each controller CLI exited zero; every GNU child exited
zero and was reaped. The intentionally incomplete instruction is an expected
owner refusal even though GNU itself returned zero. No version/help probe, real
image disassembly or native fixture was used in this validation.

| Case | Constructed target bytes | Expected and observed owner result |
|---|---:|---|
| Different file/RVA/VMA domains | 2 | Complete; 2 instructions |
| Zero run, with `-z` | 391 | Complete; 196 instructions |
| Maximum one-byte instruction count | 391 | Complete; 391 instructions |
| Fifteen-byte instruction | 15 | Complete; 1 instruction |
| Truncated `48 8b` | 2 | Refused at listing, `LISTING_ASSEMBLY`, owner exit 1 |
| Branch outside slice | 3 | Complete; 2 instructions; outgoing target not followed |
| RIP-relative indirect call | 7 | Complete; 2 instructions; referenced memory not read |

The synthetic manifest `source_commit` field deliberately identifies the
preexisting baseline, not a publication of the new code; all five actual source
hashes bind the implementation used. This convention is limited to synthetics.
The actual manifest must identify the fully read-back immutable source commit.

The frozen plan is 6,088 B, SHA256
`311eb4ae7f30c5cc650101d694f34cba3feeba0ff9e3887695adaf284af82d7b`.
Root's seven CLI results are 2,757 B, SHA256
`73e22699202fee0aa3eead385f5d1be73c462e0fcb8437b3f64fcd8d0991248b`.
The root full-byte readback is 24,805 B, SHA256
`b4447aa4169944fa6df60140a49df1a37d5734abc9dc3e1ca0a490567287d283`.
Independent runtime review admitted all seven expected outcomes, with zero
open Critical/Important/Minor findings. It independently reconstructed 811
target bytes and 596 raw records; the six complete cases' 594 six-field
instruction records exactly matched the owner reports. All 91 case files and
105 review references passed full second-process readback. The sealed review
`gnu-synthetic-runtime-review-001.json` is 66,771 B, SHA256
`e31e26181f8da23806c3fb9278bf6e52afdd01ff6c05d5a0139bd94f7360ccc0`;
its separate readback is 28,294 B, SHA256
`789dbce8577a32e1e1a184ed6b8f41051fed4d7cdab801a8b7f61c96cbcf426c`.
A spent case is never reused.

## Final resource accounting

The selected GNU child has hard/soft limits of 268,435,456 B address space,
2 CPU seconds, 131,072 B file size per regular output, zero core file size and
64 open descriptors. The selected owner admission deadline is 12 seconds from
its bootstrap's initial clock. The external controller uses 20 seconds from
before manifest loading and checks again after all persistence and cleanup.
These are acceptance deadlines, not universal bounds on OS creation, kill,
wait, close or fsync return. CORE=0 alone is not a claim about every global
core-pipe policy. No whole-host filesystem or descendant sandbox is claimed.

The environment is exactly `LANG=C`, `LC_ALL=C`, `TZ=UTC`,
`DEBUGINFOD_URLS=`. There is no inherited PATH or loader environment. The
selected installed GNU binary, eight dependency files, loader cache, nine
aliases and five absent preload/hwcaps paths are verified. This is a concrete
installed-tool trust boundary and static dependency selection, not an audit of
every possible dlopen or the complete interpreter installation.

| Controlled persisted content | Bound, bytes |
|---|---:|
| Owner `consumed.json` | 1,024 |
| Owner `target.bin` | 391 |
| Owner `tool-stdout.bin` | 131,072 |
| Owner `tool-stderr.bin` | 131,072 |
| Owner `report.json` | 524,288 |
| Owner five-file subtotal | 787,847 |
| Independent manifest | 32,768 |
| Owner plus manifest | 820,615 |
| Outer marker | 1,024 |
| Outer owner-stdout capture, including overflow detection byte | 1 |
| Outer owner-stderr capture, including overflow detection byte | 66 |
| Outer report | 8,192 |
| Outer four-file subtotal | 9,283 |
| **Owner plus manifest plus outer** | **829,898** |

The owner's 65-byte diagnostic allowance is already included in the outer
stderr allocation. It must not be added again. The external controller emits a
normal fixed-schema banner of at most 180 B, or a fixed 49 B failure banner,
outside these nine managed files and the independent manifest.
An outer `completion_candidate=true` requires the controller's clean final CLI
exit, the owner exit/cleanup evidence and the independently admitted owner
report; the candidate field is not sufficient by itself.

Five sources and fourteen actual identities total 37,706,928 B. Two checks of
these nineteen files total 75,413,856 B. Two separate manifest checks add at
most 65,536 B, so this identity-phase content bound is 75,479,392 B; bootstrap
and outer source-loading reads are separate. The generic 64 MiB identity
aggregate limit permits 128 MiB across two checks of the nineteen files, plus
64 KiB for the two manifest checks. Hash reads use 65,536 B chunks; successive chunks can coexist, so the
controlled hash-buffer peak is **131,072 B**, not one chunk.

The report grammar allows at most 391 instructions, each 1..15 bytes, at most
512 bytes per raw line and 384 ASCII assembly characters. A maximum instruction
record is 937 B; the maximum list is 366,759 B and wrapper at most 16,384 B,
giving **383,143 B**, within the 524,288 B report cap. The model codec scans raw
JSON depth and lexical nodes (including keys) before `json.loads`, rejects
duplicate keys and invalid numbers during decoding, and measures the complete
graph and encoded size before serialization. The bootstrap/outer initial
manifest parsing is bounded by the 32,768 B raw cap and precedes complete model
validation; it does not have the model codec's per-node preallocation guarantee.
Complete manifest validation occurs before observation. Invalid schema,
duplicate keys, floats and nonfinite values refuse.

The full controlled durable-readback payload bound is 1,836,423 B. The final
conservative parent application/temporary-payload accounting is **78,721,129 B
(about 75.074 MiB)**, as derived in the independent owner/bootstrap review. This
replaces the earlier tentative 64 MiB parent assumption. It is **not** a parent
RSS or address-space hard limit. Interpreter/compiler/library internals,
allocator overhead, OS accounting and the separate outer controller are outside
that owner payload estimate; no
parent RLIMIT_AS is installed. The 256 MiB child RLIMIT_AS remains separate.

## Independent review evidence

All evidence names below are relative to the delivered 014 audit directory.
Historical review candidates and RED evidence stay intact; the named final
reviews bind the frozen source bytes and contain no open Critical/Important/
Minor findings.

| Final review | Bytes | SHA256 |
|---|---:|---|
| `source-review-model-io-014.json` | 33,622 | `bf14899b98687db95891ee82a059891419cf0a67216ebbffd399df6776dddac7` |
| `source-review-owner-bootstrap-014.json` | 29,990 | `b416e116b65a5d132a2571df724f3081a30723c0c1af04a1ce62473126322945` |
| `source-review-owner-bootstrap-014-readback.json` | 5,208 | `36276638ca90a204925ff1e9629ab14213b667a5a799ad154456dfbe28f6f877` |
| `io-implementation-budget-addendum-014.json` | 1,493 | `fbcd8661016b5e40840d20f70c86b7f08f23841ae8df462861c047f6b75f055c` |
| `outer-cli-review-014.json` | 8,718 | `9e0c20d37e5cb6782a5f0cb58bfa0194a604f3fad42a393c7189445fa13fb9f0` |
| `gnu-synthetic-plan-review-001.json` | 20,759 | `1f7b9bd5d05f765be6b7158f00764c1d6c7b596e62383c7ff9cbaf625cfd9b27` |
| `workflow-path-review-014.json` | 164,548 | `984868cc1d8e24c7957dda7c359abab73e0c4d94fa6584a6d1997dda97155ec5` |

The external controller is an audit artifact, `run_bounded_cli_014.py`, 8,227 B,
SHA256 `0c795fbb4549d7c229d4b49a21102bedf9349434fc22064c2427c192619c24cd`.
It is bound separately in actual registration. Its reviewed whole-lifecycle
deadline regression closed a real failure after output persistence during
selector cleanup. The source publication contains exactly the reviewed fourteen
paths; the later closure may add only `native_startup_result_014.md` and
`native_startup_handoff_015.md` in this directory.

All 52 baseline workflow files, 283,977 B, were fully read and matched to their
Git blobs: 37 have no push event, 14 restrict push to main, and one has a feature
push with twenty exact historical paths. Neither the fourteen source paths nor
the two closure paths match that feature list. This conclusion is specific to
the existing feature push; opening a PR or changing main/dispatch scope would
need a separate event review and is not selected.

Fresh prepublication scope through 2026-10-12 07:24:25 UTC preserved feature
`02b6fae17ee36f0011b7bae0f9102478b76ba42c`, main
`87f86770cfc7e1941603074ace914e0c7faf10d3`, the three open draft/HOLD PR heads,
no feature PR, #198 at its 013 closure and the same eleven attempt-1 completed
failure runs. This snapshot is evidence, not a substitute for the fresh
expected-old and post-transition checks required at actual activation.

## Permanent interpretation limits

Native 011 remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`: outer Job total
3 instead of 2, no admitted native_summary, and `0xC0000142` with failed API and
cause unknown. Native history remains 11 runs, 10 compiler drivers, summed
compiler Job TotalProcesses 23, seven native fixture invocations and zero
ordinary MQB runs. The Linux synthetic/tool processes above are not additional
Windows native fixture invocations.

Consumer/cost remains DEFER, +3.0963 ms / +26.70%; VERSION 5.6.0; M1b/service
disabled. A1-A5, deletion, ordinary qualification and
`canonical_evidence_sealed=false` remain in force. IPC capability lifetime and
the qualified producer writer owner remain unresolved. No build/native run,
new DLL, symbol/dependency download, workflow activation, permissions/product/
VERSION/cost change, merge or release is included in 014.
