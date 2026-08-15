# MQB 并行调度契约

MQB 的并行度是**执行策略**，不属于编译/链接配方，也不参与 cache identity。

## CLI

```powershell
mqb build main.cpp -j auto
mqb build main.cpp -j 8
mqb build main.cpp -jauto
mqb build main.cpp --jobs=8
```

`auto` 是默认策略。正整数 `N` 表示固定的最大 worker 上限。`0`、负数和其他文本都会 fail closed。

支持的 auto 写法：

- `-j auto`
- `-jauto`
- `--jobs auto`
- `--jobs=auto`

## 两种策略

### `automatic`

`auto` 不会在 `Application.cpp` 里提前换算成一个整数。typed `ParallelismPolicy` 会一路保留到真正的 scheduler dispatch。

每次 scheduler 面对一个 ready batch 时，先读取一个 `ParallelismResourceSnapshot`：

- `std::thread::hardware_concurrency()` 提供 CPU 并发宽度；
- Windows `LowMemoryResourceNotification` 提供操作系统判定的低内存状态；
- 若平台观测失败，则 memory pressure 为 `unknown`，保持历史 CPU-only 行为。

正常/未知内存状态：

```text
hardware budget = hardware_concurrency
if hardware budget == 0: hardware budget = 1
workers = min(hardware budget, ready work items)
```

Windows 报告系统处于 low-memory resource condition 时：

```text
workers = 1
```

当前 automatic workload 显式区分 `compilation` 与 `dependency_scan`；两者都会启动 `cl.exe` 进程，因此都受 low-memory guard 约束。MQB 不发明“内存使用率 80%/90%”之类阈值，而是使用 Windows 自身的 low-memory resource notification。恢复到 normal 状态后的下一次 ready-batch dispatch 会重新获得正常 CPU/ready-width 并行度。

因此：

- 单 TU / 单 ready node 不会创建多余 worker；
- ordinary target 会按当前 source batch 解析；
- module scan 以 `dependency_scan` workload 按当前 scan batch 解析；
- named-module compile 会**逐 dependency level**按该层 ready width 重新解析；
- header unit / toolchain-owned module provider 进入对应 ready level 后使用同一策略；
- 系统进入 Windows low-memory condition 时，新 automatic compile/scan batch 不再继续乘增 `cl.exe` 进程。

资源观测与 resolver 完全分离：`ParallelismResourceObserver` 只负责获取平台 snapshot，`ParallelismResolver` 是纯决策逻辑，因此测试可以直接注入 normal / low / unknown memory state，而不需要在 CI 上真的制造内存压力。

### `fixed(N)`

显式 `-j N` 是用户给出的硬上限：

```text
workers = min(N, ready work items)
```

硬件线程数或 low-memory notification 都不会把用户明确指定的 `N` 再改写成另一策略。最终是否适合该机器由用户负责选择。因此 `-j N` 同时也是对自动资源适配的显式覆盖。

## Caller-participating scheduler

当解析出的 `worker_count > 1` 时，MQB 将**调用 scheduler 的当前线程本身算作一个 logical worker**，只额外创建 `worker_count - 1` 个后台线程：

```text
logical workers = caller + background workers
background threads created = worker_count - 1
```

这不会改变并发上限、任务索引分配、stop/exception 语义或 public `worker_count`，但会确定性消除每次 parallel dispatch 的一个线程创建/销毁。对 named-module graph 来说，每个 dependency level 都能获得这项收益。

单 worker 仍使用原有 inline fast path，不创建后台线程。因此 low-memory automatic batch 也自然走 caller-only fast path。

## Warm P1689 scan reuse

Named Modules / Header Unit pipeline 的 compile/link warm cache hit 还不足以构成真正 no-op：如果每次都重新启动 `cl.exe /scanDependencies`，module topology 仍有固定进程成本。

MQB 因此把可复用的 P1689 topology evidence **封存在成功 compile 的现有 compile cache 中**，而不是创建第二套独立 scan-cache artifact。compile cache v3 可选保存：

- scan recipe signature；
- source 的精确文件时间快照；
- P1689 `.scan` artifact 的精确快照；
- 成功 `/sourceDependencies` 得到的 textual include dependency 快照。

