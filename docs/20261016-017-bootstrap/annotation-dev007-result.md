# 017：Python 3.12 注解预加载兼容性已收口

2026-10-16。接续 #198 / 6209644692、6209752312。
**同一 Python 3.12.3 二进制下，两角色 MODULE_OBJECT 阻塞均消失；这不是正式观察资格。**

## 实现、失败和修正

- 生产修复提交：`bd4ec0260f6e2d33faed20254302669cda595eba`，parent 24810b55…。
- 工作流启动修正／实际采集提交：`fc891ab00b0979ca962248cabfb303fac9e2c0df`。
- 生产代码仅 preflight 的8行导入／注解替换：Mapping来自collections.abc，Any改object。
  去注解的运行逻辑AST、PINS、类型严格比较、返回、目标几何保持；object不是静态Any等价物。
- 原335项不改，新增10项反例；旧代码3 FAIL，修订本地345 PASS，无错误或跳过。
- 五份318238 B历史JSON在原／新预检结果完全相同，不是新的目标准备。
- 原guard、collector、bootstrap、scope未修改，未删除／过滤缓存或放宽ModuleType检查。

初始dev007 run38862226375 / attempt1在workflow第10行因漏import sys而NameError。
此时未写任何证据文件，未启动单测或collector，上传因空目录失败；**该run没有原ZIP**。
原workflow及GitHub job116846886597日志保持，交付保存标明为摘录的关键失败行，
不将摘录称为完整日志。实际启动前缀本地复现NameError，修订前缀通过，无子进程。

按新登记、准确bd4ec026… parent在同workflow的run_number2 / attempt1执行新push，
不是旧run的Rerun。增加started.json和独立correction输出目录；36个源码blob、解释器
摘要、345项测试、两角色和预算全不变。未再次运行dev004/005/006。

## 实际 CI 与原件

| 项目 | 普通 bootstrap | dev007 修正采集 |
|---|---|---|
| run | 38862226382 | 38862912803 |
| run_number / attempt | 3 / 1 | 2 / 1 |
| job | 116846886414 | 116849077988 |
| 执行head | bd4ec026… | fc891ab00… |
| 单测 | 319 PASS | 345 PASS |
| artifact | 12029951365 | 12030386446 |
| ZIP bytes | 125790 | 164799 |
| 唯一成员／归档源码 | 38 / 35 | 61 / 36 |

两run全部步骤success，原件大小／SHA256／CRC、准确head/workflow_sha/run/attempt、
逐项ok、源码字节及两collector退出／reap／no-timeout／argv/env均已核对。
345包含319子集，不相加。系统库内容未上传，离线只核对记录关联，不用本机库冒充CI库。

```text
bootstrap ZIP 7c63e1a136b5023ee151170c6aeafa2b8929ada065edfb3a6a61a1b429eae418
dev007 ZIP    ebed9674b15415d64c528504d14177919a92534bf333f1ec4af1c61857646bb3
```

## 同一拟用解释器的实际差异

Ubuntu24.04 / image20261012.334.1，Python3.12.3；解释器SHA256仍为
`e50d468e8b0adfb05733f5b87b3cff34829c4a8c1aea50c865aa8bdfe4bb150f`。

| 角色 | dev006普通模块＋阻塞 | 修订普通模块＋阻塞 | 哈希文件 | 两pass内容字节 |
|---|---:|---:|---:|---:|
| owner | 107＋2 | 105＋0 | 123 | 39251382 |
| controller | 103＋2 | 101＋0 | 119 | 39188294 |

两角色普通模块仅减少typing/_typing，没有新增模块；typing.io/re不再因注解导入产生。
原件仍记录完整观察集，未生成allowlist；全部blockers=[]。每角色12个映射文件，前后
各4次读取并确认EOF，PT_INTERP匹配，前后文件／模块检查点稳定。两个collector各一次，
耗时364711570ns / 214261963ns，不是性能分数。静态ELF图仍12节点／29边，不代表解码器实测。

## 下一接手点

本项兼容性修复与同版本实际预加载验证已完成。不要再重写预检、清理typing缓存、
放宽guard或重跑已完成的dev007。现有collector不等于正式bootstrap精确模块集，也
不能将临时runner记录自动作为未来宿主的授权。

从本次结果继续真实decoder加载及精确bootstrap资格／输入准备的绑定，核对非后代
写者与FD外传前提、两份独立准备和最后控制器退出材料。原五份历史JSON继续复用，
无须重新寻找。先有审核依据再登记正式103B观察，不能现场自生成allowlist或authority。

本轮真实运行仅两个预加载collector，另有各工作流控制／单测解释器；owner/controller
主体、GNU decoder、原图像／实际103B、正式准备／观察均0。独立第二审阅者未签署。
main、HOLD/DEFER、VERSION5.6.0、已有PR及清理删除／release保持，v5.7.0未发布。
