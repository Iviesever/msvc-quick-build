# V9 cache reader: production integration and frozen differential reference

The test-only prototype was accepted in #222. The separate integration follows
#198 comments5818119320 and5818290565. **This implementation has no measured speed
benefit and is not a demonstrated repair of #207 / original #701.**

## Production boundary

`cpp/src/msvc/toolchain/VisualStudioToolchainCacheReader.hpp` is a private MSVC
implementation, not a public cache framework or test dependency. It holds the
existing V9 record/limits, path normalization, environment-name predicate and
unchanged compatibility reader, plus the canonical fast lane. The existing cache
writer stays in `VisualStudioToolchainCache.cpp`.

The product's `try_reuse_visual_studio_cache` calls `v9_cache::read_prechecked` only
to obtain the record. Its original file type/size/time/age admission and
`ScopedCacheRead::opened(size)` position are preserved. Every subsequent option
key, binary-stamp, ambient PATH byte comparison, VC root/latest version,
compiler/linker/librarian existence and environment-trust check remains in the
original order. The locator's later SDK freshness and identity sealing are
unchanged. **A parsed record is not an adopted toolchain.**

For classic C++ locale, read the previously checked size (at most1MiB), check
short/error/extra-character observations, and recognize only the old writer's
canonical quoted fields/labels/LF and nonnegative decimal spelling. Every grammar
miss uses the original `read_record` on the SAME complete character bytes.
Unquoted strings, arbitrary old escapes, alternate whitespace/numbers, missing
final LF and other compatible cases therefore remain fallback, not a new grammar
rejection rule. Fields remain at most256KiB and entries at most64.

For a nonclassic file locale, use the same original stream directly. Do not reopen
or bypass filebuf codecvt, ctype or num_get. A detected size drift/bad read becomes
a cache miss; no retry or new file is substituted. This is an explicitly reviewed
fail-closed change for inconsistent reads, not proof of full equivalence under
concurrent writes. Filebuf can read ahead; nonclassic conversion keeps old file
admission rather than a new decoded-character bound. No atomic snapshot, read
lease or syscall-count bound is introduced.

The reader still constructs `ifstream`; it does NOT eliminate stream initialization.
Formatting work may be reduced, but fallback can do extra speculative work. No
performance counters, timing experiments or public configuration switches are added.

## Independent original reference

Before integration, freeze the EXACT previously generated old header at
`tests/native/v9_reader_baseline.hpp`. Its canonical-LF SHA256 remains:

`da5b64cd2e2e4d3d6b2cdb592a2c6e71bcb98da9eef40bb98c3c2a378b7dcfed`

It is the original generator output at main
`ed76d13df14acd680d5b523a27aa52fc99a1b09c`, not regenerated from the changing product.
The original generator and source files are recoverable at that commit.
`v9_reader_oracle.py` retains the original three source hashes as provenance,
validates the frozen header and its unchanged Process.hpp record dependency, and
materializes it in a fresh directory. A change to production cannot silently
refresh this oracle. Production never includes it.

`v9_reader_prototype.hpp` is now ONLY a record-shape adapter for the old probe:
it calls the real private production reader and moves every result field without
parsing. `v9_reader_probe.cpp` retains the exact5481-input corpus and comparison
logic, and links the actual `ToolchainDiscoveryPrimitives.cpp` for product path
conversion. It does not compile a copied fast parser. Full records and exception
dynamic type/code/category are compared, not just successful exit.

The portable source contracts check the moved original bodies/record/limits,
reconstruct the entire old cache source by undoing only the declared include,
using declarations, moved definitions and one reading call, and compare to the
ORIGINAL source SHA256. Negative controls alter PATH admission, age or disconnect
the reader and must fail. These narrow text checks complement, not replace, C++
execution. They are not a general C++ verifier.

## Correctness evidence required for this revision

The dedicated Windows job builds the probe through the pinned MQB seed in Debug
and Release, using synthetic data only. It preserves source/oracle/identity and
first failures. Each configuration retains5481 comparisons (including7 actual
synthetic-file cases) and6 additional transport controls. The corpus contains
all256 quoted byte values, quote/escape/unquoted compatibility, limits, numeric
sign/overflow/grouping cases, every truncation prefix,4096 deterministic edits,
invalidUTF8/path normalization and custom ctype/num_get/numpunct/file-codecvt.
Route assertions reject an always-fallback substitute. The same input executed
in two configurations is NOT10962 distinct inputs.

The original87 native test sources remain byte-identical. The driver changes only
its exact expected count and two diagnostic labels from87 to88; all scheduling,
weights, execution and failure handling remain unchanged.
One new `v9_reader_adoption_tests.cpp` is explicitly registered and joins the
normal88-test Debug/Release matrix. It uses the real installed toolchain cache
for canonical and four compatibility routes, calling BOTH the actual cache reuse
entry and locator with a subprocess-rejecting runner. It also checks malformed
records, option keys, missing/oversize/nonregular files, age/future timestamps,
record/process PATH changes, missing root and an existing but untrusted include
directory. A separate synthetic trusted installation tests latest-VC selection,
three missing tool files and changed compiler stamp without modifying installed
tools or executing synthetic programs. The28-case matrix prints labels/counts,
not environment values. Existing SDK-freshness/identity and other native tests
remain in place. Native logs, not the standalone grammar test, establish execution
of these integration cases.

Old prototype CI is not this production revision's CI. Full Native/Release,
compatibility/freshness, self-host and runtime package gates remain necessary.
CI builds fresh product/test programs for correctness; it never runs the original
#701 A/B or starts ETW. Test invocation counts are not diagnostic-study counts.

The first integration head failed before building the shared native library because
the driver still required87 tests. This manifest-registration error is retained;
the correction updates the exact count to88 and adds an inverse-hash contract for
the unchanged rest of the driver. No test or gate is removed, and old-head runs
are not retried.

## Limits and release decision

The differential domain remains immutable bytes, fresh default-format streams
and the same locale. Stream state/position after parse, allocation-failure timing,
exotic facet side effects and concurrent files are not universally proven equal.
Full source preservation plus native negative tests do not establish every race,
I/O fault or filesystem configuration. No bad sample or old failure is discarded.

Correctness acceptance does not prove speed improvement. Any performance comparison
needs a separately justified, bounded plan, preserving adverse results and allowing
no benefit/regression as outcomes. Do not rerun original#701 until green, pool
incompatible experiments, or subtract observer overhead. #207 remains original
Draft/HOLD, +3.0963ms/+26.70%; diagnostic history remains120 with the old2 unused
slots untouched. This patch grants no release, default observation, persistence,
clean/prune, deletion authority or Rium qualification.

## Supplemental local replay

This builds a fresh synthetic probe, not an archived program. Windows product
acceptance uses MQB in CI.

```sh
python -B tests/native/test_v9_reader_oracle.py
python -B tests/native/v9_reader_oracle.py --repo . --output /tmp/new-v9-oracle
g++ -std=c++23 -O2 -Icpp/include -I/tmp/new-v9-oracle \
  cpp/tests/msvc/toolchain/v9_reader_probe.cpp \
  cpp/src/msvc/toolchain/ToolchainDiscoveryPrimitives.cpp -o /tmp/v9-probe
/tmp/v9-probe /tmp/new-synthetic-v9-fixtures
```

Primary language references: [quoted extraction](https://eel.is/c++draft/quoted.manip),
[span input streams](https://eel.is/c++draft/ispanstream),
[filebuf conversion](https://eel.is/c++draft/filebuf.virtuals).
