# 017：真实 GNU 解码与加载诊断开发验收收口（dev008）

2026-10-16。接续 #198 / 6217201171 的执行前登记。
**两次真实 GNU 调用与原始附件验收完成，不是正式 103B 观察或运行资格。**
本次没有改变旧生产组件，也没有重复执行 dev004–dev007。

## 准确执行身份

| 对象 | 记录 |
|---|---|
| 执行源码 | e02e4d0f28ef71f38448a52df7423f649fccd3c7 |
| 唯一 parent | ba39f933103c07946d0fe6a8e32371d0ec831121 |
| 执行 tree | 678e0b4a4296553fde86d9f54810ddca8c337867 |
| Workflow | 017 decoder loading dev008 (single synthetic pair) / 386756023 |
| Run | 38912755534，run_number=1 / attempt=1 |
| Job | 117012430774 |
| 状态 | completed/success，所有步骤成功 |
| 时间 | 2026-10-16 14:03:56–14:04:09 UTC |
| 原 artifact | 12051460385，bcrypt-decoder-dev008 |
| ZIP | 154775 B，65个唯一安全文件成员，CRC通过 |
| ZIP SHA256 | eb0c6d21aa029ceaef43dd17696d05a82ae9b5f27051346644eabb7ab989224d |

登记清单SHA256为95d3ad8aaebd88b2152ca702f25e8ec19e6f5d624d533a851c3fcbd4e7324542。
38份归档源码逐字匹配登记Git blob、本地文件及CI身份摘要。准确head/workflow_sha、
运行次数、解释器、监督器argv/env和退出记录均核对；不是只看绿色工作流。

## 实际结果

输入是源码自产的102个0x90与一个0xc3，共103B，仅作为解码数据，不执行这些指令。
SHA256为aacfbb4f595210a9e5bac8e895de7639ab4f1f2644b33cbdcfcb8a9188bf6b76。
它不是原Windows图像中的实际103B，不能拿这个摘要填正式target字段。

| 场景 | 子进程PID | 退出 | stdout | stderr | 原解析器核对 |
|---|---:|---:|---:|---:|---|
| 原适配层/固定环境 baseline | 2188 | 0 | 6996B | 0B | 102条NOP＋1条RET |
| 同argv，仅增加LD_DEBUG=libs | 2189 | 0 | 6996B | 2911B | 同样103条，逐条字节/连续覆盖相符 |

两个stdout完整字节相同，SHA256均为
4cfc84d9012ef36da2f6987b0506c392cf63c4bae2988db101add5947a97a571。
第一次真实使用未修改的PreparedNativeBackend、OutputDirectory、资源限制和listing
解析器；第二次不修改生产环境常量，独立传入诊断环境。没有第三次GNU调用、version/help
调用、重试或补位。两次均真实wait回收，无kill、无遗留后代、scope策略恢复。

原candidate report的backend_closed=false仍保持；单独的case-result在关闭完成后记录
true。两份记录除该字段外逐项一致，未覆盖旧candidate来伪造关闭证明。

监督器实际退出0/reaped、无timeout/retry，耗时264154922ns。此值只是开发运行记录，
不是性能分数或正式执行期限证明。当前容器没有再次运行已知不支持的scope工作负载。

## 诊断加载证据及界限

诊断原stderr经同PID、完整init→initialize→transfer→fini序列检查；8个初始化对象、
包含主程序空名称的9个终结对象完整对应。搜索尝试不算已加载文件。8项已关联对象为：
ld-linux-x86-64、libbfd、libc、libctf、libopcodes、libsframe、libz和libzstd。
其规范路径/长度/摘要均匹配decoder的可达静态依赖集合，未用诊断文本任意打开宿主路径。

Python保持3.12.3及dev007同一二进制SHA256，runner镜像为20261012.334.1。
objdump为390848B，SHA256为
325c4205a4c658a9d1e1ebc469ae55975a2b897a3d3c1e79d9b158612d37f745。
前后12文件toolchain图、实际身份、aliases、cache与缺失hook检查完全相同。
每次静态collect实际26个完整pass/30942694内容字节；两次共61885388内容字节。
系统库payload没有上传，离线验收只检查记录关联，未用本机库冒充CI字节重算。

**LD_DEBUG是第二次运行的环境变化。** 初始化日志不是第一次运行的完整映射证明，
也不证明所有可能dlopen路径、无副作用、非后代写者不存在或正式bootstrap模块闭集。
原stdout一致不能扩大成上述结论。glibc诊断文本不是稳定的通用事件ABI；未知行仍拒绝。

## 单测、范围与实际未执行项

原345项不变，新增22项纯/模拟/临时文件控制；本地和本run分别运行同一套
**367 PASS / 0 FAIL / 0 ERROR / 0 SKIP**。不把两处367相加为734个不同方法。
单测Popen/scope是替身；上表两次真实GNU不是替身。原GNU parser另在单测中解析
人为构造的文本，那份结果不冒称原生运行证据。本轮没有新的单测失败或CI重跑。

发布范围为五项新增实现/测试/登记/工作流/说明，另加本结果说明。原57文件局部工作集
保持，不冒称完整仓库克隆或整个Git树重建。自造数据及两次开发解码不构成正式准备；
原图像读取、实际target读取、正式主/独立准备和正式观察均0。

## 准确下一接手点

**真实适配层解码、自造指令覆盖、受限加载诊断与本次原件验收已经完成。**
不重写本checker、不重提补丁、不重跑已消耗dev008或旧dev004–007。

下一项是将已经实现的固定bootstrap/BoundObservation/控制器与有依据的精确运行资格
和输入绑定：冻结实际入口（不是collector）的模块/源码/loader集合，核验独占输出域、
非后代写者及FD外传前提，取得两份真实独立准备及审阅回执，保留控制器最终外部退出。
满足这些条件后再分别登记端到端开发验证与正式观察。当前结果不得自动授权未来runner。

DECODER_LOADING_EVIDENCE_NOT_QUALIFIED保持；bootstrap_qualified、writer_domain_qualified、
production_loader_maps_observed和clears_hold均false。源码/测试/附件核对由同一会话完成，
没有第二位独立审阅者签署。main、HOLD/DEFER、VERSION5.6.0、既有PR、清理删除与release
权限不变；完整017和v5.7.0仍未验收完成。

运行：https://github.com/Iviesever/msvc-quick-build/actions/runs/38912755534
登记：https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6217201171
