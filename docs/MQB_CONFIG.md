# `mqb.json` 项目配置 v1

**语言：简体中文 | [English](MQB_CONFIG_EN.md)**

`mqb.json` 是 MQB 的项目配置文件。MQB 会从执行目录向上查找最近的 `mqb.json`；包含该文件的目录将成为项目根目录以及 `.mqb/` 构建产物的根目录。

## 最小配置文件

```json
{
  "version": 1
}
```

`version` 为必填项。Version 1 使用严格 JSON 规范：不允许注释、尾随逗号、重复键、未知 schema 字段、不正确的字段类型以及字符串数组中的空条目。

## 完整示例

```json
{
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x64",
    "standard": "23",
    "type": "exe",
    "runtime": "MT",
    "ltcg": true,
    "subsystem": "console",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include", "third_party/include"],
    "library_dirs": ["third_party/lib"],
    "libraries": ["math", "codec.lib"],
    "compiler_args": ["/W4"],
    "linker_args": ["/OPT:NOREF"]
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests", "tools"],
    "extra_sources": ["src/manual_adapter.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  }
}
```

## Build 构建字段

| 字段 | 类型 | 可选值 / 含义 |
|---|---|---|
| `configuration` | 字符串 | `debug` 或 `release` |
| `architecture` | 字符串 | `x86` 或 `x64` |
| `standard` | 字符串 | `14`、`17`、`20`、`23` 或 `latest`；也接受 `c++...` 拼写 |
| `type` | 字符串 | `exe`、`dll` 或 `static`；也接受 `executable`、`dynamic` 和 `lib` 别名 |
| `runtime` | 字符串 | `MD`、`MDd`、`MT` 或 `MTd` |
| `ltcg` | 布尔值 | 联动 `/GL` 编译 + 下游 `/LTCG` 策略 |
| `subsystem` | 字符串 | `console` 或 `windows`；对静态库无效 |
| `output` | 字符串 | `.mqb/bin/` 下的目标文件名；MQB 会自动补全目标扩展名 |
| `defines` | 字符串数组 | 不带 `/D` 前缀的预处理器宏定义 |
| `include_dirs` | 字符串数组 | 头文件搜索目录 |
| `library_dirs` | 字符串数组 | 库搜索目录；对静态库无效 |
| `libraries` | 字符串数组 | 依赖库；对静态库无效 |
| `compiler_args` | 字符串数组 | 有序的原样 `cl.exe` argv 选项 |
| `linker_args` | 字符串数组 | 有序的原样 `link.exe` argv 选项；对静态库无效 |

所有路径类配置项均相对于包含 `mqb.json` 的目录解析，而不是 Shell 的当前工作目录。

`type`、`runtime`、`ltcg` 和 `subsystem` 是强类型策略。MSVC 后端拥有其命令行拼写的绝对控制权，且类型化策略优先于冲突的原始参数。

Typed LTCG 为联动策略：`build.ltcg: true` 会为受影响的编译添加 `/GL`，并为下游目标添加 `/LTCG`。可执行文件/DLL 目标将 `/LTCG` 传给 `link.exe`；静态库目标将其传给 `lib.exe`。

DLL 导入库（import library）确定性地放在 `.mqb/bin/` 下对应 DLL 的旁边。静态库目标使用专门的 `lib.exe` 归档流水线与独立的归档缓存元数据。

原始编译器/链接器参数是字面 argv 元素。MQB 不会把包含空格的单个 JSON 字符串拆分为多个开关。

## Discovery 源码发现字段

| 字段 | 类型 | 含义 |
|---|---|---|
| `enabled` | 布尔值 | 开启或关闭单入口智能源码发现 |
| `exclude_dirs` | 字符串数组 | 构建图生成前直接剪枝排除的项目精确目录 |
| `extra_sources` | 字符串数组 | 即使与入口图断开也强制追加包含的受支持翻译单元精确路径 |
| `exclude_sources` | 字符串数组 | 强制排除并作为遍历屏障的受支持翻译单元精确路径 |

受支持的翻译单元扩展名包括：

