# Next handoff 014: bounded static init-function observation

Status: `TARGET_DEFINED_NOT_IMPLEMENTED_NOT_REGISTERED_NOT_EXECUTED`.

The [013 observation](native_startup_result_013.md) supplied the complete
versioned structural index that was unavailable after 012. Its single actual
reservation is consumed. The next concrete step is to design and review a
separate bounded static-disassembly owner for **only the 391-byte init-function
interval `[0xA590, 0xA717)`**, then register an exact tool/input/argv/resource
manifest before any actual tool call. Do not rerun a PE parser to rediscover
the already retained index.

This document defines the target and the remaining implementation gates.
It does not claim an implemented tool, reviewed output budget or execution
authority. No objdump call, including `--version`, was made in 013. The unused
011 disassembler allowance is not transferable.

## 1. Verify the latest state and immutable inputs

Fetch the latest feature/main/draft heads and #198 closure through Connector.
The 013 invocation source is fixed below; a later documentation-only closure
commit is a separate object. Confirm the worktree state and preserve existing
isolated work. Read the complete 013 report, CLI evidence and independent
runtime reviews from the delivered evidence before designing a new owner.

| Input / prior evidence | Required identity |
|---|---|
| 013 invocation source | `0172607ad74802a0ae74495f62cc0f07aed3ae12` |
| 013 source tree | `42f78e4932800aa212d922639095594b5f1d0ee8` |
| 013 registration | [#198 comment 6117168744](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6117168744) |
| Consumed 013 call | `013-complete-index-1` |
| 013 manifest | 3,155 B; SHA256 `eca8c4d91bdd7fa00e029abda3b5d0d0cb9f52246773fd83e830a262dc8dde8d` |
| 013 marker | 205 B; SHA256 `05ddd8ee92ae4b536a0b33ad4d72cac51a73d1319395ecb582463db3f55fb29c` |
| Complete 013 report | 271,920 B; SHA256 `ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e` |
| Owner CLI evidence | 2,382 B; SHA256 `0335eb5b78a470f649a491467ab1596fcf066c9161591948f80f50eadfe4bddf`; owner exit 0 |
| Independent byte review | 88,458 B; SHA256 `d0ba5e1da43a2ecaf7e26603c1b2b5d29f7380f1100f792bc97b9a0b7b83d755`; PASS |
| Independent budget/scope review | 32,289 B; SHA256 `00f9a6b703fd961534afcdf9ec91ce2c13e904c23f0bf76b5c77fda77fd5b161`; PASS |
| Root joint runtime admission | 20,232 B; SHA256 `c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b`; `ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013` |
| Original 011 image | 170,984 B; SHA256 `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb` |
| Last synthetic checkpoint | 275 root tests PASS; 143 independent tests plus six supplemental checks PASS |

Preserve the original image, all frozen 011/012 code and artifacts, all 013
source/tests/manifest/marker/report and their independent reviews. Do not
rewrite a consumed manifest with a new path or call ID, use a new DLL, or make
a trial actual parser/disassembler call to discover a budget.

The 013 structural index has 7 sections, 16 directory entries, 15 normal and
one delay-import descriptor, 87 IAT records, 390 runtime-function records and
318 unwind records. These are static facts from the retained artifact. Live
symbol binding, execution and the native failure cause remain unknown.

## 2. Exact first target

| Target property | Exact value |
|---|---|
| Loader init RVA and PE header entrypoint | `0xA590` |
| Containing runtime-function index | 169; init equals its beginning |
| Runtime table entry file offset | `0x227EC` |
| Runtime table raw bytes | `90a5000017a7000088ec0100` |
| Runtime table raw-byte SHA256 | `c8a2190fa83215b5ca2fc8a060f7e98f95a8be00eb4502007a314ba93d9a832e` |
| Section | index 0, `.text` |
| RVA interval, end exclusive | `[0xA590, 0xA717)` |
| Original-file interval, end exclusive | `[0xA590, 0xA717)` |
| Exact target bytes | 391 |
| Target-byte SHA256 | `e5f6892ff2bc395f315e54d5e195875f8ea817bc636f5d89995ab9f4a119cf44` |
| Unwind RVA / file offset | `0x1EC88` |
| Unwind raw bytes | `010a04000a3409000a520670`, 12 B |
| Unwind raw-byte SHA256 | `cff4d61e7f88c087c11e26c322a81decf2f0e7b7515fb78b17f2d867e224a26c` |
| Preferred PE ImageBase | `0x180000000` |
| Preferred VMA interval | `[0x18000A590, 0x18000A717)` |

File offsets and RVAs happen to coincide for this image and interval. Preserve
the verified section mapping rather than generalizing that equality. The
disassembler's address convention must be demonstrated on synthetic input
before final argv is chosen. Preferred VMA is derived from the file's ImageBase;
it is not the captured process's relocated load address.

The old 012 refusal concerned root 11, `[0x1830, 0x19BE)`, unwind `0x1D958`.
Its `16 63` bytes and original 36-byte envelope remain intact, and 013's
policy boolean is true. That earlier parser guard and handler do not define
this target and do not identify the native failed instruction.

## 3. Tool and owner design before any actual call

Select an available read-only disassembler appropriate for the PE's AMD64
machine type. Establish its exact canonical executable path, full size/hash,
trusted build metadata, dependencies and supported address/output behavior.
Do not assume a command named objdump is the intended implementation. No
installation, network download, missing dependency workaround or native
fixture execution is implied by this handoff.

A version/help probe is a process invocation too. If it is needed, its own
source/input/argv/output/process budget and registration must be reviewed
before it is run. Prefer existing verifiable metadata when it suffices; an
unavailable identity or unsupported option is a pre-execution stop point.

Design a separately versioned owner with:

- One exact target interval and one planned actual observation; no automatic
  follow-on disassembly of branch destinations or neighboring functions.
- Fixed tool, original image, admitted index, target-byte digest, complete
  argv, working directory, environment and executable/source identities.
- Bounded stdout, stderr, protocol, report, manifest and combined persistent
  output, including banners, address text, instruction bytes and diagnostics.
- Exclusive consumed reservation before process creation, owned-process
  control, complete pre/post identities, clean exit/reap and durable full
  evidence readback. A late or incomplete result cannot be labelled complete.
- A separately reviewed deadline and buffering/work/memory proof. The old
  013 five-second owner and its numeric allocations are frozen; neither the
  012 small-context caps nor the 013 full-index caps transfer automatically.

The numerical tool/output allocation is deliberately **not assigned here**:
the exact tool and its producer behavior have not yet been selected or tested.
Derive the complete producer/owner bounds from the implementation and output
contract, not from the 391-byte input interval alone. If the proof cannot fit
the proposed deadline and ownership contract, stop at the synthetic checkpoint
without registering or invoking the actual-image tool.

## 4. Required synthetic verification

Create only the necessary synthetic data and new versioned code after the
design is reviewed. Keep existing 013 sources unchanged. Verify the selected
tool and owner against bounded synthetic cases before using the real image.

| Case | Required evidence |
|---|---|
| RVA, file offset and preferred VMA differ | Correct explicit mapping; no accidental address-domain substitution |
| Exact start and exclusive end | Reported instruction addresses/bytes stay within the registered interval |
| End falls inside a decoded instruction | Refuse incomplete coverage; do not silently extend the interval |
| Wrong image, tool, source or target-byte hash | Refuse before admitting output |
| Wrong architecture/options or unavailable tool | Bounded failure; no alternate tool or implicit installation |
| Oversized stdout/stderr or malformed output | Consumed failure; no truncated output labelled complete |
| Extra lines, unexplained gaps or mismatching raw bytes | Preserve and refuse until the output contract explains them |
| Timeout, nonzero exit, crash or unreaped worker | Consumed failure and complete retained diagnostics |
| Existing reservation/output directory | Refuse; no second actual observation under a fresh identifier |
| Late write, fsync or readback | Nonzero owner exit; report alone cannot admit success |

An instruction-count or text-size bound must account for the selected tool's
actual fixed metadata and diagnostic behavior. Merely checking the byte length
after an unrestricted in-memory capture is not a producer/owner budget proof.
Keep genuine RED/GREEN evidence for any new behavior and run the affected
existing pure tests. No Windows/native test expansion is selected.

## 5. Registration, observation and interpretation

After independent source and budget review with no open Critical/Important
findings, review all workflow/path triggers for the exact new files. Publish
immutable source through Connector and fully read back its blobs/tree/commit.
Freeze and independently read back the exact new manifest and #198 registration
before any expected-HEAD feature update or actual tool invocation.

Only then may the separately admitted owner make the planned single static
observation of the exact 391-byte target. Any cap/deadline/exit/identity failure
or unavailable result consumes that observation. There is no retry, broader
range, alternate image or unregistered second tool call.

If admitted, independently match output addresses and displayed bytes against
the original interval and retain raw stdout/stderr plus the full report.
Interpret only those observed static instructions. Existing IAT associations
may identify candidate external call slots; they do not prove a slot was bound,
called or responsible for failure. An out-of-range branch/call remains a
separate future target requiring its own explicit bounded admission. Do not
walk all 390 functions or infer a dynamic call chain from static references.

Record unanswered runtime questions separately. A complete static function
listing cannot by itself establish the failed API, actual register/argument
values, the executed branch or the cause of `0xC0000142`.

## 6. Permanent scope and qualification boundaries

011 remains `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`: outer Job total 3
instead of 2 and no admitted native_summary. 013 is a versioned structural
observation with `runtime_execution_observed=false`, not native success.
Certificate trust, checksum, language-specific unwind semantics, other
directory contents and unwind stack effects remain outside its verification.

Do not build or run Windows/native fixtures, reacquire/load/execute a DLL,
download symbols, inject/hook, write process memory, expand permissions,
relax token/ACL/Job gates or activate a native workflow through this handoff.
Main `87f86770cfc7e1941603074ace914e0c7faf10d3`, draft/HOLD PR250/248/207,
VERSION 5.6.0, consumer/cost DEFER, +3.0963 ms / +26.70% cost excess, disabled
M1b/service, A1-A5/deletion/ordinary qualification/canonical_evidence_sealed=false,
and no merge/release remain in force. Native counts are unchanged.

Startup understanding still leaves the original IPC capability-lifetime
prerequisite and a qualified producer writer owner unresolved. A static
listing does not remove either requirement.
