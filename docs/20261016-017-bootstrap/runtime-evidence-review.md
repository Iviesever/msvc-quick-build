# Runtime evidence review: dev005 retained, short reads corrected

2026-10-16. Reviewed implementation: `0dac6501c73252b76d6b281ee485ea18a00ca1f2`.
No formal source/loader/writer qualification or target preparation is granted.

## What the first actual collection established

Dev005 ran once as run38856832500, attempt1, job116830083022. The CI step and
327 units completed successfully. Both isolated collectors exited0 and were reaped.
Artifact12027905322 is 159820 B, SHA256
`ac60a8615f7078ff561d2c3d86c1d3ee7fa13d107c1f90ef04c873574aba1061`.
Its 56 members and original records remain unchanged.

The owner preload recorded 107 ordinary modules plus TWO incompatible entries:
`typing.io`, `typing.re`, both rejected by the existing descriptor as MODULE_OBJECT.
The controller role similarly recorded 103 ordinary modules and the same blockers.
These are not evidence of malicious injection: CPython3.12's deprecated typing
namespaces are not ordinary ModuleType objects. The fixed guard is not compatible
with this complete cached-object set. Do not delete them from the evidence or
silently relax arbitrary-object rejection to produce an admission.

Each existing static dependency scan recorded12 ELF nodes/29 edges,26 complete
passes /30942694 B. Module/runtime hashes were actually read; they are not mocks.
The observed Python is3.12.3 and is NOT the local3.13 interpreter. These facts do
not prove exact formal-bootstrap preload or future-runtime compatibility.

## Collector bug: a single raw read did not establish maps EOF

Both dev005 records said python_pt_interp_mapped=false. Review found that
own_maps used a single unbuffered read of65537 B. FileIO is allowed to return a
short non-EOF read, so a syntactically valid first chunk can be silently mistaken
for the complete maps file. The original artifact does not retain raw addresses
or read-call counts; therefore absence of the loader in that projection is NOT
proof that the loader was absent from the process. Do not reinterpret it as such.

Eight finite read-protocol regressions produce6 failures/1 error on the original
reader (the empty-stream rejection already passes). The same assertions pass
with the fix: collect short chunks until explicit empty-byte EOF, retain the
65536 B cap, add at most128 read calls, reject None/oversized-return/hidden-tail
errors, and record EOF verification/read-call counts. Line splitting no longer
loses or discards later map records. Original reader, tests and RED log are retained
in the delivery. Total after correction:335 local PASS /0 fail /0 skip.

The correction changes only the NEW evidence collector and its tests. It does not
change the existing import boundary, any target owner, metadata pins or writer
policies. In particular it does not make typing.io/re acceptable automatically.

## Separately registered corrective collection: dev006

The consumed dev005 workflow/manifest and original files are not edited or rerun.
Dev006 is an explicit new registration with parent0dac6501..., revised source
hashes,335 units and at most the same two isolated evidence interpreters. No
unbounded retries or attempts on an unchanged head. Its purpose is to verify the
EOF fix and obtain usable full mapping evidence; its outcome must be checked
from its own original artifact, not presumed from unit tests.

All original dev005 content/read/output/time caps remain; new read-call cap128
per maps pass is explicit. No raw virtual addresses or system binary contents
are uploaded. The qualification remains EVIDENCE_RECORDED_NOT_QUALIFIED even when
collection completes and a loader match is found. A green artifact collector is
not a green formal source/module/writer admission.

## Next decision

Account for the actual Python3.12 cached-object incompatibility without erasing
it or synthesizing an allowlist. A separately reviewed compatible preload or
interpreter selection must precede formal use. Loading evidence is local to its
measured runner/process; it cannot automatically qualify a later ephemeral host.
Non-descendant writers, descriptor export, independent preparation and actual
GNU runtime behavior remain unverified. Five restored historical JSON inputs
remain available in the prior delivery; there is no need to retrieve them again.

No old016/dev004 workload, original image, 103-byte target, GNU/Windows/MQB/ETW,
main merge, release or cleanup authority changed. This review/repair is by the
same authoring session, not a second independent sign-off.

References:
- https://docs.python.org/3/library/io.html (FileIO short reads before EOF)
- https://docs.python.org/3.12/library/typing.html (deprecated typing.io/re namespaces)
- https://www.kernel.org/doc/html/v6.15/filesystems/proc.html (maps semantics)
