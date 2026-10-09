# 固定替身分析驱动与冻结协议

[English](ANALYSIS_SUBSTITUTE_DRIVER.md)

## 目标与继承的决定

本切片实施[接手记录6079598410](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6079598410)的下一点：将已合并PR251的采集组件接入新的分析驱动，在启动前冻结协议，并用固定替身检验双管道读取、进程终止、失败前缀和最终封存。

开发基线为 main `d4c0d5f143ea52862329180512caa1cf5ffd0244`，tree `e4cbc65ea1db62d3735397417656b155eff89bf7`。PR250在 `a26b103685589ab8a5b4df60d1b760ec445efdf5` 上继续保持Draft/HOLD。研究001继续为failed-preflight，002继续为INVALID。丢失的原始 `070-history_256-chain-0.stdout`、缺失的五条观察和已经消费的预算保留原有状态。本片不修改这些文件，也不解释其丢失机制。

[书面请求](20261009-114613-analysis-driver/issue.md)与[实施计划](20261009-114613-analysis-driver/implementation_plan.md)界定已接受范围。本文说明驱动合同；实际验收须依据保留的源码身份、测试原输出和审查，不预先声明通过测试数量或CI结果。

## 实现与执行边界

实现分为封闭的[固定替身](../tests/analysis/analysis_fixed_substitute.py)、[管道传输层](../tests/analysis/analysis_substitute_transport.py)以及[协议／驱动层](../tests/analysis/analysis_substitute_driver.py)。它使用不变的[PR251采集helper](../tests/analysis/analysis_evidence_capture.py)，后者的独立合同仍见[分析证据采集](ANALYSIS_EVIDENCE_CAPTURE_ZH.md)。

驱动在Linux上运行受限的Python替身进程。替身只接受随代码提供的fixture模式，并输出预先确定的字节。公开调用计划只提供 `call_id` 与 `mode`，不能提供任意可执行程序、shell命令、输入语料或工作负载。stdin为 `DEVNULL`，`shell=False`、`close_fds=True`，子进程启动于新的POSIX会话。每次尝试调用最多启动一个直接子进程，不重试。

宿主必须在整个运行期间保持 `SIGCHLD` 为 `SIG_DFL`，并让本驱动独占各子进程的回收责任。信号处理器、线程、宿主框架或其他waiter均不得回收它。驱动在协议冻结、运行／源码检查和紧邻 `Popen` 之前检查信号处置。这些检查只观察特定时点的处置，不能证明独占所有权、建立原子租约或强制宿主持续遵守前提。

本片不运行真实codec、MQB、MSVC、ETW或性能研究，对应的全部真实测量预算均为零。替身生命周期通过真实子进程管道检验；其字节数、进程数及监控时长属于合成测试证据，不是研究002的替代观察。

全部原有tracked文件、51份既有工作流、历史fixture及研究证据保持不变。新增文档对独立审查，不添加到既有22对文档checker，也不修改索引以改变CI影响范围。

## 冻结协议与身份

公开API为：

```python
freeze_protocol(*, protocol_id, calls, limits=DriverLimits()) -> FrozenProtocol
run_frozen_substitutes(root, *, protocol, expected_protocol_sha256,
                      seal_path) -> dict
audit_driver_evidence(root, *, protocol, expected_protocol_sha256,
                      expected_driver_seal_sha256) -> dict
```

`calls` 是非空有序列表。每项准确包含 `call_id` 与 `mode` 两个键，调用标识是1至128字符的唯一字符串，模式属于随代码提供的固定替身。`protocol_id` 同样为1至128字符的字符串。`FrozenProtocol.canonical` 保存规范字节，`sha256` 提供其哈希，`document()` 返回重新解码的独立副本。后续修改原列表、字典、限额对象或解码副本，均不能修订已冻结的执行合同。

启动前，协议绑定完整有序调用、准确的期望输出字节身份与stdout记录身份、解析后的可执行程序、argv、cwd、显式环境、采集限额、生命周期限额、显式 `identity_limits` 和零真实测量预算。子进程argv使用解析后的Python可执行程序、`-I -B -u`、fixture路径和一个模式。cwd为fixture所在目录，显式子进程环境仅含 `LANG=C.UTF-8` 和 `LC_ALL=C.UTF-8`。`runtime.reaper_policy` 冻结为 `exclusive-driver-waitpid-sigchld-default`。协议还记录驱动、传输层、替身、不变采集helper及解析后的Python可执行文件的SHA-256。启动前、调用之间及封存前均检查本地源码身份；不匹配不能授权改用替换后的源码执行。

