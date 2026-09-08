# Request-owned process lifetime

**English | [简体中文](PROCESS_LIFETIME_ZH.md)**

## Scope

This is the M1a prerequisite of [v6.0 roadmap #164](https://github.com/Iviesever/msvc-quick-build/issues/164),
not a daemon, project lock, new CLI switch or Ctrl+C implementation. VERSION stays
5.5.0. No existing CLI caller supplies a cancellation token in this iteration.
Cache, freshness, scheduling and generated-program behavior are unchanged.

## Contract

`process::ProcessSpec::cancellation` accepts an optional `std::stop_token`.
The default, non-stoppable token retains the existing root-only lifetime policy:
no extra Job, cancellation event or callback is created. A stoppable token opts
into ownership of the normal CreateProcess descendant tree for this `run()`.
When its root exits, remaining managed descendants are terminated before return,
even when no cancellation was requested. This is deliberate and is unsuitable
for applications that intentionally leave background children alive; their
foreground `--run` policy must remain separate from managed build-tool policy.

A pre-cancelled, otherwise structurally valid request returns a cancelled result
without launching; malformed specifications still report validation errors.
During execution, stop requests signal a private event. Cancellation is selected
when that event wins the process/event wait (it wins a simultaneously ready wait).
A stop after normal-root completion was selected need not change its outcome.
`ProcessResult::termination` distinguishes `exited` from `cancelled`; the Windows
cancelled exit code is nonzero `ERROR_CANCELLED` (1223). Captured partial output
is drained and preserved. Setup/wait/read errors remain `ProcessError`, not fake
successful cancellation. Normal nonzero exit codes remain ordinary exit data.

## Windows lifetime boundary

Each stoppable invocation creates an unnamed, non-inheritable Job with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, without either breakaway flag.
`PROC_THREAD_ATTRIBUTE_JOB_LIST` assigns the child during process creation,
not after an already-running child could have spawned. The existing inherited
stdio whitelist is retained; the Job and cancellation event are not passed to
the child. No-capture requests also receive the Job attribute without acquiring
an artificial stdout/stderr capture. Failure to establish ownership fails closed.
This opt-in backend requires Windows 10 / Server 2016 or newer.

The callback lifetime ends before its event is closed. Each invocation owns its
handles and callback, including concurrent calls to the same runner instance.
On cancellation or normal root completion, terminate the Job and query active
process accounting until zero before joining pipe readers and returning. Waiting
only for the root or waiting for the Job handle to become signaled would not
establish this boundary. The cleanup-only 1 ms query wait is not a resident idle
poller. Kernel termination has no guaranteed wall-clock bound.

Owner death closes the private Job handle and requests kernel termination of the
associated tree. It is not proof of immediate disk quiescence for a replacement
client, transactional cache writes, or a malicious-process security boundary.
Services/WMI-mediated processes and other non-descendant work are outside this
normal CreateProcess tree contract. Those limitations must remain explicit when
project leases and crash recovery are introduced.

## Validation and evidence

The existing Windows runner test executable is also its own bounded helper, so
there is no new production translation unit or native test executable. It retains
all original argv/environment/output/concurrency tests and adds normal managed
exit, pre-cancellation/no-launch, live launch failure, captured and uncaptured
cancellation, 128 KiB on each captured stream, descendant-held pipes, normal root
exit with a surviving descendant, concurrent cancellation isolation, owner death,
and repeated-handle accounting. Ready/release events synchronize tests; retained
process handles, not recycled PIDs, prove root and descendant termination.

Five-second test completion checks and bounded helper watchdogs expose failures;
they are not promises that arbitrary Windows kernel teardown finishes in five
seconds. Windows Native Debug/Release and the unchanged independent ABBA workflow
must run on the PR's exact source identity. Default-path compatibility measurements
are not cancellation-throughput measurements, and this is not a speedup claim.

## Platform sources

[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
describes descendant inheritance, nested jobs, accounting and kill-on-close.
[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
defines creation-time JOB_LIST and attribute-value lifetimes. The next milestone
is project execution ownership/cancellation propagation, not immediate IPC.
