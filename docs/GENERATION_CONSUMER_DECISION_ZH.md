# 普通 generation 消费者与结果生命周期决定

**[English](GENERATION_CONSUMER_DECISION.md) | 简体中文**

## 1. 决定与完成范围

**`DEFER`：不选择新的普通 generation 产品消费者，并停止这条成本测量路径。**
[PR254 接手记录][handoff]要求的一次有限消费者／操作／拥有结果设计已经完成。
这是依据现有证据作出的产品选择决定，不是又一套必要性复核协议。
产品成本与 PR250 集成继续 **HOLD**；真实执行继续 **BLOCKED**。
本决定不排入新的测量提案。

[来源与决定索引](GENERATION_CONSUMER_DECISION.json)绑定所审阅记录及独立审计。
本文不增加实现、CLI、生产者、写入器、矩阵、登记、预算或发布权限。

| 身份 | 准确引用 |
|---|---|
| 本设计的 main／基线 | `7dea446ea63c180e0af4a5ab5b499c244fbeaea6` |
| 基线 tree | `838071cea6120c99e3fd264dd407f9fb67ef716c` |
| 冻结的 Draft PR250 | `a26b103685589ab8a5b4df60d1b760ec445efdf5` |
| PR250 tree | `4e777bdfc33b7befaf5e3769e27eb4fe910f9f03` |

