# Producer seal prerequisite 002: exact checkout transport

Status: `PREPARED_NOT_EXECUTED`. This is a new registered attempt after the
closed admission failure of attempt 001. Base commit:
`6cca81c9073e381eff3f2b3fd35b603b8dcf8f06`.

## Preserved first attempt and demonstrated root cause

Attempt 001 is closed as `ADMISSION_FAILED_BEFORE_BUILD`. Its workflow
[38104794336](https://github.com/Iviesever/msvc-quick-build/actions/runs/38104794336)
ran on the exact registered commit, failed the C++ source hash check, and
uploaded all seven checked-out source and plan files. Compiler and fixture
invocations were both zero: the exception precedes tool selection and output
directory creation. No native sharing or oplock observation was obtained.

The complete original artifact is 32,529 bytes, SHA-256
`6be8dc2267345325ce317d83138d7a10632890bd6b63193fbc56ac4d6b90b0e1`.
It was received with exclusive creation, file/directory fsync and independent
full archive/member readback. All seven files differ from the registered Git
blobs by exactly LF-to-CRLF expansion. The C++ file is 37,843 rather than 37,135
bytes, with precisely 708 CRLF pairs. Converting only those pairs back to LF
reproduces the exact registered source; no other byte difference exists.

The original repository attributes leave these paths' text/eol unspecified.
Locally applying Git's actual checkout filter with `core.autocrlf=true`
reproduces each of the three failed source artifacts byte for byte. This is a
reproduced transport cause, not a guess from a hash error.

The fix sets `text eol=lf` only for the registered probe, runner, Job helper,
workflow, plan files and the attributes file. The existing `install.bat binary`
rule remains intact. Hash checks continue comparing full original bytes; they
do not normalize data, accept a second digest or waive the identity check.
Git documents that `eol=lf` preserves index line endings during checkout:
https://git-scm.com/docs/gitattributes.

## New execution identity and source scope

The existing workflow's second run, first attempt is the sole admission for
`producer-seal-002`: `run_number == 2 && run_attempt == 1`, push event on
`feat/producer-clean-handoff-20261011`. Runner checks require the same values.
The source and raw output prefix and plan reference change to 002. This is not
a rerun of 001. An unexpected run number, second attempt or pre-existing output
directory must refuse; there is no automatic retry or dispatch trigger.

The C++ probe remains 37,135 bytes, SHA-256
`1b62df9b7cc1253fc1a208f2a84154eba75907b7b2fef2d1fc72ed2f0423febd`.
The Job helper remains 11,417 bytes, SHA-256
`ba2dff1452112d31dfaafa01c8a15b985539317867f3c791aed9dc3eaf24fce9`.
Runner changes are limited to the new attempt/run identity and this plan path.
No native API, classification, process capture or producer implementation is
changed by the checkout correction. Independent review must bind the exact new
source tree and immutable Connector commit before the branch advances.

## Fixed budget and retained observation contract

The complete native cases, exact masks, positive controls, RAII lifetimes,
classification and evidence requirements in `native_probe_plan.md` remain the
contract for this new attempt. Its attempt-001 identifiers and first-run number
are superseded only by the explicit 002 identities above.

| Item | Maximum for attempt 002 |
| --- | --- |
| Workflow | One branch push, run number 2, run attempt 1 |
| Compiler | One cl invocation with at most its one LINK child; Job total at most two |
| Probe | One executable; exactly one Job process, zero children, at most two auxiliary threads |
| Platform | Native x64 on windows-2022; actual image, OS, tools and local fixed NTFS recorded |
| Waits | Compiler 90 s; native wrapper 40 s; probe watchdog 30 s; each teardown poll at most 10 s |
| Workflow ceiling | Five minutes; these waits do not give arbitrary OS calls hard real-time deadlines |
| Fixture content | At most 1 MiB, only newly created local NTFS fixture objects; build and capture artifacts retained separately |
| MQB / ordinary producer / M1b service / ETW / cost | Zero |
| Deletion / disposition / recursive cleanup / old evidence overwrite | Zero |

Before this new attempt, cumulative compiler/probe calls across 001 are 0/0.
Attempt 002 therefore allocates at most one compiler driver, one linker child
and one probe invocation; it does not allocate any research matrix, service
study, product build or replacement cost sample. Failed or partial 002 evidence
must also remain closed and preserved.

The compiler and fixture each use the unchanged suspended-start, private
kill-on-close Job capture helper. Staging stdout/stderr files are exclusively
created before process creation. Tool/source identities precede execution;
binaries are hashed before invocation; complete source/tool identities are
rechecked afterwards. Original failure streams and per-entry readback errors
remain distinct. Final canonical evidence requires complete exclusive writes,
file/directory fsync and a separate process's full readback after transport.

Verified WA/EA mutation before the final pin releases with RWH pending before
and after is `CANDIDATE_DISQUALIFIED`. A mere old-handle grant or directory
capability coexistence is a narrower subclaim failure and stays `INCONCLUSIVE`.
`NECESSARY_CHECKS_ONLY` is never full producer qualification. Missing or invalid
results cannot count as pass. The whole conditional-ACL and service mechanism
still needs separate qualification and actual ordinary LINK integration.

A1/A2/A4, positive C01 handoff, A3/A5 and deletion remain false. The frozen PR250
preparation remains exactly SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`;
all existing service/cost holds remain in force. This correction resumes the
already authorized implementation work without changing its acceptance target.
