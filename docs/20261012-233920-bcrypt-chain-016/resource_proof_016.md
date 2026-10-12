# Source-specific resource proof 016

Status: **SOURCE / RESOURCE PASS, C/I/M 0; concrete root execution gate still
required**. Six production units, three frozen utilities, the independent
verifier, transport, final root adapter, concrete primary manifest and final
primary plan have been fully read and bound to the identities below.
I-SOURCE-016-001, I-TRANSPORT-016-001 and I-SOURCE-016-002 are closed in their
review scopes. This proof supplies finite resource admission premises; it is
not an execution permit or a report that any 016 image slot was consumed.

This proof concerns explicitly accounted application data and named file
content. It excludes interpreter, compiler, stdlib, native allocator and OS
baseline. It is not an RSS bound, host filesystem sandbox, native benchmark
or promise that an operating-system call cannot block.

## Fixed scope and identities

Baseline is final 015 closure
`ff30a122e92b1b8fef645111ddf277aae065c275`, tree
`c7fc8dc750996797238a11f68654720adb752cb1`. Every 015 slot remains consumed.
016 selects runtime 252 / section 0 `.text`, file/RVA `[0xDB3C,0xDB8F)`,
exactly 83 B, preferred VMA `[0x18000DB3C,0x18000DB8F)`. Its admitted digest
remains null until both separately completed preparation results are joined.

The candidate is an exact two-level chain: child flags 4 / depth 2 and parent
runtime 105 flags 0 / depth 1. Independently map and compare child pdata 12 B,
child unwind 16 B, parent pdata 12 B and parent unwind 12 B, totaling 52 B,
inside the one already verified image snapshot. These are memory comparisons,
not additional original-image file reads. The embedded triple location
`0x1E620` differs from the parent pdata location `0x224EC` by design.

| Fixed input | Bytes |
|---|---:|
| Original image | 170984 |
| Complete admitted 013 index | 271920 |
| 013 index admission | 20232 |
| Admitted 015 caller report | 6740 |
| Canonical Python | 30894944 |
| **Five preparation identities** | **31364820** |

The complete path/size/SHA values are frozen by the new model and each
manifest. Actual adds the pinned GNU binary, loader cache, eight dependency
files and new primary/independent selection receipts. Its 15 fixed identities,
excluding those two new receipts, total **37667587 B**. The inherited nine
alias and five absent-path checks are metadata predicates, not content passes.
The environment is exactly `LANG=C, LC_ALL=C, TZ=UTC, DEBUGINFOD_URLS=''`.
Existing version evidence is sufficient; no probe, download or discovery child
is allocated.

Runtime provenance consists of exactly five full-hash, stream-only roles:

| Role | Bytes |
|---|---:|
| 015 invocation manifest | 8513 |
| 015 runtime admission | 24444 |
| 015 final closure admission | 14819 |
| 015 result document | 15931 |
| Published 016 handoff | 13792 |
| **Total** | **77499** |

Each provenance file is at most 32768 B and the aggregate cap is 163840 B.
All identity/source/provenance content in a phase is also subject to the
67108864 B aggregate cap. Package checks belong to the prerequisite gate;
they are not silently added to runtime provenance or decoded graphs.

## Source loading and declared-content passes

Selection has six sources: frozen 014 model/IO plus new 016 model, selector,
preparation and bootstrap. Actual has seven: frozen 014 model/listing/IO plus
new 016 model, selector, owner and bootstrap. The outer, transport and
independent verifier are separately pinned. All files in a phase source set
must be fully verified before any core module is compiled from retained bytes.
Self-hashing never replaces the external source/interpreter gate.

All following source files were fully read with exact size/SHA, EOF and stable
canonical path/FD identity. Production paths are under `tests/native/`;
verifier, transport and root adapter paths are under `evidence-016/`.

