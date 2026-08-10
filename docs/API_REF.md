# 核心接口契约规范 (API_REF)
> Based on ADR v4.3.0

本文档维护 `build.ps1` 引擎最新版的内部函数签名、输入输出契约及边界异常定义。

## 1. 缓存层 (Cache Engine)

### `Get-CompilerStateHash`
- **职能**：生成带噪音掩码与 ABI 免疫的全局参数指纹。
- **入参**：
  - `[string[]] $RawArgs`：原始编译器参数数组。
  - `[string] $CompilerPath`：`cl.exe` 的物理绝对路径。
- **返回值**：`[string]` MD5 哈希值。
- **异常边界**：如果 `$CompilerPath` 不存在，必须抛出 `FileNotFoundException` 阻断构建。

### `Get-NormalizedDependencySet`
- **职能**：解析 MSVC 生成的 `/sourceDependencies` JSON，清洗路径并生成高性能验证树。
- **入参**：`[string] $JsonFilePath`
- **返回值**：`[System.Collections.Generic.HashSet[string]]`（要求使用 `OrdinalIgnoreCase` 比较器）。
- **异常边界**：自动清洗所有带有 `..\` 和 `.\` 的脏路径。若 JSON 损坏或依赖文件在磁盘上物理消失（幽灵依赖），必须抛出异常或返回空集触发强行重编。

## 2. 编译管线层 (Pipeline Engine)

### `Invoke-MacroBatchScan`
- **职能**：执行 C++20 AST 宏观预扫描。
- **入参**：
  - `[string[]] $SourceFiles`：全量需扫描的文件绝对路径。
  - `[string] $CacheDir`：专用的输出目录（如 `\.cache\scan\`）。
- **返回值**：无。文件系统副产物为 MSVC 生成的 `.json` 拓扑树。
- **契约限制**：当 `$SourceFiles` 拼接出的命令行总长逼近 32767 时，自动降级组装为 `@scan.rsp` 文件传递给 MSVC。

### `Group-SourcesForConcurrentBuild`
- **职能**：解决 `/MP` 模式下的文件命名空间碰撞。
- **入参**：`[string[]] $SourceFiles`
- **返回值**：`[hashtable]`。Key 为父目录绝对路径的 MD5 哈希，Value 为属于该父目录的 `[string[]]` 源码数组。

## 3. 链接决议层 (Linker Layer)

### `Build-LinkerArguments`
- **职能**：根据 `TargetType` 隔离组装链接器参数。
- **入参**：
  - `[string] $TargetType`：枚举值 ("exe", "dll", "static")
  - `[hashtable] $ConfigCtx`：全局项目配置与命令行覆盖上下文。
- **返回值**：`[string[]]` 安全过滤后的最终链接参数数组。
- **契约限制**：对于 `static` 类型，遇到 `/LIBPATH:` 或任何系统 `.lib` 引用（不在白名单内的）必须静默抛弃或打印 Warning 拦截。
