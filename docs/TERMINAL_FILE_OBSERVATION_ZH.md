# 终端文件观察（内部、显式启用）

[English](TERMINAL_FILE_OBSERVATION.md)

## 成功调用与随后的观察分开

```cpp
auto result = linker.run_observed(request, mqb::platform::windows::observe_storage_file);
```

新增的 `MsvcIncrementalLinkCoordinator::run_observed` 只调用一次既有 `run_recorded`。链接或校验失败原样返回原类型错误，不调用观察器。成功或复用后，只观察本次真实链接记录中的主输出；相对路径仅使用该记录自己的绝对工作目录，不借用进程当前目录。本切片不新观察 PDB/ILK、对象、缓存、其他副产物、完整目标、模块、PCH 或 LIB。

`ObservedLinkResult::build` 原样保留成功结果、警告和记录；`observation` 单独返回。空观察器为 `not_attempted`；缺失、路径拒绝、占用或回调异常不能把构建改写为虚假失败，也不能虚构观察成功。`observer_invoked` 只说明调用过回调，不等于成功执行原生 IO。回调须回传传入的 `requested_path`，不同路径会被标为不可用；回调是受信任的组合/测试边界，不是对第三方报告的校验器。内存分配失败仍向上传递。

默认 `run`、`run_recorded`、CLI、目标调用者及 `mqb storage` 均未接入，没有新增缓存格式、日志登记、默认锁、保留或清理行为。

## 有界 Windows 句柄观察

`observe_storage_file` 接受一个普通绝对盘符文件路径，复用空间盘点的原生路径、只读句柄和文件 ID 帮助函数。既有预写盘点的严格路径谓词原样移入私有共享头，不另造一套别名规则。设备、UNC、数据流、保留名及父级跳转在打开前拒绝；路径最多32,700个字符、相对路径最多128级，另加根句柄。这限制操作量与句柄数量，不保证同步内核 IO 的时间上限。

祖先和叶项以读数据/读属性权限、只共享读取方式打开；逐层及最终项拒绝重解析点，不枚举目录。只接受具有本地卷 GUID 的 NTFS。大小、链接数和卷号加128位文件 ID 从同一叶句柄查询。允许观察多硬链接，但链接数不是独占证明；稀疏/压缩文件的物理分配保持未知，不冒充普通分配量。

缺失叶与缺失/不可访问的父目录区分，后者不可用。普通目录不是终端文件，属性/身份失败保留原生诊断，不填零大小或伪造 ID。函数不读取内容/缓存、不创建、不写入或删除；所有句柄在返回前关闭，错误路径同样如此。没有返回租约或写域标记。

原生依据：[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)、[FILE_ID_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info)。不能用仅属性访问替代既有读数据共享边界；操作系统共享也不等于能防御所有同权限恶意进程、映射、文件系统/过滤器行为或命名空间变化。

## 身份与授权限制

观察发生在链接完成之后。其他参与者可能在打开前替换或移走路径，取得的 ID 不证明该文件由编译器产生；句柄关闭后路径或内容仍可再次变化。历史观察是独立值副本，不是当前状态证书；相同 ID 不是内容哈希、所有权或跨重启永久标识。

`producer_identity_verified`、`current_content_verified`、`complete_producer_inventory`、`deletion_authorized` 均保持 false。不向旧构建记录或盘点关联回填观察，不计算可回收字节，不建立可靠持久化读回、全调用写者排除、退役规则或 `clean/prune` 授权。

## 固定测试与尚待验收

新增独立 E2E，原87个测试源保持不变，总原生清单为88。每种 Debug/Release 固定一次源码编译、八次终端调用（七成功/一次预期链接失败）、三次实际链接、二十次单文件观察、一次成功程序与一次 junction 命令。覆盖冷/复用、成功后打开的写者、抛异常/空观察器、故意移走成功输出、保存警告、失败链接不观察、保留原文件与同路径不同文件、硬链接、缺父/目录、祖先和最终重解析点、非法与超深路径。变动仅新测试夹具，不修好失败链接后重试。

原始命令/结果/诊断、选定输入字节、成功记录与观察字段在对应断言前保存到 `storage-evidence/file-observations`。它们不是完整配方/环境或原子文件系统快照。既有完整原生回归、自举/打包/安装及独立19×4 ABBA 仍是门槛；标准矩阵不直接测显式观察成本，默认不接入不构成零开销保证。既有规模/no-op/链接/模块尾部和 recorded/PCH/static/关联专项缺口保持，真实 Rium 多目标/AOT、持久化/并发及安全清理仍待完成。
