# 2026-10-15：补丁已发布，外层直接进程传输已实现

上节六文件的精确字节已经以 `cc7e8b7cdfa9c8cfc585658c4c9a8872613ccf66`
发布到原feature；上节“尚未发布”仅为保留的历史记录。该提交不改变主线或实验资格。
本次新增 `bcrypt_outer_transport_017.py`、纯单测和显式四case开发smoke，
不重写已存在的owner、backend、请求绑定器或完成回执核对器。

### 已运行的实际机制

OwnerTransport校验外部固定的launch-spec摘要后，独占新输出目录，保存launch.json，
只调用一次绝对argv的Popen，无shell、固定环境、stdin DEVNULL。stdout/stderr使用
非阻塞pipe和有界读取，不调用communicate。stdout上限65536 B、stderr上限65 B；
多读最多1 B来辨别溢出，超限保留已收集前缀并标记capture不完整，不继续抽空流。

从开目录前起使用单个monotonic deadline（spec最长16000ms），不能用每次pipe活动
重置时限。收齐两路EOF后，必须wait到包含owner的直接进程结束；该进程非零退出、
stderr非空、最终JSON/请求关联错误或超时均拒绝。异常时只poll已拥有的直接进程，
已回收不再发信号；未退出最多一次kill和一次独立1秒wait。kill报错仍尝试wait，
不会重试信号或启动另一个owner。Popen创建及同步磁盘调用仍不保证可硬中断。

外层只保存四个新文件：launch.json、owner-final.json、owner-stderr.bin、
process-exit.json；不向旧backend五文件目录添加第六项，也不覆盖早期report.json。
文件用O_EXCL/NOFOLLOW、有限写入、fsync和原描述符readback；目录替换或重用拒绝。
process-exit.json产生在最终目录fd关闭前，返回对象另外记录close结果；close失败
不会覆盖该不可变文件制造成功。缺失/未回收/超限情况下的输出只是失败前缀。

**这是直接进程传输组件，不是完整writer containment。** backend会把decoder启动
在另一个session，杀owner不保证杀decoder。pipe EOF也不能证明其他文件没有写者。
因此本组件始终保持writers_quiescent=false、observation_accepted=false，采用独立的
bcrypt-owner-transport-017-v1 schema，不能伪装成此前完成核对器接受的外层回执。
其最小JSON检查也不是完整结果审核，必须继续走已有的精确绑定/完成核对。

### 本次验证

原206项重放通过；新增19项纯协议/临时文件系统测试通过，不启动真实子进程。
补充RED发现新实现kill报错会漏掉wait；同断言修复后GREEN，原代码/日志保留。
随后在Issue198评论6185942307事先登记源码摘要和四个固定开发case，再运行：
clean退出0、exit37退出37、timeout在200ms后终止并回收、overflow检测第65537字节
并拒绝。4个真实Python child，无真实GNU、Windows/MQB/MSVC/ETW或实际目标读取。
开发smoke输出不作为017正式数据；不补跑、重试或借用已消耗的016槽位。

smoke脚本不叫test_*.py，普通unittest发现只运行纯单测；显式运行必须提供准确
transport源码SHA。此轮没有第二位独立审阅者对新源码或当前宿主作准入签署。

### 下一接手点

不要再发布同一六文件补丁或重写直接进程传输。接下来应实现并审阅**包含owner及
跨session decoder的写者范围控制**，以及固定bootstrap连接BoundObservation、独立
保存最终返回、全部写者结束后的六项原件读回；然后接入既有evaluate_completion。
不能仅把writers_quiescent从false改为true。实际loader/module资格、两份正式准备、
独立签署和正式103 B观察仍未完成；没有新实际目标摘要或正式CLI/工作流分配。

Python subprocess文档说明进程创建和timeout边界：
https://docs.python.org/3/library/subprocess.html
本实现的非阻塞fd和fsync调用依据：
https://docs.python.org/3/library/os.html
