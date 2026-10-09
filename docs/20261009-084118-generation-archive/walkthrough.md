# 普通目标代际证据归档：本地验证记录

## 基线与交付范围

本地审计日期：2026-10-09。准确基线为
`2762749b27fc89156b724d19e89cce84e388ef52`，tree
`0a4c2dd76c56309accf58d584a079b4bd483ee49`。
工作分支为 `feat/v5.7-generation-evidence-codec-20261009`。

本次接续 [main 独立验收后的接手点](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6075505709)
和 [执行前登记](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6075771527)。
交付显式普通 EXE/DLL/static 已完成证据的拥有型投影、fixed-v1 有界编解码，
以及复用同一个严格投影器和代际关联模型的回读验证。

本页记录本地验证；候选自己的首次 Native/Release/Reporting 及适用工作流
结果由对应 Draft PR 和 #198 的后续评论绑定，不能用基线 CI 替代。

## 改动审计

| 模块 | 主要文件与行为 |
|---|---|
| 纯值归档 API | `ArtifactGenerationArchive.hpp`：独立拥有型 claims、固定限额、四项恒 false 权限 |
| v1 编解码 | `ArtifactGenerationArchive.cpp` 与私有 `ArtifactGenerationArchiveInternal.hpp`：同一 schema 完成测量、编码、完整外层扫描和拥有型解码 |
| 共用严格规则 | `ArtifactGenerationModel.cpp` 和 `ArtifactStorageProjection.cpp/.hpp`：live 与 archive 共用原准入、recipe、projection、lineage、retention 和错误下标 |
| 原生回归 | `artifact_generation_archive_tests.cpp`：独立双源 EXE/DLL/LIB 夹具、全模型对照、恶意 wire、拥有性与分配前控制 |
| 已有真实生命周期 | `TargetWaveCacheEvidenceChecks.hpp`：在原证据保存后追加内存往返及完整模型比较；两个原 E2E 调用文件保持基线字节 |
| 注册和历史契约 | manifest/layout/driver 同步真实 97/97；新增精确逆变换及 12 项合同；保留 15 个历史模块全部旧测试方法和断言 |
| 格式文档 | [英文](../ARTIFACT_GENERATION_ARCHIVE.md)／[中文](../ARTIFACT_GENERATION_ARCHIVE_ZH.md)与原代际文档交叉链接 |

独立审阅确认：23 个登记修改文件逆向后的完整内容均与准确 Git 基线相等；
cpp inventory 精确为基线加四份声明文件，即一个 public header、一个 private
header、一个产品 TU 和一个 native test TU。产品与原生运行使用真实当前源码。
所有既有 workflow 字节保持不变。

## 真实本地输出

### 新归档原生程序

GCC C++23，`-Wall -Wextra -Wpedantic`；构建退出 0，无诊断。该测试程序链接
codec、generation model、storage projection、storage association、link snapshot
和 JSON 的实际产品源码，不运行 MQB、MSVC 或文件观察器。独立审阅者再次构建
和运行得到相同结果。

```text
RUN_EXIT: 0
12350 artifact generation archive checks passed
```

### 原有 generation 程序

最终源码重新链接完整 codec 后，原有 78+58=136 项均通过，编译退出 0、无诊断。

```text
COMMAND: ../evidence/artifact-generation-existing-final
CWD: /workspace/scratch/3d56575ba9f2/mqb-work/codec

78 generation-model checks passed
58 recorded generation checks passed

EXIT_CODE: 0
```

### 错误身份回归

审阅曾复现 `cache_entry.module_scan` 预检过早拒绝，遮蔽原模型的 record/source
下标。已让测量仅计 presence 字节、由原严格投影器保持拒绝；实际编码和外层
解码仍不允许该图。独立 scratch 的原红灯及修复后输出均保留；同一情形已纳入
长期 native 回归。修复后真实输出：

```text
PASS: archive shared projection/model checks
EXIT_CODE: 0
```

### Python 受影响模块

15 个历史模块加新合同共 216 项；215 pass、1 skip、0 fail/error，退出 0。

```text
----------------------------------------------------------------------
Ran 216 tests in 1.747s

OK (skipped=1)
```

### Python 全量

执行 `python -B -m unittest discover -s tests/native -p 'test_*.py' -v`，
733 个唯一测试、727 pass、6 skip、0 fail/error，退出 0。6 个 skip 与基线一致，
仅因本地缺 PowerShell 7；没有因历史原件缺失而跳过。未把 Linux 结果称为
Windows/MSVC 原生验收。

```text
----------------------------------------------------------------------
Ran 733 tests in 9.143s

OK (skipped=6)
```

两个固定输入的原始 SHA256 均在运行前核对：

| 输入 | 字节 | SHA256 |
|---|---:|---|
| original-cumulative.zip | 6215366 | `d70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530` |
| slot001-failed-original.zip | 11226368 | `70fba5cb2fe61f196614e81fee9c34229e6af30caea25ef22d91e92090625a3a` |

全量 stderr SHA256 为
`34423889075b672044d699988e6dbec5c6ef981d4558d16a831cf1e6ca730ec4`。
测试前后源码摘要一致，没有由测试修改源码。`git diff --check` 退出 0。

## 验收标准如何被检查

- 手写空文档 golden bytes、显式 wire 枚举标签、完整 populated fixture 逐字节截断，
  以及坏长度、计数、UTF-8/NUL、标志、枚举、snapshot 和尾垃圾均有独立控制。
- 完整外层 scan 在 document strings/paths 赋值、vector reserve、snapshot 解码和
  词法 key 回调之前结束；分配观察器有两个正控制，并以 256 KiB 字符串与
  2048 元素 vector 的尾部坏数据确认不会提前拥有大文档值。
- live → owning → wire → readback 的完整模型对照保留 source/target/generation、
  检查/缓存两份 compiler、原 signature、有序选项和重复依赖、各阶段 cwd、
  save error/warnings，以及缺来源、混合复用、冲突、保留请求和路径关联。
- 语法合法但造成 DLL 角色冲突的 wire 仍由原严格投影器拒绝，保留 issue、消息、
  record_index 和 source_index。读回结果不构造真实成功进程。
- 新源码/清单/驱动的缺失、额外文件、篡改、旧文件冒充当前文件、错误 inverse
  splice/hash 均拒绝；冻结期间实际发生过 stale hash 拒绝，更新当前精确 pin 后
  12 条新合同重新全绿。

## 保留边界与下一验收

四项权限 `producer_identity_verified`、`current_content_verified`、
`complete_producer_inventory`、`deletion_authorized` 始终为 false。
UTF-8 路径转换与既有 snapshot 编码可使用有界临时存储；逻辑限额不等于总堆、
分配次数、峰值驻留或延迟保证。路径跨平台比较仍需要兼容的宿主词法语义和 key。

没有默认 CLI、自动落盘、文件 writer、恢复或 clean/prune 接入。原生命周期
11 次成功返回只追加内存断言，没有增加 MQB/cl/LINK/LIB/observer 调用；新测试
程序自身的构建与运行成本计入普通 Native/Release CI。

接下来验收独立 Draft PR 的首次功能 CI，保全任何首失败。功能通过后，显式
codec 复制/分配/延迟成本的性能资格与集成决定仍须独立处理。本切片没有新增
性能采样，不重跑原矩阵。819 HOLD、既有慢值与937冷回退、file_inputs 7→1、
不完整依赖、未量化峰值驻留、默认高频采用、writer/clean-prune/Rium/#164 等
限制继续保留。VERSION 仍为 5.6.0，v5.7.0 未发布。
