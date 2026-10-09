# Analysis evidence capture and final integrity review

[简体中文](ANALYSIS_EVIDENCE_CAPTURE_ZH.md)

## Purpose and evidence

This slice addresses a demonstrated gap in analysis evidence collection: a stream can pass its immediate checks and later lose its bytes, while the driver still publishes a successful completion summary. The required outcome is bounded preservation of the original stream bytes, exclusive creation of capture roots and stream files, and a final check of every planned stream before complete evidence is reported.

The motivating record is PR250 explicit-value cost study 002. Its execution ledger recorded 100 processes and 500 observations. The retained `070-history_256-chain-0.stdout` was subsequently found empty and no longer matched its recorded SHA-256, `ea375e03db244431e48f4764f5dc90035c6b6f7362c42643e16099428e327735`. Only 495 observations could be independently reconstructed from the preserved originals. The completed search found no faithful copy of the missing stream. Its loss mechanism and writer remain unknown.

Study 002 remains **INVALID** and PR250 remains **HOLD / Draft**. Implementing better preservation or detecting the loss with a new test does not recover those five observations or change that decision. Performance944's separate limited default-path qualification remains separate evidence.

The scope is recorded in [handoff 6078819232](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078819232) and [evidence delivery 6078869889](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078869889).

## Scope and implementation status

The new [helper](../tests/analysis/analysis_evidence_capture.py) belongs in `tests/analysis/`. It accepts caller-supplied binary streams and records analysis evidence. It does not launch a process, run a codec, build MQB, start MSVC, collect ETW, choose performance samples, or grant a measurement allocation. Tests use fixed data or substitute output only.

This document records the written implementation plan and the helper's API contract. Acceptance requires retained local test results and review of the final source. No passing test count or Windows acceptance is asserted here.

The slice starts from main `2762749b27fc89156b724d19e89cce84e388ef52`, independently of PR250. All previously tracked product, test, document, and workflow files remain byte-identical. In particular, the existing 51 workflows, native manifests, historical fixtures, Recorder, and 001/002 evidence remain unchanged.

## Written implementation plan

1. Freeze the accepted call plan, caller identity, output contract, and explicit resource limits before creating a capture session. Define a successful synthetic case and failures corresponding to late deletion, truncation, replacement, and incomplete closure.
2. Add failing pure-data contracts for those cases. Preserve the red results as implementation evidence; do not run the original cost study or use another case to manufacture the missing output.
3. Implement exclusive output creation, bounded chunked preservation of each binary stream, a separate original-byte copy, and an append-only receipt journal. Keep a first failure latched and retain the already-observed prefix.
4. Implement finalization and an independent read-only audit. Re-read the persisted plan, journal, and every original/copy pair; only an exact complete match may be sealed as complete evidence.
5. Exercise normal closure, bounds, malformed or incomplete data, short writes, duplicate identities, pre-existing destinations, and failures in either stream using pure data or substitute output. Confirm that later loss cannot pass solely because an earlier receipt succeeded.
6. Review the complete diff, bilingual documentation, and actual local test output. Keep product and historical workflow identities unchanged and make the Linux-only validation scope explicit.

## Freeze the plan and its authority

`CaptureSession.create(root, *, identity, plan, limits=Limits())` freezes the complete caller identity, ordered plan, and limits before any call starts. It serializes and re-reads those inputs, then rebuilds a private `Limits` instance from the manifest. Later mutation of caller-owned inputs does not change the session. The independent audit receives the expected inputs again; it does not accept a different plan merely because a file in the evidence directory declares it.

The session `identity` is an object containing 1 through 32 string fields. Keys contain 1 through 128 characters and values contain 1 through 4,096 characters. These are declared identities, not independently verified producer facts.

The plan is a nonempty ordered list. Every entry has exactly three keys: a unique `call_id` of 1 through 128 characters, an ordered `stdout_records` list of expected record identities, and a boolean `stderr_empty`. Each expected identity contains 1 through 32 fields with keys of 1 through 128 characters. Its values are strings of at most 4,096 characters, integers whose `bit_length()` is at most 64, booleans, or null. Duplicate expected identities within a call are rejected. Type equality is part of identity: `true` is not interchangeable with `1`. The exact identifiers, record count, order, and empty-stderr policy are frozen inputs.

Stdout is strict UTF-8 NDJSON: exactly one JSON object per expected record, in the declared order, with an LF terminator for every record. Empty stdout is valid only when no stdout records are expected. Blank lines, duplicate JSON keys, invalid UTF-8, nonfinite numbers, missing final LF, and extra or missing records fail validation. CRLF is preserved and accepted without rewriting. A record must contain each expected identity field with the exact value and type; additional fields are allowed.

Matching identity and preserving bytes do not validate additional record values as measurements. Units, ranges, workload equivalence, semantic success, and acceptance thresholds belong to a separately reviewed driver.