源码文件哈希描述各检查点从指定磁盘路径读取的字节，不独立认证已加载的Python模块代码，也不阻止检查与使用之间发生变化。`development_base` 标识本片起始主线提交，不声称新编写的驱动文件已经属于该提交。后续Git提交身份须另由提交的tree确认。解析后的Python可执行文件哈希也不认证整个解释器、标准库、共享库或内核栈，更不认证输出生产者。

调用者必须独立保留期望协议SHA-256。仅从可变证据包读取哈希，不能建立调用者原先的期望。离线审计再次要求传入该协议和期望哈希，不启动任何进程。

## 固定输出合同

每个内置模式都有有限且确定的期望字节规格。协议在启动前冻结stdout和stderr完整期望字节的身份，包括字节数、SHA-256、LF数量及末尾LF标志，并冻结PR251要求的stdout有序记录身份。输出合同覆盖全部字节，因此即使可解析记录保留了身份字段，修改额外值也不能通过。

| 模式 | 固定替身行为 |
|---|---|
| `normal` | 一条带CRLF的UTF-8 stdout记录；stderr含NUL与LF；退出0 |
| `empty_stderr` | 一条stdout记录及空stderr；退出0 |
| `dual` | 先向stderr写192 KiB，再写四条各含48 KiB载荷的stdout记录；退出0 |
| `nonzero` | 输出固定双流后退出7 |
| `timeout` | 输出固定双流后等待，保持两管道打开 |
| `ignore_term` | 输出固定双流，忽略TERM并等待 |
| `closed_pipes` | 输出固定双流，关闭两管道后继续等待 |
| `busy` | 输出起始固定双流，随后重复追加其stdout记录 |

较大fixture检验管道背压。失败模式有意违反成功生命周期或有限输出合同；其中 `busy` 可输出超过已冻结单个起始stdout块的字节。全部模式仍为固定替身；预期失败依然是INVALID证据，不能因为测试预期该失败就成为成功调用。

驱动比较接收到的字节与已冻结完整输出身份。不变的采集层同时检查NDJSON合同：严格UTF-8、JSON对象、准确有序且含类型的身份、记录数量与LF闭合。两层检查互补。准确固定字节和成功合成进程不能证明业务值、单位、性能等价或真实工作负载合格。

## 创建输出前固定限额

`DriverLimits` 只接受严格正整数；布尔值、非整数、零、负值及超过绝对上限的值均被拒绝。每个默认值同时也是绝对上限，调用者只能调低。冻结协议保留实际采用的策略。

| 字段 | 默认值及绝对上限 | 含义 |
|---|---:|---|
| `max_calls` | 8 | 单份协议的计划调用数 |
| `max_stream_bytes` | 1,048,576字节（1 MiB） | 单个stdout或stderr的已准入字节 |
| `max_total_bytes` | 8,388,608字节（8 MiB） | 跨两个角色及全部调用的已准入字节 |
| `chunk_bytes` | 32,768字节（32 KiB） | 普通管道读取请求的最大值 |
| `call_timeout_ms` | 3,000毫秒 | 单次调用的主动监控截止期限 |
| `terminate_grace_ms` | 200毫秒 | TERM后升级终止前的宽限间隔 |
| `kill_wait_ms` | 1,000毫秒 | KILL后的有界等待／排空间隔 |
| `poll_ms` | 10毫秒 | selector／poll检查点间隔 |
| `overall_timeout_ms` | 20,000毫秒 | 整份协议的主动监控截止期限 |

驱动的协议、调用记录和结果元数据文件各有262,144字节（256 KiB）上限。源码文件身份有16,777,216字节（16 MiB）上限。解析后的Python可执行文件另有67,108,864字节（64 MiB）上限，按最多65,536字节（64 KiB）的块计算身份哈希，不将整个binary保留于内存。这些上限明确冻结为 `identity_limits`。

PR251采用相同的调用数、逐流、总量和块大小限额，`max_plan_bytes` 设为256 KiB，约束其manifest／result。其他限额保持既有默认值：每次调用256条记录、单条记录64 KiB、单条journal行64 KiB、整个journal为8 MiB。所有实际采集限额均明确冻结，并保持在helper既有绝对上限内。

