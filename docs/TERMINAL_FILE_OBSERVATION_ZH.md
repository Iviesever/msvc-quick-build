# 终端文件观察：独立完成结果适配器（内部、显式启用）

[English](TERMINAL_FILE_OBSERVATION.md)

## 已完成构建与后续观察分开

```cpp
#include "mqb/orchestration/ObservedLinkCompletion.hpp"
#include "mqb/platform/windows/StorageFileObservation.hpp"

auto result = mqb::orchestration::observe_link_completion(
    linking.run_recorded(request),
    mqb::platform::windows::observe_storage_file);
```

适配器只消费已经取得的 `std::expected<RecordedLinkResult, IncrementalLinkError>`。
它不接收协调器、构建请求、执行器或缓存加载器，不能重复构建、inspect 或重载缓存。
例子中的调用者只执行一次 `run_recorded`。输入失败时原类型、原值和嵌套诊断转交，
观察次数为零。成功或复用时保留原结果、警告与记录，只观察记录中的主输出；
相对路径使用记录自身的绝对工作目录，不借用进程当前目录，不提前规范化 `..`。

`build` 与 `observation` 分开：空回调为 `not_attempted`；文件缺失、不可访问、
路径拒绝和回调异常不会伪造观察成功或抹去构建成功。`observer_invoked` 只表示回调分派，
不表示 Win32 成功。回传路径必须匹配；普通/未知异常变为不可用，`std::bad_alloc`
继续传播。回调属于可信内部组合，不是第三方报告认证器。

默认 `run`、`run_recorded`、CLI、目标调用者及 `mqb storage` 都不采用新观察。
不观察 PDB/ILK、对象、缓存、整个目标、模块、PCH 或 LIB，不增加缓存格式或日志协议。

## 独立平台边界与必要共享提取

旧协调器公共头与旧平台 `StorageInventory.hpp` 保持参照主线原字节。
新声明和新主体分别位于独立公共头与产品 TU，完整登记在 `cpp/mqb.json`。
只在一个 `cpp/include/src/tests` 产品树中实现，不藏源文件、不另建平行库、不改全局编译链接参数。

`StorageReadPrimitives.hpp` 共享原盘点实现中的 RAII Handle、路径转换、读 pin 和文件 ID 转换；
`PhysicalPath.hpp` 共享原写入域的严格路径谓词。检查器逐段逆向恢复，再校验整个原文件摘要。
提取仍会导致两个旧 TU 重编，不能宣称 ABI、对象文件或机器码不变。
字面 include 可达性检查不等于条件编译、编译器依赖图或性能认证。

观察只接收普通绝对盘符路径。设备、UNC、ADS、保留名称与父级跳转在打开前拒绝；
最多 32700 路径字符、128 相对路径组件及根句柄。限制约束操作与内存，不保证同步内核 IO 截止时间。
祖先和叶节点保留原 `FILE_READ_DATA | FILE_READ_ATTRIBUTES`、`FILE_SHARE_READ`、
备份语义与打开重解析点方式，拒绝重解析祖先/叶节点，不枚举目录。
须取得本地卷 GUID 的 NTFS 身份；大小、链接数与文件 ID 通过同一叶句柄取得。
缺少叶节点与父节点缺失/不可用区分；目录不算普通文件。
稀疏/压缩物理分配仍标记未知，不伪造零。所有出口通过 RAII 关闭句柄，不返回租约。

原生 API 依据：[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)、
[FILE_ID_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info)。
共享模式不等于防御所有敌对进程、映射、过滤器或名字空间变化。

## 观察不是归属或删除授权

构建完成与观察打开之间、句柄关闭之后，文件仍可被替换或改变。
观察到 ID 不证明本次编译产生该文件；相同 ID 也不是内容哈希或永久身份。
历史观察是不可变副本，不是现状证书。
`producer_identity_verified`、`current_content_verified`、`complete_producer_inventory`、
`deletion_authorized` 四项始终为 false。不回填旧记录，不计算可回收空间，
不提供持久代际、整个调用期间的写者排斥、退休决策或 clean/prune。

## 实际测试范围与待验收边界

当前 88 个原生测试（包括 V9 采纳测试）全部保留，加入迁移的观察 E2E 后共 89 个程序。
同一 E2E 增加 26 项无构建/无 IO 的确定性适配器控制，覆盖错误零回调、成功一次、
复用、原警告/记录/嵌套错误保留、路径解析、空/错路径/异常回调及 bad_alloc。

Win32 部分保留原测试主体与预算：一次源编译、八次终端调用（七成功、一预期链接失败）、
三次真实链接、二十次文件观察、一次成功程序与一次 junction 命令。
保留忙碌写者、删除/替换、hardlink/junction、目录/父节点/非法/过深路径、
缓存 load/save 警告及返回后句柄释放断言。仅在全新夹具中操作，失败链接不修复重试。
确定性测试不能代替这些实际 Windows 行为。原件包括原诊断和选定文件，
不冒充原子文件系统快照或完整环境。

这是 [审阅 5325617783](https://github.com/Iviesever/msvc-quick-build/pull/207#pullrequestreview-5325617783)
选定的隔离后继，不合并、关闭、改写原 #207。其原提交 `bd12a59e9894068384d1a9b56b052ce5fec86098`
和 #701 的 +3.0963ms / +26.70% HOLD 与全部不利证据保留。
V9 的 `benefit_unproven` 不是修复证明。本修订没有新测量预算，不重跑旧入口，
也不能凭结构隔离或正确性绿色放行。完整候选、后续主线、单独审阅的性能处置仍需完成。
#198 的所有权、代际、写者并发、Rium 与安全清理目标不缩水；版本仍为 5.6.0，未发布 5.7.0。

历史事实编解码：[链接事实快照](LINK_FACT_SNAPSHOT_ZH.md)。
