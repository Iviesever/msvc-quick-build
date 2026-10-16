# 017 runtime evidence collection (dev005)

2026-10-16. Base: `f7a9fa234cdcdc208fde0dc8eb0acb48b169b45c`.
This step obtains **actual, bounded interpreter/loading evidence for review**.
It neither repeats completed dev004 nor admits the 103-byte target observation.
The prior 309 tests, owners, imports policy, scope and completion logic stay unchanged.

## Two fixed isolated observations

A fresh hosted Ubuntu 24.04 worker runs 327 pure/file tests, then at most two
`/usr/bin/python3 -I -S -B` collectors, one per owner/controller preload role.
Imports use only the fixed role list and existing source modules. No owner,
BoundObservation, controller constructor, subprocess/fork workload, GNU decoder,
Windows image or target is invoked by the collector. The CI runner starts the
unit interpreter and these two collector interpreters; they are not counted as
zero operating-system processes. Stop on the first incomplete collector; no rerun.

The fixed collector reports its own module metadata and every unrepresentable
cached object as a blocker, rather than silently discarding it. It hashes the
interpreter, observed module source/cached files and mapped runtime files twice.
Missing cached-bytecode paths are recorded and rechecked. File identities,
aliases, module object identities and file-backed mapping identities/permissions
must agree at the later checkpoint. Native task and own-child interfaces are
checked before and after; subreaper policy is not activated or changed.

The unchanged static toolchain collector separately reads the Python/objdump
ELF dependency graph and loader cache. This is **static resolution**, not a trace
of the decoder's real runtime. Python PT_INTERP can be joined to this probe's
actual file mappings; this does not prove all subsequent dlopen behavior.

## Discovery is not an authorization generator

The observations contain no qualifying allowlist, signature, human-review receipt,
source approval, writer authority, target identity or preparation result.
`EVIDENCE_RECORDED_NOT_QUALIFIED` means evidence collection completed, not that
an execution prerequisite is approved. CI green must not be called formal admission.

This collector, not the production bootstrap, is `__main__`. Its imports include
observer helpers. The measured module set therefore cannot simply be copied into
an actual bootstrap request. A reviewer must account for this difference, test
remaining lazy import paths, and bind the approved runtime in the same controlled
execution environment. An ephemeral runner's files do not pin a later worker.

No mechanism here proves absence of non-descendant writers or descriptor export.
Those remain a separate externally supported writer-domain prerequisite, not a
boolean to infer from mappings or no-child snapshots. Independent preparation
slots and formal observation remain unallocated; all original HOLD gates remain.

## Scope, limits and privacy

Only `/proc/self/maps`, own task/children views, fixed interpreter/decoder/cache,
paths declared by existing ELF metadata and this process's module/mapping records
are inspected. No host PID scan, environment dump, credentials, /proc memory,
mapped memory bytes, original image, nested historical archive or target read.
Observation paths must stay under the declared project source directory or fixed
system library/program prefixes; no /home secrets or arbitrary supplied files.
Mapped address intervals are parsed and discarded; only file identities and
permissions are retained. Binary library/Python contents are hashed, not uploaded.

Per collector: maps <=65536 B /512 rows; <=256 file/absence paths; modules <=256
using the existing descriptor bounds. Runtime file content <=96 MiB per pass,
two passes sharing one 20-second cooperative deadline; one chunk 65536 B. Legacy
static graph has its independent <=192 MiB aggregate and 20-second budget. Count
both domains: <=384 MiB total declared runtime+static content per collector.
Four new JSON records each <=262144 B (plus one bounded refusal record on error).
The external process deadline is 50 seconds, CI step3 minutes /job5 minutes.
These are rejection/checkpoint limits, not atomic namespace locks or hard bounds
on interpreter RSS, process creation, kernel I/O or filesystem return latency.

Source and exact role argv are frozen in runtime-dev005-source.json and the
pre-push Issue198 registration. The workflow permits only run1/attempt1 on the
existing feature and exact registered parent. All original output, refusals and
source snapshots are retained; there is no automatically authorized retry.

## Verification and use of the result

The 18 new tests cover map parsing/caps/identity joins, path restrictions,
module identity and unsupported-object records, exclusive bounded writes and
fixed no-workload policy. With the original tests: 327 local PASS, no skips.
The actual collector must be verified from its own CI artifact; no local live
collector was run in the known unsupported container.

After collection, independently inspect the observed module blockers, Python
loader mapping, static dependency limits and privacy boundary before deriving
any runtime qualification. Do not substitute this evidence for target preparation,
writer qualification, actual decoder loading or the containing controller's exit.

Primary references:
- https://docs.python.org/3.12/library/sys.html (sys.modules is mutable during imports)
- https://docs.python.org/3.12/using/cmdline.html (-I / -S / -B are not a sandbox)
- https://www.kernel.org/doc/html/v6.15/filesystems/proc.html (maps fields and identities)
