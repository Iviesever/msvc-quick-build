# Diagnostic 011 delivery walkthrough

The next handoff found at the actual feature head was the bounded actual-event
bcrypt byte acquisition described by the preserved 011 proposal. The new
collector and pure validators were implemented, reviewed and registered at
[execution commit 2d0f48c](https://github.com/Iviesever/msvc-quick-build/commit/2d0f48ce551e0a2acdb1cf34628264458fa30ca7).
[Run 38136004405](https://github.com/Iviesever/msvc-quick-build/actions/runs/38136004405)
executed once, number 11 / attempt 1, with one compilation and one native call.

## Actual verification log

Command in `tests/native`:

```text
python -B -m unittest test_producer_loader_snaps_pe test_producer_loader_snaps_capture test_producer_bcrypt_pe test_producer_bcrypt_snapshot
```

Observed output:

```text
.......................................................................................................................
----------------------------------------------------------------------
Ran 119 tests in 0.168s

OK
```

The exact 219-byte log is preserved with SHA-256
`7f248df5db11503896adf20db0206d7425fda0dae867f8ccf04aaf93a8dd774b`.
It covers pure source behavior; it is not a passed native execution gate.

## Changes and observed result

The source commit changes only the diagnostic workflow, relevant line-ending
attributes and current planning documents, and adds the 011 plan plus six
isolated collector/runner/snapshot/PE implementation and test files. It
preserves all original 001-010 sources and the product/version/cost scope.
Independent source review admits the exact frozen tree with no open Critical
or Important finding.

Independent artifact, native-chain and image reviews admit a 170,984-byte
snapshot from actual C's unique bcrypt event, with two complete reads, EOFs,
five identities and continuously retained pin lifetime. The exact-byte SHA-256
is `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`.
The same run's init/failure addresses independently derive RVA `0xA590`.

The native workflow still fails: outer Job total 3 does not satisfy required
2, so original result.json remains INCONCLUSIVE without native_summary. Its
allowed diagnostic exit 3 is not the failing gate. The once-invoked frozen PE
inspector then rejects the acquired image with `UNWIND_OPCODE_INVALID`.
Disassembly is denied, no objdump call or parser retry occurs, and no root
cause or product qualification is claimed.

## Delivery and next point

The complete [011 result](../20261011-013406-producer-handoff/native_startup_result_011.md)
records identities, real budgets, limitations and evidence fingerprints. The
[012 handoff](../20261011-013406-producer-handoff/native_startup_handoff_012.md)
selects a minimal offline wrapper to retain the exact parser refusal context
on the same bytes, before any evidence-based compatibility correction. No
new Windows/native run is needed for that next step.

The final closure modifies documentation only. The named feature branch and
existing isolated worktree are retained for continuation. There is no merge,
force update, branch/worktree deletion, new PR, workflow retry or change to
the original failure records.
