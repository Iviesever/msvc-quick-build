# 017 writer-domain：启动描述符交接与同 UID 写者边界

基线 `2cf6bde63de06e527cc79cc4f09928982ae592a0`；执行前登记 #198 / 6220936431。
本项不是新文件清单/正式准备/观察，也不生成 writer authority。生产入口、运行模块、
原448测试及候选保持；新增有限测试与审阅材料，复用原启动和证据存储实现。

## 已审阅的源码边界

| 前提 | 现有可核验依据 | 不能从中推导的结论 |
|---|---|---|
| 已知子进程的唯一监督器 | `DescendantScope` 启动前单线程、初始ECHILD、subreaper；原dev004有跨会话回收证据 | 不是宿主级进程隔离，不排除root或同UID外部管理者 |
| 独占创建输出 | backend的`OutputDirectory`、`RetainedTransport`和`ControllerRecords`创建新0700目录，O_EXCL/O_NOFOLLOW文件，保留失败前缀 | 0700/0600不排除同UID路径访问，目录不存在也不证明父目录的未来独占 |
| 额外描述符不经exec继承 | 两个现有Popen显式`close_fds=True`，没有pass_fds；stdin为DEVNULL，stdout/stderr是指定管道/捕获文件 | 不等于禁止sendmsg、/proc访问、任意原生FD导出或外部进程写入 |
| 结束后才读回 | 原直接根wait/reap、后代drain和六项固定原件读回；controller保存前后身份校验 | 不是原子锁，不证明非后代写者或特权外部修改不存在 |
| 审阅与完整来源 | `BootstrapInputs`要求外部固定根、两份准备、资格和同request的writer authority | 自己填写JSON或不同人员名字不是独立审阅/准备，不得现场自认证 |

本轮审阅没有发现需改写上述生产路径的继承参数。以实测确认它们的有限保证，
并保留“即使继承正确也可被同UID路径访问”的反例，不能将两个问题混为一谈。

## 有限开发验证

`check_bcrypt_writer_handoff_017.py`显式执行入口只能在隔离/无site/不写pyc的
专用Linux解释器中使用，证据目录必须不存在。程序创建两个自有哨兵FD，故意设置
为inheritable。子程序只对这两个编号做fstat，比较dev/ino/type，不扫描/proc/fd，
不把“编号被复用”误认为同一个文件仍然继承。

| 顺序 | 固定操作 | 预期 |
|---|---|---|
| 1 | close_fds=False，直接toy进程 | 两个同一文件身份均被识别，证明对照有检测能力 |
| 2 | 原OwnerTransport，固定toy argv | 两个额外FD均未继承；其原传输结果/退出记录保留 |
| 3 | 原PreparedNativeBackend，完整原GNU argv和限制 | toy程序忽略解码参数，只检查FD；两FD未继承，真实存储/捕获和关闭路径工作 |
| 4 | 另一个同UID、close_fds=True的toy路径写者 | 新0700目录内的0600哨兵仍可被该进程改写；它不是前述已退出owner的后代 |

第三项输入是源码构造的102 NOP+RET、103 B，只作适配器必要的合成数据，既不读取
原图像也不解码/执行这些机器码。程序是受限Python shebang脚本，不是GNU objdump；
`parse_listing`虽为原依赖传入，明确不调用。不得称为正式decoder验收。

四个进程不是四次正式观察。第1/4项是敏感性/安全边界对照，“预期可继承/可写入”
通过不构成生产允许泄漏。当前两个生产进程是第2/3项的实际启动路径而不是mock。

## 固定资源与失败保留

最多四个不fork的toy子进程，逐一停止即结束，不重试或补位。每次3秒等待、失败
仅对自己已取得的进程对象一次终止及1秒等待。backend保持原CPU2秒/AS256MiB/
文件131072 B/64FD限制；诊断输出读回≤4096 B。父协调器20秒超时，step2分钟、
job5分钟。同步I/O及强制失去宿主仍可能妨碍完整退出/保存，不能声明OS硬返回保证。

只修改本次新建目录内的自有哨兵，不改变用户文件或公共权限，不清理/删除证据。
固定C/UTC四项环境，不传GITHUB_TOKEN。旧三个014依赖和两份新增源码一并校验/归档。
既有workflow仅在准确parent及attempt1运行一次，后续提交记录跳过；旧inspect、
文件复验和dev004–008不重跑。常规单元测试没有调用实际执行入口。

## 验证与准入判定

本地448项原测试加12项新控制全部通过。新控制是纯值/临时文件/mock进程：覆盖
原始FD身份、编号复用、bool/int区别、异常关闭与终止后等待、独占输出和捕获上限。
真实四进程结果必须读取本提交CI原件后追加，不能从单测推断。

可接受的成功状态仅为`HANDOFF_CONTROLS_MATCHED_NOT_WRITER_QUALIFIED`。
`no_descriptor_export_proved`和`writer_domain_qualified`始终false。本项之后不应
再将启动继承和非后代路径写者混用：前者由两个调用点及对照验证；后者须在正式
宿主生命周期中有明确的受信supervisor/runner/root边界或真正的隔离机制，再由
独立审核固定其依据。不得把0710/0700、close_fds或新VM单独当作完整authority。

下一项仍须真实独立准备回执与完整request，但不为签字制造事实。当前没有第二位
独立审阅者，没有实际目标摘要/准备/正式观察。HOLD、版本、删除与release保持。

## 官方语义依据

- Python 3.12 subprocess：close_fds关闭0/1/2以外的描述符；pass_fds为明确例外。
  https://docs.python.org/3.12/library/subprocess.html
- Python 3.12 os：Python创建的FD默认不可继承；fork与exec继承语义不同。
  https://docs.python.org/3.12/library/os.html#inheritance-of-file-descriptors
- GitHub托管runner：标准非单CPU job通常使用新VM；不是排除VM内runner agent或root的证明。
  https://docs.github.com/en/actions/reference/runners/github-hosted-runners
