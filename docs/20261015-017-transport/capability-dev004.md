# 017 writer scope: optional procfs capability and hosted dev004

2026-10-15. Base: `d80859101cb500e403db94f7561f23d5779b06a3`.
This is development validation, not a formal 103-byte observation or release gate.

## Retained dev003 result

Issue #198 / 6194884468 registered one unchanged isolated five-case smoke.
The supervisor exited 1 after clean, joined and orphan: three Popen roots and
 two forks actually ran; double and timeout did not. Clean/joined and the clean
six-file synthetic readback passed. Orphan's root exited 0 but scope drain
returned SCOPE_IO without a signal or reap. The sleeping orphan subsequently
wrote its 27-byte marker. It was a real cleanup failure, not a successful run.
The original prefix and supervisor exit remain immutable.

A separately registered own-child primitive diagnostic (#198 / 6194923627)
used one additional supervisor and one setsid child sleeping 0.25 seconds.
It returned errno 2 (ENOENT) on `/proc/self/task/<own-pid>/children`;
P_ALL, pidfd_open, P_PIDFD and pidfd signal 0 worked. The parent waited for its
own child to exit 0 and restored its empty subreaper scope. No SIGKILL, host PID
scan, permission change or alternate proc path was attempted.

The Linux interface is optional (CONFIG_PROC_CHILDREN on recent kernels).
ENOENT here establishes that this process view lacks the required file; it does
not identify whether kernel configuration or procfs virtualization caused it.
The implementation had checked callable pidfd APIs but deferred this required
procfs read until an orphan existed. That admitted a host incapable of cleanup.

## Minimal correction

`_policy()` now requires that the bounded own-children reader works before any
workload, as well as during existing checks. OSError becomes the precise refusal
`SCOPE_CHILD_INTERFACE_UNAVAILABLE`. Malformed contents keep their original
refusal. Nonempty candidate lists are allowed while draining; an empty list is
still NOT ECHILD proof. The original wait ownership, caps and pidfd-only signal
policy stay unchanged. No guessed-PID fallback or privilege escalation is added.

Eight new pure regressions check missing/denied/error interfaces, original parse
errors, empty-list/nonempty-wait distinction, normal activation and no retry.
Against the original scope they produce five failures; the corrected source
passes all 258 tests (the original 250 plus eight), with no skips. The failure
logs and original source are retained, not rewritten as successful evidence.

## One separate hosted development run

The optional procfs file is unavailable in this container, so do not repeat the
same failing workload here. `bcrypt-writer-scope-dev004.yml` declares one fresh
GitHub-hosted ubuntu-24.04 development job, with contents:read only. It requires
this feature branch, the exact d808591 parent, run_number 1 and attempt 1. It
triggers only on its own workflow path; there is no manual/scheduled/PR entry,
no automatic retry, sudo, secret injection or main change. Re-running the job
or changing the workflow later does not grant another smoke allocation.

`dev004-source.json` freezes the 28-file local source/test workset (not a full
repository semantic audit). The workflow checks its pinned SHA256 and every
file before testing, then rechecks files afterwards. It records the actual
hosted interpreter identity rather than borrowing the local interpreter hash.
It replays exactly 258 units, then once invokes the unchanged corrected smoke
with `-I -S -B`. Five Popen roots / five explicit forks is the maximum: first
failure still stops. The existing clean/joined/orphan/double/timeout assertions
are not relaxed. The workflow preserves sources and all successful/failed
prefixes as an artifact, without replacing earlier dev001/002/003 evidence.

Job bound: five minutes; test/smoke step two minutes; one smoke supervisor has
20 seconds plus a sole 3-second termination wait. Existing case deadlines,
1-second/16-child/256-scan drain limits and finite output caps remain. These
are not a hard kernel/filesystem sandbox. The job must refuse before launching
work if its own procfs capability is also missing. Host support is verified,
not inferred merely from the Ubuntu label.

This document records the implementation and execution contract, not a result.
Read the exact GitHub run and its raw artifact before claiming hosted success.

## Remaining boundary

Even a successful descendant test does not establish that unrelated processes
cannot write files or that descriptors cannot be passed outside the family.
`writers_quiescent`, `observation_accepted` and `clears_hold` remain false.
Fixed BoundObservation bootstrap, real loader/module/source qualifications,
independent preparation and outer completion admission are still separate.
No actual image/target read, GNU/Windows/MQB/ETW or formal observation is included.
No second independent reviewer has signed this correction; source review in this
turn and synthetic/live tests must not be relabeled as that signature.

References: Linux man-pages `proc_tid_children(5)`, `PR_SET_CHILD_SUBREAPER(2)`,
`wait(2)`, `pidfd_open(2)`. In particular proc children can omit exiting tasks;
the bounded reader provides candidates only and waitid ECHILD remains mandatory.