| Source | Bytes | SHA-256 |
|---|---:|---|
| bcrypt_disassembly_model_014.py | 16716 | 3b355306adfdc1ac13c2b193663bc36fb702784779b34891bf4b1a585dc55f12 |
| bcrypt_disassembly_listing_014.py | 5164 | f21cd65291e7a079885b6c7dc76b65f309ca311d06f3904cd1b145968b8e8e6a |
| bcrypt_disassembly_io_014.py | 10976 | 18596e673d8f352a7b49c1955e608e931d69c7dd01d7b9ded47ee402fe90e72e |
| bcrypt_disassembly_model_016.py | 13200 | 7aef7b245f34c57fab746672a478839542c6f041026c89ac4aeff85cf12de611 |
| bcrypt_selection_016.py | 22610 | 24214c65d66c96b03e6c7b9589793b99cce349328457e805f4445a8ad769f6c8 |
| prepare_bcrypt_selection_016.py | 7103 | ba1056849649c9869ec2966edf0006cc569c8b2bd146fd1ab691ea28ec7d3cca |
| bcrypt_disassembly_owner_016.py | 10191 | c22d4fb291789b79451704119af619f0f3c6d9aa6cc433507e5b2a11d6790615 |
| run_bcrypt_disassembly_016.py | 5928 | 73ed14ccb9c6a5bf314f94cc0ff3e146fd1f93dc4d614bc47c2ba28301731a1a |
| run_bounded_cli_016.py | 9674 | 42d5e925e98ace2713794d4b1e4a6cf946b993615b209b2c7c74932f15af2597 |
| verify_selection_independent_016.py | 32684 | e679ecb841c15fd94807858c6f9b1559bdd3733ed18c0452d79b6756271b3688 |
| cli_transport_016.py | 9371 | a2192bbb0fffaf9d1462d6771eec41c3849e42be87672258cc114c23db617f1a |
| launch_admitted_cli_016.py | 6605 | b221db7ca10fe042fc8a66f49a75eab3454b0065199a2fe90f01c77fcda2e4eb |

Let `S_p` and `S_a` be the summed exact selection and actual source bytes,
`B` the bootstrap bytes, `M_p` the primary manifest bytes, and `M_a` the
actual manifest bytes. Let `P` and `I` be the new primary report and independent
review bytes. A symbol's exact value is recorded only after the corresponding
file is frozen; maximum source/manifest size is 32768 B.

Final `S_p=76533`, `S_a=84785`, `B=5928`. The primary manifest is
`evidence-016/selection-manifest-016.json`, **4888 B** /
`d24026b372861f60fd4286f2cb92361f28ef97a8682d6be1a0779367b23cb2bf`.
It has the exact six sources, five identities, five provenance roles, fixed
baseline/call/output/environment/limits, empty tool argv/aliases/absent paths
and null target digest.

| Preparation layer | Full identity passes | Declared content bytes |
|---|---:|---|
| Owner before | 17 | `31364820 + 77499 + S_p + M_p` |
| Owner after, excluding image content | 16 | `31364820 + 77499 + S_p + M_p - 170984` |
| Bootstrap | 7 | `M_p + S_p` |
| Outer | 8 | `M_p + B + S_p` |
| **Preparation runtime tree** | **48** | `2*(31364820+77499+S_p+M_p)-170984 + 2*(M_p+S_p)+B` |

With the frozen primary manifest the total is **63045266 B**. The general
source-specific expression is `63025714+4*M_p`; using a maximum-size manifest
would give 63156786 B. This gate uses the concrete 4888 B manifest.

Each preparation retains one verified image snapshot. Its post phase checks
the exact saved integer dev/inode/mode/nlink/size/mtime_ns/ctime_ns identity
without reopening original-image content. Selection performs no GNU call.

| Actual layer | Full identity passes | Declared content bytes |
|---|---:|---|
| Owner before | 30 | `37667587 + P + I + 77499 + S_a + M_a` |
| Owner after | 30 | Same as owner before |
| Bootstrap | 8 | `M_a + S_a` |
| Outer | 9 | `M_a + B + S_a` |
| **Actual runtime tree** | **77** | `2*(37667587+P+I+77499+S_a+M_a)+2*(M_a+S_a)+B` |

Substituting the fixed sources gives
`75835240+2*P+2*I+4*M_a`. With `P<=8192`, `I<=16384`, `M_a<=32768`, its
pre-manifest ceiling is **76015464 B**. Exact non-null selection receipts,
published source identity and actual manifest still require separate review.

