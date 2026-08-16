# `mqb.json` 配置参考

**简体中文 | [English](MQB_CONFIG_EN.md)**

`mqb.json` 是 MQB 的版本化项目配置文件。MQB 从 invocation directory 向上查找最近的 `mqb.json`；找到后，该文件所在目录同时成为：

- project root；
- 配置与 profile 相对路径的基准；
- `.mqb/` 构建状态的根目录。

## 1. 最小配置

```json
{
  "version": 1
}
```

`version` 必填。v1 使用严格 JSON：重复 key、未知字段、错误类型、空字符串条目、注释和尾随逗号都会报错，而不是被忽略。

根对象只接受：

```text
version
build
discovery
modules
profiles
```

## 2. 完整示例

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "configuration": "release",
    "architecture": "x64",
    "standard": "23",
    "type": "exe",
    "runtime": "MT",
    "ltcg": true,
    "subsystem": "console",
    "pch": "include/pch.hpp",
    "output": "game",
    "defines": ["GAME_BUILD=1"],
    "include_dirs": ["include", "third_party/include"],
    "library_dirs": ["third_party/lib"],
    "libraries": ["user32", "codec.lib"],
    "compiler_args": ["/W4"],
    "linker_args": ["/OPT:NOREF"],
    "librarian_args": []
  },
  "discovery": {
    "enabled": true,
    "exclude_dirs": ["tests", "tools"],
    "extra_sources": ["src/manual_adapter.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  },
  "modules": {
    "external": {
      "vendor.math": "third_party/ifc/vendor.math.ifc"
    }
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "runtime": "MDd",
        "pch": false,
        "compiler_args": ["/W4"]
      },
      "discovery": {
        "enabled": true
      }
    },
    "release": {
      "build": {
        "configuration": "release",
        "ltcg": true,
        "compiler_args": ["/O2"]
      }
    }
  }
}
```

## 3. `build`

| 字段 | 类型 | 含义 |
|---|---|---|
| `entry` | string | `mqb build` / `mqb run` 未提供 positional source 时使用的默认入口；路径相对 `mqb.json` |
| `configuration` | string | `debug` / `release` |
| `architecture` | string | `x86` / `x64` |
| `standard` | string | `14`、`17`、`20`、`23`、`latest`；也接受 `c++...` 拼写 |
| `type` | string | `exe`、`dll`、`static`；也接受 `executable`、`dynamic`、`lib` |
| `runtime` | string | `MD`、`MDd`、`MT`、`MTd` |
| `ltcg` | boolean | 联动编译 `/GL` 与下游 `/LTCG` |
| `subsystem` | string | `console` / `windows`；static target 不接受 |
| `pch` | string / `false` | 为普通 C++ target 启用 first-class PCH；路径相对 `mqb.json`；`false` 显式关闭前层 PCH policy |
| `output` | string | `.mqb/bin/` 下的目标名，扩展名由 target kind 决定 |
| `defines` | string[] | 不带 `/D` 的宏定义 |
| `include_dirs` | string[] | include search path |
| `library_dirs` | string[] | library search path；static target 不接受 |
| `libraries` | string[] | link libraries；static target 不接受 |
| `compiler_args` | string[] | 按顺序传给 `cl.exe` 的原始 argv element |
| `linker_args` | string[] | 按顺序传给 `link.exe` 的原始 argv element；static target 不接受 |
| `librarian_args` | string[] | 按顺序传给 `lib.exe` 的原始 argv element；只允许 static target |

### 默认入口

`build.entry` 是项目级默认入口，不是 source list。它只在下列形式没有显式 positional source 时参与解析：

```powershell
mqb build
mqb run
```

优先级固定为：

```text
显式 positional source(s)
    > build.entry
    > conventional fallback