逐流和总量预算只对传输层已准入输入计数一次。spool、raw和backup副本因此额外占用存储；逻辑输入上限并不是三者合计磁盘占用、元数据或进程内存上限。调低策略可能使某个fixture无法容纳，或使元数据无法发布。所选策略内不能完成的协议保持无效，不扩展限额。

这些限额约束控制器监控与证据，不是OS整体CPU／地址空间配额，也不是绝对墙钟返回保证。创建进程、普通文件I/O和 `fsync` 可能阻塞在内核中。合同针对控制器执行检查点的截止时间检查与有界升级终止。

## 并行传输与进程生命周期

传输层在一个Linux selector循环中使用非阻塞stdout和stderr，将两流排入各自排他创建的spool文件；即使数据持续就绪也检查单调时钟期限。两个管道均关闭后仍监控直接子进程；管道关闭本身不等于进程成功退出。

整体期限从创建采集会话之后、调用循环之前开始。单次调用的主动期限从进入传输层开始，早于打开spool和创建进程，并受剩余整体期限约束。调用之间的源码检查与接线工作消耗整体已过时间，下一次启动前会检查该期限。循环内写spool属于主动监控，因已回收且两流EOF而成功退出循环之前也检查期限。末尾spool的flush／sync／close在监督结束后进行，位于该期限之外。`finished_monotonic_ns` 在这些操作之后记录，因此记录的结束减开始间隔可长于主动监控间隔。最终采集封存、驱动发布和只读审计也没有附加调用／整体期限门禁。失败清理由下述独立有限的宽限／KILL间隔约束。

首个传输错误或主动期限到达会锁定失败。直接子进程仍可等待时，控制器向其新进程组发送TERM，等待冻结的宽限间隔，并在需要时发送KILL。在冻结的生命周期界内继续有限排空并尝试回收直接子进程。每次发送信号之前再次检查终止状态；一旦已回收子进程或失去wait-status所有权，即使管道仍有缓冲数据待排空，也不会向旧PID／进程组ID发送信号。信号与回收结果会记录；发送信号本身不能证明已终止或已回收。这些固定fixture不授予任意工作负载的一般后代进程监控或清理保证。

监督器直接通过 `os.waitpid(pid, os.WNOHANG)` 获取状态。只有为该准确子进程返回的终止状态，才会设置其 `returncode` 和 `reaped=True`，随后把同一退出码记录到 `Popen` 对象。包括清理阶段在内，`Popen.poll()` 和 `Popen.wait()` 均不参与监督或回收。`ECHILD` 或非法status会锁存 `wait_status_unavailable=True`，保持 `returncode=None`、`reaped=False`，令调用为INVALID并停止后续启动。两个管道仍可在限额内排空，但缺失status绝不转成假定的零退出。丢失回收证据也不退回已观察的启动计数。

达到逐流或总输入上限时，每个受影响管道最多再读一个字节，以区分恰好EOF与溢出。它以两位十六进制保留于元数据，不纳入已准入字节数和哈希。溢出锁定失败，不能作为更短的成功流接受。失败后，传输层保留另一流可取得的有界前缀，不会无限读取以得到更有利的结果。

每次计划调用最多尝试启动一次。首次调用失败后停止后续启动。创建进程失败记录未知退出码、未回收子进程以及没有真实pipe EOF。非零退出、缺少EOF、超时、终止／回收失败、无法取得wait status、源码或字节不匹配，以及I/O错误均保持失败，不安排重试或替代调用。

## Receipt与PR251接线

传输层先记录已准入字节，再写spool。因此spool短写不能令receipt缩小为与较短保存文件一致。

| 传输receipt字段 | 含义 |
|---|---|
| `byte_count` | 已准入原始管道字节数 |
| `sha256` | 这些已准入字节的SHA-256 |
| `lf_count` | 已准入前缀中的LF字节数 |
| `ends_with_lf` | 已准入前缀是否以LF结束；空输入为false |
| `pipe_eof` | 是否确实将原始管道读取到EOF |
| `capture_error` | 该流记录的采集错误，或null |
| `overflow_probe_hex` | 空字符串，或单个溢出probe字节的十六进制 |