Actual verifies selection and its independent association before image
content in each phase. Its two original-image passes are distinct from the
two completed preparation passes. The first actual snapshot supplies the
83 B target; the second is a complete identity check without another target
selection. Total original-image allowance is primary 1 + independent 1 +
actual 2 = **4 full passes / 683936 declared content bytes**. Root/reviewer
ad hoc original-image content allowance is zero.

Each full identity pass permits one EOF-detection request of at most 1 B.
For consistent exact-size inputs that request returns zero content bytes.
Any extra byte refuses the pass and consumes the slot; it does not authorize
a replacement image read. Root preflight reads have a separate named inventory
and are not hidden within the 48/77 counts. These are source-path-derived
declared-content counts, not an external OS read trace.

## Independent preparation read proof

The final single-file verifier and six-source primary loading set have been
reviewed. There is no runtime helper. The explicitly loaded fixture belongs
only to its pure test harness and is not imported by the verifier.

The verifier reads and hashes all six primary sources before compiling only
the two frozen generic utilities. It does not compile or call the subject
selector/preparation/owner/bootstrap. Primary marker/report/target, complete
outer output, transport raw streams and receipt, submission, source and genuine
root completion must join before its sole image snapshot is opened.

Prelude: independent manifest, verifier, Python, primary manifest, six primary
sources = **10 full passes**. The observation records 33 unique identities:
two manifests; verifier/Python/outer/transport; six primary sources; five
provenance files; seven primary owner/outer files; transport receipt; two raw
streams; submission; completion; and image/index/admission/caller. After the
comparison it hashes the 32 non-image identities and compares saved image
metadata. Total is **75 full passes**, with at most 75 one-byte EOF requests.

Let `V`, `O`, `T` denote verifier, outer and transport source sizes and `M_i`
the independent manifest size. Before content is bounded by:

`31480422 + V + O + T + S_p + M_i + M_p`.

The constant includes fixed input identities/provenance, all seven primary
owned-file caps (18582 B), receipt 8192, stdout 1024, stderr 65, submission
2048 and completion 8192. Prelude content is
`30894944 + V + M_i + M_p + S_p`; after is before minus 170984.
Final `V=32684`, `O=9674`, `T=9371`, `S_p=76533`, `M_p=4888` give:

| Independent layer | Declared content B |
|---|---:|
| Before | `31613572+M_i` |
| After, excluding image content | `31442588+M_i` |
| Prelude | `31009049+M_i` |
| **Total 75 passes** | **`94065209+3*M_i`** |

The not-yet-created independent manifest retains its explicit 32768 B cap.
The source-specific finite ceiling is therefore **94163513 B**, followed by
an exact manifest/plan/gate review after genuine primary completion.

Independent output is only marker plus review. It compares all selected bytes
to the already retained primary target without writing a second target or
decoding either target. Reservation is recorded before opening image content;
a failed identity read cannot release the consumed slot merely because the
verified-read count is zero.

## Application-data accounting convention

Use `G(N,C)=192*N+4*C` for JSON application graphs, with node ceiling 32768
and depth ceiling 16. Duplicate keys, floats, nonfinite values, oversized
integers and invalid encodings refuse. The generic index graph allowance is
7602176 B. The exact admitted index has 26721 nodes / 171892 characters and
would fit 5818000 B; this proof does not take that reduction.

Retained input caps: image 262144, index 327680, admission/caller 32768 each,
primary report 8192 and independent review 16384 B. Source and manifest caps
are 32768 B. Provenance is streamed, so it creates no decoded-provenance graph.
Hash chunks are 65536 B; old/new chunks may coexist (131072 B).

The 32768 B additional chain/start work row is separate from inherited
control allowance: `G(128,1024)+4096=32768`. Source inspection establishes
the bound through constant two-row/two-unwind references, an eight-field
logical relation, four fixed metadata segments and four caller pairs. Primary
lookups retain one match at a time; sections are capped at 96 and runtime/
unwind collections at 16384. The incoming verifier retains at most 83 start
integers plus fixed cursors/sets. It does not copy the instruction records.

