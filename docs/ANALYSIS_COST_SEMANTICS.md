# Cost-output semantics for fixed analysis substitutes

[简体中文](ANALYSIS_COST_SEMANTICS_ZH.md)

## 1. Scope and source

This implements the next point in [handoff 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173). A new [pure semantic adapter](../tests/analysis/analysis_cost_semantics.py) checks the meaning of a complete cost-output stream. The [closed driver](../tests/analysis/analysis_substitute_driver.py) invokes it after preserving both streams and again during final audit. Its [transport](../tests/analysis/analysis_substitute_transport.py) and [capture helper](../tests/analysis/analysis_evidence_capture.py) are unchanged.

The analysis development base is `bf5df76079f224ea9bb5e7822be683cb3a2e4d45`, tree `46007c773858dd50653db0a49ce6861b15733fb2`. The producer schema comes from the preserved `cost_harness.cpp`, SHA-256 `d0b7d1025a2d2d010fc2df562977ca3ec96a478ae8960672503c9847d6ac3632`, at PR250 candidate `a26b103685589ab8a5b4df60d1b760ec445efdf5`. Its original product base is `2762749b27fc89156b724d19e89cce84e388ef52`. These are different roles; source provenance does not grant execution or product authority.

This slice executes only fixed Python substitutes. It performs no real preparation, codec API, compiler, MQB, MSVC, ETW or performance study. The literal `public_preparation_calls=5` in a substitute is a schema fixture, not five consumed API calls.

## 2. Prepared identity

Each call has exactly one prepared record followed by five observation records. Prepared has exactly these seven fields:

| Field | Type and rule |
|---|---|
| `kind` | Exact string `prepared` |
| `case` | Exact known case, equal to the frozen call |
| `records` | Unsigned 64-bit integer; 2 for `reuse_save_failure`, 256 for `history_256`, otherwise 1 |
| `wire_bytes` | Positive unsigned 64-bit integer, equal to the frozen expected value |
| `public_preparation_calls` | Strict integer 5 |
| `model_equal` | Boolean `true` |
| `checks` | Positive unsigned 32-bit integer, equal to the frozen expected value |

Prepared contains no operation, instrumentation mode, trial, study or commit. The outer call binds case, operation, instrumentation and trial expectations; the frozen driver protocol supplies source and protocol identity. This slice selects no real study. Every prepared field is compared with its expected type and value. The two original modes have different check counts because allocation controls perform three additional checks. The [separate preparation protocol](ANALYSIS_COST_DECISION_PREPARATION.md) retains all ten historical input identities and both modes' prepared counts.

For `bad_text_tail` and `bad_items_tail`, `model_equal=true` describes equivalence on the valid wire before the producer appended `!`. It does not certify successful decoding of the malformed wire.

## 3. Observation types and order

Each observation has exactly these 15 fields: `kind`, `case`, `operation`, `trial`, `instrumented`, `ns`, `new_calls`, `requested_bytes`, `requested_peak_live`, `requested_live_at_return`, `largest_request`, `key_callbacks`, `process_hwm_before_kib`, `process_hwm_after_kib`, `consumed`.

The kind is `observation`; case and operation match the call; trials are strict integers `0,1,2,3,4` in that order; instrumentation is an exact boolean. The known operations are `live`, `project`, `encode`, `decode`, `model`, `chain`. The two bad cases admit only `decode`.

| Mode | `ns` | Six allocation/callback fields |
|---|---|---|
| Normal | Integer from 0 through `2**63-1`, in nanoseconds | All `null` |
| Instrumented | `null` | Integers from 0 through `2**64-1` |

Booleans, floating point numbers, strings and missing values cannot substitute for integers. A real zero time is representable; an unobserved value is never converted to zero. Strict complete keysets, bounded representations and the stronger allocation relations below are new adapter requirements. The preserved independent audit already required strict HWM types and cross-trial monotonicity, even though the original runner's selected comparisons were weaker.

## 4. Metric relationships and units

Requested payload must satisfy `requested_live_at_return <= requested_peak_live <= requested_bytes`, `largest_request <= requested_peak_live`, and `requested_bytes <= new_calls * largest_request`. Zero new calls imply zero requested payload fields. Positive new calls with all requested payload values zero remain legal because a zero-size request is possible. Key callbacks are independent of allocation counts; bad-tail instrumented calls require zero key callbacks and zero requested payload still live at return. The latter is specific to the nonowning tail-refusal path: scoped Error owners are destroyed before the lambda returns only an offset and the observer snapshots the epoch.

These are successful C++ new requested payload counts for the observation epoch. They exclude allocator headers, alignment overhead, unobserved allocation mechanisms and the retained preparation baseline. They are not copied bytes, total heap or RSS.

Both HWM fields are positive signed 64-bit integers in Linux KiB. Within a trial, before must not exceed after; the next trial's before must not be below the preceding after. This relationship applies within one process, not across calls. HWM includes preparation and earlier trials; subtracting the pair does not yield an operation's exclusive peak memory.

`consumed` is a positive unsigned 64-bit mixed-unit result sentinel. The frozen expected value must match every trial:

