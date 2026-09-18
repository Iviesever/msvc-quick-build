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
On cancellation or normal root completion, first retain the Job's current member
process handles (with Job-membership verification), then terminate the Job and
wait for those handles to signal. Also require zero active processes and an
unchanged total-assignment count across the census and cleanup. The first native
Release test exposed that accounting zero alone can precede descendant-handle
signaling; the native assertion remains a zero-time check after return. A PID
that has disappeared or been reused cannot authorize terminating another process.
If assignments change during cleanup, identity cannot be checked, or the bounded
65,536-member census is exceeded, return an infrastructure error, not successful
cancellation. Job close remains best-effort cleanup on an error; callers must not
interpret that error as a safe write-lease handoff. No completion-port message or
empty queue is used as sole proof. Waiting only for the root or for the Job handle
to become signaled would not establish this boundary. The cleanup-only 1 ms query wait is not a resident idle
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
exit with a surviving descendant (sixteen repetitions without relaxing immediate
handle assertions), concurrent cancellation isolation, owner death,
and repeated-handle accounting. Ready/release events synchronize tests; retained
process handles, not recycled PIDs, prove root and descendant termination.

Five-second test completion checks and bounded helper watchdogs expose failures;
they are not promises that arbitrary Windows kernel teardown finishes in five
seconds. Windows Native Debug/Release and the unchanged independent ABBA workflow
must run on the PR's exact source identity. Default-path compatibility measurements
are not cancellation-throughput measurements, and this is not a speedup claim.

## Shared compiler-PDB research and product regression gates

The 5.6 staging boundary is **not** the entire M1b research roadmap. The public
CLI still routes config/profile/CLI compiler options through
`ProjectSetup::normalize_native_parameters` and `MsvcParameterEngine`.
`CompilerArgumentBuilder` selects `/Z7` in both configurations; `/Zi` and `/ZI`
are unsupported, and `/Fd` is reserved by the parameter registry. `CL` and `_CL_`
are removed from compiler invocations. This is existing behavior, not a flag
change made to avoid a failed experiment. External objects, libraries, PCH/IFC
inputs and low-level API callers are **not** all covered by this input boundary.
Linker `/DEBUG` may still create a final PDB; it is not the fixture's shared
compiler `compiler.pdb`. See Microsoft's [debug-format contract](https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170).

In contrast, `cpp/tests/platform/windows/msvc_service_ownership_probe.cpp`
constructs `/Zi` or `/ZI`, `/FS` and an explicit `/Fd.../compiler.pdb` itself.
Its `compile()` calls `invoke()` and then `WindowsProcessRunner::run(cl.exe)`;
it does not pass through public parameter admission or incremental cache
inspection. The scheduler-drain variant also explicitly opts into
`BoundedWorkScheduler::run_with_admission_stop`. Core process/scheduler tests
remain required; direct invocation of those primitives is not proof that the
public CLI supports shared compiler-PDB cancellation or safe lease transfer.
Microsoft explicitly states that [/FS does not prevent every parallel PDB error](https://learn.microsoft.com/en-us/cpp/build/reference/fs-force-synchronous-pdb-writes?view=msvc-170).

### Retained failures, not a successful experiment

[Default Endpoint #37](https://github.com/Iviesever/msvc-quick-build/actions/runs/35311336307)
completed 70 cases with three real failures at `A/work0`: `pch-debug-A-started-drain`,
`pch-release-A-started-drain` and `modules-release-A-started-scheduler-drain`.
Each retained C1041, exit 2, `cancelled=false`, and original `/FS /Zi /Fd` arguments.
Artifact `10534120991` has SHA256
`ded51533c9dd889f2bdbdc08840a621157428bfa3816f92b38ecb9cd52c1d6b1`.
B's success and outer cleanup do not repair A's compile failure. The older
[Default Endpoint #35](https://github.com/Iviesever/msvc-quick-build/issues/164#issuecomment-5709658468)
B-side failure and all subsequent observations remain separate evidence.
The extra 70 default / 42 private cases caused by #194's broad `cpp/**` triggers
remain an [acknowledged allocation error](https://github.com/Iviesever/msvc-quick-build/issues/164#issuecomment-5725666783), not part of its 86-call product budget.

For the exact include-root change in [#194](https://github.com/Iviesever/msvc-quick-build/pull/194),
the probe and direct failing invocation path are unchanged and do not execute
the changed comparison. This supports classifying these observations as an
**unresolved M1b research dependency**, not evidence that this comparison changed
PDB behavior. It is not a root-cause diagnosis, a general exclusion of all C1041,
or retrospective success. #194 still requires its explicit current review and
all applicable product/performance gates; this scope decision alone does not
approve that PR or the 5.6 release. #172, historical private129 and ABBA flags,
and cold tails are not cleared.

### Future execution allocation

`msvc-default-endpoint.yml` and `msvc-service-ownership.yml` retain their original
real-tool build/collector/diagnostic/upload job bodies, but those jobs have an
unconditional job-level false guard: **current real-tool allocation is zero**.
Neither a PR event nor manual dispatch grants another 70/42-case run. Both
workflows continue automatic scope checks, and Default Endpoint continues its
original synthetic collector fault-injection contract. Each run has its own
non-cancelling concurrency group. `scope.json` distinguishes successful scope
validation from `research_executed=false`, `research_passed=null` and
`historical_failures_cleared=false`; a skipped matrix is not a passed matrix.

A future study needs a separately reviewed #164 registration and source change:
exact source/tool identities, a discriminating hypothesis, one-shot run/attempt
admission, bounded cases/builds/captures, no replacement of failed samples, and
retained original diagnostics. Prefer a minimal discriminating case, not an
automatic full-matrix replay. The scope tests pin the archived payloads and
reject relaxed guards or validators; they must be reviewed together with any
new allocation. This is workflow control, not a security boundary against a
maintainer rewriting code or manually running a collector.

Native Debug/Release, public parameter and freshness checks, process lifecycle,
inspection without writes, self-host/package/installer and independent ABBA
are unchanged. Any extension to public compiler-PDB support, managed MSVC
cancellation or write-lease transfer must revisit this boundary and resolve the
M1b evidence before claiming that capability. Do not silently substitute `/Z7`,
remove `/FS`, kill shared services, lower failure checks, or count a later green
run as resolution. Final cumulative validation and a separate VERSION/release
PR remain necessary.

## Platform sources

[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
describes descendant inheritance, nested jobs, accounting and kill-on-close.
[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
defines creation-time JOB_LIST and attribute-value lifetimes. The next milestone
is project execution ownership/cancellation propagation, not immediate IPC.