Independent matching lists range over the already fully hash-bound index and
caller, before any metadata decode. The separately inspected fixed index has
7 sections, 390 runtime rows and 318 unwind records, with unique relevant
matches; the entire decoded graph remains covered by its generic allowance.
No metadata recursion, evidence discovery or growing match history is used.
These are bounds for the fixed admitted invocation, not arbitrary calls to
the pure functions with replacement identities. At most 128 additional
references/scalars, 1024 characters and 4096 B raw scratch cover the changed
locals; the existing control allowance remains separately charged.

## Owner application-data envelopes

| Accounted item | Preparation B | Actual B |
|---|---:|---:|
| Two manifest graphs | 12845056 | 12845056 |
| Index graph | 7602176 | 7602176 |
| Admission graph | 6422528 | 6422528 |
| Caller graph | 6422528 | 6422528 |
| Primary report graph allowance | 6324224 | 6324224 |
| Independent review graph | 0 | 6356992 |
| Listing/report graph | 0 | 401512 |
| Wrapper shallow copy | 0 | 40960 |
| Decoder text/pairs temporaries | 10223616 | 10321920 |
| Two encoder temporaries | 17825792 | 25165824 |
| Bootstrap raw buffers | 294928 | 327696 |
| Manifest and retained input raw bytes | 688128 | 712704 |
| Expected owned output content | 9299 | 525395 |
| First failed report bytes | 8192 | 262144 |
| IO read/join/stream work | 1114132 | 1114132 |
| Listing line/regex/word work | 0 | 131072 |
| Popen error pipe/fixed arguments | 0 | 2097152 |
| Bounded control dictionaries/paths/frames | 262144 | 262144 |
| New chain/incoming work | 32768 | 32768 |
| **Reviewed total** | **70075511** | **87368927** |

Decoder work is `4*sum(decoded caps)+256*32768`. Only one active pairs
workspace exists; retained graphs appear separately. Encoder work is
`2*(16*max(report_cap,manifest_cap)+256*32768)`. Preparation therefore uses
the 32768 B manifest maximum, not merely its 8192 B report cap. Both temporary
charges remain for failure paths whose traceback retains an earlier object.
IO work is `3*327680+2*65536+20=1114132`.

Positive instruction lengths 1..15, exact contiguous byte/address coverage
and an end check before append imply at most 83 records. With unchanged
six-field records, listing/report graph is
`G(128+13*83,4096+462*83)=401512 B`. Per-record encoding at most 937 B gives
`83*937+82+2+16384=94239 B`, below the 262144 B report cap. The wrapper is
separately measured/encoded at 16384 B. The current fixed success wrapper
uses 85 nodes / 951 characters after removing the separately charged listing;
its two new fields fit the 128-node/4096-character premise, including bounded
refusal reason and scalar metadata. Incoming verification uses only the
existing at-most-83 records and four offsets 0,29,37,45.

## Separate independent, outer and transport envelopes

| Independent verifier item | B |
|---|---:|
| Three manifest graphs | 19267584 |
| Index graph | 7602176 |
| Admission/caller graphs | 12845056 |
| Two primary marker graphs | 12591104 |
| Two primary report graphs | 12648448 |
| Transport receipt graph | 6324224 |
| Submission graph | 6299648 |
| Completion graph | 6324224 |
| Independent review graph | 6356992 |
| Independent marker graph | 6295552 |
| Decoder temporaries | 10502144 |
| Two encoder temporaries | 17825792 |
| Bootstrap source/raw buffers | 294928 |
| Input and primary evidence raw buffers | 791767 |
| Owned output content | 17408 |
| First failed review bytes | 16384 |
| Compared slice | 83 |
| IO temporaries | 1114132 |
| Control allowance | 262144 |
| New chain/incoming work | 32768 |
| **Reviewed independent total** | **127412558** |

Independent raw input charge is
`3*32768+262144+327680+2*32768+18582+19521=791767`.
It includes the primary retained target once; the compared in-memory 83 B
slice has a separate row. Generic node allowances are deliberately retained
even for small control evidence. The independent source cap is 32768 B and
must be checked on the final implementation, not inferred from the old file.

