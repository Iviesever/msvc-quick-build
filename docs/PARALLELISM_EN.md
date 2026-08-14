# MQB Parallelism Contract

MQB parallelism is an **execution policy**. It is not part of the compile/link recipe and never participates in cache identity.

## CLI

```powershell
mqb build main.cpp -j auto
mqb build main.cpp -j 8
mqb build main.cpp -jauto
mqb build main.cpp --jobs=8
```

`auto` is the default policy. A positive integer `N` is a fixed maximum worker ceiling. Zero, negative values, and other text fail closed.

Supported auto spellings:

- `-j auto`
- `-jauto`
- `--jobs auto`
- `--jobs=auto`

## Policies

### `automatic`

`auto` is not converted to an integer early in `Application.cpp`. The typed `ParallelismPolicy` is preserved until the scheduler sees the actual ready work batch.

For each scheduler dispatch:

```text
hardware budget = OS / std::thread::hardware_concurrency()
if hardware budget == 0: hardware budget = 1
workers = min(hardware budget, ready work items)
```

Consequently:

- a single TU or single ready node does not create unnecessary workers;
- ordinary targets resolve against the current source batch;
- module scanning resolves against the current scan batch;
- named-module compilation resolves **per dependency level** against that level's ready width;
- header units and toolchain-owned module providers use the same policy when they enter a ready level.

MQB does not hard-code arbitrary heuristics such as “use 75% of cores”. Memory pressure, instantaneous machine load, and TU-cost prediction should enter the scheduler only when they can be represented by a stable, testable resource model rather than hosted-runner accident.

### `fixed(N)`

Explicit `-j N` is a hard user ceiling:

```text
workers = min(N, ready work items)
```

Hardware concurrency does not silently rewrite an explicitly selected fixed policy. The user owns the decision to choose an appropriate explicit ceiling for the machine.

## Caller-participating scheduler

When the resolved `worker_count` is greater than one, MQB counts the thread that called the scheduler as one logical worker and creates only `worker_count - 1` background threads:

```text
logical workers = caller + background workers
background threads created = worker_count - 1
```

This does not change the concurrency ceiling, item assignment, stop/exception behavior, or public `worker_count`. It deterministically removes one thread creation/destruction from every parallel dispatch, including every dependency level in a named-module graph.

The one-worker path remains the existing inline fast path and creates no background thread.

## Warm P1689 scan reuse

A warm compile/link cache hit is not a true no-op for the Named Modules / Header Unit pipeline if every invocation still launches `cl.exe /scanDependencies` to rediscover unchanged topology.

MQB therefore seals reusable P1689 topology evidence **inside the existing compile cache after a successful compile**, rather than creating a second independent scan-cache artifact. Compile-cache v3 may store:

- a scan-recipe signature;
- the exact source file-time snapshot;
- the exact P1689 `.scan` artifact snapshot;
- textual include dependency snapshots obtained from the successful `/sourceDependencies` metadata.

Only a successful compile can seal this evidence. If the source or one of its textual headers changes after the scan but before the compile completes, the compile cache is still usable for its normal purpose but no scan evidence is sealed.

A later module scan is reusable only when the signature, source, `.scan`, and every textual dependency snapshot match exactly. Any cache load failure, snapshot failure, malformed metadata, or P1689 parse failure fails open to the normal raw scan; the optimization never becomes a new build-failure surface.

Toolchain-owned `std` / `std.compat` module providers use the same scan-reuse path as project sources.

### Cache wire compatibility

New compile caches are written as v3. Historical v2 entries remain readable but contain no scan evidence, so the first module build performs a normal scan and a later successful compile can seal the new evidence. v1 remains unsupported as before.

Scan evidence persists the native `file_time_type` tick count exactly instead of converting across epochs to nanoseconds, avoiding range overflow and precision loss.

## One owner of compile parallelism

MQB launches independent `cl.exe` processes per translation unit, so it does not stack MSVC `/MP` on top. Doing so would create nested parallelism:

```text
MQB workers × cl.exe /MP workers
```

That can oversubscribe the machine and makes `-j` cease to be an understandable process-level limit.

The MSVC Parameter Engine therefore continues to reject `/MP`; MQB remains the sole owner of process-level compile parallelism.

## Cache and artifact identity

Switching from `-j 1` to `-j auto`, or from `-j 4` to `-j 2`, does not change:

- compile signatures;
- link/archive signatures;
- object / IFC / PCH / executable / library paths;
- compile/link/archive cache keys;
- dependency freshness evidence.

If source, dependencies, toolchain, and build recipe are unchanged, changing only the jobs policy must remain a warm cache hit.

The P1689 scan signature is a separate topology-recipe identity and contains only inputs that can affect scanning; runtime-library and LTCG policy do not participate in scan identity.

## C++ API compatibility

Existing request field names remain unchanged, for example:

```cpp
.max_parallel_compiles = 4
.max_parallel_scans = 2
.max_parallel_jobs = 8
```

Their type is upgraded from a raw `std::size_t` to `ParallelismPolicy`. Positive integers implicitly convert to `fixed(N)`, keeping existing numeric designated initializers source-compatible. Zero converts to an invalid fixed policy and continues to trigger fail-closed validation.

New code may be explicit:

```cpp
.max_parallel_compiles = ParallelismPolicy::automatic()
.max_parallel_compiles = ParallelismPolicy::fixed(8)
```

## Validation contract

Structural correctness tests do not use hosted-runner millisecond thresholds:

- the resolver accepts injected hardware concurrency and tests unknown, hardware-limited, work-limited, and fixed-ceiling cases;
- a multi-worker barrier test proves the caller participates while observed concurrency still equals the logical worker ceiling;
- scan-signature tests lock topology-affecting inputs and intentionally excluded non-scan inputs;
- scan-evidence tests require any source/header/P1689 artifact snapshot change to invalidate reuse;
- compile-cache v3 round-trip and v2 compatibility are directly tested;
- the module coordinator verifies cold scans, warm zero-scan reuse, precise rescan after one source mutation, and resealing after a successful rebuild;
- ordinary and `import std` E2E continue proving jobs policy does not contaminate compile/link cache identity;
- Debug/Release self-host, the complete native test graph, and Release package validation remain the final merge gates.
