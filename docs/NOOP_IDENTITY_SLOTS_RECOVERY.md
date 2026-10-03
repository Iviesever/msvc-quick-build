# slot-001：准备前失败后的单次恢复准入

本修订只交付工具与回归，不包含恢复执行许可，不触发任何诊断。
原 `pr232-slot-001` / run `36948961360` / attempt 1 永久保留为
`consumed_failed_before_measurement`。原 #819 和 #232 继续 HOLD。

## 与第一次运行的区别

原始首次工作流已消耗，不能再 Run 或 Re-run。恢复仍使用同一个工作流、同一个
allocation 和原 A/B 输入，不复制工作流重置序号。运行编号2仅是必要条件，**不是许可**。
需要同时满足：

- 固定原失败 run/attempt/artifact、完整11,226,368字节ZIP与SHA-256；原9文件前缀、
  Git多路径错误、原源码和嵌套819输入必须一致。测量0来自错误与控制流，不是OS进程普查。
- 托管Windows X64、main、准确源码、干净的跟踪文件、工作流编号2及attempt1。
- `recovery_id=slot001-preparation-recovery-001`，原allocation仍为`pr232-slot-001`；
  开关默认false；恢复标识常量仅描述实现支持的路线，不是已分配执行。
- `permit_comment_id`指向本仓库issue #198中由所有者Iviesever发布的专用许可。
  API的作者/issue归属、未编辑状态、发布时间早于当前run均须核对。
- GitHub工作流历史必须恰好包含原失败和当前恢复两次运行；旧运行仍为原源码、attempt1和failure。
  多出任何运行、删除旧运行、重复attempt或main移动均拒绝，不重试或改编号补跑。

许可正文必须严格为首行 `MQB_SLOT_RECOVERY_PERMIT_V1` 加一个JSON对象。字段模板由
`tests/native/noop_identity_slots_recovery.py:expected_permit`描述；调用模板函数不是许可。
JSON完整绑定准确reviewed_commit、reviewed_tree、工作流文件SHA-256、原失败、原819摘要、
编号/attempt及48次调用预算。工作流摘要明确针对Git文本的LF表示；实际checkout原字节另存。
许可在实现验收后由维护者另行登记，**仓库中没有生效的许可或自动登记入口**。

包装器只读三个固定GitHub API：当前工作流历史、指定issue评论、main引用。
固定域名、仅GET、禁止跳转、每响应2MiB上限、单请求20秒且不重试。
公开许可评论读取不携带token；原始响应保存且禁止覆盖。有效结果为
`owner_permit_and_failed_prefix_checked`，不是第三方审批或不可撤销的安全认证；
`remote_allocation_verified=false`仍保留。服务器快照之后的编辑/删除或宿主故障不是持续监测范围。
GitHub读取失败、输入过期、许可不符均在MQB启动前停止，保留已产生的前缀。
传递给原PowerShell入口的环境移除GH_TOKEN/GITHUB_TOKEN，不输出全部环境或凭证。

## 真实准备链与不执行边界

`Invoke-SlotPreparation`从原入口原样提取工具解析、Git HEAD/dirty、Python prepare及
native-preflight步骤，作为显式准备函数。它没有launcher参数、capture导入、
New-SlotState或Invoke-SlotStudy调用。实际入口在原执行准入之后调用此函数；
只拿到准备结果并不获得测量权限。原采样、捕获与计划函数未修改。

新测试通过PowerShell调用同一函数，使用临时Git仓库、真实Git/Python和原819完整ZIP。
两个带空格PATH目录中固定包装器转发至真实工具，检查正序/反序多工具、缺失Git/Python、
工具退出37、错误HEAD、dirty source、坏ZIP和已有目录。测试不伪造执行准入，
不调用完整测量入口。成功时slots/blocks/fixtures/retired仍为空，无host/completion或测量样本。

Reporting correctness仅增加固定失败ZIP下载，并把路径传给测试；不启动手动工作流。
原Reporting的所有旧命令/检查保留，另外49份历史工作流不变。指纹适配只识别准确的新版
Reporting文件和准确的恢复工作流；任何缺失/变更/额外工作流仍拒绝，不通配排除。

本地可在具备PowerShell 7、Git、Python时设置 `MQB_SLOT_FAILED_ZIP` 为原失败ZIP，运行：

```console
python -B -m unittest discover -s tests/native -p "test_noop_identity_slots*.py" -v
```

缺PowerShell的本地检查显式跳过真实链，不能称为已执行；CI缺固定输入则失败，不静默跳过。
自动测试只读取原程序字节，不执行其中的EXE，不重建MQB，不采集性能。

## 仍不改变的预算和判定

原8块顺序、16prime＋32测量/最多48根调用、A/B摘要、输入、输出捕获及测量窗保持不变。
20分钟job、15分钟步骤、返回后30秒和空间检查不变；不承诺同步挂起中断或故障完整上传。
原失败和Reporting85失败不可覆盖；恢复失败也不会形成第三次运行许可。
恢复结果不替代原资格、不抵消不利样本、不解除HOLD。VERSION仍5.6.0，尚未发布v5.7.0。
