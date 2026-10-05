# 模块与 Header Unit 的同次缓存证据

[English](MODULE_CACHE_EVIDENCE.md)

## 范围与结果

`MsvcModuleCompileCoordinator::run_recorded` 保留原 `result`、`record`，新增拥有型
`ModuleCompileWaveCacheEvidence cache_evidence`。`compiles` 按 `request.sources`
顺序排列，`header_unit_compiles` 按 `request.header_units` 排列。成功的整波结果中，
每个实际执行或复用的本地节点都有一份非 optional 的 `CompileCacheEvidence`。
外部 provider 仍只是依赖引用，不合成自有生产者。
旧 `ModuleCompileArtifactRecord::exact_cache_entry_captured` 保持 false；只有旧投影
不等于取得新增缓存值。

## 执行与顺序

默认 `run` 走 `run_impl<false>` 和原下层 `run`；`inspect`、CLI、上层 target 不采用新记录。
显式路径对每节点只执行一次既有下层 `run_recorded`，移动其当次返回的拥有型缓存值，
不重读缓存、不重扫、不重建签名、不再次编译补证据。依赖层次、provider 强制传播和
原错误选择不变。

仅显式实例在调度前预分配 optional 内部槽位并 reserve 两个结果向量。
`CompileCacheEntry` 没有默认签名，空槽位不能捏造缓存。每个工作线程只写自己的槽位
和原投影，工作线程不扩容向量。所有层成功后，由调用线程按输入顺序移动至非 optional
公开向量；内部缺证据就失败，不能填默认值。C++ 标准允许非 `vector<bool>` 容器不同
元素的并发修改；原 packed 传播标志仍仅在每层调度结束后更新。

## 失败与权限

非法请求、编译或调度失败保留原类型化整波错误，不返回部分成功记录。先完成的节点
可能已经写文件，不保证回滚。缓存保存失败仍保留编译成功、原警告、实际尝试保存的值
和类型化保存错误；热命中保留当次检查接受的值，不额外保存。

生产者身份、当前内容、完整清单、删除授权四项仍为 false。这些内存值不是原子现场、
写者租约、可信持久化或删除许可。工具链环境仅保留内存，不输出到证据。原 codec、
保存器、普通调用路径、VERSION、clean/prune 与发布行为均不改变。

## 验证与成本

沿用原十次 mock 和六次 native 记录波及其全部旧断言、编译预算、三次 native 初始扫描、
一次 consumer 链接/执行。新增逐节点状态、object/IFC 顺序、HU 身份、有效 force、
缓存字节副本、保存失败/sentinel、热命中每节点恰好一次成功载荷打开与零写入、
历史值跨后续覆盖和失败保全检查。有界条件变量让 mock 回调按 HU/A/B/consumer 完成，
与公开 source 顺序相反；不计时产品，也不声称控制每个槽位存储指令的顺序。
图和调度器保持真实实现。

测试副本序列化发生在 collector 激活区间之外；reads 不计失败打开，writes 包含失败替换
之前的尝试。完整请求/环境所有权并未全部序列化。便携源码及篡改契约不代替 Windows
真实执行。

显式路径通过既有下层接口复制每个实际尝试节点的请求和工具链，并随结果持有缓存依赖
载荷；内部槽位、公开向量及两种投影也消耗内存。移动不等于零成本，默认生成代码布局
也可能改变。本实现不分配性能实验、不宣称耗时或内存资格；新候选仍须验收自己的 CI
并独立决定是否集成，所有历史 HOLD 保留。

## 依据

- C++ 工作草案容器并发规则：https://eel.is/c++draft/container.requirements.dataraces
- MSVC 模块命令语义：https://devblogs.microsoft.com/cppblog/using-cpp-modules-in-msvc-from-the-command-line-part-1/
- 已有下层契约：[COMPILE_CACHE_EVIDENCE.md](COMPILE_CACHE_EVIDENCE.md)
- 上一生产者：[PCH_CACHE_EVIDENCE_ZH.md](PCH_CACHE_EVIDENCE_ZH.md)