The supplied `identity` and reported exit code are caller claims. The helper can preserve and compare them, but cannot independently prove which producer ran, which binary produced the stream, or whether a reported process exit actually occurred.

## Bound the capture before reading

`Limits` is an immutable dataclass. Every field must be a positive integer; booleans are rejected. The defaults are also hard ceilings, and a caller may only lower them. The exact values supplied are included in the frozen manifest.

| Field | Default and hard ceiling | Applies to |
|---|---:|---|
| `max_calls` | 256 | Planned calls |
| `max_records_per_call` | 256 | Expected stdout records in one call |
| `max_stream_bytes` | 1,048,576 bytes (1 MiB) | Admitted bytes of one stdout or stderr stream |
| `max_total_bytes` | 67,108,864 bytes (64 MiB) | Admitted bytes across all stdout and stderr streams |
| `chunk_bytes` | 65,536 bytes (64 KiB) | Each bounded read request |
| `max_record_bytes` | 65,536 bytes (64 KiB) | One stdout record or encoded expected identity, including its LF |
| `max_plan_bytes` | 1,048,576 bytes (1 MiB) | Encoded manifest and encoded final result, each including its LF |
| `max_journal_line_bytes` | 65,536 bytes (64 KiB) | One encoded journal event, including its LF |
| `max_journal_bytes` | 8,388,608 bytes (8 MiB) | The entire journal |

Stream and total limits count logical input bytes once, before writing either copy; they do not count the same bytes twice because two files are saved. Metadata limits apply to the encoded representation. Lowering a limit can therefore reject a plan or prevent publication even when the number of calls is small.

The supplied reader must implement `read(n)` by returning a `bytes` object of at most `n` bytes. Only an actual return of `b""` marks EOF. At the stream or total limit, the helper may read one additional byte to distinguish exact EOF from overflow. That byte is retained only in the receipt's `overflow_probe_hex`, is excluded from the admitted-byte hash and counts, and causes failure. It is not silently discarded as a successful shorter stream.

A bound violation, reader error, invalid record, or write error remains a failure. A short write does not shrink the receipt to match the shorter saved file. The helper does not retry the source to obtain more convenient bytes. It rejects a reader that returns the wrong type or too many bytes, but cannot control the internal allocation or blocking behavior of a supplied reader.

These limits describe what the helper will admit and retain. They are not codec cost budgets, a guarantee of total heap or process RSS, an OS-wide resource limit, or permission to consume another study allocation.

## Preserve original bytes and receipts

For a started call, `preserve_stream(index, role, source)` consumes the supplied `BinaryIO`; the caller retains ownership of the source. Roles are `"stdout"` and `"stderr"`. The helper opens the backup file and then the raw file with exclusive binary creation (`xb`). For each admitted chunk, it updates the receipt before writing the chunk to backup and then raw. A pre-existing destination is an error and is not overwritten.

Before writing, the helper checks that the root, raw, and backup directories are real directories, not symbolic links, and retain the device/inode identities recorded at creation. A stable replacement is rejected before the attempted stream write. These checks do not establish an atomic path transaction or immunity to a concurrent adversarial replacement.

| Evidence | Role |
|---|---|
| `manifest.json` | Canonically encoded format version 1, caller identity, ordered plan, and limits |
| `journal.jsonl` | Consecutive lifecycle and receipt records, appended and synced by one session |
| `backup/000000.stdout.bin` and corresponding stderr file | Separate bounded original-byte copy |
| `raw/000000.stdout.bin` and corresponding stderr file | Raw stream under its fixed call index and role |
| `result.json` | Final integrity verdict; it may seal an invalid capture as well as a complete one |

Indices start at zero and use six decimal digits. The journal has four ordered events per completed call: `start`, two distinct `stream` events, and `finish`. Both stream roles must be attempted exactly once; either role may come first.

| Receipt field | Meaning |
|---|---|
| `byte_count` | Number of admitted input bytes, counted before either write |
| `sha256` | SHA-256 of those admitted bytes |
| `lf_count` | Number of byte `0x0a` occurrences in those bytes |
| `ends_with_lf` | Whether the admitted prefix ends with byte `0x0a`; false for empty input |
| `eof` | Whether the source actually returned `b""` |
| `capture_error` | First stream capture error, limited to 256 characters, or null |
| `overflow_probe_hex` | Empty string, or the two lowercase hexadecimal digits of the single overflow byte |

The receipt records what the reader admitted, not a claim that every write succeeded. On an I/O failure, saved files may contain a shorter prefix; their disagreement with the receipt remains invalid evidence. Hashes and counts concern original bytes, including CRLF, NUL, and a missing final newline. Parsing never normalizes or reconstructs those files; preserving malformed stdout does not make it valid NDJSON.

