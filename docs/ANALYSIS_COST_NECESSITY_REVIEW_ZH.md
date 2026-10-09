# 真实成本测量必要性与事前验收审查

**[English](ANALYSIS_COST_NECESSITY_REVIEW.md) | 简体中文**

## 1. 当前决定

**`NO_JUSTIFIED_REAL_EXECUTION_NOW`：产品成本保持 `HOLD`，PR250 集成保持
`HOLD`，真实执行保持 `BLOCKED`。** [PR253 接手记录][handoff]要求的独立必要性
审查已完成。所审证据尚未选定一个带有事前结果规则、且能由新增真实成本观测改变的
当前产品决定。这是有范围的当前结论；产品决定和要求明确后，未来测量仍可能有用。

独立的[机器可读审查](ANALYSIS_COST_NECESSITY_REVIEW.json)记录证据与缺口，
不构成执行协议或授权。原[冻结准备][preparation]仍精确为 21000 bytes，SHA-256
`f3c978a791f5797d62739dde24759003e231eb58892f7eaf689852ef62a0e1ae`，永久处于
`PREPARATION_ONLY_BLOCKED`：15 项严格整数零真实预算、10 个 null 未来选择、
七项 blocker、四项 false authority。填充字段或重算 hash 不能把任一文档升级为许可。

| 身份角色 | Commit / tree |
|---|---|
| 本审查开发 base | `36d5219b5274221bfa6688d100bd5eb6cc70409d` / `a1bd31e779b3423a6993ede5ef575df127fb66b2` |
| 冻结的 Draft PR250 candidate | `a26b103685589ab8a5b4df60d1b760ec445efdf5` / `4e777bdfc33b7befaf5e3769e27eb4fe910f9f03` |
| PR250 原 product base | `2762749b27fc89156b724d19e89cce84e388ef52` / `0a4c2dd76c56309accf58d584a079b4bd483ee49` |

冻结准备内较早的分析开发起点保持原值；这些参考身份均未选定未来 comparator 或执行体。

## 2. 产品需要与实际消费者

