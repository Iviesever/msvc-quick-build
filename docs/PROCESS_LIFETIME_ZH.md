# 请求拥有的进程生命周期

**[English](PROCESS_LIFETIME.md) | 简体中文**

## 范围

这是 [v6.0 路线 #164](https://github.com/Iviesever/msvc-quick-build/issues/164) 的 M1a 前置能力，
不是 daemon、项目锁、新 CLI 参数或 Ctrl+C 实现。VERSION 保持 5.5.0，本轮现有 CLI 调用者
不会传入取消 token。缓存、freshness、调度和生成程序的运行行为没有改变。

## 契约

`process::ProcessSpec::cancellation` 接受可选的 `std::stop_token`。默认不可取消的 token
保留原来的根进程生命周期策略，不创建额外 Job、取消事件或 callback。可取消的 token
显式选择由本次 `run()` 拥有正常 CreateProcess 后代树：根进程退出后，即使没有请求取消，
也会终止仍存活的受管后代再返回。这不适用于故意保留后台子进程的应用；前台 `--run`
必须与受管构建工具的生命周期策略分离。

预先取消且结构有效的请求不会启动进程，而是返回取消结果；无效参数仍返回验证错误。
运行中的 stop 请求设置该调用的私有事件。当取消事件赢得根进程/事件等待时，结果为取消；
两者同时就绪时取消优先。正常根进程完成已被选中之后的 stop 不一定改变结果。
`ProcessResult::termination` 区分 `exited` 与 `cancelled`，Windows 取消退出码是非零的
`ERROR_CANCELLED`（1223）。已捕获的部分输出仍被排空并保留。初始化、等待、读取失败
继续返回 `ProcessError`，不能伪装成成功取消。正常非零退出码仍然是普通退出结果。

## Windows 生命周期边界

每个可取消调用创建未命名、不可继承的 Job，设置 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，
不允许两种 breakaway。通过 `PROC_THREAD_ATTRIBUTE_JOB_LIST` 在进程创建时完成归属，
而不是让进程先运行再 Assign。现有 stdio 继承白名单保留，Job 和取消事件不传给子进程。
无 capture 请求同样获得 Job，但不会被偷偷开启输出捕获。无法建立所有权时直接失败。
该可选后端要求 Windows 10 / Server 2016 或更新版本。

Callback 的生命周期先于事件句柄结束；同一 runner 的并发调用各自拥有句柄和 callback。
取消或正常根进程退出时，先取得 Job 当前成员的进程句柄并核验归属，再终止 Job、逐个
等待进程句柄 signaled；还必须满足 active process 为零、从清点到收尾的总关联数不变。
首次 Release 测试证明 accounting 归零可能先于后代句柄 signaled；原测试仍在返回后做
零超时句柄检查。消失或重用的 PID 不能成为终止无关进程的授权。
若收尾期间发生新的关联、不能验证身份，或超出 65,536 成员的有界清点限制，则返回基础
设施错误，而非成功取消。错误路径的 Job 关闭只是尽力收尾，调用者不能据此认为写 lease
可以安全转交。不把 completion-port 消息或队列为空当成唯一证据。只等根进程或等待 Job
句柄 signaled 同样不足以建立这一边界。
仅收尾使用的 1 ms 查询等待不是常驻 idle 轮询器；内核终止没有确定的耗时上界。

Owner 异常退出会关闭私有 Job 句柄，由内核请求终止关联进程树。这不能证明替代客户端
可以立即安全接管磁盘写入，也不是缓存事务或针对恶意进程的安全沙盒。通过服务/WMI
创建的工作和其他非普通 CreateProcess 后代，不属于本契约。项目锁与崩溃恢复接入时
必须继续明确这些限制。

## 验证与证据

现有 Windows runner 测试程序兼作自己的有界 helper，没有新增产品翻译单元或原生测试
可执行文件。原 argv、环境、输出与并发测试全部保留，新增：正常受管退出、预先取消且
不启动、真实启动失败、有/无 capture 的运行中取消、两路各 128 KiB 输出、后代持有管道、
根先退出而后代存活（重复十六次，不放宽返回后立即检查句柄的断言）、同 runner 并发
取消隔离、owner 被终止，以及重复调用句柄计数。
测试以 ready/release 事件同步；用存活时取得并保留的进程句柄验证终止，而非重用 PID。

测试的五秒完成检查和有界 helper watchdog 用于暴露失败，不是对任意 Windows 内核
终止耗时作五秒保证。必须在精确 PR 源码上完成 Windows Debug/Release 与原有独立 ABBA。
默认路径兼容测量不是取消吞吐测量，本轮不宣称性能加速。

## 共享编译器 PDB 研究与产品回归门槛

5.6 阶段边界**不等于**完整 M1b 研究路线。公开 CLI 仍通过
`ProjectSetup::normalize_native_parameters` 和 `MsvcParameterEngine` 处理
配置、profile 和 CLI 的编译参数。`CompilerArgumentBuilder` 在两种配置中
都选用 `/Z7`；参数注册表将 `/Zi`、`/ZI` 标为不支持，并保留 `/Fd` 的所有权；
编译调用会删除 `CL`、`_CL_`。这是已有行为，不是为了避开失败而更换参数。
外部对象、库、PCH/IFC 输入和底层 API 调用者**不全部属于**这一输入边界。
链接器 `/DEBUG` 仍可能生成最终 PDB，它不等于夹具共享的编译器
`compiler.pdb`。参见 Microsoft 的[调试格式契约](https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170)。

相对地，`cpp/tests/platform/windows/msvc_service_ownership_probe.cpp` 自行构造
`/Zi` 或 `/ZI`、`/FS` 和显式 `/Fd.../compiler.pdb`。它的 `compile()` 经
`invoke()` 直接调用 `WindowsProcessRunner::run(cl.exe)`，不经过公开参数准入
或增量缓存检查。scheduler-drain 变体还显式使用
`BoundedWorkScheduler::run_with_admission_stop`。核心进程和调度器测试仍是
必要门槛；直接调用这些原语不证明公开 CLI 支持共享编译器 PDB 取消或安全
转交写 lease。Microsoft 明确说明，[/FS 不能阻止所有并行 PDB 错误](https://learn.microsoft.com/en-us/cpp/build/reference/fs-force-synchronous-pdb-writes?view=msvc-170)。

### 保留失败，不改称实验成功

[Default Endpoint #37](https://github.com/Iviesever/msvc-quick-build/actions/runs/35311336307)
完成 70 个场景，其中三个在 `A/work0` 真实失败：`pch-debug-A-started-drain`、
`pch-release-A-started-drain`、`modules-release-A-started-scheduler-drain`。
各自保留 C1041、退出码 2、`cancelled=false` 和原 `/FS /Zi /Fd` 参数。
产物 `10534120991` 的 SHA256 为
`ded51533c9dd889f2bdbdc08840a621157428bfa3816f92b38ecb9cd52c1d6b1`。
B 成功和外层清理成功不能修复 A 的编译失败。更早的
[Default Endpoint #35](https://github.com/Iviesever/msvc-quick-build/issues/164#issuecomment-5709658468)
B 侧失败及后续观测继续分开保留。#194 的宽泛 `cpp/**` 入口额外触发了
70 个默认端点、42 个私有端点场景，这是[已经承认的执行分配错误](https://github.com/Iviesever/msvc-quick-build/issues/164#issuecomment-5725666783)，不是其 86 次产品调用预算的一部分。

对于 [#194](https://github.com/Iviesever/msvc-quick-build/pull/194) 的精确 include 根
改动，探针及直接失败调用路径均未修改，也不执行被改动的比较函数。
这支持将这些观测归为**尚未解决的 M1b 研究依赖**，而非该比较改变了 PDB
行为的证据。这不是根因诊断、排除所有 C1041 的证明，也不是追认成功。
#194 仍须明确的当前审查和全部适用产品、性能门槛；此范围决定本身不批准
该 PR 或 5.6 发布。#172、旧 private129 和 ABBA 旗标、冷构建尾部均未清零。

### 后续执行分配

`msvc-default-endpoint.yml` 和 `msvc-service-ownership.yml` 保留原真实工具的
构建、采集、诊断与上传 job 内容，但这些 job 使用无条件为假的 job 级守卫：
**当前真实工具执行分配为零**。PR 事件或手动 dispatch 都不能再获得一次
70/42 场景运行。两条工作流继续自动执行范围检查，Default Endpoint 继续
执行原采集器的合成故障注入契约。每次运行使用独立且不互相取消的并发组。
`scope.json` 明确区分范围验证成功与 `research_executed=false`、
`research_passed=null`、`historical_failures_cleared=false`；矩阵跳过不等于通过。

后续研究须在 #164 单独登记并审阅源码变更：锁定源码和工具身份，提出可区分
的假设，使用一次性 run/attempt 准入，限定场景、构建和捕获预算，不替换
失败样本，保留完整原始诊断。优先最小判别场景，不自动重放完整矩阵。
范围测试锁定归档 job 内容，拒绝放宽守卫或验证器；任何新分配须同时审阅
这些测试。这是工作流执行控制，不是阻止维护者改源码或手动运行采集器的
安全边界。

Native Debug/Release、公开参数和 freshness、进程生命周期、无写入 inspection、
自举、打包、安装器及独立 ABBA 门槛均不变。未来支持公开编译器 PDB、受管
MSVC 取消或写 lease 转交时，须重新审阅该边界，并在宣称能力前解决 M1b
证据问题。不得静默换成 `/Z7`、删除 `/FS`、杀共享服务、降低失败判据，
或把后一次绿色结果当作问题解决。最终累计验证与独立 VERSION/release PR
仍然必要。

## 原始平台资料

[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) 说明
后代继承、嵌套 Job、accounting 与 kill-on-close。
[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
规定创建时 JOB_LIST 和属性数据生命周期。下一里程碑是项目执行所有权与取消传播，
不是直接开始 IPC。
