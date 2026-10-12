# 014 implementation and delivery plan

## Established baseline
Feature closure: 02b6fae17ee36f0011b7bae0f9102478b76ba42c.
The current isolated feature worktree is clean. The latest Connector scope
preserves main, three draft/HOLD PRs, 11 existing workflow runs and #198's 013
closure. A fresh baseline run passes all 275 existing pure/synthetic tests.

## Design decision
Use the installed, identity-verified GNU objdump on a byte-exact binary slice.
The new owner derives and verifies file/RVA/preferred-VMA mapping from the
fixed admitted 013 index. It supplies only target.bin to the tool and never
loads or executes the original image. Display preferred VMA explicitly.

Alternatives considered:
- Passing the full PE to objdump would let the producer inspect unrelated
  structures and complicate the exact-range proof.
- A new AMD64 decoder would enlarge the correctness surface and duplicate an
  existing verified tool.
The selected exact-slice interface keeps the tool input and listing scope explicit.

## Modules
Keep each new module focused and below roughly 350 lines where practical:
- bcrypt_disassembly_model_014.py: manifest shape, frozen limits, target mapping,
  fixed argv/environment and actual-versus-synthetic admission.
- bcrypt_disassembly_listing_014.py: bounded GNU text grammar and exact byte coverage.
- bcrypt_disassembly_io_014.py: bounded identity reads, exclusive directory/files,
  child resource limits, durable full readback and fixed diagnostics.
- bcrypt_disassembly_owner_014.py: one reservation, one owned child, deadline,
  lifecycle, pre/post identity checks and joint completion contract.
- run_bcrypt_disassembly_014.py: small hash-bound CLI bootstrap.
Use separate focused tests; retain every genuine RED/GREEN checkpoint.

## Resource approach
Each child stdout/stderr is an exclusive regular file with child soft/hard
RLIMIT_FSIZE 131072 B. Reaching the cap is failure even with exit zero.
The child address-space cap is 268435456 B, CPU cap 2 seconds, core cap zero,
and descriptor cap 64. The independently selected owner admission deadline is
12 seconds; OS creation, kill, fsync or reap are not universally bounded-return
operations. Late completion is never accepted.

The complete environment is LANG=C, LC_ALL=C, TZ=UTC, DEBUGINFOD_URLS empty.
No inherited loader/environment configuration is used. All selected dependency
and loader metadata identities are verified before and after the child.
Output grammar admits at most 391 instructions, 1..15 bytes each, ASCII lines
at most 512 bytes and instruction text at most 384 bytes. The raw output bound
comes from the producer's kernel file limit, not an input-length estimate.

Manifest cap 32768 B; image cap 262144 B; fixed admitted index cap 327680 B;
prior joint-admission cap 32768 B. Identity hashing uses 65536-byte chunks,
with a 67108864-byte per-file and combined identity limit. Five sources each
have a 32768-byte cap. Report cap 524288 B, marker cap 1024 B, exact target
391 B; stdout/stderr caps total 262144 B. Runtime persistent files therefore
total at most 787847 B; adding the manifest gives 820615 B.
Owner stdout is zero and its fixed failure diagnostic is at most 65 B.
A separate resource contract must justify parent allocations, serialization,
bounded JSON and all failure paths before actual registration.

## Steps and gates
1. Read/verify full retained evidence and latest scope; establish tool identity.
2. Review this design and numerical resource contract before implementation.
3. Write failing tests, then minimal model/listing/IO/owner/bootstrap behavior.
4. Verify synthetic mapping, boundaries, invalid output, identities, caps,
   timeout/crash/reap, consumed reuse and late persistence.
5. Independently review source, numeric bounds and exact workflow/path triggers.
6. Publish immutable source through Connector; full blob/tree/commit readback.
7. Freeze/read back manifest and #198 registration; expected-old nonforced
   feature activation, then exactly one registered actual owner invocation.
8. Independently match full output addresses/bytes and all lifecycle evidence.
9. Publish result and next bounded handoff; verify final remote scope and deliver
   the audit archive. Preserve negative and historical evidence.

## Completion meaning
A complete static listing establishes decoded bytes and static references only.
Actual control flow, failed API and cause of 0xC0000142 remain unknown unless
separately demonstrated. Any outgoing target is a later bounded handoff.
