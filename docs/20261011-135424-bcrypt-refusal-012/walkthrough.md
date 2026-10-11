# Walkthrough: closed refusal-context observation 012

Status: `CLOSED_LOCATED_REFUSAL_NO_STATIC_INDEX_HANDOFF_013`.

The user's next handoff is complete for the first offline refusal-context
stage. The [full result](../20261011-013406-producer-handoff/native_startup_result_012.md)
records the evidence and limits; the [013 handoff](../20261011-013406-producer-handoff/native_startup_handoff_013.md)
defines the next finite change. No product owner or native success is claimed.

## What changed and why

Original 011 retained actual C's 170,984-byte bcrypt image but only the parser's
reason string. Five new files add a bounded extractor, one-shot owner, IO helper
and synthetic tests. The extractor binds exact frozen code objects, source
lines and input identity while the single failed call's traceback exists. It
retains one root entry and leaf envelope without continuing the parser.

Source commit [`4b0e0154790a7b3d4ef2b06920819377aa077fd9`](https://github.com/Iviesever/msvc-quick-build/commit/4b0e0154790a7b3d4ef2b06920819377aa077fd9)
contains exactly those five files and the initial task issue/plan. Source and
budget reviews preceded [registration 6110066078](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6110066078),
full manifest readback, expected-head source activation and the unique actual
call. The later closure adds one observed-byte synthetic regression and
updates the current progress/result/next handoff documentation.

## Observed result

`012-refusal-context-1` completed with CLI exit 0, one parser call and a durable
consumed marker. The exact refusal is line 438, version 1, opcode 3
UWOP_SET_FPREG, OpInfo 6, frame-register 5. Root entry index 11 and the leaf
map to `0x22084` and `0x1D958`; the function range `[0x1830, 0x19BE)` excludes
init RVA `0xA590`. The three original width-2 operations reach slot 6 exactly.
This is structural refusal context, not a located DLL-initializer failure.

The 210-byte marker and 3,485-byte report total 3,695 bytes. Independent review
reconstructs the 1,289-byte core and 1,327-byte worker stream. The observed
105.950332 ms interval and clean owner exit jointly support completion under
the five-second admission deadline, including the separate final durability
check. All posthashes match. There is no full PE index or disassembly.

## Validation

| Gate | Result |
| --- | --- |
| Initial pure baseline | 119 PASS; all preserved input/source hashes matched |
| New context and owner behavior | 55 added tests; real RED/GREEN logs retained |
| Before-registration integrated suite | 174 PASS, exact source fingerprints unchanged |
| Independent source review | 97 focused PASS; no open Critical/Important; scoped Minor note retained |
| Actual byte review | Input hash and 36 B + 12 B raw anchors match; 158 B bounded mapping-field review; zero parser re-entry |
| Invocation/budget review | Registration and manifest match; one call, clean owner/worker exits, exact output/stream budgets |
| New observed-byte regression | Four PASS; five synthetic parser inputs independently fingerprinted |
| Final integrated suite | 178 PASS in 2.625 s; eight source/test fingerprints unchanged |

The final command, working directory and full output are in
`final-integration-validation-012.json`, SHA-256
`d7db1402556a4b1c3d89a8c4b1de4751243f2bbf7d528b4ec21d27d25cccc6d4`.
The joint actual-context admission is `runtime-review-admission-012.json`,
SHA-256 `496b443b3fdb1fb66adf9b44bc7f5e347766fe88aaae3396d3f4d343369f1327`.

## Decision for the next slice

Primary Microsoft format documentation defines the field as reserved/unused;
two pinned public .NET consumer revisions use only the header's frame fields.
The [013 plan](../20261011-013406-producer-handoff/native_startup_handoff_013.md)
therefore limits its proposed behavior change to version-1/opcode-3 OpInfo.
It retains raw bytes, the nonzero-frame guard, all unrelated refusals and the
original parser. Complete-index output can exceed the 012 refusal envelope;
013 needs an independently proved new schema/resource contract before any
actual call. No compatibility implementation or second call occurred here.

The original INCONCLUSIVE result, exact outer Job gate, 11 native workflow
runs, draft/HOLD branches and all qualification flags remain unchanged.
No Windows compile, native fixture, objdump, symbols, permission change,
production change, merge or release was part of 012.
