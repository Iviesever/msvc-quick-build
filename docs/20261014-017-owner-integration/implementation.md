# 017：受导入检查约束的执行主体与原生／证据桥接

2026-10-14。基线 `b2417b7ff8b486f61460bba70a1662573ee3fcf1`。
本次新增两个实现模块、两个测试模块和本文；原导入组件与旧测试不修改。
**这是 prepared-target 执行主体的实现与合成联测，不是正式观察的准入或已执行记录。**

## 已实现的路径

`GuardedPreparedOwner` 把既有 `ImportBoundary` 接入一条单次执行流程：
激活检查 → 预留记录 → 两份已准备字节的一致性/摘要检查 → 前置身份校验 →
保存只读目标 → 一次子进程启动 → 有界等待/捕获 → 连续覆盖校验 → 后置身份
校验 → 子进程资源释放 → 候选报告保存/输出关闭 → 最终导入检查和自身 finder 移除。

目标几何固定为 RVA/file `[0x1117C,0x111E3)`、103 B；不接受任意范围。
`primary` 与 `independent` 必须是等长的不可变 bytes 且完全相同，摘要必须等于
显式提供的冻结值。**这只检查已交付输入之间的关系，不证明两个准备真的独立，
也不认证其原图像、五份元数据、pdata/unwind或作者签署。** 正式输入摘要仍未提供。

`verify('before'/'after')` 是必须显式提供的可信身份校验回调，正常返回只能是 None；
返回 True/False 都拒绝，不提供默认为成功的实现。完整入口仍须把它绑定到既有
017预检、两份准备回执与实际来源/加载资格；任意调用方给出的空回调不是批准。

## 原生与证据接口

`PreparedNativeBackend` 提供具体 Popen 配方和旧边界接口的桥接。调用者必须在
导入检查前预加载并核对如下既有函数；本模块不执行旧014/016 owner：

| 依赖文件／用途 | 本轮读取基线的 Git blob |
|---|---|
| `bcrypt_disassembly_io_014.py` 的 OutputDirectory | `44dbe51fee51b242630266ac0b51004477f7e3c7` |
| `bcrypt_disassembly_model_014.py` 的有界 encode_json | `485cb70a2f62525c540ef365d7ef70b5b5696355` |
| `bcrypt_disassembly_listing_014.py` 的 parse_listing | `8a60ce70b64308538e0fc83fddd1548d85e1a6ff` |

它们通过显式 factory/encoder/parser 参数传入；函数身份和传递模块字节必须由
外层冻结，不能把依赖注入自身说成来源验证。本轮新测试用有限替身验证这些接口，
没有重新执行或独立验收上述旧文件的真实磁盘实现和GNU解析行为。

桥接层只把派生 `target.bin` 交给绝对路径工具。18项 argv 固定 binary/x86-64/
Intel 格式、原始字节及 `[0x18001117c,0x1800111e3)` preferred VMA，不是ASLR地址。
关闭shell和默认环境继承；固定C locale、UTC和空DEBUGINFOD_URLS。stdout/stderr
重定向到旧边界拥有的普通文件，不使用可能无限缓冲的 communicate()。

准备的目标重新从拥有的输出读取比较后才启动；原始capture只读回核对，不截断、
重写或删掉超限/失败内容。已打开的输出由sink唯一关闭，child.close只结束适配器
自身的使用期，不冒称另有原生句柄关闭证据。没有对用户目录的清理操作。

## 失败、回收与结尾

Owner对象不可复用。启动预约在调用launcher之前计入；launcher异常或返回无句柄
保留 `launch_outcome_unknown`，不捏造已创建/未创建的OS进程结果。外层必须处理
这种未知；不得重试或从已花费的槽位开始另一观察。

拿到child对象后才做下一次checkpoint，所以该次检查拒绝仍会进入唯一回收路径。
先poll；已reap就不再发信号，否则一次kill和一次独立1秒cleanup wait。信号失败
仍尝试wait；poll未知则不向猜测PID发信号。未reap不读取或哈希可变capture。
已reap的非零退出/超时输出仍保留；capture尝试失败不在finally再次读取。

