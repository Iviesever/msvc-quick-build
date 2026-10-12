# 013 complete-index resource and invocation contract

This contract implements the numeric-budget checkpoint in the approved
`docs/20261011-013406-producer-handoff/native_startup_handoff_013.md`.
It binds the separate 013 consumer-compatibility parser and its sole-worker
offline observation. It does not change any frozen 011/012 file, parser cap,
native-execution qualification, production behavior, workflow or cost decision.

## Frozen implementation and acceptance boundary

The parser has exactly one acceptance change: for opcode 3,
`(frame & 15) != 0 and (version == 1 or info == 0)`. The frame-register guard
remains. Version 2 and every other opcode, width, mapping, ownership, range and
work guard remain unchanged. Original bytes are neither rewritten nor
normalized. The new index adds schema `bcrypt-pe-index-013-v1` and policy:

```json
{"id":"AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED","nonzero_opinfo_ignored":false,"notice":"CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED"}
```

The boolean records whether this same parser instance actually ignored a
nonzero version-1 OpInfo. This is a selected consumer compatibility policy;
it is not evidence about the exact captured Windows build's implementation.

All paths in the following table are under `tests/native/`.

| Source | Bytes | SHA256 |
|---|---:|---|
| `producer_bcrypt_pe_013.py` | 32,061 | `f2d9ee012615b8d901a90c59b9a422a9993c98ac6fb8c929d5cf8a6ac2cb2bf3` |
| `bcrypt_index_codec_013.py` | 11,968 | `f5fac1a0861d41585ab72d8c1d2cad90c29e22d3818921c5c89021da3dad1cb0` |
| `bcrypt_index_observation_013.py` | 5,426 | `c4aa265511ef63aa314034ee59e77a0ba1d223c4762972f59dd94216e9ad99dc` |
| `bcrypt_index_io_013.py` | 11,112 | `cb4c336e24e41f71f04cfd6db38a00348e2f56ec75741215fd71ad910c270a40` |
| `run_bcrypt_pe_index_013.py` | 12,128 | `30e6c27e0f7e63b7326f2a9b5ce1a313e00c29658efc3ea82ef29a32769a1e4f` |

The frozen parser remains 31,555 bytes, SHA256
`941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba`.
Reversing the selected predicate and fixed metadata additions reproduces that
source exactly.

## Codec profiles and hard output guards

Every public codec call checks exact built-in types and uses the following
fixed profiles. Integers are limited to `[-2**63, 2**64-1]`; floats, cycles,
subclasses, unsupported types, duplicate keys and noncanonical JSON are refused.
Shared acyclic values are charged on every serialized occurrence.

| Profile field | CORE | REPORT | MANIFEST | MARKER |
|---|---:|---:|---:|---:|
| bytes | 67,108,864 | 67,125,248 | 32,768 | 1,024 |
| nodes, including object keys | 2,097,152 | 2,101,248 | 8,192 | 128 |
| decoded characters, including keys | 33,554,432 | 33,570,816 | 32,768 | 1,024 |
| depth | 16 | 18 | 8 | 4 |
| structure bytes, occurrence accounting | 536,870,912 | 537,919,488 | 2,097,152 | 32,768 |
| logical work units per call | 536,870,912 | 536,870,912 | 4,194,304 | 65,536 |
| value-string characters | 4,096 | 4,096 | 4,096 | 1,024 |
| key characters | 64 | 64 | 64 | 64 |
| members per object | 64 | 64 | 64 | 64 |
| items per array | 65,536 | 65,536 | 256 | 64 |

Encoding measures structure and exact escaped length before allocating one
fixed-sized bytearray. Bounded `iterencode` chunks fill equal-length slices;
the final immutable bytes copy is explicitly counted. A string token is at
most `12 * 4096 + 2 = 49,154` ASCII bytes. Object-key chunks are measured against
the key limit even when the value-string profile is smaller.

Decoding checks the raw byte cap before its ASCII text copy. A bounded scanner
checks grammar, scalar ranges, duplicate keys, depth, nodes and container sizes
before `json.loads`. The conservative graph bound `192*N + 4*C` is also checked
before construction. Canonical reencoding shares that decode call's remaining
work budget and must reproduce the exact input bytes.

| Invocation/output field | Hard limit |
|---|---:|
| completion-admission seconds | 5 |
| actual parser calls | 1 |
| core bytes | 67,108,864 |
| worker READY header | 33 B |
| worker DATA header | 5 B |
| worker stdout including merged stderr | 67,108,902 B |
| additional overflow probe | 1 B |
| GO on separate worker stdin | 9 B |
| consumed marker | 1,024 B |
| final report | 67,125,248 B |
| runtime report plus marker | 67,126,272 B |
| preregistration manifest | 32,768 B |
| manifest plus runtime report and marker | 67,159,040 B |
| owner stdout | 0 B |
| owner stderr, including final LF | 65 B |
| refusal traceback nodes | 64 |
| refusal context | 8,192 B |
| core metadata with index replaced by null | 16,384 B |
| each of five source files | 65,536 B |
| input image | 1,048,576 B |
| pinned Python executable | 67,108,864 B |

