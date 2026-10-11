# Offline refusal context 012

Status: `IMPLEMENTED_SYNTHETIC_VERIFIED_NOT_REGISTERED_NOT_EXECUTED`.

The user asked to continue the next handoff after diagnostic 011. The accepted
[012 design](../20261011-013406-producer-handoff/native_startup_handoff_012.md)
requires locating the original frozen PE refusal on retained bytes before any
compatibility decision. This task implements that first observation step.

## Outcome and acceptance

Produce a uniquely bound failure guard, root exception-table entry, leaf unwind
record, offending slot and bounded raw envelope, or retain an explicit failure
to obtain that context. The original `UNWIND_OPCODE_INVALID` and original
`INCONCLUSIVE` execution remain immutable. Context does not admit a PE index.

- Start at feature commit `3bf20f99414cfaf059564773db365f8aabccde5b`.
- Reuse the existing clean isolated worktree; do not duplicate the repository.
- Retained image: 170,984 bytes, SHA-256
  `663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb`.
- Frozen PE source: 31,555 bytes, SHA-256
  `941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba`.
- Initial pure baseline: 119 tests pass. No actual image parser call was made.
- Complete synthetic RED/GREEN tests and independent exact-source review before
  registering a single actual input invocation through the GitHub Connector.

## Scope

Only add the offline context and invocation owner with focused synthetic tests.
The first actual call is one CPython offline worker on the existing Linux host,
not a Windows fixture or DLL execution. Its owner grants a single GO after
identity verification, and retains a durable consumed-call marker before launch.

The frozen 011 parser, tests, native sources, runners, workflow, actual bytes and
old evidence remain byte-identical. No native/workflow allocation, objdump,
symbols, DLL substitution, parser retry, product change, merge or release is
part of this observation step. Main, draft/HOLD PRs, consumer/cost DEFER and all
qualification flags remain unchanged. Any compatibility change needs located
byte evidence and a separate reviewed source/invocation contract.

See [implementation_plan.md](implementation_plan.md) for the finite execution
steps and [the original 011 result](../20261011-013406-producer-handoff/native_startup_result_011.md)
for the independent outer-Job and PE failures.

## Synthetic verification checkpoint

The original 119 tests and 55 new tests pass together: **174 tests, 0 failures,
0 errors**. The source bytes are identical before and after that execution.
This includes a real isolated Linux CPython worker using only a synthetic PE,
exact protocol/output boundaries, one-call behavior, durable re-entry refusal
and a late-fsync nonzero owner exit. The actual 011 image has not been parsed
in 012. Independent final source review and immutable registration remain gates.
