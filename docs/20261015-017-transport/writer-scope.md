# 017：跨会话后代范围与六项原件读回

2026-10-15；基线 `14112b620ee3783630c69a1ff2e9cd57d0fc0bca`。
本次新增两个实现、一个单测、一个显式smoke入口及本文。旧22文件工作集逐字保持。
不重写OwnerTransport，不执行旧016，不分配正式103B目标观察。

## 实现与适用前提

`DescendantScope` 只可在**独立、新鲜、单线程、无现存子进程**的Linux监督器中使用。
启动前检查自身task、默认SIGCHLD、waitid和pidfd支持，拒绝已启用的subreaper。
只对本监督器设置PR_SET_CHILD_SUBREAPER并重置已经为默认的SIGCHLD状态；不探测
宿主进程清单。作用域须在旧OwnerTransport的Popen之前激活。

OwnerTransport处理直接根进程之后，再接管并回收孤儿后代。setsid/双重fork不改变
这条祖先接管关系。waitid使用Linux __WALL覆盖非SIGCHLD clone子进程；WNOWAIT
避免先释放PID身份。只读取自身task的children（最多257 B检测溢出），以pidfd确认
可等待的子进程身份后发送信号，没有数字PID/PGID扫描兜底。

已退出的被接管子进程直接wait，不再发信号；活子进程每个句柄最多一次SIGKILL。
信号失败仍尝试之后的wait，不反复发信号。保留实际wait退出码。若存在别的waiter、
线程变化、PIDFD归属不明或策略漂移，拒绝且不对猜测进程发信号。

**只有waitid(P_ALL)的ECHILD才是空后代域证据**；None/children文件空/管道EOF都不是。
句柄尚未完成wait却出现ECHILD也拒绝。只有再次检查空域后才恢复本实例的subreaper
状态。即使所有残留后代最终被清理，发现残留这一事实仍使该次观察失败，不转为成功。

## 组合与读取

`ScopedOwnerTransport` 复用原直接进程传输。原输出原件不覆盖；新的范围结果单独返回。
原transport的wall_ms必须≤14000，另保留1秒scope drain，并检查整个组合仍少于16秒。
每次drain最多256轮、16个pidfd取得/信号尝试和16次wait回收。不是创建进程的硬限制，
也不是对同步系统调用/文件系统操作的OS硬返回保证。超过预算保留未确认状态，不扩容重试。

`read_artifacts` 只在该实例真实完成干净drain后开放，前后重新检查空域。读取旧backend
目录恰好五个文件，再读取外层的owner-final，共六项，采用既有逐项字节上限。拒绝目录
重定向、符号链接、硬链接、特殊文件、额外成员、大小变化与路径/描述符身份变化。
这些检查不是对非后代文件写者的原子快照；不把空后代域等同于全局文件写者静止。

subreaper不是权限沙箱：不防止FD向外部进程转交、ptrace、恶意本机代码、外部写者、
监督器死亡或未资格化的native加载。因此writers_quiescent/observation_accepted仍false，
暂不生产bcrypt-outer-completion-017-v1，也不绕过已有evaluate_completion的真实回执要求。

## 本轮测试与失败保留

原225项 + 新25项 = **250 PASS / 0 FAIL / 0 SKIP**（本地Linux/Python）。
新单测的prctl/wait/pidfd/信号均为替身；有限文件读取的成功和拒绝路径在真实临时目录执行。
包括活子进程回收、僵尸不误杀、回收归属丢失、线程策略变化、晚出现子进程、截止/数量
上限、信号失败后回收、原件变更和规范JSON。不能将其说成真实跨session验收。

真实开发记录严格区分：

| 登记 | 实际运行 | 结果 |
|---|---|---|
| #198 / 6192664494，dev001 | 1次普通supervisor，0个受测Popen/fork | 首次SCOPE_THREADS拒绝；后续case未运行 |
| #198 / 6192692584，自身条件对照 | 2次无子进程解释器 | 普通启动有多个task；-I -S启动单task并通过原检查 |
| #198 / 6192716165，isolated-dev002 | 1次supervisor，1个clean Popen，0个fork | 根0退出/reap、空后代域及策略恢复成功，但测试JSON非规范，transport正确拒绝 |

dev002保留139 B stdout、空stderr、全部退出/范围记录；**不是5-case通过**。orphan、double、
joined与timeout未运行，没有任何真实pidfd signal或跨会话回收结论。四个监督/条件解释器
与一个受测Popen分别计数，不与上一轮4个开发child合并成正式观察资源。

smoke错误是json.dumps默认空格不符合原canonical JSON协议。新增测试先在旧smoke输出
得到JSON_CANONICAL拒绝（25项中1 ERROR），改用既有encode后同断言通过；五个固定短
程序也只做编译与fork/sleep预算检查。旧源码、RED/GREEN及两次真实失败原件全保留。
没有放宽原协议或线程断言，没有第三次实际smoke。最终源代码的真实smoke仍待验证。

## 准确接手点

本轮代码可供独立审阅，不宣称已获得第二位审阅者签署。先审阅修正后显式smoke的
固定isolated argv与源码，另行登记一次有限开发验证，完成尚未运行的跨session路径；
不重用dev001/dev002目录或未用名额，不能把离线修正当作原失败已变绿。

随后把已存在的BoundObservation接到固定bootstrap，并绑定真实source/module/loader、
独立准备与非后代写者资格，保存owner-final，取得范围终止后的六项原件和外层退出回执，
再连接现有evaluate_completion。不要再重写本轮已实现的后代drain或旧直接传输。

实际图像/103 B目标读取、GNU/Windows/MQB/ETW、正式准备/观察均0。
main、PR250/248/207、VERSION5.6.0、HOLD/DEFER、清理删除及release边界保持。

## 接口依据

- https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html
- https://man7.org/linux/man-pages/man2/waitpid.2.html
- https://man7.org/linux/man-pages/man2/pidfd_open.2.html
- https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html

这些文档支持接口语义，不替代本实现的真实跨会话验证和独立审阅。