| Outer item | B |
|---|---:|
| Two manifest graphs | 12845056 |
| Fixed marker/result/argv/summary graphs | 163840 |
| Decoder temporaries | 8650752 |
| Two encoder temporaries | 23068672 |
| Source/bootstrap/manifest/canonical buffers | 557076 |
| Owned content | 9283 |
| Pipe capture work | 134 |
| IO temporaries | 229396 |
| Popen error pipe/fixed arguments | 2097152 |
| Control allowance | 262144 |
| **Reviewed outer total** | **47883505** |

Outer canonicalization occurs before model premeasurement. ASCII DEL can
expand sixfold, requiring `2*(16*(6*32768)+256*32768)` encoder work. Fixed
outer control graphs are bounded by 512 nodes and 16384 characters. Source
buffers are `17*32768+20`; any source-set change requires a new derivation.

| Root transport item | B |
|---|---:|
| Three bounded graphs | 688128 |
| Two encoder temporaries | 786432 |
| Owned content | 11331 |
| Pipe/conversion work | 4364 |
| Same-FD readback | 8193 |
| Popen error pipe/fixed arguments | 2097152 |
| Source buffer | 32768 |
| Control allowance | 262144 |
| **Reviewed transport total** | **3890512** |

Each transport graph is bounded by 1024 nodes and 8192 characters. Transport
input metadata is exactly three bounded ASCII strings: `schema`, `call_id`,
`manifest_sha256`. The final transport rejects a non-dict and length other
than three before constructing a key set or copying metadata; it then checks
exact keys and bounded string types. Schema <=96 characters, call_id <=64,
digest exactly 64
lowercase hexadecimal characters. External SDK execution output is separate
evidence and is not claimed to have this three-field schema.

Owner, verifier and outer totals describe their respective application
scopes. The root adapter and transport retain objects in the same process,
so their potentially simultaneous data must be added as shown below. None
of these totals is a process-tree RSS assertion; compiler/code-object/stdlib,
interpreter, native allocator and kernel baselines remain excluded.

## Final root adapter and concrete primary plan

The final adapter is 6605 B with the source digest in the table above. Its
primary plan is `evidence-016/preparation-execution-plan-016.json`, **6342 B** /
`a45d9406b47b74e80b21402c5084c49b97eebdad17de512f28b12b44135d9b87`.
The fully decoded, independently measured canonical plan has 261 nodes,
5492 characters and depth 3: `G(261,5492)=72080 B`. The earlier 5245 B adapter
and 6085 B pre-adapter plan remain historical evidence, not execution inputs.

The concrete primary preflight has eight safe source reads: the six selection
sources, outer and transport, totaling 95578 B. Together with manifest,
canonical Python, adapter self, plan and gate, this is **13 full passes**:

`95578+4888+30894944+6605+6342+gate_bytes = 31008357+gate_bytes`.

The separate canonical gate is capped at 32768 B, hence this preflight is
at most **31041125 B**, plus at most 13 one-byte EOF-detection requests that
return zero content on success. It reads no original image or target content.
It is separate from the 48-pass / 63045266 B primary runtime tree; together
the two inventories have 61 full passes and at most **94086391 B**. The plan
names all 15 unused output paths and fixes the actual argv, hashes, metadata,
file caps and root capture/completion records; no discovery read is included.

The adapter verifies complete source identities, keeps only the verified
transport bytes for later compilation, streams Python and verifies itself.
Every explicit read must reach EOF with stable canonical path/FD identity,
close, and pass a deadline check. Original image and target paths are rejected
by this preflight. Source collection is capped at 10 and unused paths at 20;
the fixed primary plan uses eight and 15 respectively. There is one transport
submission from the retained bytes, without reopening its source.

Plan/gate decoding precedes general graph premeasurement inside the adapter.
Consequently this review relies on their independently fixed complete hashes
and measured canonical ASCII integer-JSON graphs, not arbitrary replacement
JSON. Before launch the concrete gate must be fully read and checked against
its 32768-byte, 32768-node, 32768-character and depth-16 bounds. Its bytes may
be measured after this proof is sealed to avoid a circular proof/gate digest.
The proposed gate cap by itself is not execution admission.