```

conventional fallback **不会递归扫描项目**。它只检查：

```text
<project-root>/main.c
<project-root>/main.cpp
<project-root>/main.cc
<project-root>/main.cxx
<project-root>/src/main.c
<project-root>/src/main.cpp
<project-root>/src/main.cc
<project-root>/src/main.cxx
```

只有恰好一个候选存在时才会采用；0 个候选要求显式 source 或 `build.entry`，多个候选要求用户消歧。若已经配置 `build.entry` 但对应文件缺失或扩展名不受支持，MQB 会直接报错，不会悄悄回退到 conventional main。

显式 source 始终优先，例如：

```powershell
mqb run tools/tool.cpp
```

即使项目设置了 `build.entry`，这里仍构建并运行 `tools/tool.cpp`。

`build.entry` **只能声明在根 `build` 层**。Profile 是构建策略 overlay，不能通过 `profiles.<name>.build.entry` 改变项目身份；该字段会被 strict schema 直接拒绝。

### First-class PCH

`build.pch` 是 scalar policy。启用时必须给出非空 header path：

```json
{
  "version": 1,
  "build": {
    "pch": "include/pch.hpp"
  }
}
```

布尔 `true` 被拒绝，因为“启用但不指定 header”无法形成稳定 PCH identity。`false` 用于显式关闭更低层继承的 PCH：

```json
{
  "version": 1,
  "build": {
    "pch": "include/pch.hpp"
  },
  "profiles": {
    "clean": {
      "build": {
        "pch": false
      }
    }
  }
}
```

CLI 等价/覆盖形式：

```powershell
mqb build --pch include/pch.hpp
mqb build --profile dev --no-pch
```

PCH policy 遵循：

```text
显式 CLI > selected profile > base mqb.json > disabled default
```

配置/profile PCH 路径相对 `mqb.json`；`--pch` 相对 invocation directory。

MQB 自己拥有 synthetic creator、`.pch`、配对 creator `.obj`、`/FI`、`/Yc` / `/Yu`、`/Fp` 和 PCH cache/dependency tracking。Raw PCH structural switches 不能绕过这套 ownership。`.pch` 会成为普通 consumer compile cache 的显式 dependency；PCH creator 重建也会强制 downstream link/archive。完整 artifact/invalidation 契约见 [`PRECOMPILED_HEADERS.md`](PRECOMPILED_HEADERS.md)。

当前 first-class PCH 只支持普通 C++ source set，可用于 `exe` / `dll` / `static`；与 C TU 或 Modules/Header Units pipeline 组合时 fail closed。

### Typed policy

`type`、`runtime`、`ltcg`、`subsystem`、`pch` 是 MQB 的强类型/结构化策略，不是字符串形式的 MSVC flag 别名。MQB 后端负责最终命令行拼写，冲突的 raw compiler/linker/librarian argument 不应被用来绕过 typed policy 或 MQB-owned artifact routing。

`ltcg: true` 同时影响 compile 与 downstream target：

- compile：`/GL`；
- executable / DLL：linker `/LTCG`；
- static library：librarian `/LTCG`。

DLL import library 与最终 DLL 一起位于 `.mqb/bin/`。Static target 使用独立的 `lib.exe` archive pipeline 和 archive cache。

### Librarian policy

Static library 的原生 librarian 参数可以直接写在配置中：

```json
{
  "version": 1,
  "build": {
    "type": "static",
    "librarian_args": ["/WX", "/EXPORT:math_entry"]
  }
}
```

CLI 对应形式是：

```powershell
mqb build math.cpp --type static /lib /WX /EXPORT:math_entry
```

`/lib` 是 librarian boundary 的唯一公开拼写；`/LIB` 也接受。`-lib` / `-LIB` 不作为 boundary，并在 CLI 上明确拒绝，因为 `-l <name>` / `-l<name>` 已属于 MQB 的 link-library shorthand。

`/lib` 必须至少跟一个 librarian 参数，重复 `/lib` / `/LIB` 会 fail closed。其后的参数进入 `MsvcParameterEngine::route_librarian()`：MQB-owned `/OUT` 等 structural 参数不能覆盖 artifact ownership；错误工具的参数也会拒绝。最终 `librarian_args` 只允许 static target，exe / DLL 使用时 fail closed。

## 4. `discovery`

| 字段 | 类型 | 含义 |
|---|---|---|
| `enabled` | boolean | 是否启用单入口 smart discovery |
| `exclude_dirs` | string[] | 在建图前剪枝的项目目录 |
| `extra_sources` | string[] | 强制追加的精确 translation-unit 路径 |
| `exclude_sources` | string[] | 强制排除并作为遍历屏障的精确路径 |

支持的 translation-unit 扩展名：

```text
C:                  .c
C++:                .cpp .cc .cxx
module interface:   .ixx .cppm .mpp
```

规则：

- `build.entry` 或唯一 conventional fallback 解析出的入口，与显式单入口一样进入 smart discovery；
- 多个 positional source 本身就是精确 source set，不依赖 smart discovery；
- v1 使用精确路径，不支持 glob；
- entry TU 不能被排除；
- 同一文件不能同时出现在 `extra_sources` 与 `exclude_sources`；
- 内置排除目录（如 `.mqb`、`.git`、`.vs`、`build`、`out`、`cmake-build-*`）始终生效。

Discovery 只负责候选源码选择。Header freshness 的真值来自 `/sourceDependencies`；module topology 的真值来自 `/scanDependencies` / P1689。

## 5. `modules`

### Project-local modules / header units

Project-local named modules 与 header units 不需要额外配置。MQB 会让候选源码进入 `/scanDependencies` / P1689 pipeline，再根据真实 provider graph 决定依赖与编译顺序。

Modules/Header Units 需要 C++20 或更新语言模式。

### External / prebuilt named modules

`modules.external` 是 logical module name 到 **只读 IFC** 的显式映射：

```json
{
  "version": 1,
  "modules": {
    "external": {
      "vendor.math": "third_party/ifc/vendor.math.ifc",
      "vendor.io": "C:/sdk/vendor.io.ifc"
    }
  }
}
```

配置中的相对 IFC 路径以 `mqb.json` 所在目录为基准。

CLI 等价形式：

```powershell
mqb main.cpp --module-ifc vendor.math=C:\sdk\vendor.math.ifc
```

行为规则：

- external IFC 是 dependency，不是 MQB-owned writable artifact；
- MQB 不会把 external IFC 加入 source discovery 或 compile levels；
- provider 缺失会在进入真正 consumer compile 前 fail closed；
- provider identity 会进入 consumer compile/cache identity，替换 IFC 会使相关 cache 失效；
- base config、selected profile 与 CLI 按 logical module name 分层合并，后层同名 provider 定点覆盖前层；
- CLI 内重复声明同一 logical module name 会报错。

### `std` / `std.compat`

`std` 与 `std.compat` **不属于** `modules.external` registry。它们由当前选中的 MSVC toolchain 提供，用户不能通过配置或 `--module-ifc` 伪造或覆盖。

当 P1689 requirement 实际出现 `std` / `std.compat` 时，MQB 才会查询当前 VC Tools 的标准库 module source，并把它作为 toolchain-owned provider 注入同一 module graph。当前 MSVC 标准库 named modules 要求满足 toolchain capability，并使用 `--std latest`。

## 6. `profiles`

`profiles` 是 profile name 到配置 overlay 的对象。每个 profile 只接受：

```text
build
discovery
modules
```

它们复用根层完全相同的 typed policy、list、路径和 module-provider 解码规则，但 `build.entry` 除外：profile 内禁止设置入口。`build.pch` 可以在 profile 中用另一条 header path 覆盖 base，也可以用 `false` 关闭 base PCH。

示例：

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "type": "static",
    "standard": "23",
    "pch": "include/pch.hpp",
    "defines": ["PROJECT=1"],
    "librarian_args": ["/EXPORT:base_symbol"]
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "runtime": "MDd",
        "pch": false,
        "defines": ["DEV=1"],
        "compiler_args": ["/W4"],
        "librarian_args": ["/WX"]
      }
    },
    "release": {
      "build": {
        "configuration": "release",
        "ltcg": true,
        "compiler_args": ["/O2"]
      }
    }
  }
}
```

