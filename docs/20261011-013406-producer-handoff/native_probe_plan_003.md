# Producer seal prerequisite 003: explicit no-PDB fixture link

Status: `PREPARED_NOT_EXECUTED`. Parent:
`a4719396e7cee16d9c3deceb37a2c57f5d173136`.

## Closed prior evidence

Attempt 001 remains closed before compilation because checkout changed LF to
CRLF. Attempt 002's exact-path LF correction worked: all nine reviewed files
were captured byte-identically on Windows. The compiler returned 0 with empty
stderr, but the private compiler Job contained three total processes, above its
accepted maximum of two. After the root handle signaled, a Job accounting read
also reported remaining activity. The unchanged helper recorded that error,
terminated its owned Job, and observed ActiveProcesses zero. The wrapper
retained `BUILD_FAILED_OR_TIMED_OUT`; no fixture executable was invoked.

[Run 38105317836](https://github.com/Iviesever/msvc-quick-build/actions/runs/38105317836)
and its full original artifact are closed as `CLOSED_BUILD_BOUNDARY_REJECTED`.
The archive is 2,362,350 bytes, SHA-256
`fb8a92b69d88f3df2f09f62469f0e4a8e67e21ed1fb59d963732a6a010b7e79d`.
It and all members were exclusively received, file/directory fsynced and fully
read back in a separate process. All recorded source/tool hashes remained
unchanged; all original build streams and outcomes are preserved.

The build produced an EXE, OBJ and an 8,417,280-byte PDB. The third process's
image identity was not recorded. Neither the PDB nor Job count establishes that
it was mspdbsrv; a momentary accounting observation also does not establish how
long the remaining member would have lived. The observed cumulative count of
three independently violates the two-process acceptance cap. These limits
remain explicit rather than being turned into a service-completion result.

## One recipe correction, not a relaxed boundary

Microsoft documents that `/Z7` keeps compiler debug information in the OBJ and
does not stop LINK from generating a PDB. `/DEBUG:NONE` explicitly disables LINK
PDB generation. The fixture command previously supplied `/Z7` and
`/INCREMENTAL:NO`, but did not explicitly disable LINK debug output. The new
command appends `/DEBUG:NONE` after `/link`; all other compiler/linker options
remain unchanged. The observed PDB is the concrete unwanted output addressed
by this correction. It is a hypothesis, still to be tested, that removing this
debug output also satisfies the original process-count and completion gates.

Primary references:

- https://learn.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info
- https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format

After the unchanged compiler success, Job completion, error and count checks,
the runner records and requires that the expected LINK PDB path is absent.
This is an additional fixture-output check, not A2 authority. It does not
replace the Job checks. If a third process or live member remains, the attempt
still refuses and no native fixture runs. No wait is lengthened, process cap
raised, error cleared, old binary reused or compiler PDB/service study added.

The C++ source remains 37,135 bytes, SHA-256
`1b62df9b7cc1253fc1a208f2a84154eba75907b7b2fef2d1fc72ed2f0423febd`.
The Job helper remains 11,417 bytes, SHA-256
`ba2dff1452112d31dfaafa01c8a15b985539317867f3c791aed9dc3eaf24fce9`.
Other edits are only the 003 run/output/plan identity, its workflow source hash,
this plan and its exact LF attributes rule. Independent review must bind the
new complete source identity and Connector commit before activation.

## New single-attempt budget

Only `producer-seal-003`, existing workflow run number 3 and run attempt 1, may
run on `feat/producer-clean-handoff-20261011`. No rerun of 001 or 002 is allowed.

| Item | Maximum for attempt 003 |
| --- | --- |
| Workflow | One isolated branch push, run number 3, first attempt |
| Build | One cl driver; its Job total must be at most two, including its LINK child |
| Native fixture | One executable; exactly one Job process, zero children, at most two auxiliary threads |
| Platform | windows-2022 native x64, actual image/OS/tools recorded, local fixed NTFS |
| Waits | Compiler 90 s; native 40 s; watchdog 30 s; each cleanup poll at most 10 s |
| Outer workflow ceiling | Five minutes; no hard real-time guarantee for arbitrary OS calls |
| Fixture content | At most 1 MiB in new NTFS fixture objects; build/capture artifacts retained separately |
| MQB / ordinary producer / M1b service study / ETW / cost | Zero |
| Deletion / disposition / recursive cleanup / old evidence overwrite | Zero |

The actual prior spend is one compiler-driver invocation, three observed
compiler-Job processes and zero native fixture invocations. The third observed
process is retained as a budget rejection, not erased from the accounting.
003 adds at most one compiler driver, an accepted compiler Job total of at most
two, and one fixture process. Any new failure remains closed with complete
original streams and identities; no automatic retry is introduced.

The ordered native cases and limited classifications in `native_probe_plan.md`
remain unchanged. Exact-byte checkout and source identity remain enforced.
The runner captures complete raw streams before launch, rechecks source/tool
identities and keeps original errors. Transported evidence needs exclusive
complete writes, file/directory fsync and independent full readback before it
is canonical. Missing or invalid results never pass.

A1/A2/A4, positive C01, A3/A5, actual deletion, ordinary MSVC qualification and
the full conditional-ACL/service boundary remain false. The PR250 preparation
remains exactly SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`;
existing HOLD/DEFER decisions and disabled service/cost research remain intact.
