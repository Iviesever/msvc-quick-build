# 普通目标代际证据的版本化归档

**[English](ARTIFACT_GENERATION_ARCHIVE.md) | 简体中文**

## 范围与接口

`ArtifactGenerationArchive` 拥有普通 EXE／DLL／静态库已完成证据的所选历史声明，
为现有代际模型提供稳定的 v1 字节格式和严格读回路径。这是显式纯值接口，没有默认
CLI 接入、文件 writer、自动落盘、可信生产者登记、恢复或 clean/prune 操作。

公共接口位于 `mqb/orchestration/ArtifactGenerationArchive.hpp`：

| 接口 | 输入与结果 |
|---|---|
| `project_artifact_generation_archive` | 借用已完成普通目标记录、显式保留请求、词法根与路径键，得到拥有型所选声明 |
| `encode_artifact_generation_archive` | 对整批归档测量和严格准入后，输出 v1 字节 |
| `decode_artifact_generation_archive` | 整份字节完成非拥有预检和严格模型准入后，输出拥有型归档 |
| `model_archived_artifact_generations` | 直接消费拥有型声明，输出原完整代际模型和逐源上下文 |

所选请求与诊断类型属于 orchestration，因此编解码器也位于该职责层，不增加
core 对 orchestration 的反向依赖或第二套缓存校验器。`Archived*Completion` 与
`Archived*Claim` 是独立的未认证类型；读回不构造 `Recorded*Result`、`ProcessResult`、
进程参数、环境、计划、计时器或可执行恢复命令。

## 所选事实与共用校验

归档拥有显式历史来源、project/target、可选 generation、保留请求、原根路径拼写、
调用者标签、普通目标记录及源顺序的请求和缓存证据。检查与缓存两份 compiler 身份、
原始不透明签名、全部所选 compiler／LINK／LIB 有序选项、重复路径／选项、已捕获依赖
与包含根，以及每个阶段自己的 cwd 都保留。编译保存错误保留 code/file/offset/message；
编译与终端警告保留 code/path/message。强制重建、完成及保存／复用结果属于诊断，
不加入配方身份。只选择既有普通目标的最终成功编译波次。

真实调用与读回声明共用同一个 `project_recorded_target`、代际准入、配方遍历和
`finish_model` 关联。源／顺序／请求／缓存／终端不一致及真实角色冲突仍使整批拒绝，
保留原 issue、源下标、消息及批内记录下标。不遍历或复制不支持的 module-scan 图；typed 准入
保留原普通投影器的拒绝，wire 中的 module-scan-present 必须为 0。

缺 compiler／generation／origin、重复声明、混合复用、配方不一致及合法快照与
终端的矛盾仍作为原模型问题显示，不从当前机器补上下文，不根据输入顺序选择来源。
快照自身格式非法时不能编码；格式合法但与终端不对应时保留 `snapshot_mismatch`。
LINK 快照不能证明静态归档完成。

## 固定二进制 v1 格式

所有整数都是指定宽度的无符号小端值。文本必须为无 NUL 的 UTF-8；路径保留原拼写，
不规范化也不访问文件系统。字符串由 u32 字节数和对应字节组成；向量由 u32 数量和
顺序元素组成；可选值的首字节必须为 0 或 1，仅为 1 时跟随值。布尔值也只能为 0／1。
没有填充、内存布局转储、校验和或尾随数据；`recipe_evidence` 不进入 wire 格式。

| 文档顺序 | 表示 |
|---|---|
| Magic | 八个 ASCII 字节 `MQBGARCH` |
| 版本 | u32，必须为 1 |
| 文档类型 | u8，必须为 1，表示未认证的普通目标代际声明 |
| 权限位 | u8，必须为 0；任何置位或未知位都拒绝 |
| 词法根 | 路径字符串 |
| 记录 | 下述记录的向量 |
| 保留请求 | project、target、generation 三个字符串组成的向量 |

每条记录依次为 source-id、project、target、可选 generation、目标变体、可选快照。
变体标签 1 表示 LINK 普通目标（EXE／DLL），2 表示静态 LIB。快照使用字符串承载
既有严格 `mqb.link-fact-snapshot` v1 格式，保留其字段、容量及 UTF-8 规则。

下表是固定字段顺序。嵌套向量／可选值采用上述格式；签名依次保存 high-u64、low-u64，
只复制原摘要，不重算配方。

