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

PR8 deliberately avoids arbitrary heuristics such as “use 75% of cores”. Memory pressure, machine load, and TU-cost-aware scheduling belong to later throughput work.

### `fixed(N)`

Explicit `-j N` is a hard user ceiling:

```text
workers = min(N, ready work items)
```

Hardware concurrency does not silently rewrite an explicitly selected fixed policy. The user owns the decision to choose an appropriate explicit ceiling for the machine.

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

PR8 uses structural tests instead of hosted-runner timing thresholds:

- the resolver accepts injected hardware concurrency and tests unknown, hardware-limited, work-limited, and fixed-ceiling cases;
- numeric C++ API compatibility and fixed(0) fail-closed behavior are locked;
- the CLI parser covers all four `auto` spellings;
- the ordinary four-TU E2E must preserve every compile/link cache hit after fixed → auto;
- the `import std` P1689 module E2E must preserve provider/consumer/link warm reuse after fixed → auto;
- Debug/Release self-host, the complete native test graph, and Release package validation remain the final merge gates.
