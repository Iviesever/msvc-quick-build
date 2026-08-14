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

每次 scheduler 面对一个 ready batch 时，worker 数按以下规则解析：

```text
hardware budget = OS / std::thread::hardware_concurrency()
if hardware budget == 0: hardware budget = 1
workers = min(hardware budget, ready work items)
```

因此：

- 单 TU / 单 ready node 不会创建多余 worker；
- ordinary target 会按当前 source batch 解析；
- module scan 会按当前 scan batch 解析；
- named-module compile 会**逐 dependency level**按该层 ready width 重新解析；
- header unit / toolchain-owned module provider 进入对应 ready level 后使用同一策略。

PR8 不加入诸如“只用 75% 核心”之类经验常数。内存压力、机器负载、TU 成本估计等资源自适应属于后续 throughput 阶段。

### `fixed(N)`

显式 `-j N` 是用户给出的硬上限：

```text
workers = min(N, ready work items)
```

硬件线程数不会把用户明确指定的 `N` 再改写成另一策略。最终是否适合该机器由用户负责选择。

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

只要源码、依赖、工具链和构建配方不变，改变 jobs policy 后仍应保持 warm cache hit。

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

## 测试契约

PR8 使用结构性测试而不是 hosted-runner 毫秒阈值：

- resolver 可注入 hardware concurrency，验证 unknown / hardware-limited / work-limited / fixed ceiling；
- numeric C++ API compatibility 和 fixed(0) fail-closed 被锁定；
- CLI parser 覆盖四种 `auto` 写法；
- ordinary 四 TU E2E 在 fixed → auto 后必须全部 compile/link cache hit；
- `import std` P1689 module E2E 在 fixed → auto 后必须保持 consumer/provider/link warm reuse；
- Debug/Release self-host、完整 native test graph 和 Release package validation 仍是最终合并门。
