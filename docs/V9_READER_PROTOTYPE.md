# V9 cache reader: test-only bounded prototype

Follow-up to #198 comment5816719996 and implementation-only record5816988847.
**No product callsite, performance experiment, #207 HOLD waiver or release.**

## Decision and scope

Evaluate a canonical-format fast lane before considering product integration.
The existing V9 writer is unchanged. `cpp/tests/msvc/toolchain/v9_reader_prototype.hpp`
accepts only its classic-locale, quoted-field, exact-label/newline layout with
nonnegative canonical decimal numbers. Any grammar miss retries the **unedited
production `read_record` on the same complete character bytes**, not a reopened file.
This preserves unquoted fields, arbitrary escaped characters, whitespace, alternate
numeric spellings, EOF behavior and invalid-input rejection through the old parser.

A nonclassic C++ locale bypasses the file fast lane entirely: the original parser
reads the original stream, including filebuf codecvt, ctype and num_get. Checking
locale *equality*, rather than a locale name or the C `setlocale` value, is deliberate.
The original environment-name predicate and UTF-8/path-normalization code are reused.
C locale mutations and exotic C++ locale side effects are not accelerated.

The classic lane reads the previously checked file size (at most1MiB) into owned
storage and checks for short/error/growth observations before parsing. Strings stay
within256KiB, environment entries within64. This still constructs an `ifstream`;
there is **no claim to have removed its initialization cost**. A successful fast
parse avoids per-character formatted string extraction, but that mechanism alone
does not demonstrate a speedup or explain original#701. Uncommon input can perform
extra speculative work before falling back. No benchmark/timing output is added.

## Exact reference, not a rewritten oracle

`tests/native/v9_reader_oracle.py` verifies canonical-LF SHA256 of the full original
VisualStudioToolchainCache.cpp, ToolchainDiscoveryPrimitives.cpp and Process.hpp,
then extracts exact fixed source sections into a generated test header. Those include
CacheRecord/constants, stable_path, UTF-8 conversions, environment-name predicate,
and read/write grammar bodies. Process EnvironmentVariable is included from the
actual pinned header. Function bodies are not rewritten; only a test namespace and
required standard includes are supplied. The manifest records the source/header hashes.

This reference compares grammar/record semantics, **not the entire toolchain-adoption
function**. No source extraction silently drops admission or freshness checks and
claims they passed: those checks are outside this prototype and remain in untouched
production code. Production file type/size/age, ambient PATH, latest VC selection,
compiler identity, environment trust and all later freshness/link dependencies stay
unchanged. Successful parsing does not establish a usable/trusted toolchain.

## Differential coverage

`v9_reader_probe.cpp` is a standalone experimental executable, registered in the
existing exact layout checker. It deliberately does not match `*_tests.cpp`: the
existing87 native tests/driver are not renumbered or replaced. Its separate Windows
correctness workflow builds it **through the existing pinned MQB seed**, Debug and
Release, then runs synthetic data only. Normal Native/Release/self-host/package CI
remain separate. The pinned seed is a builder, not original#701 A/B.

Each configuration performs5481 deterministic old/new comparisons, including7 real
synthetic-file cases, plus6 explicit transport refusal controls. Comparisons check
accept/refuse, every record field (including environment order/duplicates/removal
flag and normalized path), and exception dynamic type/error code/category. They do
not compare exception messages containing paths. Allocation failure is fatal, not
masked as a parser pass. The generator has10 portable tests, including source drift,
CRLF canonicalization, exact extraction and refusing existing output directories.

Coverage includes all256 byte values in quoted non-path fields, both quote/escape
forms, arbitrary escapes and unquoted fallback; entry counts0/1/63/64/65; signed,
leading-plus/zero, grouped, overflow and unsigned-negative numeric forms; empty and
reordered labels/fields; truncation at every byte of the fixed writer output; trailing
whitespace/garbage/EOF; string and total-file boundaries; path normalization and
invalid UTF-8;4096 fixed-seed replacement/insertion/deletion mutations; custom ctype,
num_get, numpunct and an actual non-noconv file codecvt. Shape and route assertions
prevent a fake implementation that always falls back or merely returns success.
Transport controls cover declared oversize without consumption, shrink/growth,
already-failed input, injected read failure, and empty input. Fresh dedicated output
directories prevent replacing the user's files. Corpus contents are synthetic; CI
publishes counts/source/oracle/build logs, not cache values or test executables.

## Limits before any product decision

Fixed immutable bytes, fresh/default-format streams and the same locale are the
comparison domain. Stream rdstate/position after a parse is not an equal-output
contract here; production currently discards the stream. Memory allocation count,
exception timing under exhaustion, concurrent writer races and arbitrary custom
facet side effects are not proven equivalent. Detected size drift fails closed;
this is not an atomic snapshot, bounded syscall count or durable read lease.
Nonclassic direct-stream fallback retains the old bounded-file-admission policy,
not a new decoded-character cap. Filebuf can read ahead and codecvt may expand data.

Finite tests do not prove equivalence for every possible byte sequence. The fast
lane is intentionally a conservative subset; every new supported syntax requires
old/new controls. No real toolchain cache-freshness, reparse/race or trust integration
claim follows from synthetic grammar tests. The original failure remains
+3.0963ms/+26.70%, #207 remains original Draft/HOLD, study history120; no new study
allocation, original A/B run, ETW, version bump, default observation, persistence,
clean/prune or Rium qualification occurs.

Next: review exact Windows Debug/Release evidence and this implementation boundary.
Only afterward consider a separate product integration proposal with full original
freshness/admission/invalid-cache fallback controls; any performance validation
needs a separately reviewed fixed plan. Do not deploy the prototype merely because
this correctness workflow is green, and do not resume an old study.

## Local correctness replay

Generate a new oracle directory, compile with a C++23 library, and give the resulting
probe a new synthetic working directory. This is supplemental portability checking,
not Windows/MSVC product acceptance; use MQB in the Windows workflow.

```sh
python -B tests/native/v9_reader_oracle.py --repo . --output /tmp/new-v9-oracle
g++ -std=c++23 -O2 -Icpp/include -I/tmp/new-v9-oracle \
  cpp/tests/msvc/toolchain/v9_reader_probe.cpp -o /tmp/v9-probe
/tmp/v9-probe /tmp/new-synthetic-v9-fixtures
```

Primary language references: [quoted extraction](https://eel.is/c++draft/quoted.manip),
[span-based input streams](https://eel.is/c++draft/ispanstream),
[filebuf conversion](https://eel.is/c++draft/filebuf.virtuals).
