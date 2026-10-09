# Cost decision preparation

[简体中文](ANALYSIS_COST_DECISION_PREPARATION_ZH.md)

## 1. Status and decision purpose

The [frozen preparation data](ANALYSIS_COST_DECISION_PREPARATION.json) implements the decision-preparation portion of [handoff 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173), following the fixed-substitute capture driver merged in [PR252](https://github.com/Iviesever/msvc-quick-build/pull/252). Its state is permanently **PREPARATION_ONLY_BLOCKED**. Checking the exact bytes can qualify this preparation; it cannot authorize a real workload, accept product costs or integrate PR250.

The decision purpose is to determine whether the cost-output adapter can enforce frozen prepared/observation semantics and final evidence integrity using fixed substitutes, and identify the prerequisites for a separately reviewed decision about whether any independently justified real-cost execution is needed. No independent real-cost question has yet been selected. Replacing study 002 or filling its five missing observations is not a new question.

```json
{
  "format": 1,
  "kind": "cost-decision-preparation",
  "protocol_id": "pr250-cost-semantics-preparation-20261009",
  "status": "PREPARATION_ONLY_BLOCKED",
  "real_execution_authorized": false
}
```

Every entry in `authorized_real_budgets` is a strict integer zero. Every entry in `future_selection` is null. These nulls mean unselected and blocking, not zero cost, an inherited choice or permission to fill the field and proceed. The seven blockers and four false authorities are part of the frozen data. The document contains no execution transition.

## 2. Analysis and product identity roles

| Role | Commit | Tree |
|---|---|---|
| Analysis development base | `bf5df76079f224ea9bb5e7822be683cb3a2e4d45` | `46007c773858dd50653db0a49ce6861b15733fb2` |
| Historical PR250 product candidate | `a26b103685589ab8a5b4df60d1b760ec445efdf5` | `4e777bdfc33b7befaf5e3769e27eb4fe910f9f03` |
| Historical original product base | `2762749b27fc89156b724d19e89cce84e388ef52` | `0a4c2dd76c56309accf58d584a079b4bd483ee49` |

The analysis development base identifies where this implementation started. It does not claim the new files already belong to that commit, and it does not replace the original product comparator. New submitted source identities must be established from the submitted tree and the retained verification evidence.

The candidate, original product base, binaries and inputs below are historical references. Their continued existence does not select them for another execution. The original base has no equivalent old codec: the historical live-model comparison used the candidate's existing model entry on the same selected facts. Differences between unlike operations cannot be called an old-codec regression or isolated serialization cost.

The preserved build reported GCC 13.3.0, target `x86_64-linux-gnu`, and `-std=c++23 -O2 -DNDEBUG`, with separate `COUNT_ALLOCS=0` and `COUNT_ALLOCS=1` binaries. Six original production translation units were compiled; the source identity record covers 232 production files, including 135 headers. System, GCC and STL headers were not preserved as a complete byte-identical runtime bundle. These are Linux/GCC reference identities, not Windows/MSVC qualification. The [001 registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078630647) records these boundaries.

## 3. Historical file identities

The following names are relative to the preserved `pr250-qualification` evidence root. They identify retained bytes; they are not executable entry points, required checkout paths or new execution inputs. The same complete identities are embedded in the JSON, so an offline reader need not possess the old local directory to understand this preparation.

| File | Bytes | SHA-256 |
|---|---:|---|
| `run_cost_study.py` | 10211 | `c13b89f0ab5437986e17012af3a0879566a4c1b475d6d1c81f1c34976a5efaf7` |
| `cost_harness.cpp` | 16500 | `d0b7d1025a2d2d010fc2df562977ca3ec96a478ae8960672503c9847d6ac3632` |
| `cost_fixture.inc` | 16015 | `2774b498d1ba544c18ecc6fb08b7b48f818c0ec5ba6e7616240eda58647563c7` |
| `prepare_cost_harness.py` | 2043 | `fe33f000e4b18b697764f63f937be153d4d56825594c49b0cecef32979124666` |
| `build_cost_harness.py` | 2370 | `f9238b51f5c0ddeae9a569348f3db591a727febf77906ada937bcf90a0a41633` |
| `cost-fixture-source.json` | 782 | `70c632ffec7113f38f2397c857ab381f917ea159996243b2a404e5fa1d3e8309` |
| `cost-final-build-manifest.json` | 4250 | `3929fce949b7c57c29d90dbd682c6c5d89d238ee9473a2754f9f27183ff02fc6` |
| `cost-preparation/manifest.json` | 15338 | `e6d5e0202e708290f63c4c6ca41d24a73d567d282bfc96653eacf237aa9bf6ea` |
| `independent-production-source-identity.json` | 56952 | `223bd1ea0ae060e48a803f1bfd574a2a51eb7796a686a1b53c0da3c0219af0c3` |
| `run_cost_study_002.py` | 10219 | `0a07fc21997cdf0b5d69b08cde290a2e7d9aad0cb7369bc9cabaa971f7705124` |
| `cost-study-frozen.json` | 6757 | `41cdf32e1ee737efe176dde1c1155f68b0f81e542969687f475a546d645faa2d` |
| `cost-study-002-frozen.json` | 7126 | `6632fafcb73fef3e5b1ab5e6c344d8eca05e81c5831e85aad95ee5389c53a766` |
| `cost-final-0` | 729752 | `378be587c5b54fb7ba83fa04e0e3f9b4732d40e9af0ee2c6242a7fba5a571528` |
| `cost-final-1` | 731440 | `544ea84717c7cec9d79c98ab83ad916b7e08cac94b21e1737f5aae746e652dd6` |

The [002 registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078685272) preserves the old allocation, runner and manifest separately from 001. Neither runner is imported or replayed by this preparation.

## 4. Input and prepared relationships

Each case has two historical files, `cost-preparation/CASE-0.wire` and `cost-preparation/CASE-1.wire`. Both modes have the listed wire size and SHA-256. They are synthetic prepared inputs, not original persisted evidence from a real lifecycle.

| Case | Wire bytes | Wire SHA-256 |
|---|---:|---|
| `exe` | 3405 | `cdfdcd07edceb308837bd115857dd76fc008a5c10af27dc4dd8ca7006a37225e` |
| `dll` | 3429 | `b65d212ca2b21051ee03a086a16b0ddfe6cfd8a12b5279eb4de63ff30d1be473` |
| `static` | 3174 | `b190a2fff3aadb75e509fe499553cff2e5f30f3a3c003c057bca07f7bd92c58f` |
| `reuse_save_failure` | 6878 | `599fa256096913e4385240d88cde43da96b842ac98309d1dbb9d721cb8ef46a1` |
| `snapshot_128k` | 135422 | `2d43cb09a00c115084c44d6144d2c46391ca3464b49082f7ffb12fd129f9fdfa` |
| `history_256` | 304248 | `070f87a96a4c6f68e2e40a3706d127016a3276baab9222643620a20895f9899e` |
| `text_7m` | 7343780 | `3db6c28c33f4ff9b9386ef1b740042927324e316be9be0fb07668c535f80ef94` |
| `items_60000` | 363373 | `ae55e61087c39579b48703070f2c592128cb47dff01be47261c4b4bd3f41e8b5` |
| `bad_text_tail` | 7343781 | `6af269bef4cd49836b4891149942d7c91798d39f468aacd2ed6797a0f417b0d1` |
| `bad_items_tail` | 363374 | `471f9b987af3256900d9a778c46a8ee077affb972563ee429ffefae2c4bd8719` |

All prepared records have `kind="prepared"`, `public_preparation_calls=5` and `model_equal=true`. The prepared object has no operation, mode or trial; the outer frozen call contract supplies those identities. Mode-specific checks must be bound separately:

| Case | Records | Normal checks | Instrumented checks |
|---|---:|---:|---:|
| `exe` | 1 | 115 | 118 |
| `dll` | 1 | 118 | 121 |
| `static` | 1 | 106 | 109 |
| `reuse_save_failure` | 2 | 203 | 206 |
| `snapshot_128k` | 1 | 115 | 118 |
| `history_256` | 256 | 8207 | 8210 |
| `text_7m` | 1 | 129 | 132 |
| `items_60000` | 1 | 115 | 118 |
| `bad_text_tail` | 1 | 129 | 132 |
| `bad_items_tail` | 1 | 115 | 118 |

The instrumented checks include three allocator-control checks. For the two bad cases, model equality concerns the valid parent wire before `!` was appended; it does not assert that the bad wire decoded successfully. The error window returns an offset, whose meaning must not be replaced by a model size or a successful-decode claim.

These fixed shapes have specific limits: history uses 256 executed single-source records and one retain; the large text is terminal warnings omitted from the live/model recipe; the item case uses short arguments; the snapshot label is ASCII. None qualifies every combination of upper bounds, arbitrary callbacks, large Windows UTF-16 conversion or high-frequency use. The prepared facts and these coverage limits come from the historical manifest and harness identified above.

## 5. Historical consumption and current zero budgets

Study 001 remains failed-preflight with no measurement process or observation. The preceding 20 prepare processes and their 100 public API calls remain consumed. Study 002 remains INVALID under the [exact-head HOLD review](https://github.com/Iviesever/msvc-quick-build/pull/250#pullrequestreview-5468664960).

| Historical item | Count |
|---|---:|
| Prior prepare processes | 20 |
| Prior prepare top-level API calls | 100 |
| 002 processes in execution ledger, all exit zero | 100 |
| Preparation API calls inside those measurement processes | 500 |
| Observation-window top-level API calls | 740 |
| Full study top-level API consumption | 1340 |
| Observations claimed by execution ledger | 500 |
| Independently reconstructable prepared records | 99 |
| Independently reconstructable observations | 495 |
| Reconstructable normal / instrumented observations | 245 / 250 |
| Intact original streams at the prior full audit | 199 / 200 |

The original order was case table, then live/project/encode/decode/model/chain, then normal/instrumented; bad cases only admitted decode. Each of the 100 cells used a fresh process and trials 0 through 4. This is the description of an already-consumed allocation, not a proposed new matrix. `future_selection.matrix` remains null.

Each measurement process performed five preparation APIs. A chain observation made four top-level APIs; every other observation made one. These facts explain the 500 plus 740 plus prior 100 ledger, without claiming a census of nested implementation calls. Each instrumented process also ran ordinary 257-byte and aligned 129-byte allocation controls: two requests, 386 requested bytes/peak/live, largest 257, then live zero after release. Controls are separate from public API calls and observations.

The missing original remains `cost-execution-002/070-history_256-chain-0.stdout`: expected SHA-256 `ea375e03db244431e48f4764f5dc90035c6b6f7362c42643e16099428e327735`, preserved size 0 and SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`. Its prepared record and all five normal chain times remain null. The completed faithful-copy search found no matching original. Missing evidence does not refund calls or authorize a replacement, interpolation, another search or another study.

All current real budgets are zero: codec compile, preflight, prepare and measurement processes; public API, warmup and allocator-control calls; real observations; retries; replacement processes and observations; MQB and MSVC processes; ETW sessions; performance studies. The separate bounded fixed-substitute test budget is synthetic verification, not a real allocation. Unknown future global process/API/resource/output/metadata/copy budgets remain unselected. There is no new study number or executable command in this preparation.

## 6. Observation windows and collection overhead

The JSON retains the source-grounded window definitions even though it authorizes no real execution. They prevent future readers from assigning incompatible meanings to the same field names.

| Window or quantity | Required interpretation |
|---|---|
| Normal `ns` | Nonnegative steady-clock nanosecond API/wrapper return latency, including internal validation, temporary destruction and take/move packaging. Excludes caller destruction of the final owned result, consume and stdout formatting. |
| Instrumented allocation | `ns` is null. Count successful C++ new requested payload and callback activity for this window; exclude preparation baseline and allocator/OS overhead. Requested bytes are not copied bytes, total heap or RSS. |
| Positive ownership | Preserve the returned owning value until statistics are captured. Chain keeps projected archive, wire, decoded archive and model together through return. |
| Bad-tail decode | Includes refusal and exact error-code/zero-callback checks, plus Error destruction before the offset return. A zero live-at-return value does not show that the public Error has no allocation. |
| Preparation | Five public APIs precede measurement; all prepared input representations remain alive across the warm trials. Full warning bytes/order and PRIVATE exclusion are checked separately from model equality. |
| `process_hwm_before_kib` / `process_hwm_after_kib` | Positive Linux process-history high-water marks in KiB, including startup, preparation, retained inputs and earlier trials. The after read follows consume and precedes current stdout formatting. Their difference is not operation-exclusive RSS or Windows working set. |

The existing [driver contract in English](ANALYSIS_SUBSTITUTE_DRIVER.md) and [Chinese](ANALYSIS_SUBSTITUTE_DRIVER_ZH.md) distinguishes active supervision from later work. Pipe reads and spool writes happen during active supervision. Final spool flush/fsync/close happens afterward; the recorded finish includes it. Bounded spool reads, BytesIO, raw/backup copying, parsing, hashing, record/result publication, sealing and final audits are not all covered by those active deadline gates. Blocking OS operations also prevent an absolute return-time guarantee.

Spool, raw and backup are three copies in one storage fault domain. Admitted pipe-byte limits count input once, not aggregate storage, metadata or temporary memory. Formatting, buffering, pipe backpressure, copying, reading, parsing, hashing, synchronization and audits have costs and can affect scheduling/cache and later calls. Being outside an API time window does not make them free or prove no measurement disturbance. The [capture contract in English](ANALYSIS_EVIDENCE_CAPTURE.md) and [Chinese](ANALYSIS_EVIDENCE_CAPTURE_ZH.md) retains its own independent final-integrity rules.

Whole-child wall is not a replacement for missing API latency. Instrumented values, adjacent trials and nearby cases cannot fill the missing five normal times.

## 7. Seven blocking prerequisites

The seven strings in `blockers` describe the following unresolved prerequisites. None is satisfied merely by preserving the historical references:

1. Select a real-cost decision purpose independent of replacing study 002 or filling its missing observations, and decide whether real execution is needed at all.
2. Select exact future candidate/comparator, source/binary/compiler/runtime and callback/input identities, plus the exact invocation. The historical identities do not select these automatically.
3. Fix future product-cost acceptance criteria or numerical budgets before new data. The original registration had no numerical product threshold; existing results cannot supply a retrospectively invented one.
4. Select the full global matrix/order, process/API/warmup/control ledger, resource/output/metadata/copy bounds and stop accounting. The closed driver permits at most 8 calls, 3 seconds active supervision per call and 20 seconds overall. The old study used 100 calls, 25 seconds per child and 600 seconds overall, plus 1 GiB address space and 20/21 CPU seconds per child. Those policies are incompatible without separately reviewed design; batching cannot silently reset a global budget.
5. Define observer windows and interference policy for formatting, pipes, spool writes, copies, reads, parsing, hashing, flush/fsync/close, sealing and auditing. Their effects are not determined from API window placement alone.
6. Qualify real producer support and any required global supervision, exact reap ownership and complete evidence binding. The current driver accepts fixed Python substitutes; schema and exact fixed-byte checks do not authenticate real output.
7. Obtain independent review and a separate exact protocol registration/readback before a future real launch. This artifact has no approved or executable successor state within its own format.

Accordingly, the future purpose, candidate, comparator, binaries, inputs, matrix, acceptance criteria, observer, global budgets and registration remain null. The existing evidence cannot decide them or change `real_execution_authorized` to true.

## 8. Acceptance and absent execution transition

Acceptance has three separate scopes. First, an offline verifier may accept only the exact preparation bytes and closed state using an independently supplied expected SHA-256. A hash obtained solely from the same mutable file is insufficient. Malformed or altered data must be rejected, including changed budgets, non-null selections, missing blockers or changed authority.

Second, complete typed semantics and the frozen call identity/order may qualify the fixed-substitute adapter only when the transport, capture and fresh final audit also pass. Synthetic observation numbers are literal test data. They are not cost samples, proof that study 002 is valid or evidence of a real producer.

Third, the product-cost and PR250 integration decisions remain HOLD; real execution remains BLOCKED. There is no numerical cost threshold in this preparation and no editable grant, approve/proceed switch, real runner entry point or automatic third-study allocation. Filling unknown fields changes and invalidates this frozen artifact. Any independently justified later work needs a different reviewed artifact and decision.

The stop rule preserves the first identity, schema/type/value/order, budget, EOF, exit/reap, timeout, stderr, I/O, publication or final-integrity failure and stops subsequent calls. Available bounded prefixes and observed consumption remain evidence. Unknown consumption stays unknown; it does not become zero. No retry, refund, interpolation or replacement is permitted by this preparation.

Passing preparation or substitute checks does not approve PR250 integration, release, default/high-frequency use, automatic persistence, recovery, clean/prune or #248 integration. The four authorities remain false: `producer_identity_verified`, `current_content_verified`, `complete_producer_inventory` and `deletion_authorized`.

## 9. Preservation and qualification limits

This preparation leaves 001 closed as failed-preflight and 002 closed as INVALID. PR250 remains Draft/HOLD. Default Windows Performance944 remains a limited existing default-path qualification; it does not repair the explicit-cost evidence. All 819/886/897/907/919/937/944 adverse records and block variation remain relevant. Equal counts do not establish a noise explanation.

The inherited limits also retain file_inputs 7-to-1/dependency gaps, dedicated DLL/LIB/PCH/HU and Windows/high-frequency/real-wire cost gaps, different-byte bootstrap, writer/atomicity/reparse/concurrency/recovery, clean-prune/Rium/#164 and #248 isolation. VERSION remains 5.6.0; this preparation authorizes no v5.7.0 release. These decisions are preserved in [handoff 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173) and the [PR250 HOLD review](https://github.com/Iviesever/msvc-quick-build/pull/250#pullrequestreview-5468664960).

Exact-byte verification describes the files read during that check. It does not attest already loaded Python modules, the whole runtime/kernel, atomic check-to-exec behavior or producer authenticity. Final bundle verification remains a sequential read interval rather than an atomic snapshot or permanent integrity guarantee. This document records no new real measurement or passing test count; actual implementation acceptance belongs to its independently retained verification and review.