选择方式：

```powershell
mqb build --profile release
mqb run --profile dev -- input.txt
```

也接受：

```powershell
mqb build --profile=release
```

当前契约刻意保持简单且确定：

- 一次 invocation 最多选择一个 profile；重复 `--profile` 报错；
- 不存在 profile inheritance；
- 不支持多 profile stacking；
- 没有隐式 default profile；未写 `--profile` 就只使用 base config + CLI；
- `--profile` 需要项目存在 `mqb.json`；
- 未找到 profile 时 fail closed，并列出可用 profile 名；
- profile 中的 relative path（包括 `pch`）与 base config 一样，以 `mqb.json` 所在目录为基准；
- profile 中的 `compiler_args` / `linker_args` / `librarian_args` 仍进入同一 MSVC Parameter Engine，semantic option 会先在 profile 自身层归一化，然后再由更高优先级 CLI 覆盖；
- profile 可以选择 `type` / `pch` 等 policy，因此最终 target 仍受同一 executable/static/DLL/PCH 合法性门禁约束。

## 7. 优先级

Scalar policy：

```text
显式 CLI > selected profile > base mqb.json > built-in default
```

包括：

```text
configuration
architecture
standard
type
runtime
ltcg
subsystem
pch
output
```

其中 PCH 的 built-in default 是 disabled；`pch: false` 与 `--no-pch` 都是显式 scalar override。

同一层内，typed policy 与等价 native MSVC semantic option 必须一致，否则在进入 toolchain 前 fail closed。例如 profile 同时写 `runtime: "MT"` 与 `/MD` 会被拒绝。PCH 的 `/Yc`、`/Yu`、`/Fp` 等 structural switches 由 MQB ownership model 直接保留，不属于可与 `pch` 并行配置的 raw semantic alias。

入口选择是独立的 source-selection policy：

```text
显式 positional source(s) > root build.entry > 唯一 conventional main
```

普通 list-like 输入按顺序追加：

```text
base mqb.json entries -> selected profile entries -> CLI entries
```

