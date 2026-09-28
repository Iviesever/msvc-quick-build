# 历史链接事实快照

**[English](LINK_FACT_SNAPSHOT.md) | 简体中文**

## 范围

这是 #228 和主线 `d3fc25572fb96654a79e14074fe88c63b1f7f80d` 之后，#198 的下一项独立、可选内存编解码能力。它不是代际台账、缓存格式、保留计划或 `clean/prune` 实现；没有 CLI 或普通构建默认调用，不访问文件系统或执行观察。

`LinkFactSnapshot` 拥有已经成功执行或复用的 `ObservedLinkResult` 的有限投影。失败构建没有这种成功结果。独立投影接口不接收协调器、请求、运行器或观察回调，不重复执行，不读取缓存、时钟或当前工作目录。

## API 与所有权

```cpp
#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"
// completed 是已经取得的成功 ObservedLinkResult。
auto facts = mqb::orchestration::capture_link_fact_snapshot(completed, "caller-label");
if (facts) {
    auto json = mqb::encode_link_fact_snapshot(*facts);
    if (json) {
        auto historical = mqb::decode_link_fact_snapshot(*json);
        // historical 仍是未经认证的历史数据，不是缓存或执行凭证。
    }
}
```

采集标签是可空的调用者注释，不是原子时间戳。路径仅复制为 UTF-8 标签，不规范化或解析。读回仍是字符串，因此在另一个操作系统读取 Windows 记录，也不会按读取者当前目录重新解释相对路径。非法编码会被拒绝，内存分配失败不会被吞掉。

## 版本 1 的覆盖字段

固定 schema 为 `mqb.link-fact-snapshot`，版本 `1`，覆盖标记 `link-main-output-v1`，来源等级 `unverified-historical-facts`。字段为：

- `capture_label`：字符串或 null。
- `build`：完成／缓存状态、linked 标记、输出／缓存／工作目录标签、签名高低两部分、链接器路径／版本／不透明 stamp、配置、架构、目标种类和带 code/path/message 的有序警告。
- `observation`：回调是否执行、请求路径、状态、历史物理 ID、可空逻辑／分配字节和硬链接数、已打开组件数、带 path/message/native_code 的有序问题。
- `authority`：producer_identity_verified、current_content_verified、complete_producer_inventory 和 deletion_authorized，必须全部精确为 `false`。

只覆盖这些字段，明确不保存对象／库清单、完整 LINK 选项、旁产物、进程输出或完整配方。签名两部分来自同一记录，不从不完整投影重算；没有转换回 `LinkCacheEntry` 的接口。

所有字段（包括可空字段）必须存在，每层对象都拒绝未知字段，不保存未知 schema 扩展。未知的元数据值仍为 null／空值，不猜补；已知逻辑大小零不等于缺失。枚举和键区分大小写。整数使用十进制无符号 JSON 整数，直接从原字面量读成 uint64/uint32，不经过 double。无法精确保留大于 2^53 整数的消费者不能重新解释这些数字。

## 校验与上限

编码与解码均检查完成／缓存状态一致性、枚举、UTF-8 和内嵌 NUL、诊断预算及已知硬链接数。非 observed 状态不能携带文件元数据。调用者提供的 observed 仍可带未知字段；该状态不认证真实句柄或回调。未调用观察器时，只允许 not_attempted 或适配器调用前的 invalid_path 拒绝。

上限：编码文档 1 MiB，单字符串 128 KiB，投影字符串合计 512 KiB，64 条警告，128 条问题，129 个已打开组件（128 个相对组件加根）。调用既有 `json::parse` 前，词法资源扫描限制嵌套 12 层、结构标点 4096 个；它不替代 JSON 语法解析。解析器拒绝解码后重复键（含转义别名）、非法转义、截断和尾部垃圾。快照边界拒绝 BOM，不改变其他 JSON 消费者。

负数、小数、指数、布尔值、引号内数字和越界整数不能强转为计数。编码时逐次追加检查字节上限；投影逐字段检查字符串预算后才保留副本。

## 信任与证据边界

读回有效快照只证明字节满足该模式。路径、ID、签名、调用者标签和消息都是未认证数据；摘要一致或历史物理 ID 不证明当前内容、生产者关联、独占或删除权。同路径替换不应修改历史值，更不能将它升级为当前观察。

解码拒绝把任一授权字段设为 true，不静默清除或升级外来权限；全部 false 也不代表数据来源已经可信。

原生测试含纯编解码和投影控制，测试程序内不启动编译器、链接器、观察器或文件探测。既有原生驱动仍用 MQB 构建这些测试，必须验收新候选自己的 Windows Debug/Release。可移植 C++ 测试不替代 Windows 路径转换、完整归属或性能验收。旧实验及不利样本保留为历史，不重跑，也不冒称本次新结果。

## 尚未实现

没有文件日志、默认持久化目录、代际退休、扫描、内容哈希、缓存回填、写者协作、恢复、删除或 Rium 验收。#198 最终范围不变；显式观察器成本及任何默认采纳仍须另行审阅。本代码不发布 v5.7.0，不重开原 #207/#701 或已消耗的 #787 实验。
