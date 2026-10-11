# Prerequisite 008: one default-DACL startup hypothesis

Status before activation: `SINGLE_VARIABLE_SOURCE_PREPARATION`.
Parent execution commit: `76789bb89c5922cade3094277414bf91dcc26ff5`.
The [closed 007 diagnostics](native_delegation_result_007.md) observed actual
C exit `0xC0000142 / STATUS_DLL_INIT_FAILED` before cleanup. No startup frame
or IPC case was observed. The specific failed DLL remains unidentified.

## Hypothesis and primary-source basis

Hypothesis: the newly restricted C token's default DACL lacks its fresh
restricting SID R, preventing a required default-created object from receiving
the access needed during initialization. A single additive R grant to that
new token's default DACL is sufficient to remove this particular startup
obstacle. This is a testable hypothesis, not an established explanation of
007 or a claim that a particular DLL creates a particular object.

The closed 007 source constructs a unique restricting SID but does not read or set
TokenDefaultDacl. The pinned Chromium source at
`50da56df411ff9bffaf219f1bcf5e83d6d91e028` explicitly pairs a unique
restricting SID with a GENERIC_ALL default-DACL grant:
[restricted_token_utils.cc](https://github.com/chromium/chromium/blob/50da56df411ff9bffaf219f1bcf5e83d6d91e028/sandbox/win/src/restricted_token_utils.cc)
and applies those entries using SetDefaultDacl in
[restricted_token.cc](https://github.com/chromium/chromium/blob/50da56df411ff9bffaf219f1bcf5e83d6d91e028/sandbox/win/src/restricted_token.cc).
This is an implementation precedent; its different token profiles do not
prove that this exact WRITE_RESTRICTED fixture will start.

[TOKEN_DEFAULT_DACL](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_default_dacl)
defines the default ACL for objects created under the token. It is read and
set with GetTokenInformation and SetTokenInformation. The new restricted
token handle inherits the source handle's access rights; setting this class
requires TOKEN_ADJUST_DEFAULT. This access is requested for the existing
host-token handle solely so the newly created restricted handle supports
that operation. The sole SetTokenInformation targets the new C token.

A separate GENERIC_EXECUTE difference was examined and is not bundled into
008. Chromium commit
[233c9ecac91e2df7be19ae6ef88c7f4a5c08b470](https://github.com/chromium/chromium/commit/233c9ecac91e2df7be19ae6ef88c7f4a5c08b470)
states that its GE grant allows the user to terminate isolated processes.
It is not evidence of a DLL-initialization repair or a purely read-only right.
The process/thread DACL and desktop setup therefore remain unchanged here.

## Exact single behavioral change

Only `tests/native/producer_delegation_probe.cpp` gains the default-DACL
construction and readbacks below. The runner changes only 007 to 008 attempt
identity, plan reference and exact run number 8. The workflow binds those
new source hashes and the same unchanged capture dependencies.

1. Capture the host S token's default DACL with a complete bounded
   GetTokenInformation read. Validate TOKEN_DEFAULT_DACL, its absolute PACL
   pointer, the full ACL and each ACE inside the returned buffer. Reject a
   missing/NULL, malformed or oversized ACL; do not silently replace it.
2. After creating the new restricted C token, read its actual default DACL.
   All token/ACL buffers are at most 64 KiB and ACE counts/lengths are bounded.
   Keep original ACE order, type, flags, size and all AceSize bytes. Append
   exactly one noninheritable ACCESS_ALLOWED_ACE for the exact fresh SID R,
   access mask GENERIC_ALL. Do not replace, merge or reorder existing ACEs.
3. Call SetTokenInformation(TokenDefaultDacl) exactly once on the new
   restricted token. No SID attribute, enabled group, privilege or
   ProcUnique value is changed. Read the new token back completely and
   require exact expected original ACEs plus the single appended ACE.
4. Read S's default DACL again and require it to remain unchanged. After
   creating C suspended, use the existing query-only actual C token handle
   to require that its default DACL has the same expected ACE sequence.
   Continue the existing actual-claim/restriction/authentication checks.

Comparisons are of validated ACL structure and ACE bytes, not the entire
GetTokenInformation allocation containing an address-dependent PACL pointer.
ACL allocation size and free space are not ACE contents; all observations
retain those lengths explicitly and never interpret padding as another ACE.
The appended ACE must have flags 0, GENERIC_ALL and exactly R. A readback
difference or unexpected original structure fails closed before resume.

This setting affects default-created objects under C. The existing explicit
process/thread, leaf, named-pipe and protocol object security descriptors
remain byte-for-byte source-equivalent to 007. S's default DACL is not set.
No desktop change, logon/restricting-SID substitution, privilege enablement,
token impersonation, debugger, shared-object ACL change or new broker is added.

## Distinguishing the outcomes

The existing 007 startup/first-command snapshots and actual C exit/times/Job
unwind diagnostics remain in place, including the single at-most-1,000-ms
failure wait. The first discriminating observations are whether the exact
default-DACL readbacks succeed and whether actual C reaches its first
startup snapshot. Preserve every later failure and do not accept an
unexpected exit or helper process merely because startup advanced.

- If all DACL readbacks pass and C again exits DLL_INIT_FAILED before startup,
  this single additive grant was not sufficient on this run. Do not repeat it
  or call the missing R ACE the proven cause of 007.
- If startup advances, that is evidence that this change removes the
  observed obstacle in the tested construction; it still does not identify
  the failed DLL or establish ordinary MSVC compatibility.
- The four IPC cases may proceed only through the unchanged startup and
  case gates. Full P3, all four cases, actual C natural exit 0, measured inner
  Job 1/0/limit-terminated0, exact revocation, retained-WA mutation/readback,
  and outer total 2 remain necessary for a conclusive finite-route result.

`FIXED_ROUTES_REFUSED_ONLY` remains a finite observation, never a full owner
qualification. `WRITE_RESTRICTED_IPC_DISQUALIFIED` still requires the actual
post-completion counterexample plus all controls. Anything incomplete remains
INCONCLUSIVE. No diagnostic or default-DACL change issues A1/A2/A4 authority.

## Immutable one-shot registration and unchanged execution budget

Independent source review covers the precise additive DACL change, pointer
and ACE bounds, actual-token readbacks and unchanged gates. Create an exact
immutable commit/tree, register its hashes and budget in issue #198, read it
back and check the branch lease and seven completed prior runs before
Connector activation. Use only workflow `380958963`, **run number 8 / attempt
1**, push on `feat/producer-clean-handoff-20261011`. Do not dispatch or rerun
001-007.

The fixed [006 cases](native_delegation_plan_006.md) and
[007 diagnostic budget](native_delegation_plan_007.md) remain the execution
envelope: one five-minute Windows 2022 job, one fixture compiler driver
(Job total 1 or 2; 90-second wait and the held telemetry image pin), one
native fixture S plus C (outer Job total exactly 2, child active limit 1),
one S watchdog, eight named-pipe instances including baselines, two anonymous
channels, 1,024 protocol bytes and at most four test-connection payload bytes.
One zero-data leaf and at most 4,096 retained payload bytes; native watchdog
30 seconds, outer wait 40, cleanup at most 10. The additive ACL processing
uses bounded memory; it adds no fixture process, file, pipe or thread.

Before 008: seven prerequisite runs, six compiler drivers, compiler Job
TotalProcesses summing to fifteen, three native fixture invocations. No
ordinary MQB, cost, ETW or service study is included. Original artifacts and
closures are retained. Frozen cost PREPARATION_ONLY_BLOCKED bytes, PR250
HOLD, consumer/cost DEFER and disabled M1b stay unchanged. Product A1/A2/A4
and positive live handoff remain pending; A3/A5/deletion remain disabled.
