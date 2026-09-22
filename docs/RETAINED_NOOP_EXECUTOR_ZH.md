# 原 #701 计时边界研究的单次执行器

**[English](RETAINED_NOOP_EXECUTOR.md) | 简体中文**

本工具接续 #211 已验收的辅助预检，供单独冻结、审阅和明确分配原有40次诊断。
提交、打开PR、同步、转Ready或合并都不自动启动研究；自动CI只运行合成契约及
Windows解析／解释器别名继承检查，不运行执行器或任何MQB研究。

## 固定内容

工作流 `retained-noop-study.yml` 仅支持 `workflow_dispatch`。只接受原
run35681224762、attempt1、artifact10675079360，4880704字节，SHA256
`940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3`。
下载保持原ZIP，不依赖解压后重打包；原A/B、两份源码归档和前后身份再次核对。
不重建、不换seed、不用当前main程序替代。

原采集器和审计器不改：20个独立夹具、40次调用（20次prime及20次no-op），
含两种方法各四组交替A/B和一组B/B。参数、源码、调用顺序和180/10/5秒等待
继续使用 [原研究计划](EXTERNAL_NOOP_BOUNDARY_ZH.md)。不同模式不合并，不做
跨时钟相减、B/B成本扣除或新旧得分替换。

`retained_noop_executor.pins.json` 固定五份执行关键文件的规范LF摘要。实际
checkout与快照保持原字节，原CRLF只在摘要比较表示中映射为LF。原采集器仍按
其自己的原字节摘要验证快照。清单本身的规范LF摘要必须由审阅记录独立提供；
不能仅相信工作区中一份可以一起被修改的清单。精确审阅提交还覆盖整个源码树，
包括测试、文档和契约工作流。

## 单次准入

真实派发前需正式审阅精确main提交及清单摘要，并在 #198 写明一次分配。
人工输入为 `reviewed_commit`、`manifest_sha256` 和固定标识 `701-boundary-001`。
参数通过环境传递，不插入shell源码。只接受本仓库main上的workflow_dispatch、
与审阅提交相同的GITHUB_SHA／workflow SHA／实际checkout，以及工作流编号1、
attempt1。首次请求在下载前留下记录；第一派发即消耗机会，即使下载、准备或
首个调用失败也不允许第二次派发或rerun补足。不能删除／改名工作流来重置计数。
这些是防误操作条件，不是对能修改仓库或伪造环境的维护者的安全边界。

实现PR及其平台契约通过还不是研究授权。本实现轮不派发；正式合并审阅和新main
验收之后，才能另行冻结最终提交和清单摘要，明确分配首轮执行。

## 执行与失败保留

新入口先核对事件／提交／清单／原输入，再在全新证据目录保存源码、原ZIP、
前置收据和解释器身份。只选择查找顺序中的一个Python Application；不失败后
轮换解释器。本地别名把原采集器的Python检查绑定至同一路径，解释器及保留源码
以只读句柄限制写入／删除。解释器标准库、DLL、runner镜像不是完整隔离快照。

实际执行只有一次对原采集器的调用。第一次异常保留有效前缀及未使用槽位，
不恢复目录、重试或补样本。完成后重新核对原输入、源码、解释器前后身份、
40份逐调用判定与原审计结果，并保存外层收口结果；cause仍为null、clears_hold仍为false，原HOLD不解除。

研究步骤15分钟、整个job20分钟，尽量给后续 `always()` 上传留出窗口；上传整个
`execution-out/`（包括隐藏的夹具缓存、原输入及失败记录）。只读contents/actions
权限用于取得原附件，下载令牌不传入执行步骤，checkout不持久保存凭据。
强行取消、机器丢失或附件服务失败仍可能造成证据缺失；缺失阻断验收，不保证
上传一定成功。原legacy同步等待没有逐调用中断，不宣称任意后代进程都能清理。

## 只读复核

本地原输入检查不启动任何程序：

```text
python tests/native/retained_noop_executor.py inspect-input ORIGINAL.zip --output NEW_IDENTITY.json
```

完整执行后，离线复核也不启动研究；输出必须是新文件：

```text
python tests/native/retained_noop_executor.py audit EVIDENCE_ROOT --output NEW_REVIEW.json
```

离线模式只能核对保存的解释器前后身份，不能重新取得旧runner解释器字节；真实
执行收口还核对正在运行的解释器。日志一致性不证明没有未记录进程、并发干预或
完整原子文件系统快照。检查器不把任何计时解释成根因。

原 #207/#701 的 +3.0963毫秒／+26.70% 硬门槛失败和其他不利样本永久保留。
诊断收集成功不解除HOLD，不批准默认观察、clean/prune、Rium资格或5.7发布。

## 一手工作流契约

[人工派发与ref/commit](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_dispatch)、
[运行编号及尝试编号](https://docs.github.com/en/actions/reference/workflows-and-actions/variables)、
[步骤与job时限](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)、
[固定artifact ID／不解压下载](https://github.com/actions/download-artifact/blob/v8/action.yml)。
