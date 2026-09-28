# Historical link fact snapshots

**English | [简体中文](LINK_FACT_SNAPSHOT_ZH.md)**

## Scope

This opt-in, in-memory codec implements the next slice of #198 after #228 and
main `d3fc25572fb96654a79e14074fe88c63b1f7f80d`. It is not the generation ledger,
a cache format, a retention plan or a `clean/prune` implementation. No CLI or
ordinary build calls it. It performs no filesystem access or observation.

`LinkFactSnapshot` owns a limited projection of an already successful or reused
`ObservedLinkResult`. A failed build has no such result. The separate projection
interface does not accept a coordinator, request, runner or observer callback.
It neither repeats the invocation nor reads a cache, clock or current directory.

## API and ownership

```cpp
#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"
// completed is an already obtained successful ObservedLinkResult.
auto facts = mqb::orchestration::capture_link_fact_snapshot(completed, "caller-label");
if (facts) {
    auto json = mqb::encode_link_fact_snapshot(*facts);
    if (json) {
        auto historical = mqb::decode_link_fact_snapshot(*json);
        // historical is still unverified history, never a cache/execution ticket.
    }
}
```

The capture label is an optional caller annotation, not an atomic timestamp.
Paths are copied to UTF-8 labels without canonicalizing or resolving them. On
readback they remain strings: reading a Windows record on another OS cannot
reinterpret its relative paths against the reader's current directory.
Invalid encoding is refused; memory-allocation failures are not swallowed.

## Version 1 coverage

The exact schema is `mqb.link-fact-snapshot`, version `1`, coverage
`link-main-output-v1`, provenance `unverified-historical-facts`. Fields are:

- `capture_label`: string or null.
- `build`: completion, cache state, linked flag, output/cache/cwd labels,
  signature high/low limbs, linker path/version/opaque stamp, configuration,
  architecture, target kind, and ordered warnings with code/path/message.
- `observation`: callback invocation flag, requested path, state, historical
  physical ID, optional logical/allocated bytes and hard-link count, opened
  component count, and ordered issues with path/message/native error code.
- `authority`: producer_identity_verified, current_content_verified,
  complete_producer_inventory and deletion_authorized, all exactly `false`.

These are the only covered fields. Object/library lists, full LINK options,
side outputs, process output and complete recipes are deliberately not encoded.
The two signature limbs are copied from that record, not recomputed from this
incomplete projection. There is no conversion to a `LinkCacheEntry`.

Every field, including nullable fields, must be present; extra fields are
rejected at every object level. The reader does not preserve unknown schema
extensions. Unknown metadata *values* remain null/empty and are never guessed.
Zero logical size is a known value, not missing data. Enum spellings and field
names are case-sensitive. Integer values are decimal unsigned JSON integers,
parsed directly from their lexemes to uint64/uint32, never through double.
Consumers that cannot preserve integers above 2^53 must not reinterpret them.

## Validation and bounds

Both encode and decode validate completion/cache-state consistency, enum values,
UTF-8 and embedded NUL, diagnostics budgets, and known hard-link counts. A
non-observed state cannot carry file metadata. An observed state supplied by a
caller can still have unknown metadata; the state is not evidence of a real
handle or an authenticated callback. An uninvoked observer only supports
not-attempted or the adapter's pre-invocation invalid-path refusal.

Limits: 1 MiB encoded document, 128 KiB per string, 512 KiB total projected
string bytes, 64 warnings, 128 issues, and 129 opened components (128 relative
components plus the root). Before calling the existing `json::parse`, a lexical
resource scan caps nesting at 12 and structural punctuation at 4096. It is not
a replacement JSON grammar. Duplicate decoded keys, including escaped aliases,
invalid escapes, truncation and trailing garbage are rejected by that parser.
BOMs are refused at this codec boundary without changing other JSON consumers.
Negative, fractional, exponential, boolean, quoted or overflowing integers
cannot be coerced into counters. Encoder size is checked as bytes are appended.
Projection string budgets are checked incrementally before retaining copies.

## Trust and evidence boundaries

Reading a syntactically valid snapshot proves only that the bytes satisfy this
schema. Paths, IDs, signatures, caller labels and messages are untrusted data.
Neither checksum consistency nor an old physical ID proves current content,
producer association, exclusivity or deletion rights. Same-path replacement
must not mutate the historical value or promote it to a current observation.
The decoder rejects every attempt to set an authority field to true; no flags
are silently cleared or upgraded. No authenticity claim is made for false flags.

Native tests include pure codec and projection controls; they launch no compiler,
linker, observer or filesystem probes from inside the test program. The existing
native driver still builds those tests using MQB and must run the new candidate's
Windows Debug/Release gates. Portable C++ tests alone do not certify Windows
path conversion, full ownership or performance. Existing experiments and adverse
samples remain history, not retries or new results of this codec change.

## Not implemented

No file journal, default persistence directory, generation retirement, scan,
content hash, cache rehydration, writer cooperation, recovery, deletion or Rium
qualification is supplied. #198's final scope is unchanged. Explicit observer
cost and every default adoption still require separate qualification. This code
does not release v5.7.0 or reopen original #207/#701 or the consumed #787 study.
