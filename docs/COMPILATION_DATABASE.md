# 编译数据库导出

**[English](COMPILATION_DATABASE_EN.md) | 简体中文**

`mqb compdb` 使用真实 MQB 构建所消费的同一套 typed MSVC compile recipe，生成确定性的 `compile_commands.json`。

```powershell
mqb compdb
mqb compdb src/main.cpp src/helper.cpp --no-discover
mqb compdb src/main.cpp modules/math.ixx --no-discover --std latest
mqb compdb --output out/compile_commands.json
```

数据库使用标准的 `directory`、`file`、`arguments` 和 `output` 字段。`arguments` 是 argv 数组，第一个元素为选中的 `cl.exe`；MQB 不会把 Windows 命令行压平成含义可能不明确的 shell 字符串。

## 副作用契约

该命令会完成项目/配置解析、源码发现、产物布局建模和 MSVC 工具链发现，但不会运行：

- `cl.exe` 编译；
- `cl.exe /scanDependencies`；
- `link.exe`；
- `lib.exe`；
- 目标程序。

它不会创建或修复 OBJ、IFC、PCH、依赖元数据、EXE、DLL 或静态库产物。除了发布用户指定的编译数据库外，它不应写入其他项目文件。工具链发现仍可按照 MQB 既有策略维护项目内的发现缓存。

输出按照 MQB 的 Windows 路径身份规则排序；在有效输入与工具链相同的情况下，输出字节应保持一致。

## 普通 C 与 C++

普通源码条目使用真实目标构建将采用的精确 typed compile recipe，包括：

- 选中的编译器可执行文件；
- 有序的原生编译器 argv；
- 源文件父目录作为工作目录；
- typed standard、architecture、runtime、defines 与 include directories；
- `/sourceDependencies` 元数据路径；
- MQB 拥有的 object 输出。

使用 first-class PCH 的项目会导出 consumer recipe，其中包含 `/Yu`、`/Fp` 和 `/FI`。合成 PCH creator 不是用户 translation unit，因此不会进入数据库；`mqb compdb` 也不会创建它。

## C++ Modules 与 Header Units

模块感知导出依赖真实依赖图，而不是根据文件名猜测。只有此前成功构建后，所有必需的 MSVC P1689 文档都仍可复用时，MQB 才能准确生成 `/reference` 与 `/headerUnit` 参数。

### 冷状态或拓扑过期

对于新模块项目，或源码变化导致必需 P1689 证据过期时，`mqb compdb` 会在发布输出前 fail closed：

```text
error: mqb compdb requires reusable P1689 topology for Modules/Header Units
```

诊断会指出待执行的扫描及其 typed reasons。MQB 不会主动执行这些扫描，也不会生成包含猜测 provider 或残缺 reference 的部分数据库。

推荐顺序：

```powershell
mqb plan src/main.cpp modules/math.ixx --no-discover --std latest
mqb build src/main.cpp modules/math.ixx --no-discover --std latest
mqb compdb src/main.cpp modules/math.ixx --no-discover --std latest
```

`mqb plan` 用于解释待执行的扫描；一次成功构建负责生成并验证 P1689 拓扑；之后 `mqb compdb` 才能导出精确 recipe。

如果旧的编译数据库已经存在，冷状态或过期状态下的失败会保留旧文件，而不是用部分 JSON 覆盖它。

### 可信的 warm 拓扑

当完整拓扑可复用时，数据库会包含以下 compile entries：

- 项目 translation units；
- 项目 named-module interfaces 与 partitions；
- 项目 Header Unit producers；
- 实际需要时，由所选 MSVC 工具链拥有的 `std` / `std.compat` providers。

显式 external/prebuilt IFC providers 只是只读图输入，不需要 MQB 编译，因此不会产生 compile entry。

Named-module consumer 会携带精确 `/reference name=<IFC>` 映射。Header Unit consumer 会携带精确 `/headerUnit:quote` 或 `/headerUnit:angle` 映射。JSON 条目按路径确定性排序；真实依赖执行顺序由 P1689 图与参数表达，而不是由数组位置表达。

### 主 `output` 字段

每个条目会暴露该 compile recipe 的主要产物：

| 条目类型 | `output` |
|---|---|
| 普通 translation unit | OBJ |
| named-module interface 或 partition | OBJ |
| named-module consumer | OBJ |
| Header Unit producer | IFC |
| 工具链拥有的 `std` / `std.compat` provider | OBJ |

Named-module interface 还会通过 `/ifcOutput` 生成 IFC；标准编译数据库 schema 只有一个 `output` 字段，因此 MQB 使用可参与链接的 OBJ 作为主输出，同时在 `arguments` 中保留精确 IFC 路径。Header Unit producer 是 IFC-only，所以其 `output` 为 IFC。

## 编译产物缺失与拓扑过期的区别

OBJ 或 IFC 缺失本身不会让仍可信的 provider graph 失效。在这种情况下，`mqb compdb` 依然会导出精确 recipe 与 reference，但不会修复缺失产物。

源码或依赖变化导致 P1689 过期则不同：provider 拓扑可能已经改变，因此必须先成功构建并重新密封扫描证据，导出才会继续。

## 与链接阶段解耦

编译数据库导出只使用 module target 的 scan/graph/compile inspection authority，不检查最终 link freshness。未解析的库或无关 linker 输出不应阻止有效编译数据库生成。需要 Modules/Header Units pipeline 的静态库目标仍不受支持，因为 MQB 本身尚未实现该产品组合。

## 接入工具

让编辑器、language server 或分析工具读取生成文件，通常路径为：

```text
<project>/compile_commands.json
```

使用方需要支持 MSVC 风格 argv，才能理解 `/std:`、`/reference`、`/headerUnit:*`、`/Yu` 与 `/Fp` 等参数。该数据库记录 MQB 的精确 MSVC 契约，不会把它翻译为 Clang 或 GCC 参数。