| Additional adapter application item | B |
|---|---:|
| Fixed primary plan graph | 72080 |
| Gate graph at its declared cap | 6422528 |
| Decoder text/workspace | 8545048 |
| Two encoder temporaries | 17825792 |
| Raw plan and gate | 39110 |
| Retained source buffers | 131072 |
| Read/join/hash work | 229396 |
| Summary and diagnostic caps | 321 |
| Bounded control references/paths/frames | 262144 |
| **Additional adapter total** | **33527491** |
| Co-live transport envelope | 3890512 |
| **Same-process adapter plus transport** | **37418003** |

The gate graph is `G(32768,32768)`. Decoder work is
`4*(6342+32768)+256*32768=8545048`; two encoder temporaries are
`2*(16*32768+256*32768)=17825792`. Raw plan/gate is 39110 B;
source buffers `4*32768` cover retained transport and prior/current source,
manifest and self-check state. Read/join/hash work is
`3*32768+2*65536+20=229396`. The 321 B output row is stdout 256 plus stderr 65.
The combined row retains the existing transport envelope in full even where
this conservatively overlaps a source buffer. Named files are created by the
transport/runtime, not by this adapter. Independent and actual launcher plans
still need their own exact graph and preflight substitutions before their
separate execution gates; this numerical row is for the concrete primary plan.

One monotonic deadline starts before the plan or gate is read. The initial
28-second maximum narrows, from that same start, to 20 seconds for primary or
independent; actual remains 28 seconds. Checks occur around bounded reads,
EOF and closes, compilation, transport return, final stdout and termination.
These are admission checks, not a claim that blocking OS calls are preempted.
The success return must exactly equal the fixed five-field transport summary,
encode within 256 B and complete its write before the final check. Any caught
exception or lateness takes the fixed `ROOT_ADAPTER_REFUSED\n` diagnostic,
**21 B**, within stderr cap 65, and nonzero `os._exit`; no unbounded traceback
is emitted. An overdue or failed containing CLI cannot be admitted merely
because inner candidate files already exist.

## Named content, children and success completion

| Named content | Maximum B |
|---|---:|
| Primary owner: marker 1024 + target 83 + report 8192 | 9299 |
| Actual owner: marker 1024 + target 83 + two GNU captures 131072 each + report 262144 | 525395 |
| Each outer: marker 1024 + stdout 1 + stderr 66 + report 8192 | 9283 |
| Primary owner/outer plus maximum manifest | 51350 |
| Actual owner/outer plus maximum manifest | 567446 |
| Independent: marker 1024 + review 16384 | 17408 |
| Each outer transport: submission 2048 + stdout 1025 + stderr 66 + receipt 8192 | 11331 |
| Independent transport: submission 2048 + stdout 1 + stderr 66 + receipt 8192 | 10307 |
| Each external execution/completion root record | 8192 |
| Actual owner/outer/manifest/transport plus both root records | 595161 |

These are content bounds; directory allocation, archive copies and external
service transport are separate. Frozen IO creates owner/outer files at 0600;
the transport owns its four files at 0400. Actual GNU capture success requires
each capture strictly below 131072 B, empty stderr and a reaped zero exit.

| Lifetime | Admission deadline |
|---|---|
| Preparation owner / outer / root transport | 8 / 12 / 15 s |
| Actual owner / outer / root transport | 12 / 20 / 23 s |
| Independent verifier / root transport | 12 / 15 s |
| Root adapter: primary / independent / actual | 20 / 20 / 28 s |

Success includes exclusive creation, file/directory fsync, complete bounded
same-FD readback, matching final path/FD identity, all closes and a final
deadline check. Root separately requires genuine containing CLI exit 0 and
complete raw captures. Persisted candidate status alone never admits a result.

Preparation and independent verification launch no GNU or native child.
The preparation outer owns exactly one Python owner; actual outer owns one
Python owner and that owner may reserve exactly one GNU child. Each external
transport attempts one reviewed CLI. GNU preexec installs and reads back
fresh hard=soft CPU 2 s, AS 268435456 B, FSIZE 131072 B, CORE 0 and NOFILE 64.
Default SIGCHLD is required before relying on wait/reaping. Each controller
has at most one additional one-second failure wait after kill; nested
descendant reclamation on controller failure is not assumed. Late cleanup
cannot turn an overdue candidate green.