Stdout follows the frozen NDJSON identity contract. When `stderr_empty` is false, stderr may contain opaque binary data; it is still preserved and checked byte-for-byte. An empty stderr stream is a real recorded stream, not an omitted artifact.

Two copies improve preservation and allow disagreement to be detected. Copies in one evidence directory are not independent storage fault domains and do not make the bytes immune to later modification.

## Finalize every stream before reporting completion

The lifecycle is `start_call(index)`, both `preserve_stream` attempts, `finish_call(index, *, exit_code, failure=None)`, and `finalize()`. Calls follow the frozen zero-based order, with at most one active call. `exit_code` is a required keyword argument containing null or a strict integer in `[-2**31, 2**32 - 1]`; booleans and out-of-range values are rejected and latch failure. A successful call requires zero and no failure string. A supplied failure string is limited to 256 characters. `finish_call` performs an immediate check, but that check cannot substitute for finalization.

Before recording a stream's receipt event, the helper attempts to flush, sync, and close each writer; an error invalidates the capture. Finalization attempts to close the journal and re-reads persisted evidence. It checks the exact directory membership, expected manifest, complete ordered journal, all raw and backup bytes, receipt anchors, byte/line counts, end-of-stream state, and planned stdout records. Reads reject non-regular files, hard-linked files, and observed replacement or metadata changes during the read. A successful earlier per-call validation is insufficient.

`finalize()` permanently closes the session. When result publication succeeds, it returns a verdict even if the audit found failures. Callers must check `complete` and `integrity_status`: only `complete == true` with `integrity_status == "VERIFIED"` represents complete evidence. A failed gate produces `complete == false` and `"INVALID"`. `execution_finished` only describes ordered lifecycle closure; it can remain true while a stream is missing or corrupt. Counts in the ledger or an in-memory summary cannot override that verdict.

The result is created exclusively, flushed, synced, closed, and read back. Publication failure raises `CaptureError`; the helper does not return a successful result or reopen the session. `finalize()` returns an additional `seal_sha256` for the exact `result.json` bytes. That digest is not embedded inside the file it hashes and does not by itself mean the verdict was successful.

The caller must retain the expected seal independently of the evidence being audited. `audit_existing(root, *, identity, plan, limits=Limits(), expected_seal_sha256)` requires that external seal, re-freezes all expected inputs, and checks existing evidence without executing tools or repairing files. Its returned verdict must also be checked for `complete`. A digest obtained only from mutable files in the same directory is not an independent trust anchor. A sealed invalid result cannot become valid through this audit.

`VERIFIED` summarizes the final sequence of bounded file reads. It is not an atomic snapshot of every file at one instant and does not guarantee that all files remain unchanged at result publication or function return. A later write can invalidate that judgment, so a caller using retained evidence must perform a fresh audit against the externally retained seal. Flush/sync and exclusive creation do not establish a permanent writer lease, transactional filesystem semantics, power-loss recovery, or continued integrity after an audit. The helper does not repeatedly scan until concurrent modification becomes impossible.

## Keep the first failure and its prefix

A failure poisons the session for new calls. It does not authorize re-capturing the failed role or replacing the prefix. The still-unattempted other stream of the current call may be preserved so that a stdout failure does not erase available stderr, or conversely. The caller can attempt `finish_call` for the current call with its failure information; the call still raises as invalid. It can then attempt `finalize` to retain the failed verdict.

The first remembered failure is not replaced by later errors. The result's `capture_failures` retains up to the first 32 capture, flush, close, or similar exception texts, each limited to 256 characters. Final audit problems have a separate `failure_count` and `failures` list, also limited to 32 diagnostic strings of at most 256 characters. This is bounded diagnostic retention, not a lossless trace of every secondary exception. A journal write failure prevents further journal appends; existing bytes and attempted stream files remain. A readable journal prefix does not establish that the planned experiment completed.

Creation requires a new root whose parent already exists. Existing roots or output identities are rejected. Failure during creation can leave a partial root, which is retained rather than cleaned up. The helper does not delete, move, truncate, relabel, or auto-recover existing evidence, and it does not promote backup bytes into a damaged raw file. Any later forensic recovery must use an exact preserved copy and must keep the failed originals and verdict.

## Synthetic usage sequence

Run this example from the repository root on Linux. It uses only `io.BytesIO` with fixed stdout NDJSON and fixed stderr bytes, including an opaque NUL. It starts no producer process. It retains its fresh temporary directory, including any failed prefix, and prints the path before capture starts.