[#198][issue]提出了真实需求：可归属的空间盘点，以及对不再需要的 MQB 管理产物进行
显式、安全的回收。其当前记录没有选择普通 generation 历史读取方、调用时机、
保留哪些拥有型值，或可追责的成本要求。其中的报告数值不是新的验收上限。
最新评论尾页已读到 PR254 的交付评论，随后一页为空；本次有界回读没有发现更晚的产品选择。

## 2. 三种不同的生命周期边界

| 边界 | 现有值支持什么 | 哪些仍由调用方／产品选择 |
|---|---|---|
| 一次同步 API 返回 | 拥有型投影、模型、归档和字符串可以在返回后继续存在，不借用原输入。被借用的 invocation 引用必须在调用期间保持有效且不变。 | 使用哪个返回值、由谁持有，以及何时销毁。 |
| 同一调用方进程内的多次 invocation | 调用方可以保留已完成的 `RecordedTargetResult`／`RecordedStaticTargetResult`，在之后的纯批次建模中提供借用引用；也可以保留独立拥有所选内容的模型。 | 实际消费者、输入集合、保留时长、重复频率和同时保留状态的最大范围。仅仅跨越这些调用不要求 wire 表示。 |
| 另一进程或重启之后 | PR250 可以产生拥有型字节字符串，并消费调用方提供的字节视图。 | 载体、记录发布、读取准入、来源证明、并发写入和中断规则。纯 codec 不提供这些持久化保证。 |

这些是[记录模型][model]与[归档 API][archive-api]的语义所有权事实，不是在声称某个分配次数、
RSS、优化器行为或运行时性能。不能把跨 invocation 使用悄然改写为跨进程持久化。

## 3. 三种备选及其取舍

| 备选 | 具体消费者／操作 | 收益与尚缺的决定 |
|---|---|---|
| A. 保留当前入口 | 用户调用 `mqb storage`；[该命令][storage-command]扫描一个项目的产物根目录、关联旧缓存引用并输出报告。现有显式 C++ 值 API 在原有限制下继续可用。 | 保留当前有用的观察能力；不增加普通 generation 历史消费、当前存活状态、可回收字节或安全清理。**当前选择：保留既有行为。** |
| B. 以后选择同进程库消费者 | 一个具名调用方保留已完成的普通 EXE／DLL／静态目标 invocation 值，再带着显式来源／目标／generation 声明及保留键调用 `model_recorded_artifact_generations`，消费拥有型模型／编译上下文。 | 同进程分析不必要求序列化。目前没有需求选择该调用方、触发时机、输入集合、保留生命周期或成本结果后果。**仅为候选。** |
| C. 以后选择归档／字节消费 | 一个具名调用方从完成记录中选择拥有型归档，可按明确选择的载体编码，再在兼容路径环境下解码／建模所提供的历史声明。 | 可与较大的原始 invocation 值分离，并可选地传输；也会增加语义验证模型、拥有型副本及载体／读取方义务。尚未选择普通 generation 的生产者／发布／读取方或可追责的成本契约。**仅为候选。** |

B／C 的频率与生命周期均**未由现有需求指定**。“显式”不是频率预算。
当前未选择默认／高频构建采用。现有测试辅助程序展示了有限次调用和保留值，
但其作用域、阶段名称、空保留向量和历史研究形状不能定义产品工作负载或保留策略。
[Generation 契约][model]

未来需要的输出本身仍未确定：诊断模型、供传输的所选历史声明、可信的清理资格，回答的是
不同问题。选择一个方便的 codec API 并未作出这些选择。未来消费者必须先指明该输出及其
改变的决定，然后才讨论成本提案。

## 4. 实际操作与所有权台账

下表描述所检查的准确实现，包括临时拥有状态；它不规定未来测量链，也不要求所有对象
在整个产品操作中始终共存。

| 操作 | 借用／调用方拥有的输入 | 函数拥有的工作与共存 | 返回及释放责任 |
|---|---|---|---|
| 当前 `mqb storage` | 解析后的命令参数；显式／当前项目路径 | 扫描、旧引用关联和报告完成期间，命令拥有一个 `StorageInventory`；存在问题时观察仍不完整。 | inventory 随命令作用域结束。报告字节输出到选定流；不返回普通 generation 模型，也不写 journal。 |
| 严格记录投影 | 一个已完成的普通／静态目标结果及词法键 | [投影][projection]创建所选引用及逐源编译上下文；不把原始进程输出／环境复制进结果。 | 返回投影拥有所选值；其生命周期由调用方独立于原已完成结果管理。 |
| 记录的 live 模型 | 输入 span 中的引用包装器借用已完成的普通／静态目标结果；保留 span／根目录／键 | [模型实现][model-source]预检批次，拥有投影引用、编译上下文及 recipe／模型数据。`finish_model` 把投影传给拥有副本的 association；构造期间本地投影与这些副本共存。 | 返回模型及 `compiles` 拥有所选状态。函数局部值在返回／异常展开时释放；调用方输入与返回结果有各自的释放点。 |
| `project_artifact_generation_archive` | 同样借用的 live 输入及保留／根目录／键 | [PR250 投影][archive-model-source]在向新拥有型归档复制所选声明时，保留一个完整、成功的验证模型。live 输入、验证模型和归档可以共存。 | 返回归档；本地验证模型在调用结束时释放。原 live 结果仍由调用方负责。 |
| `encode_artifact_generation_archive` | const 归档引用及键 | [编码器][codec-source]构造完整验证模型，然后在归档及模型仍有效时构造拥有型 wire 字符串。 | 返回 wire；本地模型在调用结束时释放。归档与 wire 的生命周期由调用方分别管理。 |
| `decode_artifact_generation_archive` | 指向调用方拥有 wire 的 `string_view` 及键 | [解码器][codec-source]执行不拥有内容的结构准入，构造解码后的拥有型归档，再在返回前持有完整验证模型。调用方 wire、解码归档和验证模型可以共存。 | 成功归档拥有其值，返回后不借用 wire。函数局部值在调用结束时释放；调用方决定是否同时保留 wire 与解码归档。 |
| `model_archived_artifact_generations` | const 归档引用及键 | 直接对归档声明使用共享严格模型；从不重建成功的 `Recorded*Result`。它也拥有投影／模型临时值。 | 模型及编译上下文独立拥有内容；归档和模型何时释放由调用方决定。 |
| 错误与异常 | 输入保留原有所有权义务 | 公开类型化错误拥有字符串，也可能拥有嵌套的模型／投影诊断。转换／分配／键异常可以传播。 | 返回错误由调用方拥有；已构造的局部值正常展开。丢弃诊断的标量测试包装器不能证明公开错误零所有权或总分配量。 |

Association 的复制边界在其[契约][association]与[实现][association-source]中明确。
移动或复制消除不能把语义共存台账变成精确到字节的峰值测量。内部临时值、所选输出上限
和调用方已经保留的值不能混为一谈。格式上限限制准入结构，不限制全部堆／RSS 或时间。

较短的同进程操作可以在产出可用的拥有型结果后释放 live 输入；传输消费者可以在载体接收
所需字节后释放归档。这些是**可能的设计**，并非已选释放点、测得的节省，或丢弃所需诊断的
权限。载体缓冲、格式化、复制、解析、哈希以及最终 flush／fsync／close，可能增加单个 codec
API 返回窗口以外的工作。

## 5. 声明、保留与持久化仍须区分

显式保留键选择所提供批次中的未验证历史声明。它不是数量／年龄策略、持久保留决定、文件
存在证明、未选记录退役决定或删除清单。来源 ID、项目／目标／generation 键和调用方标签
不是经过认证的写入方。词法路径匹配及空 inventory 模型不观察当前文件。
生产者身份、当前内容、完整生产者清单和删除权限仍为 **false**。
[Generation 与保留契约][model]

现有 [LINK completion archive][link-archive]是真实存在的另一套显式单快照文件能力。
它不发布普通 generation 归档、不把 generation 读取方绑定到生产者，也不提供整个记录集
事务。[Storage][inventory] 使用的旧 LINK／LIB 缓存引用同样不能补上缺失的普通 generation
消费者。

任何未来跨进程设计都必须由可追责的负责人另行选择：发布什么、目的地／载体、记录集版本
和身份、获准读取方、完整性／失败报告、兼容路径语义、原子性与并发边界，以及中断／持久性
行为。Codec 往返成功或一个哈希都不提供这些保证。所选归档不序列化完整进程环境、时间
收集器或可执行恢复请求；保存所选声明并不等于保存一个 live invocation。

## 6. 成本决定与有限停止

所审阅记录没有提供 B／C 的产品成本要求。因此，消费者、触发／频率、选定操作／结果
生命周期、受支持输入集合／平台，以及可追责的指标／窗口／决定后果都保持未选。
#198 对普通构建不做全局扫描的约束仍然有意义，但不能回答尚未确定的显式消费者成本问题。

已经完成的[必要性复核][necessity]保留了历史上显式库集成目的的合理性；默认 CLI 调用方不是
普遍前提。该复核还解释了独立的 external-no-op `>1 ms AND >10%` 规则、schema／输出上限、
固定替身值以及 INVALID002 观察为什么不能成为本消费者的产品准则。
本文不引入替代数值，也不引入没有实际决定后果的非数值门槛。

**决定后果：不继续设计或运行这条路径上的真实成本提案。** 原七项执行阻塞继续成立。
准确冻结的[准备文件][preparation]仍为 `PREPARATION_ONLY_BLOCKED`：21000 字节，
SHA-256 `f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`，
15 个严格整数零、10 个 null 未来选择和四个 false 权限。
001／002 保持 failed-preflight／INVALID 的关闭状态，已消耗台账、空流及缺失观察均保留。
没有补测或恢复搜索。

只有实质新增、可追责的消费者／决定证据选定契约及可辩护的成本规则后，才重新考虑。
那会是受全部七项前提约束的独立提案，不是把冻结文件切换到可执行状态。
下一接手点不自动重复同义的消费者／必要性复核。

## 7. 回到 #198 产品需求

下一项有界产品接手点是：**针对用户明确选择的、已知可重建目标输出，定义显式 clean
资格与拒绝契约**。必须指出一条有依据的正向路径、每个输出获得资格的准确证据、
受保护／共享／未知项排除、预览到执行的身份核验，以及受支持的写入方／运行中产品边界。
如果正向路径缺乏支持，应记录具体缺失的权限依据；不要交付永远拒绝的命令，也不要从
历史声明制造清理资格。

使用现有类型化布局、生产者及物理身份依据。[Write-domain 基础][write-domain]已经包含
已知写入收集与保守准入原语。当前 [API][write-domain-api]在 `begin_writes` 后没有
completion／reset；仅 cleaner 参与的标记不能排除旧构建方，作用域退出也不能证明写入方
已经停止。其历史 VERSION 文字不覆盖当前 `VERSION` 5.6.0。
更广的单写入方／会话／恢复路线仍由 [#164][roadmap]承接。
本接手点既不假定该路线完成，也不在此扩张它。

这是独立于普通 generation codec 采用的具体清理契约任务。本轮不选择自动 `prune`、
保留数量策略、写入器实现、新执行预算或启动。#198 保持开放；清理、Windows／Rium 及发布
验收仍未完成。PR250 与 #248 保持各自独立的 Draft／HOLD 工作。VERSION 维持 5.6.0。

## 8. 验证范围

独立生命周期审计与产品需求审计支持本决定。全部产品代码、测试、工作流、既有文档及冻结
证据均保留。本轮仅新增中英决定、其 JSON 索引和带日期的 issue／plan／walkthrough。
本地验证只使用只读 Git、源码文本、JSON 与哈希；没有启动项目模块、分析器、测试、固定
替身、codec／编译器／MQB／MSVC／ETW 或真实工作负载。

新的双语文件对接受直接审阅。实际适用的自动文档和路径分类 CI、准确来源身份、正常合并及
交付另随 [walkthrough](20261010-000618-generation-consumer/walkthrough.md)记录。
文档 CI 原有的枚举文件对本身不能验证这组新文件。静态生命周期推理不是 native 或产品成本
资格证明。

[handoff]: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6084515239
[issue]: https://github.com/Iviesever/msvc-quick-build/issues/198
[roadmap]: https://github.com/Iviesever/msvc-quick-build/issues/164
[model]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ARTIFACT_GENERATIONS.md
[model-source]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp
[projection]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/RECORDED_STORAGE_PROJECTION.md
[association]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ARTIFACT_STORAGE_ASSOCIATION.md
[association-source]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/core/cache/ArtifactStorageAssociation.cpp
[inventory]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/STORAGE_INVENTORY.md
[storage-command]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/src/app/targets/StorageCommand.cpp
[archive-api]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/include/mqb/orchestration/ArtifactGenerationArchive.hpp
[archive-model-source]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/src/orchestration/incremental/ArtifactGenerationModel.cpp
[codec-source]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/src/orchestration/incremental/ArtifactGenerationArchive.cpp
[link-archive]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/LINK_COMPLETION_ARCHIVE.md
[necessity]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ANALYSIS_COST_NECESSITY_REVIEW.md
[preparation]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/ANALYSIS_COST_DECISION_PREPARATION.json
[write-domain]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/docs/WRITE_DOMAIN_ADMISSION.md
[write-domain-api]: https://github.com/Iviesever/msvc-quick-build/blob/7dea446ea63c180e0af4a5ab5b499c244fbeaea6/cpp/include/mqb/platform/windows/WindowsWriteDomain.hpp