| 值 | 字段顺序 |
|---|---|
| 目标声明 | 目标记录、编译证据向量、源完成向量、any-compiled、终端 executed、终端警告向量 |
| 目标记录 | 可选调用者标签（target、generation）、编译选项、源关联向量、额外 object、LINK／LIB 记录 |
| 源关联 | source、object、依赖 JSON、compile-cache 路径、完成状态、警告布尔值 |
| 编译证据 | 请求、检查 compiler 身份、缓存条目、证据状态、可选保存错误 |
| 编译请求 | 编译单元、选项、cache 路径、依赖 JSON、可选 scan-output、可选 cwd、force |
| 编译缓存 | source、单元类型、compiler 身份、签名、产物、依赖、包含根、module-scan-present（必须为 0） |
| 源完成 | source、compiled、警告向量 |
| Compiler 身份 | compiler 路径、版本、不透明 stamp |
| 编译选项 | configuration、architecture、standard、可选 runtime、LTCG、defines、包含路径、arguments、可选 PCH、外部 providers |
| 编译单元 | source、kind、可选 header-unit 身份、dependencies、module 引用、header-unit 引用、产物 |
| PCH | header 路径、artifact 路径、role |
| 外部 provider／module 引用 | logical name、interface 路径 |
| Header-unit 身份／引用 | header name、lookup；引用另有 interface 路径 |
| 产物 | path、kind |
| LINK 记录 | 完成、缓存状态、LINK cache、LINK options、cache 路径、可选 cwd |
| LINK cache | linker 身份（path/version/stamp）、签名、objects、主输出、libraries、file inputs、side outputs |
| LINK options | configuration、architecture、target kind、subsystem、LTCG、可选 ASan／VCAsan／fuzzer runtime、OpenMP、library directories、library names、arguments |
| LIB 记录 | 完成、缓存状态、LIB cache、architecture、LTCG、arguments、cache 路径、cwd |
| LIB cache | librarian 身份（path/version/stamp）、签名、objects、主输出 |
| 警告 | code、path、message |
| 编译保存错误 | code、file、u64 offset（必须可由本机 size_t 表示）、message |

枚举按照下表顺序使用从 1 开始的连续 wire ID，独立于 C++ 的枚举数值，其他值拒绝。

| 枚举 | Wire 顺序 |
|---|---|
| Configuration／architecture | debug、release／x86、x64 |
| C++ standard | cpp14、cpp17、cpp20、cpp23、latest |
| Runtime | md、mdd、mt、mtd |
| LINK target／subsystem | executable、dynamic_library／console、windows |
| PCH role／header lookup | create、use／angle、quote |
| Unit kind | source、module_interface；普通语义准入仍拒绝模块单元 |
| Artifact kind | object、module_interface、precompiled_header、executable、dynamic_library、static_library |
| Completion | executed、reused |
| Artifact cache state | saved、reused、save_failed |
| Compile evidence state | reused、saved、save_failed |
| 编译／LINK／LIB warning | cache_load_failed、cache_save_failed、file_snapshot_failed |
| 编译保存错误 | file_open_failed、file_read_failed、file_write_failed、invalid_magic、unsupported_version、corrupt_data、replace_failed |

## 限界、错误与拥有性

| 限额 | 最大值 |
|---|---:|
| 文档／单个文本字段 | 16 MiB／1 MiB |
| 累计 UTF-8 文本，包含快照 | 8 MiB |
| 累计向量元素／标量格式字段 | 65,536／262,144 |
| 记录／保留请求 | 256／256 |
| 编译与终端阶段总数 | 4,096 |
| 单目标源关联或编译／结果条目数 | 4,095 |
| 所选路径字段总数 | 65,536 |

Wire 限额与既有模型／投影限额同时生效，归档不能提高任何上限。扫描器先核对完整
外层文档的容量、计数、标志、枚举、UTF-8 和结束位置，再赋值文档字符串／路径、
扩展向量、解码快照或调用路径键。临时 typed 对象不持有输入文档内容。第二遍使用
同一字段遍历生成拥有值，再执行共用语义模型。快照的既有有界解码器仅在外层预检
成功后运行。

编码先测量相同格式，再严格模型准入，最后分配输出字节。真实调用的归档投影先测量
所选 wire 字段并校验原模型，再复制拥有型声明。有界路径 UTF-8 转换和既有快照编码
仍可能使用临时存储。失败不返回部分归档或字节；分配和纯路径键回调异常继续传播。
这些是逻辑限额，不保证总堆、分配次数、峰值／驻留内存或运行耗时。

同一路径在 Windows 与 Linux 可能有不同词法含义；模型比较必须使用兼容的宿主路径
语义和纯路径键。字节格式可移植，不据此推断物理身份或跨平台模型等价。

## 验证与后续边界

原生归档测试覆盖确定性往返、历史拥有性、EXE／DLL／LIB 冷热声明、部分复用／保存
失败、有序选项、逐源身份、来源与快照矛盾、严格错误，以及恶意长度／计数／标志／
UTF-8 和完整夹具的逐字节截断拒绝。旧代际断言保持。既有真实生命周期 helper 仅在
原证据保全后增加内存往返断言，不增加 MQB／编译器／LINK／LIB／观察器调用；新增
原生程序本身仍有正常构建与运行成本，这些测试不构成性能实验。

生产者身份、当前内容、完整库存及删除权限仍全部为 false。合法字节、成功读回或
保留选择都不认证写者、不证明文件存在、不授予删除权。文件发布／原子性、重解析点、
并发写者互斥、恢复、clean/prune 与 Rium 验收仍需独立实现，不借用未完成的 #164。
历史性能失败与不利样本、依赖覆盖缺口及未量化的显式分配／延迟成本保留；本接口
不批准默认／高频使用或 v5.7.0 发布，VERSION 仍为 5.6.0。
