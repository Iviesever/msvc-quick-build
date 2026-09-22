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

原采集器不改；#213已修正审计器对真实汇总输出的误判。计划仍是：20个独立夹具、40次调用（20次prime及20次no-op），
含两种方法各四组交替A/B和一组B/B。参数、源码、调用顺序和180/10/5秒等待
继续使用 [原研究计划](EXTERNAL_NOOP_BOUNDARY_ZH.md)。不同模式不合并，不做
跨时钟相减、B/B成本扣除或新旧得分替换。

`retained_noop_executor.pins.json` 固定五份执行关键文件的规范LF摘要。实际
checkout与快照保持原字节，原CRLF只在摘要比较表示中映射为LF。原采集器仍按
其自己的原字节摘要验证快照。清单本身的规范LF摘要必须由审阅记录独立提供；
不能仅相信工作区中一份可以一起被修改的清单。精确审阅提交还覆盖整个源码树，
包括测试、文档和契约工作流。

## 精确准入：保留001，拟议002

保持同一工作流文件、名称和并发锁。历史 `701-boundary-001` 已是
**consumed_stopped**：run35725316959、run_number1／attempt1，提交
`ac95a9f4efac785af0e3ac225f8e6db16cbf5f7b`。只记录2次baseline／legacy调用、
1次运行时验证，随后因汇总格式误判停止。38个未执行槽位不补跑，旧派发JSON停用。

当前执行入口**只接受** `701-boundary-002`、run_number恰为2、attempt恰为1，
同时核对本仓库、main、工作流路径及审阅／事件／工作流／实际checkout的提交。
不接受001、003、任意>=2、rerun、改名工作流、恢复目录或补样。人工输入仍为
`reviewed_commit`、`manifest_sha256`、`allocation`，通过环境传递，不插入shell源码。
请求在下载前写入；下载或准备失败也消耗本次机会。

002还必须取得001原始未解压失败附件：run35725316959／artifact10693057419，
16641257字节，SHA256
`d75b31e472439426f5cff42abf4f0213b86f140b310ea3e3ae7723a1906da0a7`。
核对旧请求、内外stopped记录及9份调用文件，不执行归档代码、不重判旧call02。
原ZIP复制为 `previous-001.zip`，采集期间只读固定，收口时再次校验。schema2的
收据和结果单独保留历史已调用2次／运行时通过1次；不能用摘要替代缺失原件。

计划仍为20个全新夹具、最多40次**新**调用，连同001累计最多42次已记录研究调用。
不混合新旧样本、不额外预热、不追加标准ABBA／ETW或调整环境；首错停止，
不会自动进入003。这些是日志记账与防误操作条件，不证明没有未记录进程，
也不是对能修改仓库的维护者的安全边界。

**本准入实现不等于002分配或派发。** 先审阅补丁、适用CI及最终main／清单，
再在 #198 单独登记一次准确执行机会。未来派发前确认同一工作流只有历史001；
已有其他运行则读取该记录而非再次启动。源码变化须重新审阅，不能只替换输入SHA。

## 执行与失败保留

新入口先核对事件／提交／清单／原输入，再在全新证据目录保存源码、原ZIP、
前置收据和解释器身份。只选择查找顺序中的一个Python Application；不失败后
轮换解释器。本地别名把原采集器的Python检查绑定至同一路径，解释器及保留源码
以只读句柄限制写入／删除。解释器标准库、DLL、runner镜像不是完整隔离快照。

实际执行只有一次对原采集器的调用。第一次异常保留有效前缀及未使用槽位，
不恢复目录、重试或补样本。完成后重新核对原输入、001原ZIP／收据、源码、解释器前后身份、
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