| Operation | Binding for this producer |
|---|---|
| Good `project` / `decode` | `records + 1`, because this exact fixture retains one entry |
| Bad `decode` | `wire_bytes - 1`, the zero-based offset of the appended tail byte |
| `live` / `model` | Explicit frozen sentinel for model records plus compile entries |
| `encode` | Explicit frozen sentinel for wire length plus the unsigned final byte |
| `chain` | Explicit frozen sentinel for the sum of four owned results |

The adapter does not infer a universal `2 * records` rule or treat wire length as the encode sentinel. It also does not infer allocation-free errors from a bad-tail return: the lambda checks the error and destroys it before returning an offset. Exact historical allocation amounts are not acceptance thresholds.

## 5. Bounded pure API

```python
from analysis_cost_semantics import freeze_cost_contract, validate_cost_output

contract = freeze_cost_contract(
    call_id="fixed-example", case="exe", operation="live", instrumented=False,
    prepared={"kind": "prepared", "case": "exe", "records": 1,
              "wire_bytes": 3405, "public_preparation_calls": 5,
              "model_equal": True, "checks": 115},
    expected_consumed=2,
)
# Retain contract.canonical and contract.sha256 outside the evidence bundle
# before accepting stdout. Supply the independently retained hash at audit.
```

`FrozenCostContract.canonical` is immutable bytes; `document()` returns a fresh copy. `validate_cost_output(raw, contract=contract, expected_contract_sha256=registered_hash)` returns a semantic verdict or raises `CostSemanticError`. Freezing invalid expected inputs raises `ValueError`. Expected values must be established independently before observations, not learned from the output being validated.

The raw input is bytes, at most 64 KiB; exactly six records are required, with at most 8192 bytes per record including its LF. LF and CRLF are accepted; the final LF is mandatory. Duplicate keys, noninteger numeric tokens, nonfinite numbers, invalid UTF-8, BOM, extra fields, empty or missing records and incorrect order are rejected. The contract is bounded to 8192 bytes and revalidated against its external SHA and canonical shape on every call.

## 6. Closed fixture and driver integration

| Fixed mode | Meaning |
|---|---|
| `cost_time` | Literal normal `exe/live` output |
| `cost_allocation` | Literal instrumented `exe/live` output |
| `cost_bad_tail_time` | Literal normal `bad_items_tail/decode` output |
| `cost_bad_tail_allocation` | Literal instrumented `bad_items_tail/decode` output |
| `cost_invalid_time` | Byte-intact fixture with boolean `ns` |
| `cost_invalid_allocation` | Byte-intact fixture whose live payload exceeds its peak |

All six emit predetermined bytes and empty stderr. Their time, allocation and HWM values are synthetic literals. The prior eight lifecycle modes retain their exact stdout/stderr bytes and behavior. The launch interface remains only `call_id` and a closed `mode`; there is no arbitrary command or real producer entry point.

The driver protocol and verdict are format 2. It adds the semantic source identity and each cost call's canonical `semantic_contract`, binds complete expected stream bytes, and compares frozen call documents canonically so boolean/integer equality cannot relax admission. The returned verdict includes `planned_semantic_calls`, `verified_semantic_calls` and `verified_cost_observations`. These are synthetic validation counts, not measured API consumption.

The public driver functions and original hard ceilings are unchanged: at most 8 calls, 1 MiB per stream, 8 MiB shared admitted bytes, 3 seconds active per call and 20 seconds overall active supervision. The current implementation rejects archived format-1 protocols; use the exact PR252 source and its external identities for those historical bundles. Do not rewrite old protocols or seals to the new format.

## 7. Stop and final audit

Both streams are preserved before business validation. A semantic error becomes the sticky first failure; capture is finalized INVALID, available prefixes remain, and the next planned call is not started. Correct stdout hashes, six record identities, return code zero and original-pipe EOF cannot override a business failure.

At the final gate the driver reopens and checks current spool/raw/backup bytes, call records, protocol and the capture seal, and runs the semantic parser again. It does not trust the earlier boolean result. A semantic verification count is incremented only for an intact call with no newly detected transport, copy or semantic problem. The externally sealed verdict is audited again before returning.

Known launch/reap/admitted-byte observations remain consumed after later record damage; an untrusted seal makes lifecycle counts unknown. Semantics do not add a refund, retry, repair or resumption path.

## 8. Limits and next decision

The [PR252 supervision boundary](ANALYSIS_SUBSTITUTE_DRIVER.md) still applies, including Linux-only qualification, default SIGCHLD with exclusive reap ownership, bounded prefixes and no signaling of a lost or reaped child identity. Disk hashes do not authenticate loaded modules or a producer. Final reads are sequential, not an atomic snapshot or guarantee of permanent integrity.

Spool writes, copy/read/parse/hash, synchronization and final audits have real overhead and may affect later scheduling or caches. Active deadlines do not cover every final flush/fsync/close or post-supervision operation. Three copies share a storage fault domain. None of these costs may be treated as zero or subtracted from an API window without a separate justified protocol.

The [independent decision preparation](ANALYSIS_COST_DECISION_PREPARATION.md) remains `PREPARATION_ONLY_BLOCKED`, real execution unauthorized and PR250 cost/integration HOLD. It freezes known identities and unresolved prerequisites; successful substitute semantics do not start a new study, refill five missing observations, reopen 001/002, grant product authority or qualify v5.7.0 for release. Actual verification and the next handoff are recorded in the [walkthrough](20261009-140634-cost-semantics/walkthrough.md).
