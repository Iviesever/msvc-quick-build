# 017: exact-entry preload inventory, not execution qualification

Base: `948af32d77218c96688b8a3854803b08f38d44aa`. This closes the gap between
an imported observer role and the real script's `__main__` identity. It does not
prepare a target, issue writer authority, or approve the formal observation.

## Two explicit paths

Both existing fixed scripts now accept `--inspect-preload` as their sole argument
under `-I -S -B`, without `-O`. They remain the actual script entry; no `runpy`,
wrapper-owned `__main__`, copied module table, or cache deletion substitutes for
that identity. The owner shares its fixed legacy dependency imports between
normal and inspection paths. Imports are completed before the normal request
read; inspection instead records the current state and exits.

Inspection is implemented in the already loaded `bcrypt_bootstrap_plan_017`
module. No extra observer module enters only the inspection branch. It uses the
unchanged module describe/manifest rules; one incompatible entry refuses the
record, rather than silently disappearing. Strong module/spec/loader references,
metadata, and the cache object/set are rechecked after file reads and encoding.

The record includes the real `__main__` file and spec, all module metadata,
canonical file identities and aliases, missing cache paths, interpreter identity,
and two-pass read accounting. Only own declared module paths, the source directory
and fixed standard-library/interpreter roots are eligible for reads. There is no
host process search, raw map address, library payload upload or native decoder.
Empty package files still consume their two file passes and two EOF probes.

The normal execution schema and its required externally frozen root, preparation
receipts, qualification and writer authority are unchanged. Inspection accepts
no request path; additional arguments are refused rather than falling through.
Its result schema cannot be used as a bootstrap plan or completion receipt.

## Scope and resource accounting

This is **pre-request** evidence. It does not establish the module set at the
later guarded execution boundary, prove absence of native late loading, verify
all dynamic paths, or authenticate independent people or preparations. Every
qualification flag remains false. A later execution must still use a separately
reviewed frozen expected set and pass the original guard; do not automatically
copy this table into an allowlist. Source and preload order changed, so earlier
entry-file hashes are superseded only for this revised source.

At most 256 modules and 256 candidate-plus-absent file paths; the original module
text cap remains 131072 characters. Canonical file content is limited to 96 MiB
per pass, two passes sharing a single cooperative 20-second deadline. Each file
uses the unchanged bounded snapshot/EOF implementation. The canonical JSON record
is limited to 262144 bytes. These caps are not an OS disk-I/O timeout, memory
sandbox or atomic lock against external writers.

The normal execution path gains only fixed preload imports and the shared
function definitions; it does not perform these diagnostic file passes.

## Validation and one-time execution boundary

The original 389 test methods are unchanged. Nineteen new tests cover exact main,
empty package EOF accounting, object/set drift, alias/path restrictions, file
changes, absent-cache changes, thread refusal, argument separation, no target
path, and shared preloads. File tests use real temporary files; runtime objects
and thread assertions are explicit stand-ins. No live decoder is run by tests.

The existing bootstrap CI runs all 408 methods. A subsequent inspection step
runs only for the exact above parent and attempt 1; later commits skip inspection.
No new permanent experiment workflow is added. At most two real interpreter
entries execute, once each, with 30-second parent timeouts, fixed C/UTC environment,
Python 3.12.3 and the dev007 interpreter digest. The 2-minute step and 5-minute job
bound are retained. Raw stdout, stderr, launch/exit records and failures remain.
A green collection is not a formal observation or transferable host permission.

Old dev004–008 are not rerun. Image reads, real target preparations and formal
observations remain zero. The next join still needs independently supported
runtime/writer qualifications and actual preparation receipts, not synthetic
assertions or another generic execution framework.

References: Python 3.12 import-system documentation (script `__main__.__spec__`
and cache semantics), command-line documentation (`-I`, `-S`, `-B`).
- https://docs.python.org/3.12/reference/import.html
- https://docs.python.org/3.12/using/cmdline.html
