# 017: bind an empty Python package without treating absence as content

2026-10-16. Base: `20e94ed0a02174a8ae3466ca7f4b65560317ba15`.
This is a request-binding correction, not another bootstrap or observation gate.

## Evidence and reproduced obstruction

The retained dev007 correction artifact `12030386446` (SHA256
`ebed9674b15415d64c528504d14177919a92534bf333f1ec4af1c61857646bb3`)
records `/usr/lib/python3.12/urllib/__init__.py` as an ordinary **zero-byte** file
in both preload roles, with the SHA256 of empty bytes. Its recorded bytecode
cache is a different, nonempty 135-byte file. The original request validator
rejects every zero-byte file, so this recorded package cannot enter its runtime
identity set even after the typing compatibility fix. Removing it would instead
break the complete module/source binding.

Python documents that an ordinary package's `__init__.py` may be empty:
https://docs.python.org/3.12/tutorial/modules.html#packages

## Narrow correction

Only `context.files` may represent empty content. An empty record must have an
integer zero length and the exact empty SHA256. After validating the entire
module and alias schema, it must also be the source of an explicitly registered
ordinary package: matching module/spec/package name, identical source/origin,
`__init__.py` filename and matching single package/search directory. Any alias
must already point to the same explicitly pinned canonical package source.

The existing `identity()` function is unchanged. Programs, required project
sources, `__main__`, metadata, preparation/qualification receipts, bootstrap
inputs and cached bytecode remain nonempty. Unbound zero-byte files, arbitrary
empty libraries, missing source files, false-as-zero, wrong hashes and unknown
module metadata remain refusals. No exception accepts arbitrary cache objects,
creates an allowlist or permits a namespace package.

An empty source is NOT skipped in either runtime identity pass. The existing
snapshot function opens it, checks regular-file identity, requests explicit EOF,
checks the path again and closes the descriptor. It consumes one file slot,
one file pass and one EOF request, but zero content bytes. File-count, byte,
module, time and output limits are unchanged. No new filesystem pass or native
workload is introduced; the additional schema pass is bounded by the existing
256-module / 384-file / 256-alias ceilings.

## Verification and scope

The original 367 unit cases remain byte-identical. Twenty-two new cases cover
real empty temporary files, full bound-owner before/after passes with the
existing fake child, a controller byte pass, mutation/absence, strict program
and receipt roles, package aliases, bytecode separation and a minimal unchanged
projection of the real dev007 urllib record. Runtime provenance/processes in the
end-to-end unit fixture remain explicitly synthetic; no actual target or GNU
process is executed.

Old code fails the valid package/alias/bound-owner cases; the corrected code
passes the same assertions. An initial new fixture incorrectly fed normalized
tuples to the JSON wire encoder. That fixture error is retained separately from
the production RED result; it was corrected to the already-required JSON lists,
not by relaxing the encoder. Current local suite: **389 PASS, zero failures,
errors or skips**. The ordinary unit-only bootstrap workflow now runs this same
inventory and records the changed admission source pin. Consumed dev004-008
workflows/registrations are not edited or rerun.

Rechecking the retained owner/controller runtime identities removes exactly the
one zero-file shape rejection in each role (123/119 records respectively). This
uses original archived metadata, not current-host library bytes, and does not
turn a collector's module set into an approved fixed-entry manifest.

## Handoff

The empty-package representability obstruction is fixed. Reuse this correction
when assembling the **actual fixed entry** module/file binding; do not omit empty
sources or broaden all identity roles to zero length. Exact entry preload,
trusted writer-domain premises, independently prepared real target inputs and
outer completion evidence remain required before formal execution. No trusted
qualification or preparation receipt was created in this change. The author and
test executor are the same assistant session, not an independent second signer.
Main, existing HOLD/DEFER, VERSION, deletion authority and release are unchanged.