All fixed root submissions consume their slot even setup/mkdir/marker failure,
refusal, timeout, cap, interruption or persistence failure. New constructed
GNU calls and real cap-test children are both **zero**. Pure fake-process and
constructed-buffer tests cover the changed chain/start/transport predicates;
retained 015 constructed GNU evidence covers the unchanged parser/tool/argv
mechanism. No observation or old permit is rerun.

## Completed independent source/resource evidence

The following sealed receipts under `evidence-016/` each have C/I/M 0 within
their stated source/resource scope; none grants execution on its own.

| Review | Bytes | SHA-256 |
|---|---:|---|
| budget-production-source-review-016.json | 10996 | 6d391d26fa4d1a1433df88cddf425a3b2ff8a0c5e5fc48f34111a2aae9ff2d53 |
| budget-independent-source-review-016.json | 8328 | b23e842daf93d273cfe533a1a0c5517bedee53cb888a02317cafb3379181ca5e |
| budget-transport-final-review-016.json | 3719 | 93db31a8001671c7fd6f77c01b501a4260eb678b0fb5df03612101a61c3171df |
| budget-launcher-primary-review-016.json | 8445 | 103800ed73a4e56b796523ff843900102bc4abc2c42e1f0e9fc5b44581cca087 |

Production's retained final 79-method suite exited 0 (tool chunk `340e3c`),
independent's distinct 26-method suite exited 0 (`397ff1`), transport's
separate 15-method suite exited 0 (`abdffc`), and adapter's separate 27-method
suite exited 0 (`dc19af`). These are four actual pure invocations, not a
claimed combined integration run. Each review joins the
full fixed sources and retained logs/actual exits. No new constructed GNU or
real cap child was used.

I-SOURCE-016-001 is closed by the original reviewer's retained before buffer,
its sole loader-cache-size correction 34016 to 34015, a full comparison of
all 14 inherited identities except the changed caller plus all three frozen
source pins, meaningful 19-method RED and final 79-method GREEN. Transport
I-TRANSPORT-016-001 is closed by checking exact dictionary cardinality before
key-set construction; its discriminating spy RED and final GREEN are retained.
I-SOURCE-016-002 is closed by the fixed adapter's total deadline and bounded
termination path. The same fixed 27-method pure harness produced RED exit 1
(`7b5a1e`, 27 failure records including subtests, zero errors) and GREEN exit 0
(`dc19af`, 27 methods, zero failures/errors). Both complete external stdout
payloads match their saved logs exactly. The fix receipt is
`evidence-016/adapter-fix-receipt-016.json`, 7035 B /
`4877fad12cb7495481f2e60bc6ca24c61cd3f10c2e6caa7ae16c47abda279188`.
The independent resource review includes the final adapter, before source,
complete harness, retained logs/actual exits and final 6342 B plan.

## Remaining concrete execution gates

1. Freeze immutable requirement/plan/contract/proof context and fully measure
   the concrete canonical root gate against the proved caps. Join the final
   adapter, exact primary manifest/plan/argv/outputs, source-review closures,
   all unused paths and single remaining primary slot. This proof, a proposed
   gate or a persisted candidate cannot replace that concrete execution gate
   and the genuine containing clean completion required afterward.
2. After clean primary completion, separately freeze and review the independent
   manifest, exact launcher plan and root gate. Metadata completion/build
   helpers have their own source/input/output inventories; they are not added
   silently to the verifier's six-source / 75-pass runtime proof.
3. Before actual, substitute non-null selected identity, immutable source
   publication, exact canonical manifest, fixed source/dependency/ENV set,
   unused output paths and separate root preflight inventory; close exact
   registration/activation/final execution admission.

Native 011 remains INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE, counts
11/10/23/7/0; failed API and 0xC0000142 cause remain unknown. All product,
cost, IPC, qualification and canonical-evidence HOLD boundaries persist.
## Preparation completion at source publication

