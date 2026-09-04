# Persistent Warm Fast Path

**[English](WARM_FAST_PATH.md) | 简体中文**

MQB 的 compile/link/archive cache 已经能避免重复启动工具，但一次完全 warm 的 build 在真正查询这些下游 cache 之前，仍可能花时间递归索引项目并重新读取 source/header 文本。Persistent source-discovery cache 在不削弱 discovery correctness 的前提下，去掉这部分重复的前半段工作。

## 范围

Smart source discovery 会针对每个 discovery root 持久化一份 best-effort record：

```text
<discovery-root>/.mqb/cache/discovery/source-discovery.mqbcache
```

Public `SourceDiscovery::Request` 默认启用 persistent state。直接调用者可以设置 `persistent_cache = false`，或显式指定 `cache_file`。

这份 state 只服务性能。Cache read、parse、validation 或 write 失败都不会造成 build failure；MQB 会回退到普通 recursive discovery path。

## 被 sealed 的内容

一次成功且可缓存的 discovery 会记录：

- normalized discovery request identity：
  - project/discovery root；
  - entry translation unit；
  - include search order；
  - excluded directories；
  - 显式包含的 extra sources；
  - excluded sources；
- selected translation-unit list 与 ordering；
- indexed-file count；
- selected closure 是否需要 Modules/P1689 pipeline；
- 每个已索引 C/C++ source/header 的精确 `file_time_type` snapshot；
- discovery root 与每个递归访问、未被排除目录的精确 directory snapshot。

File evidence 用来捕获内容变化。Directory evidence 则能捕获新增、删除与 rename，即使此前已索引文件本身没有发生变化。

## Race-safe sealing

Discovery 在构建 source index 时收集 freshness evidence，并在该 record 获准持久化之前，重新检查所有已捕获 file 与 directory snapshot。

如果 source/header 或 traversal directory 在 discovery 过程中发生变化，本次 pass 对当前 invocation 仍然有效，但不会被认定为可复用的 persistent evidence。下一次 invocation 会重新执行普通 discovery。

无法读取 indexed file 产生的 warning 也会阻止 sealing。显式 extra source 在结果可被缓存之前必须已经存在于 index 中。

## Warm reuse

后续 invocation 中，MQB 会先执行为了保留既有 diagnostics 所需的低成本 request/path validation；随后，在读取 extra-source text 或递归枚举/分析项目之前加载 persistent record。

Reuse 要求：

1. normalized request identity 精确一致；
2. 每一个 indexed file 仍作为 regular file 存在，并具有与记录完全一致的 timestamp；
3. 每一个 recorded directory 仍作为 directory 存在，并具有与记录完全一致的 timestamp；
4. cache record 的 version 与格式有效。

任何不一致都会变成普通 cache miss。

`.mqb` state directory 本来就被 smart discovery 排除，所以写入或替换 discovery-cache file 不会反过来使它所保护的 project directory evidence 失效。

## Cache 格式

当前 binary format 是 version 1（`MQBDISC1`）。Parser 对 file size、string size、path/snapshot count 都有上限；会拒绝 malformed 或 trailing data；path 以 normalized generic UTF-8 form 存储，并精确保留 native `file_time_type` tick count。

该格式有意保持 private。未来不兼容的新版本应当被视为 cache miss，而不是为了迁移旧 state 去牺牲 build correctness。

## Validation 与 performance evidence

`source_discovery_cache_tests.cpp` 覆盖 cold/warm reuse、header invalidation、通过 directory evidence 检测 source add/remove、request-identity change、corrupt-cache fallback/repair 与显式关闭 cache。

Native benchmark harness 包括：

- `discovery-cold` —— 第一次 smart-discovery/build invocation；
- `discovery-no-op` —— unchanged warm invocation，此时 persistent evidence 应避免 recursive indexing/text analysis；
- `discovery-header` —— 必须使 persistent evidence 失效的 header mutation。

`compare_mqb_benchmarks.ps1` 会为每个 scenario 同时报告 total-wall-clock 与 discovery-phase median/delta。这些 measurement 是 review evidence，而不是 hosted-runner correctness threshold。

## 有意保留的限制

第一版实现针对每个 discovery root 只保存一个 discovery request/result。交替使用多个 entry/request identity 因此可能替换之前的 record，造成额外 cache miss，但绝不会造成 stale reuse。未来 multi-key cache 可以改善这类 workload，而不改变 freshness contract。

这个 milestone 不持久化 MSVC environment/toolchain discovery，也不会绕过 project configuration parsing。它们属于不同的 fast-path layer，具有不同 invalidation 与 security boundary。

## Invocation-scoped target filesystem evidence

普通 executable/DLL target 与 static-library target 会在一次 MQB invocation 内，让各 translation-unit inspection 共享 compile-cache **dependency** snapshot。该表不会持久化，也不会被另一个 target 或进程复用。

Key 使用 Windows path identity，并保留现有 file-or-directory dependency 语义。并发请求采用 single-flight entry：一个 worker 执行 metadata query，其余 worker 复用完全相同的 existence/timestamp 结果，同时保留各自请求的 path spelling，用于 validation 与 diagnostics。

Source 与 object/output snapshot 有意继续走原始直连路径。Target validation 已经保证这些 regular-file artifact 在目标内唯一，因此将它们送入同步共享表不可能产生复用，只会让没有公共依赖的项目承担额外开销。

Dependency table 会把 warm path 从按 dependency occurrence 增长推进到更接近按 unique dependency path 增长，但不会修改 cache record、build signature、compiler recipe 或 freshness comparison。Cache load 与 compile validation 仍然属于每个 translation unit。

凡首次 snapshot 曾被其他 inspection 复用的 dependency，都会在 compile scheduler join 所有 worker 后再探测一次。如果 existence、timestamp 或可靠 metadata 状态在该窗口内发生变化，或者 barrier 本身无法完成，MQB 会拒绝所有可能依赖共享 observation 的结果，并在不使用 evidence reuse 的情况下保守重编整个 target。因此优化失败只会增加工作量，绝不会产生 stale success。

当前边界是有意限定的：

- ordinary 与 static target 的 compile-cache dependency inspection 参与；
- 唯一的 source 与 object/output probe 保持直连；
- PCH 与 Modules/Header Units 保持现有 dependency-ordered path；
- link/archive object snapshot handoff 是独立的后续优化；
- 该表仅在 invocation 内存在，不引入 daemon、watcher、USN journal、content hash、cache pack 或 persistent lock。

`incremental_target_coordinator_tests.cpp` 锁定 single-flight counter 行为与 mutation revalidation rejection。`verify_performance_instrumentation.ps1` 证明 129-TU common-header no-op 保持精确 cache/process 行为，同时每个共享 dependency 最多执行一次初始 probe 与一次 revalidation。
