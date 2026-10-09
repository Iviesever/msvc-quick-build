# Implementation and acceptance contract

## Design decision

Use a pure cost semantics module and a narrow extension of the existing closed
fixed-substitute driver. A separate driver would duplicate process supervision;
an offline-only parser would not stop a later call after a semantic failure.
The selected composition covers both per-call admission and final audit without
changing the transport or capture implementations.

The design implements the already accepted next handoff. The standing user
instruction to continue through review and normal merge also covers routine
implementation choices. The brainstorming, using-git-worktrees, agentic-coding,
test-driven-development and executing-tasks workflows apply. The separately
referenced writing-tasks skill was unavailable in the prior catalog search;
this written plan provides the concrete task/checkpoint contract.

## Components

1. `analysis_cost_semantics.py`: immutable, hash-bound call contract; exact
   prepared and observation schemas; bounded strict JSON; typed values, units
   and cross-record relationships. It starts no processes and reads no old
   evidence. Caller-supplied mutable objects cannot change frozen validation.
2. Existing fixed fixture: six explicitly synthetic cost modes (four valid,
   two semantically invalid). Values are literals, not measured observations.
   Existing eight modes retain their exact bytes and behavior.
3. Existing driver: bind the semantic source and call contract in the protocol;
   validate after preserving both streams but before finish/next call; repeat
   validation during the final audit. No arbitrary producer or command hook.
4. Independent decision preparation: machine-readable reference identities,
   windows, overheads, budgets and blocking prerequisites, with a strict static
   validator. It has no execution transition and allocates zero real work.

## Cost contract

Each call contains exactly one prepared followed by observations trial 0..4.
Prepared has exactly kind, case, records, wire_bytes, public_preparation_calls,
model_equal and checks. It is compared with a typed frozen expected object;
operation and instrumentation are supplied by the outer call contract.

size_t counts use bounded unsigned 64-bit integers and checks is a positive
unsigned 32-bit count; timing is a nonnegative signed
64-bit nanosecond count; Linux HWM is a positive signed 64-bit KiB count with
within- and across-trial monotonicity. Booleans and floating point numbers are
not integers. Normal output has integer ns and six null allocation/callback
fields; instrumented output has null ns and integer allocation/callback fields.
Allocation relations include live <= peak <= total and largest <= peak. Zero
calls imply zero requested payload; positive zero-byte requests remain valid.

Freeze expected_consumed independently. Good project/decode must consume
records + 1 for this producer's single retained entry. Bad-tail decode consumes
wire_bytes - 1 and has zero key callbacks and zero retained epoch payload when
instrumented, as its scalar-returning lambda destroys scoped Error owners. Other operations
use the frozen positive consumption identity, not an invented size or time
formula. Bad-tail model_equal refers to the valid wire before appending '!'.

The adapter admits at most 64 KiB stdout and 8192 bytes per record, complete LF
or CRLF framing, exactly six records, no duplicate keys, nonfinite values,
unknown fields, extra or missing records. Contract SHA must be externally
provided. These are validation rules for this frozen producer, not universal
codec guarantees or measurement provenance.

## Independent future decision preparation

State is PREPARATION_ONLY_BLOCKED, real_execution_authorized=false. Historical
candidate/base/binary/input identities are reference facts, not new selections
or grants. Every currently authorized real process, public API, warmup, retry
and replacement budget is zero. Unknown future choices remain explicit null
blockers. Historical 100-process/1340-API consumption is a separate ledger.

Required future decisions include an independent purpose beyond the missing
five, exact newly selected identities, acceptance criteria fixed before data,
complete global matrix/process/API/resource budgets, observer-window and
copy/sync interference policy, real producer support and independent registered
approval. The current 8-call/3-second/20-second closed driver cannot silently
become the old 100-call/25-second/600-second study.

## Sequential work and gates

1. Verify remote main, PR252, PR250, #198 and preserved source identities.
2. Prepare this isolated branch and run the unchanged analysis baseline under
   an exclusive retained harness with a closed fixed-substitute launch budget.
3. Write pure semantic, decision and integration tests before implementation;
   retain expected missing-feature RED, then implement the minimum contract.
4. Use targeted RED/GREEN for any discovered regression. Never replace the
   original failure or change historical gates to make a result pass.
5. Freeze all final Python sources and run the complete analysis suite once
   after implementation; source hashes before/after must match. Actual child
   launches are separately counted; codec and real study budgets stay zero.
6. Obtain independent implementation and bilingual documentation review, fix
   material findings with evidence, then verify the final frozen source.
7. Preserve all existing tracked blob/mode pairs except the intentionally
   extended driver/fixture files and revision notices in the prior driver
   document pair; keep 51 workflows, VERSION, product sources,
   prior tests, PR250 and all old evidence identities unchanged.
8. Use Connector Git Data APIs for exact source submission, inspect the actual
   PR files/head and actual applicable CI logs, record an exact-head review,
   mark ready, merge normally, and read back merge parents/tree/main CI.
9. Write a walkthrough with actual results, update #198, and deliver a validated
   evidence archive. Do not claim new local contracts were executed by old CI.

## Verification budget and boundaries

Each retained test stage may launch at most 40 direct fixed Python substitutes,
all through the exact six-argument Python -I -B -u fixture command. Each frozen
driver protocol still permits at most 8 calls and keeps every original hard
ceiling. Static tests and reviewers must not launch workload processes. A test
leak, unknown real-child reap status or emergency cleanup is a failed stage.

The final audit is sequential, not an atomic snapshot. File identities do not
attest loaded modules, shared libraries, the kernel or Windows qualification.
Spool flush/fsync/close, subsequent copying/parsing and sealing are not covered
by all active deadlines; copies share a storage fault domain. No operation is
claimed free or harmless to future measurement simply because it is outside an
API timing window.
