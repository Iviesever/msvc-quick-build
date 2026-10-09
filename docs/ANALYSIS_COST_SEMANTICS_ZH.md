# 固定分析替身的成本输出语义

[English](ANALYSIS_COST_SEMANTICS.md)

## 1. 范围与来源

本片完成[接手记录 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173)的下一点。新增的[纯语义适配器](../tests/analysis/analysis_cost_semantics.py)检查完整成本输出的业务含义。[封闭驱动](../tests/analysis/analysis_substitute_driver.py)在保全两路流后及最终审计时调用它；[传输层](../tests/analysis/analysis_substitute_transport.py)和[采集 helper](../tests/analysis/analysis_evidence_capture.py)保持不变。

分析开发基线为 `bf5df76079f224ea9bb5e7822be683cb3a2e4d45`，tree 为 `46007c773858dd50653db0a49ce6861b15733fb2`。生产者 schema 来自保留的 `cost_harness.cpp`，SHA-256 为 `d0b7d1025a2d2d010fc2df562977ca3ec96a478ae8960672503c9847d6ac3632`，对应 PR250 candidate `a26b103685589ab8a5b4df60d1b760ec445efdf5`。其原产品基线为 `2762749b27fc89156b724d19e89cce84e388ef52`。这些身份承担不同角色；源码来源不能授予执行或产品权限。

本片只执行固定 Python 替身，不执行真实 preparation、codec API、编译器、MQB、MSVC、ETW 或性能研究。替身中写死的 `public_preparation_calls=5` 是 schema 测试数据，不是五次已消费 API 调用。

## 2. Prepared 身份

每个 call 恰有一条 prepared，随后是五条 observation。Prepared 恰有以下七个字段：

| 字段 | 类型与规则 |
|---|---|
| `kind` | 准确字符串 `prepared` |
| `case` | 已知 case 的准确字符串，与冻结调用一致 |
| `records` | 无符号 64 位整数；`reuse_save_failure` 为 2，`history_256` 为 256，其余为 1 |
| `wire_bytes` | 正无符号 64 位整数，与冻结预期值一致 |
| `public_preparation_calls` | 严格整数 5 |
| `model_equal` | 布尔值 `true` |
| `checks` | 正无符号 32 位整数，与冻结预期值一致 |

Prepared 不含 operation、instrumentation mode、trial、study 或 commit。外层调用绑定 case、operation、instrumentation 和 trial 预期；冻结驱动协议提供源码与协议身份。本片不选择真实研究。每个 prepared 字段都按预期类型和值比较。原两种 mode 的检查次数不同，因为分配控制额外做三项检查。[独立准备协议](ANALYSIS_COST_DECISION_PREPARATION_ZH.md)保留十组历史输入身份及两种 mode 的 prepared 次数。

对于 `bad_text_tail` 和 `bad_items_tail`，`model_equal=true` 描述生产者追加 `!` 以前对合法 wire 的等价检查，不证明畸形 wire 成功解码。

## 3. Observation 类型与顺序

每条 observation 恰有以下 15 个字段：`kind`、`case`、`operation`、`trial`、`instrumented`、`ns`、`new_calls`、`requested_bytes`、`requested_peak_live`、`requested_live_at_return`、`largest_request`、`key_callbacks`、`process_hwm_before_kib`、`process_hwm_after_kib`、`consumed`。

kind 为 `observation`；case 和 operation 与调用相符；trial 按顺序为严格整数 `0,1,2,3,4`；instrumentation 为准确布尔值。已知操作为 `live`、`project`、`encode`、`decode`、`model`、`chain`。两个 bad case 只接受 `decode`。

| 模式 | `ns` | 六个分配／回调字段 |
|---|---|---|
| Normal | 0 至 `2**63-1` 的整数，单位为纳秒 | 全部 `null` |
| Instrumented | `null` | 0 至 `2**64-1` 的整数 |

布尔值、浮点数、字符串和缺失值不能冒充整数。真实的零时间可以表示；未观测值不能改成零。严格完整字段集合、有界表示及下述更强分配关系属于新增适配器要求。保留的独立审计已要求严格 HWM 类型及跨 trial 单调性，尽管原 runner 的部分比较较弱。

## 4. 指标关系与单位

请求载荷须满足 `requested_live_at_return <= requested_peak_live <= requested_bytes`、`largest_request <= requested_peak_live` 和 `requested_bytes <= new_calls * largest_request`。new 调用数为零时，各请求载荷字段必须为零。new 调用数为正而请求载荷值全零仍然合法，因为允许零大小请求。Key callback 独立于分配次数；bad-tail instrumented 调用要求 key callback 及返回时仍活跃的请求载荷为零。后者仅针对不拥有数据的尾部拒绝路径：作用域内 Error 对象在 lambda 仅返回 offset、observer 对 epoch 取快照前已经销毁。

这些值统计观测 epoch 中成功的 C++ new 请求载荷，排除 allocator header、alignment 开销、未观测的分配机制和持续存活的 preparation 基线。它们不是复制字节数、总 heap 或 RSS。

两个 HWM 字段都是正有符号 64 位整数，单位为 Linux KiB。同一 trial 中 before 不得超过 after；下一 trial 的 before 不得低于前一条 after。该关系限于一个进程，不跨 call。HWM 包含 preparation 和以前 trial；两值相减不能得到操作独占的峰值内存。

`consumed` 是正无符号 64 位、混合单位的结果哨兵。每个 trial 都必须匹配冻结的预期值：

