# 017 runtime evidence: corrected collection completed, compatibility blocked

2026-10-16. This closes dev005/dev006 evidence review, not formal qualification.
Execution commit: `bca12b9d69872472a4af7b6cf4ada3f5a9b37f3b`.
Registration: Issue198 / 6209030919. Original dev005 registration: 6208894745.

## Real CI and originals

| Collection | Run / attempt | Artifact | Bytes | SHA256 |
|---|---|---|---:|---|
| dev005 | 38856832500 / 1 | 12027905322 | 159820 | ac60a8615f7078ff561d2c3d86c1d3ee7fa13d107c1f90ef04c873574aba1061 |
| dev006 | 38857820976 / 1 | 12027354743 | 161585 | ee7ccb47bc8ede203c702033ce991b2be99d55a489a3aba9fa5c72b891d86b3a |

Dev006 workflow386354821/job116833142480 and every step completed successfully.
Each original ZIP has56 unique safe members with valid CRC,35 source files
matching its registered Git blobs and CI SHA256s. Unit results are327/335,
respectively, all individually successful, no skipped cases. The corrected335
also pass locally; these overlapping runs are not summed as distinct methods.
Both workflows are consumed once, not rerun. Raw files remain unchanged.

## Corrected actual loading projection

| Role | Ordinary modules | Blockers | Mapped files | Hashed runtime files | Unique file bytes | Two-pass bytes |
|---|---:|---:|---:|---:|---:|---:|
| owner | 107 | 2 | 12 | 125 | 19884942 | 39769884 |
| controller | 103 | 2 | 12 | 121 | 19853398 | 39706796 |

Both maps passes use4 reads and reach explicit EOF. File mapping identities and
permissions match at both checkpoints. Python PT_INTERP joins to the actually
mapped `/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2`; mapped inode/device joins
its hashed file record. Python3.12.3 executable SHA256 is
`e50d468e8b0adfb05733f5b87b3cff34829c4a8c1aea50c865aa8bdfe4bb150f`.
The two roles took414490396ns and164015799ns and exited0/reaped without timeout.
These are observer intervals, not performance scores or formal owner runtimes.

The unchanged static ELF scan separately has12 nodes/29 edges and26 content
passes/30942694B per role. It never executes the decoder. Actual mapped Python
files include extensions and their libraries beyond that static root graph.
Do not call a static DT_NEEDED graph a complete native-runtime trace.

## Findings that remain, and the next bounded decision

`typing.io` and `typing.re` remain MODULE_OBJECT blockers in BOTH actual roles.
The observed Python3.12 cached objects cannot pass the existing strict module
contract. Their documented deprecated-namespace origin is not malicious injection,
but it is also not a reason to erase evidence or permit arbitrary cached objects.

The next implementation decision is a reviewed compatible preload/interpreter
policy for those two exact entries, followed by evidence from the **same intended
execution environment**. Do not repeat maps-EOF work or the consumed collectors,
rebuild existing bootstrap/transport components, generate a permissive allowlist,
or label differing producer strings as independent preparation. A minimal review
should trace which project annotation imports require typing at runtime before
changing the interpreter or broadening the module boundary.

Dev005's missing-loader projection remains unusable as complete-map evidence:
it lacked EOF proof. Its raw addresses were not retained, so do not invent the
missing byte count. Eight deterministic controls expose6 FAIL/1 ERROR with the
old reader; the corrected reader satisfies the same assertions. The original
327 tests and all40 baseline workset files remain byte-identical.

The observer is __main__, so neither module table is the exact formal bootstrap
preload. Collection identities cannot pin a future ephemeral runner. Decoder
runtime, non-descendant writer/FD-transfer qualification, independently admitted
preparations and containing-controller exit still require their own evidence.
No formal authority/allowlist or target SHA was created; no actual103B/image/GNU/
Windows/MQB/ETW workload or formal preparation/observation ran. Main, HOLD/DEFER,
VERSION, existing PRs, deletion and release authority remain unchanged.

Source changes and this review were produced by the same authoring session,
not a second independent reviewer. A delivery-side audit initially rejected the
legitimate empty urllib/__init__.py; it now preserves the original nonnegative
snapshot bound and requires the empty-file SHA. That local audit correction
changed no source collector, CI output or execution verdict.
