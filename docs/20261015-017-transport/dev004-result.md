# 017：跨会话后代回收开发验收收口（dev004）

2026-10-15。接续 #198 / 6195109086 的独立 hosted 开发登记。
**修复已提交、真实五用例与原始附件验收通过；正式 103B 观察仍未执行。**

## 执行与原件身份

- 执行源码：`a3324609a926ef546b395147589a445a025b9d8b`。
- 执行树：`938ddddfc36a8e2c0a2aeff64e0a6140078a4a9b`；parent 为 d808591…。
- 工作流：`017 writer-scope isolated dev004 (single run)`。
- run：`38756055122`，run_number=1，attempt=1；job：`116494658954`。
- 工作流及全部步骤 completed/success；不是 skipped 或旧提交结果。
- 原 artifact：`11983207844`，名称 `bcrypt-writer-scope-dev004`。
- ZIP：115,102 B，74 个唯一安全成员，CRC通过。
- ZIP SHA256：`c646944d58157e636246f407fd8b9812b01f96ee0e3df80c814b3767729d22d8`。
- source manifest SHA256：`e950b8e9f12fe6f1b066578f610a70fbce7da1c33b96e2e456278b358d54e286`。

实际宿主为 ubuntu24 / X64，镜像 `20261012.334.1`；Python 3.12.3，
解释器 SHA256 `e50d468e8b0adfb05733f5b87b3cff34829c4a8c1aea50c865aa8bdfe4bb150f`。
没有套用本地 Python3.13 的身份。28份归档源码逐字节匹配冻结清单。
这不是完整宿主依赖图、loader闭集或全仓库语义审计。

## 实际验证

本地及此 Linux CI 分别执行同一套 **258项单测，0失败/0跳过**；不相加成516个
独立方法。新增8项在原scope上有5项失败，修复后的同一断言全部通过。

| 固定case | 根退出码 | 被接管后代回收 | pidfd SIGKILL | 结果 |
|---|---:|---:|---:|---|
| clean | 0 | 0 | 0 | 正常通过；另完成六文件读回 |
| joined | 0 | 0 | 0 | 后代由直接父进程等待；范围干净 |
| orphan | 0 | 1，退出-9 | 1 | 正确拒绝遗留后代，完成清理 |
| double | 0 | 1，退出-9 | 1 | 双重fork/跨session后代被接管并清理 |
| timeout | -9 | 1，退出-9 | 1 | 根超时被终止，跨session后代也被清理 |

这一run只运行1个smoke supervisor、5个Popen根、5个显式fork。
三个遗留后代均真实wait回收，共三次pidfd定向SIGKILL；另有timeout根的直接kill。
所有scope均确认ECHILD、恢复自身策略，cleanup_failed=false。
负向用例仍为refused/SCOPE_ORPHANS，没有被清理成功升级成正常观察。

supervisor退出0且被外层回收，总耗时364.822256ms；timeout组合耗时209.579203ms
包含清理/保存，不将200ms配置说成精确OS硬返回承诺。无第二次CI尝试。

clean的六项原件逐一长度/摘要读回通过，target是测试自产bytes(range(103))，
不是原始目标。process-exit.json的output_closed仍false，关闭后的返回另记true，
没有重写预关闭候选。所有writers_quiescent/observation_accepted/clears_hold保持false。

## 失败历史仍保留

本地dev003确实启动3个根/2次fork，在orphan处停止；未被回收的后代随后写出了
27B标记。独立原语诊断又运行1个supervisor/1个短fork child，定位自身children
接口ENOENT，而pidfd可用。这些与hosted-dev004分开计数，不替换或删掉原件。

实际修复只有scope的6行前置能力检查；原回收算法、上限和五用例smoke未放宽。
CONFIG_PROC_CHILDREN是可选接口；当前容器不可用于这一完整后代控制。
不能对缺接口环境增加猜测PID兜底，也不要在这里再跑同一组失败工作负载。

## 下一接手点（不重复dev004）

后代范围控制和六文件有限读回的**开发级真实验证已完成**。不要重写它们、重复
提交旧补丁、重跑已消耗dev004或重新调查已定位的children缺口。

下一项是固定bootstrap与完整BoundObservation集成：在具备已检查procfs/pidfd
能力的独立监督器中绑定真实源码/模块/loader和两份独立准备，控制非后代文件
写者/FD外传前提，保存owner-final与独立退出回执，最后接evaluate_completion。
本轮只验证了scope/transport和合成六文件，不声称完整正式bootstrap已贯通。

源码复核、实现及验收由同一会话完成，没有第二位独立审阅者签署。实际103B摘要
仍null；原图像、GNU/Windows/MQB/MSVC/ETW、正式准备/观察均0。017整体和5.7.0
发布尚未完成；main、PR250/248/207、VERSION5.6.0、HOLD/DEFER和清理删除权限不变。

运行：https://github.com/Iviesever/msvc-quick-build/actions/runs/38756055122
登记：https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6195109086