[#198][issue]的产品需要是有用的空间盘点和显式、安全的 clean/prune，涵盖当前、
共享、历史和未知归属。问题中引用的报告数值尚未核验，属于需求背景，不能作为已测得的
验收阈值。这一最终需要本身没有选定 codec 消费者、调用频率或其可容忍成本。

[原登记][registration]确有合理的历史目的：量化固定 Linux/GCC 上显式、非默认纯值
API 的成本，并与既有 live-model 对照，为有限开发集成提供证据。Study002 通过
[单独登记][registration2]延续了该目的。这些分配已经关闭。重述原目的、替换 INVALID002
或补缺五条，均不能建立一个新近得到独立论证的决定。原 base 没有等价旧 codec：candidate
live-model 和 archive chain 是不同操作，不能解释成旧 codec 回退或孤立的序列化净成本。

显式 library 使用或集成决定本身也可以构成未来成本审查的理由，并不要求先有默认 CLI caller。
当前缺少的是所选使用方式、所有权/资源取舍及事前结果规则；仅仅没有默认调用方，不足以
推出当前 HOLD。

| 所审范围 | 源码支持的发现 | 对决定的含义 |
|---|---|---|
| Main 生成模型 | 有公开声明和定义。在限定生产源码搜索中，两个模型入口均无直接调用方；直接调用方是模型测试，以及由普通/static fixture 调用的[测试 helper][main-helper]。 | 测试消费未定义生产触发点、频率或延迟容忍度。 |
| 当前 storage 命令 | [StorageCommand.cpp][storage]调用空间扫描、旧引用关联和报告，没有调用生成模型或 PR250 archive。 | 已有盘点需要没有隐式接入 archive。 |
| 冻结 PR250 archive | [四个显式 API][archive]负责 project、encode、decode 和 model，拥有未验证的 claims。生产实现有内部模型/archive 委托；外部直接 archive 调用位于[往返 helper][archive-helper]和 archive 测试。 | 内部委托和 fixture 断言均未选定产品消费者。 |
| 已有 LINK archive | [已完成 LINK 归档][link-archive]已提供单独的显式文件操作，采用同步 IO，没有默认 CLI 接入。 | 仓库已有归档能力，但这不等于普通生成证据持久化或 codec 成本验收规则。 |

“无调用点”结论限定于静态仓库检查。保留的审查枚举准确 main 和 PR250 tree 中的 tracked
路径与全部匹配符号，分类声明、定义、内部调用和测试，并记录 blob 与 hash。它不发现仓库外
客户端、间接运行时使用或所审记录外的需求。[生成模型合同][generations]和
[archive 合同][archive]也分别明确其当前显式选择范围。

## 3. 新数据之前已有的验收规则

准备身份、严格语义准入和证据完整性可以在数据之前规定，但它们与某个产品使用方式的成本
是否可接受是不同问题。[类型化语义][semantics]约束记录身份、顺序、单位、模式分离和
所有权关系；有效字节不能认证 producer，也不能确立代表性的性能。

| 验收范围 | 当前已有 | 正向产品成本决定仍缺少 |
|---|---|---|
| 准备 | 精确冻结字节与零/null/false 闭合状态 | 执行权限 |
| 固定替身语义 | 冻结 prepared/call 身份、五条有序 observation、严格值与最终完整性规则 | 真实 producer 与有代表性的成本证据 |
| 产品使用 | 保守的显式/非默认限制与当前 HOLD | 所选 caller/触发/频率、支持的输入/平台、所持有对象寿命、可追溯成本要求及可接受/不利/无效证据的处理决定 |

历史登记明确没有数值产品预算。[性能治理规则][governance]确实给出了另一个 external-no-op
HOLD 门槛：在已登记 schema-v3 的 19 场景/四 pair profile 内，配对 candidate-minus-baseline
差值中位数 **>1 ms，且**各配对百分比变化中位数 **>10%**。这是已有且范围明确的准则，
不能转用于纯 codec 纳秒、requested allocation、Linux HWM 或不同操作。不越过该门槛也不等于
完整产品批准。

未来准则必须在新观测之前由预定产品决定和有依据的要求导出，明确指标/单位、操作和拥有结果
寿命、支持的工作负载/环境、理由及结果对应的决定。非数值规则只有确实能回答该成本问题时才
足够。若无法论证要求，就记录其缺失；先测量不能创造要求。

## 4. 数值边界保持原有含义

| 已有数值 | 实际用途 | 不得推导 |
|---|---|---|
| Archive 16 MiB document / 1 MiB field / 8 MiB text；256 records 和 retentions；65536 items/paths；262144 scalar fields；4096 stages；每 target 4095 entries | 逻辑 schema、遍历和选中值准入 [archive][archive] | 总 heap、分配次数、RSS、callback 时间或延迟保证 |
| 其他 projection/inventory 上限 | 各自的 [projection][projection] 和 [inventory][inventory] 遍历/输出合同；native text code units 与 UTF-8 bytes 含义不同 | Archive 成本预算或单位可互换 |
| 非负 signed-64-bit ns；有界整数 allocation；一条 prepared 加 trial 0..4；64 KiB stdout / 8192-byte records | 表示域与证据形状 [semantics][semantics] | 延迟容忍度、稳定 p95/p99 或五 trial 产品阈值 |
| Allocation 关系；bad-tail callback/live-at-return 为零 | 准确语义和 scalar wrapper 寿命不变量 [semantics][semantics] | 累计分配为零或公共 Error 不拥有内存 |
| 固定替身数值与历史 synthetic case 形状 | 字面量合同 fixture 与有限历史覆盖 [preparation][preparation] | 性能样本、代表性最大输入或验收预算 |
| 历史 100 processes / 1340 top-level APIs；child 1 GiB、CPU 20/21 s、wall 25 s；overall 600 s | 已消费研究分配与约束 [registration][registration] | 可退回额度、可接受产品成本或未来矩阵 |
| 闭合 driver 8 calls / 每 call active 3 s / overall active 20 s，以及 metadata/capture 上限 | 固定替身监督与传输上限 [preparation][preparation] | 承载历史工作负载、分批重置预算或完整操作返回时间硬保证 |
| External no-op >1 ms 且 >10% | 已登记默认路径准则 [governance][governance] | 纯 codec 验收 |
| 旧观测值 | INVALID002 内保留的描述性观测 [preparation notes][preparation-notes] | 事后通过阈值或缺失记录的替代值 |

独立验收审查保留了完整的 17 项来源分类。本表选取决定所需的类别，未选择任何新数值，
也未放宽已有边界。

## 5. 七项未来执行阻塞全部保持

完成当前否定性必要性审查，已经回答本接手点，但不会替后续正向执行提案满足第一项 blocker。
[原七项 blocker][preparation]保持原样。

| Blocker | 当前发现 | 以未来独立必要性成立为前提的具体先决条件 |
|---|---|---|
| 1. 独立目的 | 未新选定带有事前结果规则的产品决定 | 消费者/操作/寿命决定，说明新信息如何改变选择、为何保留/静态证据不能决定；否则 DEFER |
| 2. 准确身份 | 历史身份只是参考事实；未选定未来可执行对照 | 与决定对应的 candidate/comparator、源码、binary、compiler/runtime、callback/input、操作/寿命和 invocation 身份 |
| 3. 产品验收 | 缺少有依据的显式 codec 成本准则 | 数据前有理由的要求及 metric/window/workload/environment，明确可接受/不利/无效结果的后续决定 |
| 4. 完整分配 | 未选未来矩阵/全局预算；历史工作负载超过当前 driver | 跨阶段、跨批次的完整顺序/process/API/warmup/control/resource/output/metadata/copy/stop 账本，不能重置预算 |
| 5. 窗口与扰动 | 已知现有局部窗口；未选未来干扰政策 | 把 preparation、API return、caller 析构、formatting/pipes、copy/read/parse/hash、flush/fsync/close 和最终 audit 映射到实际决定 |
| 6. Producer 与监督 | 当前 driver 仅准入固定 Python 替身 | 独立的真实 producer、完整证据绑定、全局监督及准确 child/reap 所有权资格 |
| 7. 审查与登记 | 不存在独立的未来授权 | 实质 blocker 关闭后，独立审查另一份准确协议，并在任何启动之前取得授权登记与回读 |

这是依赖关系，不是待执行计划。本审查未选未来身份、验收准则、矩阵、观察器、正预算、
登记或研究编号，也未实现真实 producer。

## 6. 历史证据与产品边界

001 保持 failed-preflight；002 保持 INVALID。保留 100-process 账本及已消费的 1340 次
顶层 API：100 次先前准备 + 500 次测量进程准备 + 740 次 observation 调用；先前准备既不
重复计数，也不退回。保留记录仍为 199/200 完整流、99 prepared 和 495 observations
（245 time / 250 allocation）。`070-history_256-chain-0.stdout` 仍为零字节，其
prepared 和 normal trial 0..4 仍缺失/null。这些是保留的历史事实，本次未重审全部原始流。
[Preparation][preparation]

Normal time 覆盖 API/wrapper 返回窗口，排除正向拥有结果在 caller 处的析构。Requested
allocation payload 不等于复制字节、总 heap 或 RSS；Linux HWM 包含进程历史和准备。
Capture、复制、解析、hash 和同步有成本，也可能扰动后续调用。主动监督未覆盖部分末端
flush/fsync/close、seal 与 audit。顺序 hash 不认证已加载代码、runtime 真实性或原子快照。
[Preparation windows][preparation-notes]

PR250 保持 Draft/HOLD。Performance944 仅保留既有有限默认路径资格；
819/886/897/907/919/937/944 的失败、HOLD 和不利尾部保持。原 dependency、DLL/LIB/PCH/HU、
Windows/高频、real-wire、bootstrap、writer/atomicity/reparse/concurrency/recovery 及
clean-prune/Rium/#164 缺口保持，#248 继续隔离。Producer identity、current content、
complete producer inventory 和 deletion authority 保持 false。VERSION 仍为 5.6.0。
[Current handoff][handoff]

## 7. 唯一且有限的下一接手点

完成**一份 #198 普通生成证据的消费者、操作与拥有结果寿命决定**，判断跨 invocation 是否
需要、以及如何需要选中的历史 claims/retention。这是完成必要性审查之后的产品设计先决条件。

1. 明确预定 caller 或显式 library 使用合同、触发点、消费结果、调用频率、输入形状/平台及 API 或其组合。
   若不能从既有 #198 需要中选定消费者，就记录 `DEFER`。
2. 说明借用输入及全部拥有的 live/archive/wire/decoded/model 值：哪些共存、谁拥有、
   何时释放、什么必须跨 invocation 传递。列出缺失的 writer/persistence 保证，不假定
   这些保证已经存在，也不授权其实现。
3. 写明备选方案和能够改变选择的信息。定位可追溯的成本要求及其 metric/window/决定后果，
   或记录来源缺失。只有现有记录不能解决该具体要求时，才向决定负责人作一次窄范围澄清。
4. 最终给出 yes/no 建议：一个不同的测量提案是否值得继续静态设计。若没有所选消费者或有
   依据的要求，则记录 `DEFER`，保持 HOLD/BLOCKED，并停止该测量路径；不重做本必要性
   审查，也不虚构阈值。

该接手点不规定研究矩阵、登记、实现或启动。以后任何正向提案仍须满足全部七项 blocker。

## 8. 审查方法与证据

独立产品消费者审查与验收来源审查支持本决定。JSON 配套记录准确来源身份及保留审查 hash。
通过 source/text/Git/JSON/hash 静态检查核对冻结准备、来源引用、原 tracked 文件和双语对；
未导入或执行项目 analyzer、test、runner 或 module。本地发起的项目测试、固定替身启动和
真实工作负载启动均为零；这是所发起操作范围的说明，不是全系统进程普查。

[Walkthrough](20261009-150439-cost-necessity/walkthrough.md)记录本地核验。
准确 head 的最终审查、实际适用 CI、正常 merge 和交付回读随接手证据保留。已有 CI 核对其
枚举的文档对与变更路径分类器；新增双语对需要直接独立审查。本次文档审查不产生功能或性能
通过结论。

[handoff]: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6083356927
[issue]: https://github.com/Iviesever/msvc-quick-build/issues/198
[registration]: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078630647
[registration2]: https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078685272
[preparation]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/ANALYSIS_COST_DECISION_PREPARATION.json
[preparation-notes]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/ANALYSIS_COST_DECISION_PREPARATION.md
[generations]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/ARTIFACT_GENERATIONS.md
[main-helper]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/cpp/tests/e2e/TargetWaveCacheEvidenceChecks.hpp#L116-L137
[storage]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/cpp/src/app/targets/StorageCommand.cpp#L13-L56
[archive]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/docs/ARTIFACT_GENERATION_ARCHIVE.md
[archive-helper]: https://github.com/Iviesever/msvc-quick-build/blob/a26b103685589ab8a5b4df60d1b760ec445efdf5/cpp/tests/e2e/TargetWaveCacheEvidenceChecks.hpp
[link-archive]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/LINK_COMPLETION_ARCHIVE.md
[governance]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/PERFORMANCE_GOVERNANCE.md#L63-L110
[semantics]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/ANALYSIS_COST_SEMANTICS.md
[projection]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/RECORDED_STORAGE_PROJECTION.md#L75-L94
[inventory]: https://github.com/Iviesever/msvc-quick-build/blob/36d5219b5274221bfa6688d100bd5eb6cc70409d/docs/STORAGE_INVENTORY.md#L29-L35
