# Actual-event bcrypt snapshot diagnostic 011

Status: `CLOSED_ACTUAL_EVENT_BYTES_ADMITTED_PE_REFUSED`.

The [011 execution plan](native_startup_plan_011.md) was registered and executed
once. Its independently reviewed raw capture supplies the exact file bytes
associated with its own actual child's bcrypt LOAD_DLL event. The original
workflow failed and original `result.json` remains
`INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`, without `native_summary`: outer
native Job TotalProcesses was **3**, against the unchanged requirement **2**.
The permitted native diagnostic exit **3** is not that failing predicate.
The subsequent frozen pure PE inspection refused the input with
`UNWIND_OPCODE_INVALID`. No disassembly was authorized or performed. No earlier
attempt is rerun, replaced, or reclassified.

## Immutable execution and original evidence

| Item | Recorded identity |
| --- | --- |
| Execution commit | `2d0f48ce551e0a2acdb1cf34628264458fa30ca7` |
| Tree | `a2aa09ae66e2af43609622a12dbbd487abd43abf` |
| Sole parent | `0a7509e48dbd829bae3795a7bcca0ffcf387a83d` |
| Registration | [#198 comment 6108577968](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6108577968) |
| Workflow / run / number / attempt | `380958963` / [38136004405](https://github.com/Iviesever/msvc-quick-build/actions/runs/38136004405) / **11** / **1** |
| Job | `114459882647` |
| Original artifact | `11700264622`, `producer-bcrypt-snapshot-011-38136004405-1` |
| ZIP | 1,416,464 B; SHA-256 `64a83269c2cfc2dc7611ec5f81a49044ff2fb5a3c8b126b9af503c96ad878608` |
| Original stdout | 1,279,420 B; 4,510 strict JSONL rows; SHA-256 `9783ca9c1fce65809375412b7991ca08add260fd023ba11defac9c1b7b30dd74` |
| Original result | SHA-256 `87672e4b00a20f1278899f6d1d0d5179b41931f829acbabcfb0694e7304431af` |

The full registration body was read back byte-for-byte before the expected-head,
non-force activating ref update. The candidate tree's 771 files and 100 trees
were independently reconciled; all 11 changed source/plan files matched their
reviewed blobs. The original ZIP has 55 members / 5,800,748 expanded bytes, each
checked for CRC, length, SHA-256 and extraction equality. All **17 actual runner
inputs** are exact. Of the 42 repository members, **39 are byte-identical**;
three historical Markdown copies (`native_startup_handoff_011.md`,
`native_startup_result_010.md`, `walkthrough.md`) differ only by checkout CRLF
conversion. Both archive and Git fingerprints are retained separately, with no
normalization of the original copies or relaxation of executable-source gates.
The 13 runtime originals and final 12-member pre-result inventory also match.

Two preliminary archive checks stopped at those historical newline differences
before the complete inventory was available. Those analysis stops and the later
exact explanation are retained; neither caused a native retry. A separate
pre-admission hash guard also stopped when the root observed an unpublished
review draft just before its final API-count label correction. The final
published review hash was required before any snapshot output was created.

## Implementation and source verification

The new collector retains one non-inheritable S-only read pin opened from the
actual event hFile. It performs two complete, bounded reads with explicit EOF
checks and five file-identity observations. The new strict validator reconciles
the actual PID/TID, executable identities, event/continue order, raw row sizes,
every collector operation, identity and byte comparison, continuous pin lifetime,
owned Job close, actual/debug exit and shared cleanup deadline. The companion
pure PE parser rejects incomplete or ambiguous structure before static decoding.
The frozen 008 client and all 001-010 implementations remain unchanged.

Source review found and closed concrete malformed-input, lifecycle, ownership
and PE table-alias defects; their initial failing tests and later passing results
are retained. The final exact source admission had no open Critical or Important
finding. The completed source suite was:

```text
.......................................................................................................................
----------------------------------------------------------------------
Ran 119 tests in 0.168s

OK
```

That is 28 frozen PE tests, 9 frozen capture tests, 40 new snapshot tests and
42 new PE tests. These are pure Python tests; no local native compilation or
fixture invocation was used. `combined-pure-tests-011.log` is 219 B, SHA-256
`7f248df5db11503896adf20db0206d7425fda0dae867f8ccf04aaf93a8dd774b`.

## Build, held executable images and native completion

There was one compiler-driver invocation and one native fixture invocation.
Compilation exited 0 in 3.907 seconds, with compiler Job total 2 / active 0,
completed membership, no timeout and empty stderr. MSVC 14.44.35207 used the
registered `/MT /Z7 /Od /W4 /WX /permissive-` command and no incremental link.

Parent and child EXEs are each 542,720 B. Parent SHA-256 is
`4581a2fe89d514bf93b0e1d87eb9edbbdf26dcf362e137df35cce21a5af0bb42`;
child SHA-256 is
`febd7dce7444e635d0eb0bd3f507545c2d3657609ec60ae741ddef2d6f659298`.
Independent PE derivation locates the GlobalFlagsSet DWORD at file offset
471,296. Its four bytes change from `00000000` to `02000000`; all bytes outside
that field are identical. Nine copy writes, one DWORD patch and one flush are
accounted. Six held complete image passes each use nine data reads plus EOF,
for 60 reads total. All four acquired image/anchor handles are non-inheritable
and released, both final image inventory reads match, and actual C's three EXE
identity observations bind to the retained child image.

Native S was PID 8044 / TID 5616; actual C was PID 7896, initial TID 3052.
The native invocation exited 3 in 0.110 seconds, without timeout or stderr;
outer Job final total was 3 / active 0, with completed membership and no recorded
process-runner error. The owned child Job changed from total 1 / active 1 before
resume to total 2 / active 1 after debug exit, before its recorded close. The
extra member's identity and cause are not established. Natural observation of
C's own exit does not substitute for the unchanged outer total-2 requirement.

All 644 debug events received successful matching continuations. Ten event
hFiles were checked and closed before continuation. There were 628 debug-string
records containing 79,456 captured bytes; the remaining events were one process
creation, nine DLL loads, two thread creations, two thread exits, one initial
breakpoint and one process exit. Debug and actual process exit both report
`0xC0000142`. The EXIT_PROCESS event's reporting TID is 4588, distinct from the
initial thread; it still belongs to the same actual C. No startup frame or IPC
case was observed. The initial ntdll breakpoint is not an internal bcrypt cause.

## Exact event-object DLL acquisition

The selected bcrypt event is 428, `bcrypt_selected` sequence 3135, referring to
path record 3134. It belongs to C PID 7896 / TID 3052, with event hFile 264 and
image base `0x7FF96AC50000`. The recorded path is
`\\?\C:\Windows\System32\bcrypt.dll`; the event object, rather than the pathname,
is the acquisition authority. ReOpenFile produces read-only, non-inheritable
pin 252. All five identities agree:

| Property | Value |
| --- | --- |
| File bytes | **170,984** |
| SHA-256 | `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Volume serial, decimal | `10116456482831564288` |
| File ID | `18bc0000000001000000000000000000` |
| Allocation size / positive hard-link count | 172,032 B / **2** |
| Directory / reparse / delete-pending | false / false / false |

Each pass reads 65,536 + 65,536 + 39,912 bytes and then obtains a successful
one-byte EOF probe returning zero bytes. The first pass emits every file byte;
the final pass's exact byte equality is supported by the native full-byte
comparison records and reviewed source. It is not a separately emitted second
raw byte copy. First observations occur at elapsed 62 ms; final observations
and release occur at 94 ms within the shared cleanup deadline at 5,094 ms.

The event hFile closes at sequence 3164 before continuation 3165. The retained
pin stays alive through owned Job close 4469, final queries/read/comparison, and
its own successful close 4487 and release record 4488. The raw closure is 4489.
There is no contradictory intervening close of the retained pin.

| Collector/capture budget | Actual | Frozen limit |
| --- | ---: | ---: |
| Whole bcrypt file | 170,984 B | 1,048,576 B |
| Principal APIs, including reads | 32 | 58 |
| ReadFile, including EOF | 8 | 34 |
| Identity observations / comparisons | 5 / 4 | 5 / 4 |
| New collector rows | 48 | 100 |
| Largest new row metadata | 804 B | 1,024 B |
| Native complete stdout | 1,279,420 B | 33,554,432 B |
| Native complete JSONL rows | 4,510 | 16,384 |
| Retained C fixture leaves / payload | 0 / 0 B | 0 / 0 B |

The source proof also retains the conservative worst-case combined envelope:
16,371 rows / 28,699,648 bytes. This measured smaller output does not replace
that proof or increase any original bound.

Both independent runtime reviews admit this raw diagnostic snapshot while
preserving the original execution failure. Only after their final hashes were
checked was `bcrypt-image.bytes` exclusively created outside the native fixture,
flushed, and completely read back in a separate process. Its complete bytes
match a fresh strict reconstruction from unchanged raw stdout. It describes
the actual 011 file only and does not establish the original 010 DLL identity.

## This run's initializer mapping and offline analysis

The narrow loader records at sequences 4419 / 4425 (events 637 / 638) give the
unique bcrypt init/failure pair, both at `0x7FF96AC5A590`, with matching actual
C PID/TID and DLL path. Their captured 130 / 156-byte prefixes have LF followed
by one terminal NUL. The full original string length remains unknown under
the debug API's narrow length field; no omitted text is inferred. Subtracting
this run's base `0x7FF96AC50000` yields **RVA `0xA590`**, independently derived
from these records. The old 010 RVA was not an input constant.

The exact archived pure parser (31,555 B, SHA-256
`941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba`)
was invoked **once**, as `inspect_bcrypt_pe(image_bytes, 42384)`. It raised
`PeValidationError`, reason/message **`UNWIND_OPCODE_INVALID`**, and returned no
complete PE index. `bcrypt-pe-admission-011.json` therefore records
`DENY_DISASSEMBLY_PE_VALIDATION_REJECTED`. No entrypoint function range,
preferred ImageBase, IAT index or runtime-function index is admitted for
downstream use. Passing the synthetic tests does not establish compatibility
with every valid Windows image.

The first exception report retained type, reason and message but no traceback
locals. Its process had already exited when exact rejection-location evidence
was requested. The specific unwind RVA, file offset, opcode/OpInfo and slot
remain unknown. The frozen source has three guards using this same reason:

| Source condition | Reason is raised when |
| --- | --- |
| `UWOP_ALLOC_LARGE`, opcode 1 | OpInfo is neither 0 nor 1 |
| `UWOP_SET_FPREG`, opcode 3 | OpInfo is nonzero or FrameRegister is zero |
| `UWOP_PUSH_MACHFRAME`, opcode 10 | OpInfo is neither 0 nor 1 |

These source predicates do not identify which guard this image reached, nor
whether an earlier slot-width interpretation led there. This refusal alone
does not prove that the image is malformed, corrupt or modified, that the
parser is wrong, or that an unwind record caused bcrypt's runtime failure.
The original input, source and refusal remain unchanged. There was no
diagnostic replay to recover locals, alternate decoder, acceptance-rule
change or post-fix retry under 011.

The offline disassembler allocation is entirely unspent: **0 objdump calls**,
including `--version`; **0 decoded code bytes / basic blocks / callees**;
**0 tool-output bytes**. The proposed output validator and command runner were
halted before any implementation file or synthetic test was written. The
DLL was never loaded or executed by the offline phase, and no symbols or other
DLL build were obtained. Internal initialization calls, return tests and candidate FALSE
paths are consequently still unknown.

## Review fingerprints and remaining boundary

| Evidence | Bytes | SHA-256 |
| --- | ---: | --- |
| `source-review-admission-011.json` | 266392 | `5bc798064cf37824609bf39b2c513c8bb5f6f0f3e5dd5055be7dc1b35b8f6058` |
| `runtime-native-review-011.json` | 59069 | `85db68c6b5231436b7e7691ba49d38ee252d325ceac4b5285f5cde57c03dd28b` |
| `runtime-image-review-011.json` | 88973 | `5ee0944d6d261687dca64ac70eb38bad984c7b4e6786073968b88055e0b7efb8` |
| `runtime-review-admission-011.json` | 3988 | `49255bd5d056f91ac3e35f332ade433ebdea92ab2fd3a6ea7d50fbf69c82eca9` |
| `image-readback-011.json` | 643 | `f69cdcab2d9548a2d3335e444d322cac99c2d403c8a6481231f11c9fc5a20950` |
| `init-address-evidence-011.json` | 26229 | `2dd11c8d8484228cf60aab510cbe55e29e257414793facecc8e2638638c1354c` |
| `bcrypt-pe-report-011.json` | 2804 | `ba22936cb6d6bd7a0e6fd4000f8c616e6e4eb4841c850a54098c2f5db934289f` |
| `bcrypt-pe-admission-011.json` | 3419 | `bcd22d860e4b4d0e8759bd615e1fa143a5cad7ba4d18303410d36860a8aa476a` |
| `pe-refusal-independent-review-011.json` | 37623 | `21066548ebaba6ff834e4e3a6f2514c04a3bd79dbf79c4bad512769417080c26` |

After 011: eleven prerequisite workflow runs, ten compiler-driver invocations,
compiler Job TotalProcesses sum 23, seven native fixture invocations, zero
ordinary MQB producer runs, and no observed IPC case. All A1-A5, deletion,
ordinary qualification and `canonical_evidence_sealed` remain false. The
metadata-writer counterexample and original 006-011 outer Job failures remain.

Main stays `87f86770cfc7e1941603074ace914e0c7faf10d3`; PR250 remains draft/HOLD
at `a26b103685589ab8a5b4df60d1b760ec445efdf5`, PR248 remains draft at
`e30f45ee5989d1a5e94a16a78ecb1d4f75a9443e`, and PR207's original draft/HOLD and
cost result are retained. Consumer/cost DEFER, disabled M1b/service jobs,
all 51 other workflows, production C++, `VERSION=5.6.0`, and the 21,000-byte
cost preparation SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`
remain unchanged. No merge, cleanup, force update or next native allocation is
part of this diagnostic closure.

The [next handoff 012](native_startup_handoff_012.md) is an offline parser
diagnosis using this already acquired image: first preserve a precise,
bounded refusal context without changing acceptance rules, then establish
the relevant format rule and minimal regression before considering a reviewed
compatibility correction. No new Windows compilation, native run or DLL
acquisition is needed for that step. The original 011 refusal remains separate
from any later versioned analysis. Startup, IPC, writer-lifetime and ordinary
producer qualification remain pending.