| 操作 | 对此生产者的绑定 |
|---|---|
| 正向 `project` / `decode` | `records + 1`，因为这份准确 fixture 保留一条 retain |
| Bad `decode` | `wire_bytes - 1`，追加尾部字节的零起始 offset |
| `live` / `model` | 明确冻结的哨兵，对应 model records 加 compile entries |
| `encode` | 明确冻结的哨兵，对应 wire 长度加无符号末字节 |
| `chain` | 明确冻结的哨兵，对应四个拥有结果的哨兵之和 |

适配器不推断通用的 `2 * records` 规则，也不把 wire 长度直接视作 encode 哨兵。它也不从 bad-tail 返回推断错误无分配：lambda 在返回 offset 前检查并销毁 Error。准确历史分配量不构成接受阈值。

## 5. 有界纯 API

```python
from analysis_cost_semantics import freeze_cost_contract, validate_cost_output

contract = freeze_cost_contract(
    call_id="fixed-example", case="exe", operation="live", instrumented=False,
    prepared={"kind": "prepared", "case": "exe", "records": 1,
              "wire_bytes": 3405, "public_preparation_calls": 5,
              "model_equal": True, "checks": 115},
    expected_consumed=2,
)
# Retain contract.canonical and contract.sha256 outside the evidence bundle
# before accepting stdout. Supply the independently retained hash at audit.
```

`FrozenCostContract.canonical` 是不可变字节；`document()` 返回新副本。`validate_cost_output(raw, contract=contract, expected_contract_sha256=registered_hash)` 返回语义判定，或抛出 `CostSemanticError`。冻结非法预期输入时抛出 `ValueError`。预期值须在观测前独立建立，不能从被校验的输出中学习。

原始输入为 bytes，最多 64 KiB；恰有六条记录，每条最多 8192 字节，包括其 LF。允许 LF 和 CRLF，最后一个 LF 必须存在。重复 key、非整数数值 token、非有限数、非法 UTF-8、BOM、额外字段、空白或缺失记录以及错误顺序均被拒绝。契约最多 8192 字节，每次调用都按外部 SHA 和规范形态重新验证。

## 6. 封闭替身与驱动接线

| 固定模式 | 含义 |
|---|---|
| `cost_time` | 写死的 normal `exe/live` 输出 |
| `cost_allocation` | 写死的 instrumented `exe/live` 输出 |
| `cost_bad_tail_time` | 写死的 normal `bad_items_tail/decode` 输出 |
| `cost_bad_tail_allocation` | 写死的 instrumented `bad_items_tail/decode` 输出 |
| `cost_invalid_time` | 字节完整、但 `ns` 为布尔值的替身 |
| `cost_invalid_allocation` | 字节完整、但活跃载荷超过峰值的替身 |

六种模式均输出预定字节和空 stderr；时间、分配及 HWM 值都是合成字面值。原八种生命周期模式的准确 stdout/stderr 字节和行为保持。启动接口仍仅为 `call_id` 与封闭 `mode`，没有任意命令或真实生产者入口。

驱动协议与判定升级为 format 2，增加语义源码身份和各成本调用的规范 `semantic_contract`，绑定完整预期流字节，并按规范字节比较冻结调用文档，避免布尔／整数相等关系放宽准入。返回判定包含 `planned_semantic_calls`、`verified_semantic_calls` 和 `verified_cost_observations`；它们是合成校验次数，不是实测 API 消费。

公开驱动函数和原有硬上限保持：最多 8 calls、每流 1 MiB、共享接收字节 8 MiB、每 call 主动监督 3 秒、总体主动监督 20 秒。当前实现拒绝已归档的 format-1 协议；历史 bundle 须使用准确的 PR252 源码及外部身份检查。不得把旧协议或 seal 改写为新格式。

## 7. 停止与最终审计

业务校验前先保全两路流。语义错误成为持续有效的首错；capture 以 INVALID 完成封存，保留可得前缀，不启动下一计划调用。正确的 stdout hash、六条记录身份、零退出码及原 pipe EOF 都不能覆盖业务错误。

最终门禁重新打开并核对当前 spool/raw/backup 字节、调用记录、协议和 capture seal，再运行语义解析器，不信任先前的布尔结果。只有调用完整且未新增传输、副本或语义问题时，才增加语义验证计数。外部封存判定在返回前再次审计。

已知的启动／回收／接收字节观察在记录后续损坏时仍已消费；seal 不可信时，生命周期次数未知。语义层不增加退款、重试、修复或恢复执行路径。

## 8. 限制与下一决定

[PR252 监督边界](ANALYSIS_SUBSTITUTE_DRIVER_ZH.md)继续适用，包括仅限 Linux 的资格、默认 SIGCHLD 与独占回收责任、有界前缀，以及不得对失去或已回收的子进程身份发送信号。磁盘 hash 不认证已加载模块或生产者。末尾读取是顺序过程，不是原子快照或永久完整性保证。

Spool 写入、复制／读取／解析／hash、同步及最终审计均有开销，也可能影响后续调度或 cache。主动期限不覆盖每个最终 flush/fsync/close 或监督后操作。三份副本共享存储故障域。没有单独成立的协议时，不得把这些成本视为零或从 API 窗口扣除。

[独立决定准备](ANALYSIS_COST_DECISION_PREPARATION_ZH.md)保持 `PREPARATION_ONLY_BLOCKED`，真实执行未授权，PR250 成本／集成仍为 HOLD。它冻结已知身份和未决前提；替身语义成功不会开启新研究、补五条缺失观察、重开 001/002、授予产品权限或使 v5.7.0 获得发布资格。实际验证和下一接手点记录在 [walkthrough](20261009-140634-cost-semantics/walkthrough.md)。