只有成功 compile 才能 seal 这份 evidence。若 source/header 在 scan 之后、compile 完成之前已经变化，则该次 compile cache 不写入 scan evidence。

下一次 module scan 只有在 signature、source、`.scan` 和全部 textual dependency snapshot **全部精确匹配**时才能复用旧 P1689；任何 cache 读取失败、snapshot 失败、metadata 损坏或 P1689 parse 失败都会 fail-open 到正常 raw scan，而不会让性能缓存成为新的构建失败面。

工具链提供的 `std` / `std.compat` module provider 与项目源码共用同一套 scan-reuse 规则。

### Cache wire compatibility

新写入的 compile cache 是 v3。历史 v2 cache 仍可读取，只是没有 scan evidence，因此第一次 module build 会正常重新 scan；后续成功 compile 才会获得 warm scan reuse。v1 继续按原策略拒绝。

scan evidence 使用 native `file_time_type` tick count 原样持久化，不做跨 epoch 的纳秒换算，避免范围溢出或精度损失。

## 单一并行所有权

MQB 自己按 TU 启动独立 `cl.exe`，所以不会再叠加 MSVC `/MP`。否则会形成：

```text
MQB workers × cl.exe /MP workers
```

这种嵌套并行容易 oversubscription，并让 `-j` 不再是可理解的进程级上限。

因此 `/MP` 继续由 MSVC Parameter Engine 明确拒绝；MQB 是 process-level compile parallelism 的唯一所有者。

## Cache 与产物身份

从 `-j 1` 切到 `-j auto`，或从 `-j 4` 切到 `-j 2`，不会改变：

- compile signature；
- link/archive signature；
- object / IFC / PCH / executable / library 路径；
- compile/link/archive cache key；
- dependency freshness 证据。

resource snapshot、memory pressure、resolved worker count 同样只属于执行策略，不进入 build/cache identity。只要源码、依赖、工具链和构建配方不变，改变 jobs policy 或机器压力状态后仍应保持 warm cache hit。

P1689 scan signature 是独立的 topology recipe identity，只包含会影响 scan 的输入；runtime library 与 LTCG 等不进入 scan identity。

## C++ API 兼容性

原有 request 字段名保持不变，例如：

```cpp
.max_parallel_compiles = 4
.max_parallel_scans = 2
.max_parallel_jobs = 8
```

这些字段的类型从裸 `std::size_t` 升级为 `ParallelismPolicy`。正整数可隐式转换为 `fixed(N)`，因此已有 numeric designated initializer 保持 source-compatible；`0` 转为 invalid fixed policy，并继续触发原来的 fail-closed validation。

新代码可显式写：

```cpp
.max_parallel_compiles = ParallelismPolicy::automatic()
.max_parallel_compiles = ParallelismPolicy::fixed(8)
```

平台资源观测通过 `ParallelismResourceSnapshot` 与 `ParallelismResourceObserver` 暴露给 scheduler；普通调用者无需自己采集系统状态。

## 测试契约

结构性正确性测试不使用 hosted-runner 毫秒阈值：

- resolver 可注入 hardware concurrency + normal / low / unknown memory pressure；
- low-memory automatic compilation/dependency-scan 必须解析为单 worker；
- fixed `-j N` 在 low-memory snapshot 下仍保持显式用户上限；
- unknown memory observation 必须保持历史 CPU/ready-width 行为；
- multi-worker barrier test 验证 caller 实际参与，同时 observed concurrency 仍严格等于 logical worker 上限；
- scan signature test 锁定 topology-affecting 与非 scan 输入边界；
- scan evidence test 锁定 source/header/P1689 artifact 任一 snapshot 变化必失效；
- compile-cache v3 round-trip 与 v2 compatibility 被直接验证；
- module coordinator 验证 cold scan、warm zero-scan、单 source mutation 精确 rescan、成功 rebuild 后重新 seal；
- ordinary 与 `import std` E2E 继续验证 jobs/resource policy 不污染 compile/link cache；
- Debug/Release self-host、完整 native test graph 和 Release package validation 仍是最终合并门。
