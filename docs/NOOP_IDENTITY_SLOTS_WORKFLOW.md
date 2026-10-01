# #819 身份／槽位诊断：独立手动工作流

本页描述 `.github/workflows/noop-identity-slots-study.yml` 的接线与审阅边界。
入口实现及捕获限制见 [NOOP_IDENTITY_SLOTS.md](NOOP_IDENTITY_SLOTS.md)。
工作流安装、契约测试和合并都不是执行分配。原 #232／#819 仍为 HOLD。

## 唯一一次手动执行

只有 `workflow_dispatch`，没有 push、PR、schedule、workflow_run 或可复用工作流入口。
手动参数 `execute_reviewed_diagnostic` 默认 false。准入要求同时满足：

- 当前仓库、main、准确工作流路径，`GITHUB_SHA`、`GITHUB_WORKFLOW_SHA` 与
  `reviewed_commit` 三者一致且为40位小写提交摘要；checkout 再核对实际 HEAD 和干净的跟踪文件。
- GitHub 托管 Windows X64；已有 PowerShell 7、Git、Python，入口再次检查平台。
- 工作流运行编号和 attempt 均为1，固定标签 `pr232-slot-001`，显式执行开关为 true。
- 本次 `RUNNER_TEMP/pr232-slot-001-<run_id>-1` 不存在；不续跑、不覆盖、不清理旧结果。

提交本工作流不会自行调度上述执行。审阅通过并合入main后，维护者须先在 #198／#232
单独登记准确提交、固定协议、预算、标签以及唯一首次运行，执行后绑定真实 run_id。
`pr232-slot-001` 是预留标签，不是目前已经分配的实验。工具不读取平台审阅记录；
`remote_allocation_verified=false` 明确保留，环境和标签检查不冒充远端授权认证。

工作流文件须在默认分支上才接受手动事件，因此不能通过触发本诊断来测试PR。
**首次运行即使因为开关为false、下载失败或准入拒绝而停止，也消耗这个工作流的首次机会。**
后续run和rerun拒绝，不通过更改编号、标签或门槛补跑取绿；异常后的方案须重新独立审阅。

## 原始输入与调用边界

下载器固定原 #819 的 run `36682255779`、artifact `11083055313`，保留压缩原件不直接解压。
复用原 `noop_identity_slots.py.check_archive` 验证完整大小、SHA-256、CRC、成员安全、
run／attempt、原HOLD、源码与A/B程序身份。下载失败或原件过期即停止，不改用新构建或其他包。

原件10,237,432字节，SHA-256：
`34295afd020d1ff140b053630347f9b39004f9de1b743387864bdc9f211ffcd8`。
A／B均为1,933,824字节，摘要分别为：
`48fc85fe1777b142599e24673928bc719dc77ef635a19e757f033945337b0bea`、
`55aff284fcd1cf09a7446c689754a40b3ad51a249f64fe709709e53ec5033af0`。

Python包装器只调用现有 `run_noop_identity_slots.ps1` 一次，使用参数数组而非shell命令拼接。
该入口继续掌握准备、预检、固定两个槽、8个区块、16次初始调用和32次测量，最多48次根MQB调用。
其五份源码依赖与原协调器均不修改；没有内部计时、ETW、额外预热、重建MQB或默认build接入。
外层Git来源记录和最终审计均位于测量窗口之外。

运行出口先检查真实入口退出码，再用同一冻结审计器重新读取完整日志，并与入口保存的audit逐项比较，
最后核对run_id和reviewed_commit。退出码0、不为空的文件或“上传成功”均不能独立代表诊断完成。
完整结果仍为 `diagnostic_recorded_unreviewed`，不生成新性能资格、噪声判断或解除HOLD的决定。

## 失败与保留

准入前先以禁止覆盖方式保存允许列出的请求字段，不转储整个环境或凭证。
checkout使用子目录且不持久化Git凭证；仅授予contents/actions读取权限，三个Actions版本固定到提交摘要。

`slot-execution-out/` 保存请求、准入、原输入ZIP、完整源码ZIP、实际工作流、入口调用意图、
外层stdout／stderr、结果及成功时的审计读回。研究的原始文件直接保留在本次固定RUNNER_TEMP目录。
调用意图不是MQB进程已经创建的证明。非零退出、无法启动、审计失败都失败退出并保留前缀；
保存结果再次失败时不覆盖首个异常。没有重试、删除、日志归一化或伪造未来调用。

最后一步使用 `if: always()` 上传这两个固定范围，包括隐藏的 `.mqb` 内容；禁止覆盖已有artifact。
上传路径不来自任意用户输入，不包含整个checkout、整个RUNNER_TEMP或凭证目录。
下载或准备失败时也保留已经生成的请求。未开始的入口没有伪造的wrapper成功结果。

作业硬上限20分钟，调用步骤上限15分钟；为通常的后续上传留出空间，但这不是上传保证。
入口30秒限制依然只是返回后检查，4GiB／256MiB依然只是检查点，不是磁盘配额或内存输出硬限制。
同步挂起、进程终止、runner丢失、磁盘故障或硬job超时可能丢失内存样本并阻止最后上传。
平台的取消／超时处理不被本工具声称为完整子进程清理或崩溃恢复证明。

## 契约测试与历史保护

`python -B tests/native/test_noop_identity_slots_dispatch.py` 执行工作流中实际的两段Python，
仅替换平台环境、Git、原件认证及外层子进程依赖为明确合成控制。不会触发Actions、运行原A/B或MSVC。
覆盖默认拒绝、每个必填字段、重复run／attempt、源码漂移、下载异常、非零退出、未知启动状态、
审计不完整、错误来源、禁止覆盖和日志二次失败。上传接线及超时为结构检查，不冒称实际超时／云端上传测试。
现有Reporting CI自动发现这些测试；它不调用新的手动工作流。

历史检查继续验证原50份工作流的完整指纹。只有新增的这一份文件按准确名称及完整摘要单独识别；
缺失、修改或额外工作流仍会被拒绝。五份已交付入口依赖另有精确摘要检查，旧native断言不削弱。

## 平台语义依据

- [GitHub工作流语法](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)：
  手动输入、权限、作业／步骤超时与并发配置。
- [GitHub默认变量](https://docs.github.com/en/actions/reference/variables-reference)：运行编号与重试编号。
- [download-artifact固定版本](https://github.com/actions/download-artifact/tree/3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c)：
  固定artifact ID、跳过解压及摘要不符时失败。

这些平台说明不证明原48次研究已经执行。VERSION仍为5.6.0；v5.7.0尚未发布。