The runtime output directory contains only `consumed.json` and `report.json`.
Source, input, historical evidence and review records are separately retained
inputs and audit material. They are not silently included in the runtime
two-file byte claim.

## Complete-index proof

The author's structural proof, recomputed independently, derives a success
core bound of **42,060,832 bytes, 1,473,808 nodes, 18,197,712 characters and
88,751,088 sort units**. The independent derivation is tighter:
41,968,194 bytes, 1,468,331 nodes, 18,122,243 characters and 87,492,922 sort
units. This contract uses the larger author bound for admission headroom.

The proof expands all fixed result fields and source cap domains. It charges
4,096 combined normal/delay symbols twice: under descriptors and in the IAT
index. All integers receive a conservative 20-digit allowance and all printable
ASCII source names may double under JSON escaping. Descriptor limits do not
multiply the combined symbol cap. Mutually exclusive chain/handler metadata is
overcounted deliberately. A chain stores a runtime-function record, not a
recursive nested unwind object.

For at most `U=16,384` unwind records and `S=65,536` combined code slots, raw
envelopes total at most `18*U + 2*S` bytes. Their raw hex therefore contributes
at most **851,968 characters**. A single envelope is at most 528 bytes and its
hex string at most 1,056 characters. Success core JSON depth is at most seven;
the final report adds one level.

Using the codec's named accounting model, the larger success bound gives:

- Encode: `2*(N+C+S_sort) + 4*B <= 385,088,544` units.
- Decode, including its canonical reencode:
  `3*B + 2*N + encode <= 514,218,656` units.

Both fit a 536,870,912-unit call. These units count the named bounded operations;
they do not count every CPython instruction or internal copy and do not imply
wall duration. The parser's own 4,194,304-unit limit remains separate.

The owner report wrapper has an independently synthesized conservative shape
of 3,785 bytes, 251 nodes, 2,583 characters and 15,365 occurrence-accounted
structure bytes. This fits all REPORT increments over CORE. Refusal context
is derived from the same call's source-bound scalar frames and fixed policy:
`512 + 64*72 + 16*48 + 212 = 6,100` bytes, below its 8,192-byte guard.
It includes neither the image nor traceback objects. Extraction performs no
second parser, mapper or image read.

## Full lifecycle accounting

A normal successful owner lifecycle makes **10 external codec calls**:
three decodes and seven encodes. Two MANIFEST decodes, two MARKER encodes and
three small MANIFEST metadata encodes accompany one worker CORE encode, one
parent CORE decode and one final REPORT encode. The parent CORE decode includes
its own same-budget canonical reencode. Consequently the full index is
serialized **three times** over the lifecycle.

A non-null refusal context adds two small MANIFEST-profile encodes, for 12
external calls. The sum of the independent per-call work ceilings is:

- Normal success: `3*536870912 + 5*4194304 + 2*65536 = 1,631,715,328`.
- Non-null refusal: the previous sum plus `2*4194304 = 1,640,103,936`.

These totals are a call-site accounting proof, not an implemented aggregate
counter. They include the small owner bootstrap/marker calls before the
invocation clock. Two initial bootstrap `json.loads` calls also exist outside
the codec and are covered by the separate raw-manifest proof below.

## Explicit object and buffer storage

This model is for the pinned 64-bit CPython 3.12 platform's explicit data
objects, container backing allocations and controlled buffers. It excludes
RSS, allocator fragmentation, resident interpreter/standard-library storage
and kernel buffers. It is not an OS memory limit.

- Before codec import, the raw manifest is limited to 32,768 bytes. Its initial
  standard-library decode is covered independently, including integer-token
  length, graph, old/new key memo, container growth and raw/Unicode copies.
  A **16 MiB** bootstrap reserve suffices; the later MANIFEST 8,192-node and
  2 MiB profile is not claimed as a predecode guarantee here.
- The loose returned-graph bound is `192*N + 4*C`. Keys and aliased occurrences
  are charged repeatedly. The successful core's larger proof gives at most
  **355,761,984 bytes**. Key memo storage is separately bounded by
  `256 + 64*N`, including old/new tables during resize.
- A **2 MiB** codec transient reserve covers the one actively resizing
  container, bounded token copies, short duplicate-key sets, sorting tuples
  and encoder frames. The conservative generic codec formula is
  `REPORT.structure + 4*REPORT.bytes + 64*REPORT.nodes + 256 + 2 MiB`, or
  **942,997,760 bytes**.