```sh
PYTHONPATH=tests/analysis PYTHONDONTWRITEBYTECODE=1 python -B - <<'PY'
import io
from pathlib import Path
import tempfile

from analysis_evidence_capture import CaptureSession, Limits, audit_existing

identity = {"protocol": "synthetic-demo-v1", "producer": "io.BytesIO"}
plan = [{
    "call_id": "fixed-0",
    "stdout_records": [{"call_id": "fixed-0", "record": 0}],
    "stderr_empty": False,
}]
limits = Limits(
    max_calls=1,
    max_records_per_call=1,
    max_stream_bytes=4096,
    max_total_bytes=8192,
    chunk_bytes=32,
    max_record_bytes=512,
    max_plan_bytes=4096,
    max_journal_line_bytes=2048,
    max_journal_bytes=8192,
)
directory = Path(tempfile.mkdtemp(prefix="analysis-capture-demo-"))
root = directory / "capture"
print(f"Retained synthetic evidence: {directory}", flush=True)
session = CaptureSession.create(root, identity=identity, plan=plan, limits=limits)
session.start_call(0)
session.preserve_stream(
    0, "stdout", io.BytesIO(b'{"call_id":"fixed-0","record":0,"value":"fixed"}\r\n')
)
session.preserve_stream(0, "stderr", io.BytesIO(b"synthetic diagnostic\x00\n"))
session.finish_call(0, exit_code=0)
result = session.finalize()

# Retain the expected seal outside the audited capture root.
seal_path = directory / "capture-result.sha256"
with seal_path.open("x", encoding="ascii") as destination:
    destination.write(result["seal_sha256"] + "\n")
if not result["complete"] or result["integrity_status"] != "VERIFIED":
    raise RuntimeError(f"Invalid capture: {result['first_failure']}")

review = audit_existing(
    root, identity=identity, plan=plan, limits=limits,
    expected_seal_sha256=seal_path.read_text(encoding="ascii").strip(),
)
if not review["complete"] or review["integrity_status"] != "VERIFIED":
    raise RuntimeError(f"Invalid current evidence: {review['first_failure']}")
print("Synthetic capture and independent read-only audit: VERIFIED")
PY
```

The sibling seal file demonstrates supplying a separately retained expected digest. It does not provide authenticated storage or a separate fault domain. A real caller must establish its own retention and trust policy. Negative tests use separate synthetic fixtures to change or remove saved bytes and require an invalid verdict; they do not repair historical evidence or launch replacement measurements.

## Local verification and CI scope

The local Linux test command is:

```sh
PYTHONDONTWRITEBYTECODE=1 python -B -m unittest discover -s tests/analysis -p 'test_*.py' -v
```

Use the same bytecode policy for any Python substitute child that imports repository code. This avoids leaving an untracked `__pycache__` that can invalidate a later clean-worktree identity preflight. Save the actual test output and source identity; do not infer passing counts from this command being documented.

The existing CI does **not** automatically execute the new pure Python contracts. Local Linux results do not establish Windows filesystem behavior or a Windows run of this module. New bilingual documents must be reviewed together by this slice's checks; the existing [Documentation workflow](../.github/workflows/docs-ci.yml) still runs the [checker for its original 22 maintained pairs](../tests/docs/verify_bilingual_docs.ps1).

With only the new analysis files and these independent documents, the expected existing PR checks are [Native's classifier/gate](../.github/workflows/native-ci.yml) with its real build and shards skipped, Documentation, and a skipped [Performance Evidence](../.github/workflows/performance-evidence.yml) comparison under a non-`perf:` title. The branch name does not admit that comparison; a `perf:` title or explicit dispatch would change the boundary. No new workflow or change to the [native-impact classifier](../tests/ci/classify_native_ci_impact.ps1) is part of this slice. In particular, changing the old documentation index or bilingual checker would start unrelated real build-system work, and adding a 52nd workflow would break existing exact workflow inventory contracts.

## Limits of adoption and the next handoff

This slice implements preservation and detection. It does not diagnose the mechanism that emptied study 002's stream, restore its missing observations, approve PR250 integration, or allocate a replacement experiment. Studies 001 and 002 remain closed with their original statuses and consumed budgets.

A real driver adopting this helper needs a later independent protocol: exact source and binary identities, the selected call plan, input and output contracts, limits, clocks and observation boundaries, measurement purpose, full budget, stop rules, final acceptance, and retained originals. The additional copies, parsing, syncing, and audits may affect future observations; they cannot be subtracted from old scores or called free. This implementation does not open a third study, add five replacement trials, or authorize codec/MQB/MSVC/ETW execution.

The product's producer-identity, current-content, complete-inventory, and deletion-authority flags remain false. No default CLI integration, artifact writer protocol, automatic persistence, clean/prune, recovery, release, or high-frequency adoption is granted. VERSION remains 5.6.0 and v5.7.0 is not released. Existing HOLD decisions, adverse matrices, default-path tails, dependency gaps, Stage1/Stage2 differences, and writer/atomicity/reparse/concurrency/Rium/#164 limitations remain in their original evidence.