适用于 defines、include dirs、library dirs、libraries、compiler args、linker args、**librarian args**，以及 discovery list corrections。对于 librarian，CLI list 来自 `/lib` tail；因此最终顺序为：

```text
build.librarian_args
-> profiles.<name>.build.librarian_args
-> CLI /lib <...>
```

External module provider registry 按 logical module name 合并；同名后层项定点覆盖前层项，而不是制造重复 provider。

## 8. 路径基准

MQB 明确区分两种相对路径：

```text
CLI relative path                 -> invocation directory
mqb.json / profile relative path  -> directory containing mqb.json
```

`build.entry` 与 `build.pch` 都属于 config-relative 路径；`entry` 只能出现在 root build 层，而 `pch` 可被 profile 覆盖。CLI `--pch` 始终使用 invocation-relative 路径。

例如：

```text
project/
├─ mqb.json
├─ src/main.cpp
├─ include/pch.hpp
├─ include/
└─ nested/work/
```

配置：

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp",
    "include_dirs": ["include"]
  },
  "profiles": {
    "dev": {
      "build": {
        "include_dirs": ["third_party/dev/include"]
      }
    }
  }
}
```

从 `project/nested/work` 运行：

```powershell
mqb run --profile dev
```

仍会加载 `project/mqb.json`，把 entry 解析为 `project/src/main.cpp`、PCH 解析为 `project/include/pch.hpp`、base include dir 解析为 `project/include`、profile include dir 解析为 `project/third_party/dev/include`，并把 writable artifacts 放到 `project/.mqb/`。

显式 CLI source/path 仍按 invocation directory 解析：

```powershell
mqb ../../src/main.cpp --pch ../../include/pch.hpp
```

## 9. Cache 语义

MQB 不以“配置文件时间戳变化”或“profile 名变化”作为全量 rebuild 信号；它根据**最终生效的构建语义**计算 identity。

`build.entry` 自身不额外进入 compile/link identity；它只决定本次 source-selection 的入口。Profile name 也不是额外 cache 维度。两个 profile 如果解析成完全相同的 effective build policy/source graph，应复用同一套既有 cache identity。

PCH 的 identity 由最终 effective PCH header/artifact/role 与正常 compiler/toolchain semantics 共同决定；profile 名本身不作为额外维度。PCH creator 使用独立 compile cache，`.pch` 同时是 consumer compile cache dependency。缺失 `.pch` 会触发 creator repair；PCH creator 重建会使 consumers 失效并强制 downstream link/archive。

典型影响：

- `runtime` / compiler args / typed module references 改变 compile identity；
- `pch` 改变 PCH creator/consumer compile identity，并通过 `.pch` dependency 传播 freshness；
- `ltcg` 同时改变 compile 与 link/archive identity；
- `subsystem` 只影响 link identity；
- libraries / library dirs / linker args 改变 link identity；
- **librarian args 改变 archive recipe/cache identity，并触发 static target 重新归档**；
- `type` 改变 downstream target ownership；
- discovery/profile corrections 通过最终 source set 影响参与构建的 TUs；
- external/prebuilt IFC identity 改变会使依赖它的 consumer compile cache 失效；
- selected MSVC toolchain identity 改变时，不会静默复用不兼容的 toolchain-owned standard-library IFC。

缺失的已记录输出也会使相应 compile/link/archive/PCH cache 失效并触发修复。

`-j/--jobs` 是执行策略，不属于 build identity，也不是 v1 配置字段。

## 10. 当前明确边界

- `exe`、`dll`、`static` 均受支持；
- `mqb run` 与 source-first `--run` 仅适用于 executable；
- ordinary C++ `exe` / `dll` / `static` 支持 first-class PCH；
- first-class PCH 与 C TU 或 Modules/Header Units pipeline 组合时当前 fail closed；
- static target 不接受 subsystem、library paths/libraries、linker args；
- librarian args 只允许 static target；native librarian CLI 使用 `/lib` / `/LIB`，不支持 `-lib` / `-LIB`；
- **需要 Modules/Header Units pipeline 的 static-library target 当前仍 fail closed**；
- discovery correction 使用精确路径，不支持 glob；
- default-entry conventional fallback 不递归、不使用 glob；
- profile 当前仅支持单个显式选择，不支持继承、多 profile stacking 或 implicit default profile；
- profile 不能覆盖 `build.entry`；
- 间接 `/DEFAULTLIB` 传递依赖不承诺完全的新鲜度跟踪。

PCH artifact/ownership 细节见 [`PRECOMPILED_HEADERS.md`](PRECOMPILED_HEADERS.md)；更底层的 provider graph、artifact identity 与职责边界见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。