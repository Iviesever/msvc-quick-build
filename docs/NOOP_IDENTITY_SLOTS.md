# #819 身份／槽位／顺序诊断：受限真实入口与独立契约

接续 #198 的规格5907289669、#233 的非启动协调层、#234 的元数据修复及
main870验收5928595525。本切片补充 **受限真实捕获模块和 Windows 手动执行入口**。
源码已提供不等于已经审阅、获分配或测量完成；#232／#819 的正式 HOLD 保持。
**不新增研究工作流、不自动启动原 A/B，不提供新的性能资格分数。**

## 组件与版本

- `noop_identity_slots.py`：固定计划、原件准备、执行前校验、完整日志审计；自身不执行 MQB。
- `noop_identity_slots_runtime.psm1`：保留原显式依赖注入、文件轮换和首错停止协调逻辑，没有默认启动器。
- `noop_identity_slots_capture.psm1`：只导出 `Invoke-PinnedSlotCapture`，真实调用限定为原 #819 的两份固定摘要映像、两个槽位、固定顺序和参数。
- `run_noop_identity_slots.ps1`：先验证手动分配和准确源码，再准备、运行、保存、审计；默认拒绝执行。

计划 schema 为2，`native_entry_available=true`，但 `execution_allocated=false`、
`may_clear_hold=false`、`gate_replacement=false`。准备保存以上五份实际依赖源码（含
`external_noop_boundary.py`）；v1历史证据仍使用当时冻结的v1审计器读取，不改写历史日志。

历史特定写入拒绝及当时未知原因继续留在 #235；新的受限实现及其提交/CI结果应单独登记，
不据一次普通写入成功宣布旧拒绝原因已查明，也不借合成控制冒充真实测量。

## 固定范围与准入

只接受原#819 ZIP及两映像的固定大小、SHA256、run/attempt和原HOLD；不重建或修改它们。
公开adapter不接受任意命令字符串或自定义参数，独立推导48行调用顺序并匹配路径、参数、
大小和已核对的槽位摘要。内部私有捕获函数不是公开通用启动器；其模块作用域测试仅运行
固定无害子进程。模块作用域不是恶意调用者隔离/权限边界。

入口要求显式 `ExecuteReviewedDiagnostic`、Windows x64 PowerShell7、该仓库的
`workflow_dispatch`、attempt1、独立disposable标识和匹配分配标签。源码HEAD须等于
`ReviewedCommit` 且所有跟踪文件干净。输出目录固定为 `RUNNER_TEMP` 下的
`<allocation>-<run_id>-1`，必须全新；不接受任意输出根或续跑。

准备与native-preflight再次核对完整固定计划、原件、映像、冻结及当前五份依赖源码，
并要求槽位/夹具/日志目录为空。`host.json` 只记录选定来源字段、QPC频率和捕获协议，
不收集环境变量全集、凭证或进程/网络信息。`audit-native` 要求该来源链及完整48行证据。
分配字符串和环境声明本身不是权限凭证：最终仍须独立核对真实GitHub分配记录和准确run，
审计明确返回 `remote_allocation_verified=false`，不得据此自动解除HOLD。

本PR未新增工作流或执行分配。审阅通过后，另行登记准确驱动版本、一次性工作流/run和
硬job上限，再执行一次；没有合格分配时入口必须在准备/启动之前失败。

## 固定计划，不是已执行次数

两个隔离槽L/R，首轮AA、AB、BB、BA，第二轮BA、BB、AB、AA；原两源文件参数为：

```text
main.cpp helper.cpp --output timing_bench -j 1
```

每块使用新夹具，共用块cwd；第一个prime须2次编译/1次链接，第二个prime须no-op。
四次测量固定LRRL/RLLR。上限8块、48根请求：16prime+32测量，A/B及L/R测量位置各16。
不打开内部计时，不减去自对照，不筛选方向，不构造替代原门槛的中位数。

## 真实捕获与失败边界

PowerShell原生调用使用参数数组、`2>&1` 合流以及外部Stopwatch。
非零退出保留原exit code和输出，不提升为成功；未能启动时exit保持unknown。
捕获错误在数组表达式内部处理，避免丢弃已返回前缀；cwd恢复放在finally中。
字符串转换在外部计时终点之后，不把合流行称为独立stdout/stderr原字节或根进程寿命。
`root_times`、`cleanup`、CPU/子进程数量均保持未知；不启动ETW，不杀全局进程或服务。

沿用原协调层的块前/后哈希、禁写/删除共享读句柄、唯一retired位置和禁止覆盖复制。
原程序PE时间戳和类型名不改；副本文件时间据实记录。四个测量请求之间不增加哈希、清单、
磁盘journal或资源采样；adapter只作内存准入/固定请求验证，不能宣称无扰动重放。

返回值先记录后校验；未返回请求保留pending。首次身份、调用、输出、clock、文件或
journal异常后停止，不补位/重试/删除/续跑。30秒仍是**返回后检查**，不能打断同步挂起。
未来研究须20分钟硬job上限；强制终止可能损失内存中的测量或阻止上传。
4GiB可用空间、256MiB证据是检查点，不是磁盘配额或输出内存硬上限。
这不是对抗恶意写者、断电耐久或所有外部工具副作用的安全协议。

## 契约与真实研究必须分开

原20个PowerShell协调案例仍使用显式假callback、小型文本映像与真实文件IO；不导入捕获
模块，不启动原A/B。完整假日志继续送交真实Python审计器；仅合成原ZIP认证被明确mock。

新增11个PowerShell捕获案例执行5次固定无害PowerShell子进程，核对正常/非零退出、
双流行保留、空输出、Unicode、cwd恢复、启动失败，以及公开接口、48行独立派生、
路径/参数/身份/大小/预算拒绝和入口默认拒绝。没有调用原A/B或MSVC。
这验证了真实子进程IO边界，不等于原程序端到端研究、挂起/容量故障穷举或性能放行。

现有Reporting CI发现Python测试，两平台分别保存 `journal-checks/slot-native-controls/`
及原 `slot-controls/` 证据。缺少PowerShell则明确skip；不把编写完成计作实际通过。

```text
python -B -m unittest discover -s tests/native -p "test_noop_identity_slots*.py" -v
```

完整审计分别展示所有外部包围计时、16个分块相邻R−L对照及AA/BB标签，原决定仍为HOLD。
本工具不能直接解决129TU冷构建、链接风险或显式模型API成本；#819全部不利证据保留。
产品、95原生程序、50份工作流和VERSION保持；v5.7.0未发布，不批准安全clean/prune。

## 后续独立手动工作流

已交付入口的手动接线、首次运行限制、失败上传和契约测试另见
[NOOP_IDENTITY_SLOTS_WORKFLOW.md](NOOP_IDENTITY_SLOTS_WORKFLOW.md)。
工作流的安装与测试不分配原48次诊断；上述入口交付阶段与历史结果不改写为真实研究已完成。
