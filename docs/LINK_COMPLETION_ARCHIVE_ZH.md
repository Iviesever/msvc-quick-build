# 已完成链接结果的显式存档

[English](LINK_COMPLETION_ARCHIVE.md)

## 范围

`archive_link_completion(completed, destination, capture_label)` 只借用调用者已经获得的
const `ObservedLinkResult`。先调用原 `capture_link_fact_snapshot`，然后调用原
`create_link_fact_snapshot_file` 一次。不启动第二次构建，不再调用观察器，不检查当前输出、
加载缓存或选择默认路径。没有默认 build/CLI 接线。失败构建没有 `ObservedLinkResult`；
调用者处理原失败，不执行存档。

## 独立结果

返回文件回执或 `LinkCompletionArchiveError`，后者是原 capture 错误与原文件错误的 variant。
原生阶段、错误码、已知字节数、是否创建文件、附加关闭错误及嵌套 codec 错误全部保留。
原构建成功/复用、警告、进程诊断和观察值不会被移动或改写。capture 拒绝时不调用写者；
文件失败不重试。内存分配和非预期异常继续传播，调用者仍持有原输入。

```cpp
// completed 是已成功获得的 ObservedLinkResult，destination 由调用者明确指定。
auto archived = mqb::orchestration::archive_link_completion(
    completed, destination, "caller annotation");
// 单独处理 archived，失败不抹掉 completed.build。
if (archived) {
    auto historical = mqb::platform::windows::read_link_fact_snapshot_file(destination);
    // 这是独立的后续读取，不是自动写入验证或回执。
}
```

## 文件与权限边界

原[单文件契约](LINK_FACT_FILE_ZH.md)和有界严格 JSON v1 编解码器不变：绝对本地 NTFS 路径、
只新建、1 MiB 上限，不覆盖、不建父目录、不重试、改名或清理。写入失败可能留下部分或完整文件；
不会隐式读回来把失败改成成功。回执或合法历史 JSON 都不证明生产者身份、当前内容、完整库存或
删除权限。标签不是可信时间或代际编号。

不增加崩溃原子提交、目录持久化、事务、跨调用写者协调，或针对全部映射/特权变更的保护。
同步文件 IO 无硬超时。默认性能矩阵不覆盖显式文件 IO 成本；非默认使用不等于零成本。

## 验证与证据保留

私有操作接缝以合成写者执行同一 capture/转发主体，验证成功回执、原 capture 拒绝、文件阶段
字段和异常；它们不是 Windows 磁盘故障实测。Windows E2E 使用一次真实源码编译及四次链接完成
调用（三个链接器进程，包含一次预期失败）；三次观察先于七次显式存档尝试。
覆盖冷构建/复用/带警告结果的读回、旧名冲突、父目录缺失、capture 拒绝及共享冲突。
存档不能增加工具、构建或观察调用；选中输出/缓存的字节和时间保持不变。

`storage-evidence/completion-archive/` 保留逐进程请求/结果、存档尝试/结果、原映像/缓存字节、
三个历史 JSON、检查及完成计数。失败前缀保留。原 93 个原生测试程序内容不变，新增两个后为95。
准确候选必须通过原生 CI 才能宣称 Windows 验收，本地合成检查不能替代。此适配器不产生新性能
执行、默认采用、Rium/clean/prune 权限或正式发布。
