# 017 接续：元数据预检已完成，正式观察尚未准入

## 已恢复的真实接手点

016 并未停在登记阶段。远端 feature 已指向最终收尾提交
`f63c3eca924e6f2a3a4f30e95f3381bdee64dffc`，tree
`2a6a31926ca9b12039b31bcc6f7f26fe7d409d13`。
执行源码仍为 `51bbce58ae37b36521be6425de55e784e8ceee72`。
Issue198 的 016 收尾评论为 6129789788；正式登记评论为 6129084740。

已从 Library 恢复并重新核对 016 的原 ZIP 和两份原始包外回执：
ZIP 为 6,818,112 B，SHA256
`afe92dc6f574467e686a49690bcbcac4c62b582696705febf068b1ac6eae32d6`。
346 个外层成员的完整长度、SHA256、CRC、唯一名称和非符号链接属性均已重新核对。
原回执和 ZIP 均未改写。本轮核对不冒称重新完成了原独立源码审阅。

016 的 83 B / 18 条指令和已消耗的一次正式观察保持不变。
本轮继续的是 017 的前置条件，而不是重跑 016。

## 本轮实际交付

`tests/native/bcrypt_preflight_017.py` 是无文件 I/O、无子进程的元数据预检器。
公开 `preflight(inputs)` 必须首先核对五份固定原件的完整长度和 SHA256，
然后才解析 JSON，检查完整 016 收尾与实际运行审阅的关联。
内部 `_validate` 仅供语义单元测试，不是可以绕开输入身份检查的执行入口。

它从已验收的 016 指令原字节 `DB84: e8f3350000` 重新计算目标，
核对唯一 runtime-function302、完整文件映射、`.pdata` 行、无 CHAININFO/
无 handler 的 unwind，以及 017 提案。地址字符串不作为 CALL 计算依据。

结果：`METADATA_READY_EXECUTION_NOT_ADMITTED`。
固定范围仍为 `[0x1117C,0x111E3)`，103 B，不替换成相邻的 `0x111EC`。
新目标机器码未读取，SHA256 仍为 null。

`tests/native/bcrypt_environment_017.py` 是只读旧环境兼容性检查器。
仅检查两个明确的旧路径，不执行 Python 探针或反汇编工具，不扫描系统目录。
即使这两个文件匹配，也只返回 `OLD_FILES_MATCH_ONLY`，不授予执行许可，
也不声称依赖库已经重新验收。

本次真实环境结果：`BLOCKED_ENVIRONMENT_CHANGED`。

| 项目 | 016 冻结值 | 本次观察 |
|---|---|---|
| Python | `/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12` | 路径不存在 |
| objdump 字节数 | 390,848 B | 396,336 B |
| objdump SHA256 | `7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5` | `96afb8521834982d0e711b5d6e9785252bf82129fb0106b93a6bd54d83fae11f` |

不能把旧绝对路径机械替换成当前 Python，或把新工具的哈希写进旧 016 清单。
这些变化需要新的 017 工具链、资源和调用清单审阅。

## 验证与明确限制

本轮两个测试模块共有 **46 个测试，46 PASS，0 FAIL，0 SKIP**。
其中元数据 33 个，环境 13 个；没有 GNU/Windows 子进程或原图像读取。
环境测试使用模拟结果和临时普通文件、目录、符号链接、FIFO；没有启动 FIFO 写者。

保留了一次真实 RED/GREEN：JSON 数字 `1e9999` 原先会被解码为无穷大，
新增测试先失败，补充有限浮点检查后全套通过。原失败日志与修复前源文件保留在交接包。
另外，真实五份固定元数据输入通过公开预检器；真实旧环境检查按预期拒绝继续。
单元测试成功不抵消环境拒绝，也不证明 Windows 启动成功。

复跑纯测试：

```sh
python -B -m unittest discover -s tests/native -p 'test_bcrypt_*_017.py' -v
```

本阶段只有作者自审、确定性测试和原包校验。
**尚未取得新的独立源码/资源审阅，也尚未完成当前工具链的执行资格。**
不把测试结果改称独立审阅；不使用本中间提交作为已登记的正式执行源码。

## 唯一下一接手点：继续 017，不另开 018

在本预检中间提交之后继续；远端 expected-old 应读取最新 feature HEAD。
016 诊断证据基线仍固定为 `f63c3eca...`，不得混同为其执行源码 `51bbce58...`。

下一步先建立当前环境的 017 工具身份、依赖与解释器清单，重新证明 103 B
目标对应的选择、完整解码、输出、子进程回收、截止时间和持久化边界，并取得独立审阅。
之后才分配主准备、独立准备和一次正式观察；每次准备各自从完整验证快照取得目标身份。
正式观察前仍须发布、完整回读源码与清单，登记并完成 expected-old 激活核对。

本轮没有读取新目标或原图像，没有消耗 017 的任何准备/正式观察槽位，
没有新反汇编/原生执行、workflow dispatch/rerun、PR、main、VERSION 或 release 变更。
没有开启清理、删除、M1b/service 或普通生产者资格。

011 仍为 `INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE`；失败 API 和
`0xC0000142` 原因未知。原 11/10/23/7/0 原生计数不变；VERSION5.6.0；
cost DEFER（+3.0963 ms / +26.70%）；A1-A5/删除/ordinary qualification HOLD；
`canonical_evidence_sealed=false`。IPC 生命周期与合格 producer/writer 仍未解决。