The preceding pre-execution proof remains unchanged in
`preparation-contract-snapshot-016/resource_proof_016.md`, 28914 B /
`371d2f50b16bfb51bf245f098c209ee37079310de121206edf69d042628e0f93`.
This supplement closes the primary/independent preparation gates above.
Actual execution still requires source publication and its own exact gate.

| Completed inventory | Primary | Independent |
|---|---:|---:|
| Genuine containing CLI exit | 0 (`780b4a`) | 0 (`68d1ca`) |
| Exact manifest bytes | 4888 | 4232 |
| Root preflight identity passes | 13 | 14 |
| Exact root preflight content B | 31016478 | 31047813 |
| Runtime identity passes | 48 | 75 |
| Exact registered runtime content B | 63045266 | 94016977 |
| Original-image full passes consumed | 1 | 1 |

Both complete external SDK records were independently read and their exact
five-field canonical success summaries verified: under `evidence-016/`,
`primary-external-exec-result-016.json`, 344 B /
`5abaf8e07e8f436ecc587fcf65e4e722ce954bae4650902480db7d72ed5f7c94`, and
`independent-external-exec-result-016.json`, 352 B /
`f6064803d8b7ae42b20fe4f81222c5defcd31ecac37dfed85e27e74b50b82fe2`.
No attempt was repeated.

The independent plan is 7828 B /
`3d2f00ef7d981672f1eb87145cc3f09448a3d5417a0e2bd8ae00ae1de055dbbc`:
333 nodes, 6726 characters, depth 4, graph 90840 B. Its gate is 5942 B /
`c2821f08cbc0dbd08dd4308ec4cd273e2352aa3c5f8662c280f87d5606e7be6b`:
251 nodes, 5136 characters, depth 3, graph 68736 B. Nine preflight sources
total 128262 B. The root inventory is `31041871+5942=31047813 B`.

Keeping the same gate cap and transport envelope, the adapter rows become
90840 + 6422528 + 8550992 + 17825792 + 40596 + 131072 + 229396 + 321 +
262144 = **33553681 B**. With transport 3890512 B, same-process application
data is **37444193 B**. This substitutes the independent plan graph and raw/
decoder sizes; it does not reuse the primary 72080 B graph.

Independent runtime before/after/prelude content is respectively
31587340 / 31416356 / 31013281 B, across 33 / 32 / 10 identity passes.
The exact 94016977 B sum remains below the distinct schema ceiling
`94065209+3*4232=94077905 B`. Parameter reviews are sealed as
`budget-primary-execution-parameters-review-016.json` (2324 B /
`5c7e1490ece9b4bb657cb0e2469f40eeb0584363f76d26165d6d0df873a7c29b`) and
`budget-independent-execution-parameters-review-016.json` (3228 B /
`28969f7d3b0dd2cfde61d857d94c43102c765a4cb34ad4383c3f32f107e26389`).

The joint observer completed once, exit 0 (`b802a8`). Its separate inventory
has 41 unique input passes / 31441512 B, including one 83 B retained-target
hash and no original-image content. Its two output readbacks add 12683 B:
43 explicit full reads / 31454195 B. These auxiliary reads are separate from
the runtime identity inventories above. The observer's completion is 2586 B /
`674e9512b027da789ca15058d179b3916c6050480bdd05a526c0ffcdf91780b3`;
`selection-identity-admission-016.json` is 10097 B /
`ae3b9063b6e26c424802b897f733fdb1816f9afc6d897db81af03a1fedf5302f`.
It admits the 83 B target digest
`ff762eb73a125811551bfd8cfeb3b7cf68f26607d045d78311f50d247250df32`
after independent byte comparison and genuine clean completion, C/I/M 0.
The source/resource reviewer read only these metadata outputs, no target.

Both preparation slots are consumed; original-image total is 2, GNU/native
calls remain 0. Actual is unexecuted and unadmitted at this checkpoint, with
one possible actual attempt requiring two full image passes. Fixed selection
report sizes `P=1780`, `I=8701` reduce its formula to `75856202+4*M_a`;
the exact actual manifest/plan must still be frozen after source publication.
