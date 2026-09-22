# 保留的外部 no-op 计时边界诊断

**[English](EXTERNAL_NOOP_BOUNDARY.md) | 简体中文**

## 范围与执行授权

这是针对 #207 原 #701 证据的独立诊断，不是产品优化、重测得分或 HOLD 豁免。
原 +3.0963 毫秒／+26.70024178525% 仍是硬门槛失败，
[性能治理](PERFORMANCE_GOVERNANCE_ZH.md)中的判定器不变。

必须先审阅和冻结采集器，再单独分配执行。自动工作流只运行 Python 合成测试及
Windows 语法／辅助函数检查；不启动 MQB、诊断、MSVC 或 ETW。
打开、同步、转 Ready、合并这个工具 PR 都不会启动40次诊断；脚本存在不等于授权。

## 固定输入与计划

只接受原 #701/run35681224762、attempt1 的 ZIP：
`940472d871e47aa01be0347f7bf86ac3d158c0247d929cfeda4fcc6c3b4f6ca3`。
核对原前后身份、两份源码归档及实际程序。A 摘要为
`9c23cde3450a7c10c2edca75838923601c9a4fd300617928b04263d128d19b58`，
B 为 `76b55ca6176eb816131dad7de624170bcc3b21631b13190f0e943034d00655a9`。
不重建、换种子或使用替代程序。

派发前在全新目录保存原 ZIP、程序副本、采集器源码及完整固定计划。
两方法×四组交替 A/B 配对×两侧×一次 priming 加一次 no-op，共32次；每方法一组
B/B 对照再加8次，总上限40次、20个独立夹具。按配对奇偶交替两侧及方法次序。
每夹具只有原 timings 场景两段源码，UTF-8 无 BOM、CRLF；参数固定为
`main.cpp helper.cpp --output timing_bench -j 1`。
无额外预热、内部计时调用、运行产物、自适应重试或补样本。原基准中夹在中间的
开启内部计时调用没有复现；这是此前登记的两调用诊断，不是假称重放历史条件。

## 分开的计时边界

邻近 legacy 的方法保留 PowerShell `&`、合并文本行和 Push/Pop-Location。
QPC 分开记录外框与原生调用／捕获包围层，没有子进程句柄、PID 或 OS 生命周期，
对应字段保持 null，不能补零或伪称纯进程时间。

Process 方法明确工作目录和 ArgumentList，不使用 shell；启动后同时读取两条
字节管道，再等待退出，保存启动返回、等待返回和排空边界。
根进程退出后，通过持有的进程句柄查询 GetProcessTimes 的创建、退出、内核与用户
时间。FILETIME 的100纳秒单位与 QPC 分属两种时钟，禁止跨时钟相减。
根 CPU 是根线程之和，不含全部子进程，可能大于墙钟；生命周期也不是纯应用 CPU。
原 stdout/stderr 字节分别保存；替换解码只用于 ASCII 诊断前缀检查。
legacy 合并文本不是逐流原始字节。

Process 方法等待根进程180秒、管道排空10秒；失败时单独记录尽力终止自身进程树
及5秒根等待。根退出不证明全部后代退出。同步 legacy 没有逐调用中断，后续执行器
必须设置固定 job 时限并保留未结束的 started 记录。文件／等待上限不保证任意
文件系统操作或输出增长的时限；不修改服务、杀毒软件、机器策略或进程优先级。

## 失败保留与审阅

调用前保存输入／文件清单及 started；原退出、错误、输出先于后快照与语义验收保存。
运行期间只读持有两份程序，重查源码、计划和采集器摘要。no-op 必须有三项 up-to-date、
没有编译／链接／内部计时，观察的文件字节和元数据保持不变，并与此前 prime 后状态一致。
这是语义、输出和文件系统检查，不是完整子进程追踪。

首次身份、派发、退出、快照、计时/API或写入错误就停止余下计划。有效前缀不算完成，
不补未运行槽位，不覆盖或恢复旧目录。Python 审计只给同方法配对和摘要，cause 保持
null、clears_hold 保持 false；B/B 单独保留，不扣成本。
快照、散列、Python 检查、查询和磁盘捕获都可能扰动测量，不证明原子快照或生产者独占。
某种方法、顺序或新结果变绿，都不能自动解释或替换 #701。

仅在另行明确审阅分配后执行：

```powershell
./tests/native/collect_external_noop_boundary.ps1 -ArtifactPath ORIGINAL.zip `
  -OutputRoot NEW_DIRECTORY -ExecuteRegisteredStudy
```

不执行 MQB 的复核，输出必须是新路径：

```powershell
python tests/native/external_noop_boundary.py audit NEW_DIRECTORY --output NEW_AUDIT.json
```

## 一手 API 契约

[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
定义根进程创建、退出、CPU及100纳秒单位。
[RedirectStandardOutput](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processstartinfo.redirectstandardoutput?view=net-10.0)
说明重定向管道的死锁和并发读取要求。
[PE 格式](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
用于独立保存的静态段／导入检查；静态差异本身不能解释计时根因。