```text
C 普通源码：        .c
C++ 普通源码：      .cpp .cc .cxx
Module 接口文件：   .ixx .cppm .mpp
```

C 源码参与普通 include/main 发现，但绝不会被解析为 C++ module 语法。

Version 1 使用精确路径而非 glob 通配符。普通 `extra_sources` 条目中不得再定义另一个 `main()`。入口 TU 本身不能被排除。同一源码不能同时出现在 `extra_sources` 和 `exclude_sources` 中。

内置排除项（如 `.mqb`、`.git`、`.vs`、`build`、`out` 和 `cmake-build-*`）在配置排除项之外保持生效。

### 命名模块与 Header Units

项目本地命名模块（project-local named modules）与项目本地 header units 不需要单独的 v1 配置章节。智能发现会自动筛选可达的本地 module-provider 候选，但 MSVC `/scanDependencies` P1689 元数据仍对 provider 选择、依赖排序、歧义/循环诊断与未解决依赖保持权威地位。

项目本地 header-unit IFC 会被自动分配、构建、跟踪新鲜度并修复。Modules 与 header units 需要 C++20 或更新的标准。

外部/预编译命名模块提供者（external/prebuilt named-module providers）以及 `import std` 仍明确不支持并 fail closed；该功能边界跟踪于 Issue #16。

## 优先级规则

标量选项优先级：

```text
显式 CLI 选项 > mqb.json > 内置默认值
```

包括 `configuration`、`architecture`、`standard`、`type`、`runtime`、`ltcg`、`subsystem` 和 `output`。

列表选项按确定性顺序追加：

```text
mqb.json 条目，接着是 CLI 条目
```

适用于 defines、include 目录、library 目录、libraries、`compiler_args` 和 `linker_args`。

## 路径基准 (Path Bases)

项目中存在两个明确的相对路径基准：

```text
CLI 相对路径     -> 当前执行目录 (invocation directory)
mqb.json 相对路径 -> mqb.json 所在目录
```

例如在如下结构中：

```text
project/
  mqb.json
  main.cpp
  include/
  nested/work/
```

从 `project/nested/work` 运行：

```powershell
mqb ../../main.cpp
```

仍然会加载 `project/mqb.json`，把构建产物放在 `project/.mqb/` 下，并将 `"include_dirs": ["include"]` 解析为 `project/include`。

## 缓存行为

配置文件的时间戳不是全局重新构建的触发器。生效的类型化选项会参与 compile/link/archive 的唯一标识计算：

- 更改 `runtime` 会改变编译配方标识；
- 更改 `ltcg` 会改变编译标识以及下游 link/archive 标识；
- 更改 `type` 会改变下游目标归属，但当编译策略未变时不会强制重新编译无关文件；
- 仅更改 `subsystem` 会改变链接标识并重新链接；
- 更改编译器参数会改变编译标识；
- 更改链接器参数或显式库/搜索路径会改变链接标识；
- 命名模块/header-unit 的编译标识包含 typed provider/reference 与 IFC 输出标识。

缺失的记录输出会导致新鲜度失效，并由相应的 compile、link 或 archive 操作自动修复。

## 并行度

`-j/--jobs` 仅属于执行策略，故意不属于 v1 配置文件字段：

```powershell
mqb main.cpp -j 8
```

更改并发任务数不会改变 compile、link 或 archive 签名。

## 当前边界

- `build.type` 支持 `exe`、`dll` 和 `static`。
- `build.ltcg` 为 compile/downstream 联动布尔策略。
- 静态库目标支持普通 C/C++ 翻译单元；需要命名 Modules/Header Units 的静态库目标在归档拓扑显式验证前维持 fail closed。
- `--run` 仅限可执行文件。
- v1 配置没有外部/预编译 module-provider 或 `import std` 策略。
- v1 配置不保存并行任务数。
- discovery 修正字段使用精确路径而非通配符。
- 显式用户库具有新鲜度跟踪；间接 `/DEFAULTLIB` 传递依赖不承诺完全跟踪。

更广泛的 build/module/cache 架构见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。稳定版自举说明见 [`SELF_HOSTING.md`](SELF_HOSTING.md)。
