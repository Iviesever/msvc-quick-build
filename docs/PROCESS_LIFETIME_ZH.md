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

## 原始平台资料

[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) 说明
后代继承、嵌套 Job、accounting 与 kill-on-close。
[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
规定创建时 JOB_LIST 和属性数据生命周期。下一里程碑是项目执行所有权与取消传播，
不是直接开始 IPC。