- The parser's partial/returned graph and input are counted separately from
  its **32 MiB** private reserve. Independent private accounting is
  **25,607,328 bytes**. It includes the refusing-path transient 4,353rd IAT
  slot, claim tuples, ranges, sorting buffers, map keys, active paths and
  bounded strings; the successful IAT bound alone is 4,352.
- The stream including overflow probe is at most 67,108,903 bytes. The
  incoming bytearray's current object is at most 75,497,644 bytes, and old/new
  backing allocations together at most **150,995,288 bytes**.
- The worker is reaped before successful parent decode. A memoryview-based
  payload copy avoids an intermediate full bytearray slice; `del raw` releases
  the wire before decode. Save/readback buffers and retained source/input or
  executable-verification reads are included in their respective phases.

Even conservatively combining an unreaped failed worker and the parent's
report phase yields **1,010,106,712 bytes**, below the declared **1 GiB
(1,073,741,824 bytes)** explicit-payload envelope. This is an unsuccessful,
consumed path and never a completed-index admission.

## Sole-call ownership and completion admission

The exact canonical manifest binds call ID `013-complete-index-1`, schema
`bcrypt-pe-index-013-manifest-v1`, stage `complete_index`, input identity/init
RVA, all five source identities, pinned Python, exact argv, new output path,
all 17 owner limits and all four codec profiles. Validated paths are absolute,
canonical and ASCII, at most 512 characters. Subsequent IO-helper manifest and
identity reads enforce regular files and NOFOLLOW. Initial hash-bound bootstrap
reads have separate 32 KiB manifest and 64 KiB per-source limits. Bootstrap
hashes both IO and codec bytes before executing either. IO pins the reviewed
parser identity and validates the complete manifest and source set.

The retained input is 170,984 bytes, SHA256
`663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`,
with init RVA 42,384 (`0xA590`). The executable is
`/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12`,
30,894,944 bytes, SHA256
`fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7`,
CPython version tuple `[3,12,14,"final",0]`. The worker prefix is exactly this
Python, `-I`, `-S`, `-B`, the reviewed owner path, and `--worker`.

Reservation creates a new exclusive output directory and durable consumed
marker before any worker launch. One worker proves the manifest digest in
READY; one nine-byte GO carries the single unchanged deadline. Source/input
identities are verified before and after, with worker-retained verified bytes.

The clock begins immediately before Popen and is never refreshed. The worker
checks it before inspection, after inspection and after serialization. Parent
transport, clean exit/reap, decode, postidentity and final report persistence
are admission gates. A successful-looking report saved before a late file or
directory fsync/readback does not suffice: the CLI must also exit zero after
the final durability deadline check. Synchronous OS calls may return late;
the rule rejects late completion rather than promising universal OS return
times or completion for every maximum input.

Admission jointly requires the exact manifest and marker, bounded complete
wire/report, matching pre/post identities, one parser call, a reaped zero-exit
worker and clean owner CLI zero within the deadline. `complete_index` admits
only the full indexed result. A clean `observed_refusal` records the refusal
without admitting an index. Any error, deadline/cap failure or unclean exit
is consumed and unsuccessful.

## Evidence and remaining gate

Author proof: `output-budget-design-013.json`, 26,157 bytes, SHA256
`34a12092b32816b30589f918e0b6580eb0120aa1209d177eaba553890f842318`.
Independent proof: `output-budget-review-013.json`, 39,359 bytes, SHA256
`97438cb9299069dc1297a8a904de0bc5d421d2274d72bb7672df99b9881d6a4b`.
Its decision is PASS for the bound output budget and completion gates, with
zero Critical or Important findings. Review records are retained in the
separately delivered 013 evidence set.

`output-budget-review-addendum-013.json`, 4,484 bytes, SHA256
`ca97ea1de6cace5512a5e7917685314201df886116c84743737acffdec7affa3`,
replays the wrapper proof with the final `f5fac1...` codec. It preserves the
original review's historical probe identity and proves that its earlier codec
differs only by three module-description lines, with unchanged executable logic.

Fresh integration record `integration-check-004.json`, 4,184 bytes, SHA256
`761140f794d3c2da14c63930a4d82409c3203d2ce0eba57415d3978fb35f2de1`,
records 275 synthetic tests passing, unchanged nine candidate source/test
identities before/after, and no change to baseline tracked files. Its suite
duration is not an actual-owner duration. Actual 013 image inspection is zero
at this contract's preparation.

Final independent full-source/contract review, exact immutable Connector
readback, canonical manifest review and #198 registration must precede the
expected-HEAD feature CAS and the sole actual invocation. Root owns that
invocation. A later refusal or failure stops actual work, retains all original
bytes and becomes a separately reviewed next handoff. There is no retry, new
call ID, second compatibility change, native invocation, objdump operation,
alternate DLL or symbol download authorized by this contract.
