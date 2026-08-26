# First-class PCH

**简体中文 | [English](PRECOMPILED_HEADERS_EN.md)**

MQB 把 MSVC 预编译头视为一等 build artifact，而不是把原始 `/Yc`、`/Yu`、`/Fp` switch 暴露成 escape hatch。

## 快速开始

项目配置：

```json
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp"
  }
}
```

CLI：

```powershell
mqb build --pch include/pch.hpp
mqb run --pch include/pch.hpp
```

关闭从项目或 profile 继承的 PCH：

```powershell
mqb build --profile dev --no-pch
```

`build.pch` 接受非空 header path string 或 `false`。Boolean `true` 会被拒绝，因为在不指定 header 的情况下启用 PCH 会使其 identity 不明确。

## 优先级与路径基准

PCH 是 scalar build policy，遵循普通 scalar precedence：

```text
explicit CLI > selected profile > base mqb.json > disabled default
```

示例：

```json
{
  "version": 1,
  "build": {
    "pch": "include/base.hpp"
  },
  "profiles": {
    "dev": {
      "build": {
        "pch": "include/dev.hpp"
      }
    },
    "clean": {
      "build": {
        "pch": false
      }
    }
  }
}
```

```powershell
mqb build                         # include/base.hpp
mqb build --profile dev           # include/dev.hpp
mqb build --profile dev --pch alt.hpp
mqb build --profile dev --no-pch  # disabled
mqb build --profile clean         # disabled
```

来自 `mqb.json` 和 profile 的 PCH 路径相对包含 `mqb.json` 的目录解析；`--pch` 路径相对 invocation directory 解析。

## Ownership 模型

启用 first-class PCH 后，MQB 拥有完整的 structural contract：

- synthetic creator translation unit；
- `.pch` artifact path；
- 配对 creator `.obj`；
- `/FI<header>`；
- creator 上的 `/Yc<header>`；
- consumer 上的 `/Yu<header>`；
- `/Fp<artifact>`；
- incremental PCH cache metadata 与 dependency tracking。

`/FI`、`/Yc`、`/Yu` 使用同一个 normalized header identity。原始 PCH structural switches 继续由 MSVC Parameter Engine 拒绝，因此用户参数不能静默地与 MQB-owned artifact routing 竞争。

Synthetic creator 生成在 `.mqb/pch/` 下，并且只有固定内容发生变化时才重写。因此普通 warm invocation 不会仅因为触碰 generated source 而使 PCH 失效。

## Build graph 与 cache 行为

Creator 通过现有 incremental compile coordinator 在普通 PCH consumer 之前构建。它的 compile action 拥有两个输出：

```text
creator.obj
project.pch
```

Creator cache 记录 MSVC `/sourceDependencies` 产生的 header/include dependencies。`.pch` 也会作为每个 consumer compile cache entry 的显式 dependency 被记录。

因此：

- 完全 warm 的 build 会复用 creator、所有 consumer object 和下游 target；
- 仅修改普通 source 时，只重编该 source，不重建 PCH；
- 修改 PCH header 或其 tracked dependency 会重建 PCH，并使 consumer 失效；
- 删除 `.pch` 会使 creator cache 失效并修复缺失 artifact；
- 修改参与 creator identity 的 compile semantics 会重建 PCH；
- target name、build configuration、architecture 会选择彼此独立的 PCH state。

Creator object 会有意保留为下游 target input。Executable/DLL link 会包含它，static-library target 会归档它。这样保留 MSVC PCH object/debug-information contract，而不是假设 `.pch` 与 link 无关。

Creator 一旦重建，MQB 会强制下游 link/archive，即使普通 object freshness 本来会认为 target 仍是 warm。

## Generated state

对于 target `app`、Debug、x64：

```text
.mqb/
├─ pch/
│  └─ app/
│     └─ debug/
│        └─ x64/
│           ├─ creator.cpp
│           ├─ creator.obj
│           ├─ creator.deps.json
│           └─ project.pch
└─ cache/
   └─ pch/
      └─ app/
         └─ debug/
            └─ x64/
               └─ creator.mqbcache
```

这些路径全部是 project `.mqb/` root 下的 writable MQB-owned state。

## 当前边界

First-class PCH 当前支持 executable、DLL、static-library target 的普通 C++ source set。

当 PCH 与以下情况组合时，MQB 会 fail closed：

- C translation unit；
- 需要 Modules/Header Unit pipeline 的 target。

MQB 不会近似处理这些组合。只有当其 artifact 与 dependency semantics 能被显式建模并验证时，才应在未来启用。

## Diagnostics 与 timing

Cold 或 rebuilt PCH work 会显示为：

```text
[pch] include/pch.hpp ...
```

Warm creator 会显示为：

```text
[up-to-date] pch include/pch.hpp
```

PCH creator work 会计入 compile timing 与 cache counters，而不会被隐藏成 generic setup 或 link work。
