# 017：固定启动入口与完整控制器连接

2026-10-16；接手基线 `a6c37c1100da946ef5d7c02f91759c3d5a531acf`。
此实现连接已经存在的 BoundObservation、ScopedOwnerTransport、原六文件读回和
`evaluate_completion`，不重写它们，不重跑已消耗的 dev004。

## 已接通的路径

`bcrypt_observation_controller_017.py PLAN ABSOLUTE_TRUSTED_SHA` 读取外部登记的
plan 根，核对五份历史元数据、两份准备回执、资格和写者权限回执。正式命令固定为
`python -I -S -B /.../bcrypt_observation_controller_017.py /.../plan.json SHA`。
没有 `-c`、`-m`、任意脚本字段、任意 verify 回调或自动生成模块白名单的正式入口。

plan 严格八字段：schema/request/primary/independent/authority/bootstrap/
transport_dir/receipt_dir。五个输入引用是完整 path/bytes/sha256；三个输出目录
（含 request 的 backend 目录）互不包含、不得覆盖输入、别名或 absence 路径。
primary/independent 仅接收已经分别准备并准入的派生103B文件；不读取原图像、不
在启动时产生目标摘要。根摘要由可信账本提供，不能给未信任输入现场算个哈希充当批准。

控制器先核对声明的解释器和全部运行文件字节，再使用固定 bootstrap 的七项
完整 argv（解释器、三个隔离选项、脚本、plan路径、plan摘要），cwd 为固定脚本目录。
在新监督器中建立原 descendant scope，使用原 pipe/read/wait/kill 有界传输；
bootstrap 固定导入已有 OutputDirectory/parse_listing，构造原 PreparedNativeBackend，
调用原 BoundObservation，原来两轮身份检查、首次失败、候选与最终值规则全部保留。

bootstrap 正常关闭后仅向stdout输出完整规范 owner-final，成功才返回0；外层实际
观察退出/reap及后代域结束，再保存并读回六项原件。解析结果不能替代进程退出。
本次不新造 parser 或文件存储实现；旧014组件仍是显式固定依赖，旧014/016 owner不执行。

## 写者回执不能由运行方自我声明

writer authority 必须在有信任根的 plan 内被完整摘要固定，并关联同一 request、
原 qualification 摘要、相同 reviewer 和准确三个输出目录。它必须明确审核如下前提：
独立单监督器、独占本地输出命名空间、没有非后代文件写者、没有FD外传、没有外部
进程修改。缺少、换目录、换 reviewer、只给 true 或修改任一项都在 launch 前拒绝。

这些前提是外部可信审核事实，不是本代码对整个宿主的检测，也不是签名验证。
当前没有这样的正式回执，本代码不生成它。只有此前提加上当前真实干净scope、
退出后的六文件读回及再次身份核对，才可产生条件性的 writers_quiescent=true。
`scope.json` 同时保存 authority 摘要及其“外部前提”属性；不把subreaper说成权限沙箱。

## 结尾和保存

三个输出目录分别归 backend、旧 transport、controller 所有。controller 新目录只容许
reservation/scope/completion/assessment/failure 五个固定JSON；独占创建，逐个fsync和
读回，拒绝额外名称、符号链接/硬链接/特殊文件、路径与描述符变化及超长内容。
controller reserve 在 launch 前完成；失败不删输出、不覆盖旧候选、不复用本对象。

`completion.json` 来自观测值，不复制 owner 自报的进程退出。原 evaluate_completion
再核对原件、候选与最终差异、两轮检查的计数、103B连续覆盖和可信引用。
成功仍仅为 `CONSISTENT_TRANSPORT_REQUIRES_RESULT_REVIEW`。

controller 后续落盘、目录关闭或最终scope检查失败，返回refused；已经保存的候选
不覆盖。返回值含 `requires_clean_controller_exit=true`，其最终包含进程退出仍须外部
独立保存与核对，不凭候选 assessment 或 JSON 的存在批准正式观察。

## 有限资源与尚未证明之处

plan≤16384B；request≤262144B；单份资格/准备/authority≤32768B；两份派生输入各103B。
每次 detached load≤1MiB、协作2秒，14个文件（plan/request/bootstrap/五metadata/三receipts/
authority/两份target）。单文件先查长度再读取；无界 read_bytes()、枚举宿主目录或loader探测不存在。
controller加载、bootstrap加载、controller结束重读分开计数，最多三次（3MiB）；控制
源码检查每次另≤3×32768B。两轮 controller 运行文件检查每轮≤96MiB、4秒；它们是
原 owner 两轮≤96MiB检查之外的新工作，不冒称已经包含在旧读量里。

总controller协作截止仍16秒、原子传输最多14秒、原scope drain最多1秒；到期拒绝，
并不保证这些各项最坏耗时之和能成功。进程创建、磁盘/fsync与内核阻塞不可由这些
检查硬中断；正式宿主仍需外层隔离/资源监控和事先独立审阅，不复制旧限制为新证明。
此次没有改变原子进程的CPU/AS/FD/输出cap，没有实际分配正式执行预算。

bootstrap依赖先固定预加载，再由既有ImportBoundary核对完整预声明模块集。加载的
文件字节、已声明模块元数据不等价于实际native loader闭集；源目录在隔离启动前须
由可信控制器冻结。本实现不会从当前sys.modules自动“学习允许全部”的名单。

## 验证与修复记录

原258项不变，新增34项，**292项本地PASS/0FAIL/0SKIP**。新集成测试实际运行
bootstrap→BoundObservation→原owner/backend，再经过真实临时文件的六文件读回和
原 completion 核对器；原图像、元数据事实、解释器/模块清单、Popen、scope内核事实
和旧存储/parser均是明确测试替身，不是正式机器码解码或真实端到端多进程验收。

首轮符号链接已拒绝，但原OSError未转换为稳定拒绝码，原日志1 ERROR保留并修正。
补充反例另得到3 FAIL：解释器变更和依赖变更在启动owner前漏检，及首次保存错误
漏记persistence_failed。已增加真正的prelaunch完整字节pass并修复保存状态；同一
断言通过。原源码、失败日志和最终结果留在交付包。没有改旧258项断言。

专门的CI只运行明确列出的单元/合成集成测试；不调用旧dev004、check_*smoke、GNU、
Windows、MQB、ETW或真实scope激活。源码和结果保存在该次artifact，CI结果以实际
run/head/原件为准，源码提交本身不是CI通过。

## 下一接手点

固定bootstrap和完成控制器已有可调用实现；不要重写这条连接。接下来由独立审阅
核对本次新增启动路径、preload闭集、外部写者依据和完整资源，然后提供有可信根的
真实资格/准备输入，在已验证能力的独立宿主登记一次受控端到端验证。实际目标摘要
仍null，正式准备/103B观察为0；没有自动生成计划/资格或缺资料时自动降级的模式。

main、PR250/248/207、VERSION5.6.0、HOLD/DEFER、原失败证据、清理删除及release
保持。本轮作者/测试者是同一会话，没有独立审阅者签署；017和v5.7.0尚未完整验收。

依据：https://docs.python.org/3/using/cmdline.html （-I/-S/-B 与脚本目录）；
https://docs.python.org/3/library/subprocess.html （固定argv、timeout及preexec限制）。
