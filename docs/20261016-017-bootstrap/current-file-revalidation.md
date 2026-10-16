# 017：已冻结候选的当前宿主文件复验

接续0056916d…及#198/6218746374。候选仍为49116 B、SHA256
`8a5206cd8a1947030a6e4b11139fea4de748b0def0f418bda5b5df541e800f38`。
不重新选择文件，不用现场摘要替换原根，也不把历史inode当作新宿主身份。

## 本项实现与边界

在已有`check_bcrypt_entry_binding_017.py`增加`verify_current_files`；原纯值
`review_binding`逻辑不改。完整核对四份外部固定字节根及原件关联后，才能接触
候选中的路径。首先核对实际解释器路径、固定环境及原生单线程条件，然后用原
`Budget/snapshot`完成恰好两遍已声明文件，前后复核16个别名和22个缺失路径。

每遍前拒绝不是普通文件的主路径或错误长度，包括符号链接。每次snapshot校验
完整内容、明确EOF、文件描述符与路径状态；两遍还要比较这个新宿主自身的状态。
别名不许换目标；缺失路径使用lexists，即使是断链也拒绝。0 B包仍打开、计数、
请求EOF，不将不存在当作空文件。既有正式入口、绑定schema、空包例外均不改。

共同协作deadline20秒；每文件仍受原64 MiB cap；总内容按固定候选两倍限额，
成功时256个完整file pass、256次EOF、36214558 B。失败时`declared_content_bytes`
只表示原Budget已分配的声明量，不冒称短读前已实际读取全部字节。第一次差异
即停止；保留失败阶段和文件标签，不序列化任意异常正文，不删除/改写宿主文件。

结果成功名称是`CURRENT_RUNTIME_FILES_MATCHED_NOT_QUALIFIED`。只证明本次
有界检查期间的指定文件/别名/缺失条件，仍不证明原子快照、所有执行分支的实际
模块加载、非后代写者或FD外传、独立准备、人类签署和正式观察资格。所有对应
资格字段保持false，没有目标路径参数、准备回执生成器、decoder或exec操作。

## 一次性托管验证

复用既有bootstrap workflow，只有直接parent为0056916d…、attempt1的提交
执行一次新文件复验；不同parent写明skip，不重复旧入口inspect和dev004–008。
保留主线不变，不新增永久实验workflow或手工Rerun。同一Ubuntu24.04、Python3.12.3
以及已登记解释器摘要，否则停止且不启动文件复验进程。

候选没有放入公开源码。工作流以actions:read从原artifact12054111874/12051460385
下载两份固定ZIP（总331037 B），校验其原SHA256/大小/唯一安全成员/CRC；仅恢复
两份entry JSON和toolchain-before JSON，再按已冻结排序恢复候选。**恢复后必须
逐字节匹配原49116 B摘要才准继续**；这是旧artifact的确定性恢复，不是从当前宿主
生成新清单或授权。候选变化即拒绝，没有择优拼接或第二种排序重试。

GitHub API凭据只发给api.github.com。302的HTTPS签名下载使用无Authorization的
新请求；限定两个下载，每个压缩≤1 MiB，展开≤4 MiB，timeout20秒，无自动重试。
在owned的专用证据目录保存原ZIP、恢复的原字节及起止记录。真正文件复验在一个
-I/-S/-B解释器里运行，子环境只有原C/UTC四项，无GitHub令牌；父超时30秒，step
2分钟，job5分钟。同步I/O不能由合作检查保证硬中断；超时不是成功且不追加运行。

## 测试与审阅身份

新增22项：真实临时文件两pass、空文件EOF、缺失/增长/同长漂移、同字节换inode、
符号链接与断链、别名漂移、历史inode不应跨宿主强等、环境/解释器/线程/deadline、
EOF计数、interrupt及返回不共享输入。新进程和历史资格没有在这些单测中被伪造。

首版新代码漏将既有ImportError子类Refused纳入结果出口；初始测试保留15条error，
其中4条来自根拒绝测试错误期待ValueError。修正准确异常类型与对应期望后，同样
的拒绝条件全部保留，不扩大为吞掉任意BaseException。日志和初版在用户包保留。

本次作者自审/测试不是第二位独立审阅者签署；不生成writer authority或真实目标
SHA。若宿主不匹配，保留固定候选并停止，不自动更新摘要。若匹配，下一步使用
该来源复验结果继续writer-domain依据及两份独立准备的审核/绑定，而非重跑此项。

接口依据：Python3.12 os.lstat/lexists及文件描述符语义；GitHub REST Actions
artifact下载的302响应。该文档不是这些系统接口的额外安全保证。

## 已执行结果：当前宿主文件复验已收口

执行前登记：#198/6219699712。实现/执行提交：
`f8ee708b6023a7a66d701ce5acbbae0ab6569a8c`，parent为0056916d…。
workflow386262890，run38931001405/run_number7/attempt1，job117074659767，
所有步骤与run均completed/success；UTC 2026-10-16 16:19:13–16:19:28。

**原426+新增22=448项单测，本地和CI均PASS、0错误/失败/跳过。**
随后真实启动一份文件复验解释器，使用原Python3.12.3二进制摘要与固定四项环境。
它不是模拟snapshot：128个当前宿主文件实际读两遍，256完整pass、256 EOF、
36214558 B；16别名和22缺失路径在边界复查通过。两遍当前状态摘要同为
`42ca83f81861ae0d2f0e818e245f819e461ed1e158c0eaa8633312d7a1beaa98`。
它仅是该进程的状态集合摘要，不把历史inode与新宿主inode强行相等。

实际结果为CURRENT_RUNTIME_FILES_MATCHED_NOT_QUALIFIED，failure_stage=null，
current_files_verified/interpreter_identity_checked=true，其余资格标志仍false。
父记录exit0、reaped=true、无timeout/retry，stderr为空；264178524ns仅为耗时记录，
不是产品性能分数。入口inspect写明different_parent、entry_invocations=0，没有
重复之前实际入口检查，没有GNU或原目标执行。CI控制解释器、单测和git parent
读取是额外基础设施操作，不混入这一个文件复验进程计数。

原artifact12059145721，516735 B、61个唯一安全成员；SHA256：
`73e888a8e9622498b438ca4e55fbe6531844fbbc3e78481a56f6ba6cbf71b68d`。
CRC、43份归档源码、448条逐项ok、head/workflow_sha、launch/exit、两个pass和
completed记录均已核对。两个旧ZIP确实原样下载保留，三份JSON和49116 B候选
逐字等于上一交付；不是自行修改候选来适应新宿主。系统库payload不上传，离线
核对的是源码与记录关联，不把当前工作容器当作CI宿主再哈希。

**本项不再需要重写、重新采集或单独重跑。** 下一接手点转到尚缺的真实writer-domain
审核依据、两份独立准备回执与完整request的绑定，在同一受控执行生命周期里接通
现有前后身份检查和controller外层退出。禁止仅凭此文件匹配把非后代写者/FD外传、
完整动态模块闭集或人类独立审阅标为已完成，也不复制临时runner的资格到下一宿主。
目标/原图像读取、正式准备和观察仍0，main/HOLD/DEFER/删除/release不变。
