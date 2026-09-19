# 只读构建产物空间盘点

**[English](STORAGE_INVENTORY.md) | 简体中文**

这是基于 v5.6.0 推进 #198 的第一个开发切片，不是已发布 v5.6.0 可执行文件中的功能，也不代表 v5.7.0 已完成。独立发布 PR 之前，VERSION 仍为 5.6.0。

## 命令与范围

```powershell
mqb storage
mqb storage --project C:\work\Rium --format json > C:\reports\rium-storage.json
mqb storage --help
```

默认项目就是当前目录；不会向上搜索 mqb.json、加载配置、发现工具链、构建、运行产物或创建 .mqb。指定的项目目录必须存在，其产物根目录复用 ProjectArtifactLayout。不存在的 .mqb 输出空盘点。未知或重复参数属于用法错误。本轮没有 clean、prune、执行删除开关、按年龄筛选或保留策略。

使用前停止构建器和其他写入者。只向标准输出和标准错误输出报告，重定向文件应放在 .mqb 外。读取句柄暂时拒绝写入与删除共享，因此竞争的写入或重命名操作也可能失败。这不是 build/clean 互斥协议、项目写租约，也不能保证外部不合作活动下的安全。

## 字节数与证据分类

JSON 的 schema_version 为 1，逐文件给出未四舍五入的无符号 logical_bytes、allocated_bytes、hard_links 和 physical_id；无法取得的字段为 null，不猜成零。GB 为 1,000,000,000 字节，GiB 为 1,073,741,824 字节。文本报告同样提供原始文件清单及汇总。

目录分组只包含直接子文件。扩展名、直接目录、证据分类各自构成已观察普通文件行的不重叠分组，已知逻辑字节总数可与 totals 闭合。按路径汇总的分配空间刻意重复计算硬链接路径；unique_file_allocated_bytes 按已观察的卷和文件身份去重，身份相同但大小冲突或出现任何盘点问题时留空。分配空间不等于可回收空间：根目录外的硬链接、文件系统去重、元数据及未观察的活动都未被这一数字证明。首轮压缩／稀疏文件的物理分配空间明确不可用；不包含备用数据流和目录元数据。

证据分类为 protected、unknown、cache_reference、shared_cache_reference。扩展名仅用于描述，不构成所有权证明。日志、生成输入、发布包、VS 状态、未知目录及非普通文件保持受保护。bin/obj/ifc/deps/scan/cache/pch 等常规位置也不会因此获准删除，它们的生命周期仍为 unknown。reclaimable_bytes、live_bytes、obsolete_bytes 始终为 null，deletion_authorized 始终为 false。

## 历史引用不等于所有权

只对已观察的 cache/link/*.linkcache、cache/archive/*.archivecache 且不超过 16 MiB 的文件复用现有 LinkCacheFile、ArchiveCacheFile 读取器，不保存或升级缓存。损坏、不支持、超限及解码错误明确报告。仅使用 MQB 既有 Windows 路径身份规则把绝对引用与已观察路径关联；不打开引用指向的外部路径，不猜测相对历史路径的基准。

每条记录输出缓存路径、目标输出、不透明签名及工具版本。多个缓存记录引用同一行，只能说明共享历史引用，不能证明当前共享所有权。各目标统计有交叉，不能相加。旧签名无法还原 Debug/Release 或完整配置，因此 by_configuration 归入 unknown。源码缺失、旧哈希或未被本次调用列出都不证明目标已经退役。未来删除之前，仍需另行建立可信的当前目标、共享依赖和显式 AOT 代际记录。

## 文件系统与错误边界

Windows 通过句柄读取 FileStandardInfo 和 FileIdInfo。观察某文件期间保留该文件及其祖先目录的句柄；拒绝穿过重解析点。固定句柄同时申请 FILE_READ_DATA（目录对应 FILE_LIST_DIRECTORY）和 FILE_READ_ATTRIBUTES，而不是仅申请属性访问，使只读共享掩码真正参与 Windows 共享检查。因此，即使只报告元数据，也需要读取文件数据／列出目录的权限。权限不足仍作为部分盘点报告，不回退到仅属性句柄。枚举限制为 1,000,000 行、128 层深度。这不是文件系统原子快照；即使遍历成功，atomic_snapshot 仍为 false。权限、占用、不支持的身份／分配信息、跳过的重解析子树及枚举／解码错误会留下 issues，尽可能输出部分报告并返回 1。致命初始化或输出错误可能只留下诊断而没有完整 JSON；调用方必须同时检查退出码、解析结果、complete 和 issues。

退出码 0 只表示有界遍历未记录错误，不证明目录不可变或允许安全删除。1 表示盘点／输出错误或覆盖不完整，2 表示参数／项目初始化错误。不删除文件、不终止进程或服务，也不改变编译参数、普通构建或缓存行为。

原始契约：[FILE_STANDARD_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_standard_info)、[GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex)、[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)。这些接口本身不证明可回收容量或并发写入安全。

## 复现与验收

新增 mqb_storage_e2e_tests.cpp，保留原来的 79 个原生测试程序，成为第 80 个。便携模型／报告测试覆盖字节闭合、引用去重、受保护项、未知／溢出值、JSON 转义和输出失败，不等于 Windows 测试。

Windows 文件夹具覆盖根目录不存在、有效／共享／损坏的旧缓存、真实硬链接、Unicode 与空格路径、已有写入者、观察时尝试重命名文件与祖先目录并申请写入／删除访问、句柄关闭后的成功访问／改名对照、文件名／内容／修改时间不变，以及真实 junction 拒绝。每项固定句柄测试都在断言前保存实际结果与原生错误码，失败结果也会保留。Debug、Release 各执行固定八次双源码构建：稳定目标首次／重复、源码改名、目标改名、配置切换、两个 AOT 命名目标及最后目标重复。最后运行一次生成程序，保留旧对象与旧目标，在不删除它们的情况下检查清单字节与文件数闭合。这些 AOT 名称仅是最小生命周期模型，不是 Rium 的实际 AOT 生成链。

storage-evidence 保存原始 argv、stdout、stderr、退出码及成功／失败盘点；CI 产物保留选定源码、目标输出和链接缓存，期限 30 天。现有 Windows Debug/Release、自举／打包、适用兼容检查和独立 19×4 ABBA 仍是验收门槛，不能用便携测试替代。不承诺固定空间节省比例。Rium 实测、可信所有权／配置状态、压缩／稀疏空间统计及安全 clean/prune 仍未完成。
