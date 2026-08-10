# 核心架构与设计理念 (DESIGN)
> Based on ADR v4.3.0

本文档维护 `build.ps1` 最新版本的架构演进、设计理念、模块关系与数据流向。

## 1. 宏观架构：双轨制 (Dual-Track Context)
构建引擎的输入阶段分离为两种模式，统一收敛到底层编译图。
- **Track 1 (零配置模式)**：以用户指定的入口 `.cpp` 为根节点，使用 BFS 扫描本地相关文件。注意：此模式不支持非对称 C 库（隐式多 `.c` 文件），通过链接阶段的 `LNK2019` 异常降级提示用户。
- **Track 2 (项目模式)**：如果存在 `msvc_list.json`，则接管工作区，根据 `sources` 和 `exclude` 配置提取源文件全集。

## 2. 预扫描防火墙与宏观批处理 (Macro-Batch Scanning)
针对 C++20/23/latest 模块 (`import` / `.ixx`) 的提取机制：
- **Level 1 旁路防火墙**：若命令行与 JSON 参数中未出现 `-std:c++20/23/latest`，跳过所有预扫描。
- **Level 2 宏观批处理**：如果触发扫描，不再按文件逐个启动进程。将全量源码拼接入同一条带有 `/MP` 的 `cl.exe /scanDependencies` 命令中，由 MSVC 内部并行解决 AST 解析，防范进程创建风暴。长命令自动降级使用 `@response_file.rsp`。

## 3. 并发编译与缓存隔离 (Collision-Free Concurrency)
- **按目录哈希分组算法**：为了保留 `cl.exe /MP` 的极速，同时防止同名文件（如 `A\utils.cpp` 与 `B\utils.cpp`）在 `/sourceDependencies` 写入时发生锁碰撞与数据覆盖，构建引擎按文件的“父目录物理路径”进行 `GroupBy`，分批次执行编译。
- **物理隔离**：每批同目录源码的依赖副产物，安全输出至 `\.cache\deps\<DirectoryHash>\` 中。

## 4. Tlog 级缓存状态机与 ABI 免疫指纹
缓存击穿的判定不仅基于源文件和头文件的时间戳，还加入了强校验：
1. **参数掩码指纹 (Masked Parameter Hash)**：剔除 `/nologo`, `/MP`, `/showIncludes` 等噪音参数。
2. **工具链溯源**：哈希载荷中强制压入 `$ctx.VcDir\cl.exe` 的 `LastWriteTimeUtc`。一旦编译器升级，全域缓存自动失效。
3. **幽灵依赖拦截**：通过解析 `cl.exe` 输出的完全绝对化且转小写的 JSON 路径，执行 `[System.IO.Path]::GetFullPath()` 过滤 `..\`，随后送入配置了 `[System.StringComparer]::OrdinalIgnoreCase` 的 .NET 高性能 HashSet 进行严格比对。任一依赖物理丢失即强制触发编译。

## 5. 链接器多态管线 (LinkerBuilder)
针对 `link.exe` (EXE/DLL) 和 `lib.exe` (Static Lib) 的参数不兼容特性实行物理隔离：
- **静态库白名单**：当 `TargetType == "static"`，暴力拦截所有系统库（如 `kernel32.lib`）与搜索路径（`/LIBPATH`），仅透传 `[ "/NOLOGO", "/OUT:", "/LTCG", "/DEF:", "/MACHINE:" ]` 确保纯净归档。