驱动在进入传输层之前开始对应的PR251调用。监控及终止／回收之后，传输层尝试flush、同步并关闭spool writer。驱动随后通过有界普通文件检查读取每份spool，比较其receipt和固定输出身份，再经 `io.BytesIO` 将这些字节逐角色传给 `preserve_stream`。PR251保留单writer合同；驱动不会为两个活跃管道并行调用它。

将截断或中断的spool读至自身EOF，不能证明原始子进程管道已经EOF。传输层的 `pipe_eof` 与生命周期记录独立于PR251源流的 `eof`。即使两个保存前缀都能完整读完，驱动仍将传输失败带入采集闭合。实际创建的文件和前缀继续保留供诊断；创建失败或后期损坏可能只留下不完整的无效证据包。

首错保持锁定。传输层和驱动各层保留有界诊断，最多32项、每项最多256字符；receipt保留该流首个采集错误。这不承诺无损记录全部次生异常。已保存前缀或已回收子进程不能清除首错。

此方案增加第三份副本，并增加有界spool内存读取、解析和同步。这些开销不视为免费；任何后续真实测量协议选择观察窗口之前，都须将其纳入成本考虑。

## 证据布局与外部发布

驱动排他创建新的证据root。root必须尚不存在，其父目录和外部seal的父目录必须已存在。seal路径必须位于被审包之外，且尚不存在。冲突会被拒绝，不覆盖既有证据。后续失败可能留下部分证据包，该包会保留。

| 证据 | 用途 |
|---|---|
| `protocol.json` | 冻结协议的规范字节 |
| `spool/000000.stdout.bin`与对应stderr | 从原始管道读取的有界字节 |
| `calls/000000.json` | 逐调用传输receipt及进程生命周期 |
| `capture/manifest.json`与 `capture/journal.jsonl` | PR251独立冻结的计划与采集生命周期 |
| `capture/raw/`与 `capture/backup/` | PR251为各spool输入保留的原文字节／副本对 |
| `capture/result.json` | PR251最终采集判定及其seal所绑定的字节 |
| `driver-result.json` | 绑定协议、调用记录与采集结果的驱动判定 |
| 外部 `seal_path` | 在包外排他保存的driver-result SHA-256 |

序号从零开始，以六位十进制数表示。仅将 `capture/` 作为root传给PR251。每份已完成调用记录均排他写入，其 `index`、准确 `sha256` 以及已观察的 `launch_attempted`、`launched`、`reaped`、`admitted_bytes` 作为内存锚点保留，随后由驱动seal绑定。期望目录成员通过 `os.scandir` 逐项检查，遇到非预期成员即停止，不将损坏目录的全部条目先分配成列表。

调用记录无法发布时，驱动尝试保全当前采集并抛出 `DriverError`，不会返回暗示缺失记录没有消费启动次数的判定。缺少有效结果或外部seal，绝不代表零执行。证据缺失不能退回启动预算。验收启动账本还使用独立 `Popen` 记录。

| 驱动结果字段 | 账本含义 |
|---|---|
| `lifecycle_counts_available` | 是否存在可信且结构有效的观察锚点 |
| `launch_attempts`、`launched_calls`、`reaped_calls`、`admitted_bytes` | 由这些锚点绑定的观察，即使后续审计发现调用记录损坏仍保留 |
| `verified_launch_attempts`、`verified_launched_calls`、`verified_reaped_calls`、`verified_admitted_bytes` | 从当前哈希、结构及观察值均匹配锚点的调用记录重建的计数 |

驱动seal不可信时，`lifecycle_counts_available` 为false，四项观察计数均为null（未知），不会归零。`verified_*` 计数针对调用记录完整性，本身不证明spool／raw／backup字节完整或传输成功；这些独立检查仍共同决定完整性判定。

驱动封存前，实现重新核对协议、期望布局、全部原始spool、传输记录和PR251证据。驱动结果绑定采集结果的准确SHA-256以及协议和调用记录锚点。结果发布与外部seal均排他写入、同步、关闭并读回，随后再次执行完整只读驱动审计，才可返回成功。发布失败抛错，不返回成功摘要，也不覆盖原seal。

外部同级seal是单独提供的期望，不是经过认证的存储或独立故障域。调用者须定义其协议和seal锚点的保留与信任方式。

## 重新审计与成功规则

调用者必须同时检查 `complete` 和 `integrity_status`。证据成功要求 `complete == True` 且 `integrity_status == "VERIFIED"`。返回SHA-256本身不是成功判定；无效证据也可能发布seal。

