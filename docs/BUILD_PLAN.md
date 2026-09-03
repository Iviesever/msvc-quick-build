# 构建计划检查

**[English](BUILD_PLAN_EN.md) | 简体中文**

`mqb plan` 会根据当前源码树、配置、已选择的 MSVC 工具链，以及现有的项目本地缓存证据，说明 MQB 此刻会执行哪些增量构建工作。

```powershell
mqb plan
mqb plan src/main.cpp --format text
mqb plan src/main.cpp modules/math.ixx --no-discover --std latest --format json
```

该命令与真实构建共用同一套 typed scan、compile、link、archive、PCH 和 module graph authority。它不会执行编译、`/scanDependencies`、链接或归档，也不会创建或改写项目 `.mqb/` 状态。为了建立计划，工具链发现阶段仍可能检查本机 Visual Studio 环境。

## 输出格式

`--format text` 是默认的人类可读格式；`--format json` 输出确定性的 UTF-8 JSON，便于工具和测试消费。

每个步骤都会报告：

- `kind`，例如 `module_scan`、`pch`、`compile`、`link` 或 `archive`；
- `status`：`planned` 或 `up_to_date`；
- typed rebuild reasons；
- MQB 拥有的输出路径；
- 仅在步骤确实计划执行时给出精确的结构化进程配方。

进程配方包含 executable、按顺序排列的 argv、working directory、environment policy 和需移除的环境变量。已经 up-to-date 的步骤不会带 `process` 字段，因为真实构建也不会启动对应进程。

模块感知输出还可包含：

- `pipeline: "modules"`；
- `module_graph.status`：`pending` 或 `ready`；
- graph ready 时的按依赖层划分的 `compile_levels`；
- 步骤 `owner`：`project` 或 `toolchain`；
- 步骤 `role`：`translation_unit`、`module_interface` 或 `header_unit`；
- graph 节点所属的 compile `level`。

JSON 文档版本仍为 1；以上字段是在现有 plan 格式上的兼容性扩展。

## 普通目标、PCH 与静态库

普通 C/C++ 可执行文件和 DLL 的计划会检查 compile 与最终 link。First-class PCH 计划会显示 PCH creator，并把计划中的 PCH 重建传播到 consumer 与最终链接。静态库计划会显示精确且确定性的事务式 `lib.exe` 配方，同时仍以最终 `.lib` 作为公开输出。

## Modules 与 Header Units

MQB 不会通过文件名或源码拼写猜 module topology。只有当每一个必需源文件都存在可复用的 MSVC P1689 证据时，dependency graph 才是可信的。

### 冷项目或过期 topology

对于首次构建的 module target，或任意必需 P1689 证据已经过期时，`mqb plan` 只会输出精确的待执行 `/scanDependencies` 步骤，并报告：

```text
pipeline: modules
graph:   pending
```

在这个边界上，MQB **不会**虚构 provider、compile wave、Header Unit producer、`std` / `std.compat` provider 或 link decision，因为这些阶段依赖尚未生成的扫描结果。

运行一次真实构建即可生成并验证缺失的证据：

```powershell
mqb build src/main.cpp modules/math.ixx --no-discover --std latest
```

### 可信的 warm topology

当全部必需 P1689 证据均可复用时，计划会继续进入真实 provider graph，并报告：

- project-local named-module provider；
- project-local Header Unit producer；
- 作为只读 dependency 的显式 external/prebuilt IFC provider；
- 属于所选 MSVC 工具链的 `std` / `std.compat` provider；
- 按依赖层划分的编译顺序；
- 计划节点的精确 `/reference` 与 `/headerUnit` 编译配方；
- 最终 incremental link decision。

如果 topology 仍然可信，但某个 provider IFC 丢失，计划会重建该 provider；下游 consumer 会得到 typed `explicit rebuild` reason，最终链接也会进入计划。Inspection 本身不会修复该文件。

如果源码变化导致 P1689 过期，graph 会重新变成 `pending`；旧 compile levels 和 link decision 会被抑制，而不是以仍然可信的样子继续展示。

## 无副作用契约

对于项目构建状态，`mqb plan` 是只读操作：

- 不执行 compile 或 module scan；
- 不执行 `link.exe` 或 `lib.exe`；
- 不生成 OBJ、IFC、PCH、EXE、DLL 或静态库；
- 不写 dependency metadata；
- 不写 compile、link、archive、PCH 或 scan cache；
- 不修复缺失或损坏的产物。

计划描述的是当前证据。之后的真实构建会在执行时重新检查文件系统与缓存状态，不会盲目信任更早生成的 plan snapshot。

## 当前边界

- executable 与 DLL 支持 module-aware planning；
- 需要 Modules/Header Units pipeline 的 static-library target 仍会 fail closed，因为该组合尚未实现；
- first-class PCH 仍不能与 Modules/Header Units pipeline 混用；
- `mqb compdb` 尚未导出 graph-aware Modules/Header Units 条目。