直接子进程reap不证明所有后代/继承写者终止，`descendant_cleanup_verified=false`。
killpg仅针对新会话根，仍依赖受控单线程、默认SIGCHLD和无外部回收者；它不建立
完整后代清点。异常、kernel阻塞、宿主丢失和强制终止都可能使回收/保存不完整。

首次失败原因不被后续关闭成功或另一保存错误覆盖。报告另记录cleanup/persistence/
capture失败以及未确认的关闭。错误原因只使用有限代码，不输出异常正文或宿主清单。

**落盘的 report.json 永远只是候选。** 它产生于输出关闭和最后的导入检查之前，
不能证明后续关闭成功。返回结果还必须由独立外层回执结合clean owner exit确认；
候选文件存在、close成功或本地测试绿都不能替代。保存后发生导入变化、超时或关闭
错误时，返回refused，不覆盖先前候选来伪造成功。失败日志所需模块必须预加载。

## 候选资源边界（不是新预算分配）

主体协作式12秒；cleanup wait独立1秒。子进程候选限制：CPU2秒、AS256MiB、
单输出文件131072 B、core0、64个描述符。捕获验收严格小于文件上限；报告编码
上限65536 B；最多103条、每条1–15 B、汇编文本最多384字符，逐条核对原始字节
和连续RVA，不能用一个形式合法的字符串代替真实解码证据。

原生上限回调仅供受控单线程Popen preexec调用，不在父进程执行。Python文档警告
preexec_fn在有线程时可能死锁；本次未做实际运行/加载观察，不能据此宣称当前
Python/工具适合这些限制。创建进程和同步文件调用也不受协作式检查的硬中断保证。
调用方原先已分配的输入、旧函数临时分配、解释器RSS与最终外层保存均须另行核算。

## 已执行验证与审阅身份

原91项加新33项生命周期和25项原生桥接控制：**149 PASS / 0 FAIL / 0 SKIP**。
包括实际执行新core+bridge的完整合成联测、启动后导入拒绝、超时/未知回收、非零
退出、capture边界、真假退出码、连续覆盖错误、保存/关闭异常和最终候选失效。
Popen、killpg、set/getrlimit、旧文件存储和旧parser均用替身，真实原生子进程0。

首版33项测试记录10个失败（含subtests）：阶段名滞留使准备和deadline拒绝被误标
为OUTPUT。保留首版源码/日志，以同一断言验证修复；之后再补三项关闭/持久化
控制。原91项文件的完整字节保持。本轮没有全仓库/Windows/CI或真实GNU验收。

上一会话的导入组件做了源码复核；新代码是本轮作者实现/测试，**没有第二位独立
审阅者签署**。全部source/module/loader/original-lineage/observation资格仍为false。

## 准确下一接手点

主体和原生/证据桥接不必再重写。下一项是**外层准确准入与回执**：把五份固定元
数据、两份独立准备结果、源码/模块/loader闭集接到verify和固定依赖；预加载所有
成功/拒绝/保存路径，设计并独立审阅其完整资源与调用契约，再分别登记受控合成
运行和唯一正式103B观察。没有可直接使用的正式CLI、默认allowlist或自动workflow。

本轮没有读取原图像/实际目标，实际目标SHA256仍null；主准备/独立准备/正式观察
均0。不重跑016、不进入018，不重传受阻宿主清单，不变更main、PR250/248/207、
VERSION5.6.0、HOLD/DEFER、清理/删除权限或release。完整017和v5.7.0仍未完成。

## 技术依据

- https://docs.python.org/3.11/library/subprocess.html ：进程创建时限、preexec线程风险、等待与回收、文件/PIPE捕获边界。
- https://docs.python.org/3.11/library/resource.html ：候选RLIMIT语义，非本次实际资格证明。
- https://docs.python.org/3/reference/import.html ：cache优先与meta-path边界。
- https://sourceware.org/binutils/docs/binutils/objdump.html ：binary target、VMA及显示选项，非机器码运行证据。