成功要求全部计划调用按序完成、直接子进程均成功且已回收、两个原始管道均确实EOF、准确匹配冻结的固定输出身份、原始spool和生命周期记录完整，并通过PR251所有既有完整性门禁。完整backup不能补救损坏的raw或spool。先前内存计数、缓存的 `complete` 或采集层 `execution_finished` 均不能覆盖重新读取发现的失败。

`audit_driver_evidence` 使用调用者的冻结协议及外部保留的协议／驱动seal哈希。它不启动进程，重新读取保存的包和PR251证据，报告当前有效性。它依照传入协议核对保存身份，不重新运行生产者或独立认证已加载模块代码；不恢复backup、不重写元数据、不删除失败前缀，也不将先前无效结果提升为成功。

`VERIFIED` 描述最终顺序读取区间，不建立单一时刻的原子多文件快照、永久完整性、发布／返回时字节未变、经过认证的生产者、永久writer租约或断电恢复。后续使用保留证据时，需要依照外部保留锚点重新审计。Linux结果不建立Windows文件系统或进程资格。

## 本地验证与CI范围

两份驱动测试模块使用真实的内置替身进程与不变的PR251 helper。故障测试修改合成fixture或注入传输／发布失败，不编辑或重跑历史001/002研究。已接受矩阵包括正常与较大双流、非零停止、超时／升级终止、关闭管道后挂起、输入限额、不可变协议／源码身份、目标冲突、短写、最终损坏和发布失败。附加进程边界合同涵盖回收后不再发送信号、缺失或非法wait status、直接waitpid调用，以及冻结／默认SIGCHLD前提。

保留的 `boundary-red-001` 中，alias合同的预检因 `file_limit` 失败：实际Python可执行文件为30,894,944字节，超过初始共享16 MiB身份界。该失败证明启动前文件界问题，不是alias回归的RED。同次执行另用fake child确证了末次回收晚于期限的回归；两项均没有启动实际子进程。后续明确区分source／executable界，保留源码上限，并对既有解释器增加有界流式身份计算，不改变真实测量预算。

完整本地分析命令为：

```sh
PYTHONDONTWRITEBYTECODE=1 python -B -m unittest discover -s tests/analysis -p 'test_*.py' -v
```

登记的验收预算规定：每次完整驱动测试执行最多64次直接替身启动尝试，每份协议最多8次调用、最多20秒主动监控。具名诊断执行采用相同的逐协议上限。保留的验证记录须列明准确命令、前后源码哈希、stdout／stderr、实际启动数和全部首次失败。文档存在命令不等于某次执行已经通过。导入仓内代码及替身子进程使用禁用bytecode策略，避免验证在工作区留下 `__pycache__`。

现有CI不会自动执行这些新Python合同。不变的[Native工作流](../.github/workflows/native-ci.yml)通过既有分类器判断影响范围；已接受的analysis-only范围预期执行classifier／gate，真实native构建及分片跳过。不变的[Documentation工作流](../.github/workflows/docs-ci.yml)检查原22对文档，本对另行审查。非 `perf:` PR题名使[Performance Evidence](../.github/workflows/performance-evidence.yml)比较保持跳过。提交后必须检查实际作业结果和准确checkout身份，不能仅凭绿灯或分支名推断。

本片不新增workflow、不手动dispatch、不重试无关工作，也不改变性能准入。本地Linux替身验收与这些既有CI结果分别记录。

## 后续真实驱动资格与下一接手

本片验收受限的传输／采集接线。真实成本驱动仍需单独审查的协议，明确准确candidate／base／源码／binary／输入身份、输出值语义、观察窗口、存储／复制／解析／sync成本、判别目的、完整新预算、停止规则和最终验收。如果需要实际测量，须先将该具体协议单独登记后再执行。

本工作不分配第三次研究或五次替代trial，不重开001/002，也不批准PR250集成。已有HOLD决定、不利样本、依赖缺口、默认路径资格界及writer／原子性／重解析点／并发／恢复／Rium／#164限制继续附于原证据。

产品的producer identity、current content、complete inventory和deletion authority仍为false。本片不增加默认CLI接入、自动持久化、clean/prune或#248集成，也不发布版本。VERSION保持5.6.0，v5.7.0仍未发布。下一接手须在这些界内决定可单独审查的真实驱动协议，不能将已验证替身包当作真实测量证据。
