# MSVC Parameter Engine

**[English](MSVC_PARAMETER_ENGINE.md) | 简体中文**

MQB 不试图把每一个 MSVC switch 再重命名成第二套 property system。相反，每一个进入 MQB 的原生 MSVC option，在 compiler、linker 或 librarian 启动之前，都必须得到一个确定的 ownership 结果。

## Ownership 模型

| Class | 含义 | 示例 | MQB 行为 |
|---|---|---|---|
| A — MQB-owned structural | 改变由 MQB 拥有的 target/TU shape、primary artifact、dependency metadata 或 graph topology | `/Fo`, `/OUT`, `/ifcOutput`, `/ifcMap`, `/sourceDependencies`, `/scanDependencies` | 拒绝用户/raw ownership escape，并给出解释 |
| B — Semantic typed | 已经由 MQB build model 表达 | `/std:`, `/MD`/`/MT`, `/GL`, `/MACHINE`, `/SUBSYSTEM`, `/LTCG` | 在 project-option resolution 前归一化到 typed policy |
| C — Safe/conditional passthrough | 不破坏 MQB structural ownership，可以继续作为普通 MSVC argv element | `/W4`, `/WX`, `/fp:fast`, `/arch:AVX2`, `/favor:AMD64`, `/I`, `/D`, `/U`, `/external:I`, `/FI`, `/DEF`, `/ORDER`, `/STUB`, `/MANIFESTINPUT`, `/DEFAULTLIB`, `/STACK`，以及 version-conditional `/DEBUG:FASTLINK` | 保留原生 argv ownership/order；现有 signature/cache identity 纳入它；需要时暴露 non-owning graph/execution evidence；再按需要执行 toolchain lifecycle admission |
| D — Unsupported / conflicting | 改变尚未建模的 pipeline、隐藏输入、引入未跟踪文件/artifact、无条件 obsolete，或与另一 semantic value 冲突 | `/MP`, raw PCH `/Y*`, `/FU`, `/analyze`, `/experimental:log`, `@response` | 在调用 MSVC 前 fail closed |

`unsupported` 与 `unregistered` 不同。当前官方 option 可以有意标为 unsupported，但它仍必须有明确 registry entry 与理由。`unregistered` 表示 registry 完全没有 classification；对于当前 reference snapshot，这是 coverage failure。

Ownership 与 availability 有意分离。某个 option 在结构上可能可以安全 passthrough，但只存在于受支持 MSVC toolset range 的一部分。这类 option 会先通过 semantic routing，在 MQB 发现真实 toolchain 后再决定是否 admission。

Class C passthrough 不代表 MQB 必须对 option 的所有效果保持盲区。当其他 subsystem 需要时，MQB 可以提取 **non-owning semantic evidence**，同时继续以原始 compiler/linker/librarian argv 为权威。

原生 `/I` 与 `/external:I` 会把 include root 暴露给 smart discovery，但仍保留为 raw compiler argv，因此与其他原生 compiler option 的相对顺序不变。原生 `/D` 同样会解析成 preprocessor metadata，但仍保留 raw。原生 `/FI` 仍属于 raw compiler argv/compile identity，同时 smart discovery 会观察其有序 forced-header operand。`/FI` operand 不会按 config/profile/CLI path base 重写，因为 MSVC 对它使用 quoted-include semantics：discovery 从 selected entry source directory 开始，再按 effective include search roots 解析。原生 `/I` 或 `/external:I` 的 path 若是相对路径，MQB 会以 supplying layer 的 path base 解析 payload（config/profile 用 project root，CLI 用 invocation directory），但不会改变 token 在 argv 中的位置。

原生 linker `/DEF:<file>` 使用同样的 evidence-without-ownership 模型。Raw LINK argument 保持权威；MQB 在提供该参数的 layer 内解析相对 definition-file payload，并把 resolved file 记录成 generic linker freshness input。Config/profile path 以 project root 为基准；CLI path 以 invocation directory 为基准。Argument 保持原 argv 位置。由于 LINK 一次 invocation 只接受一个 module-definition file，第二个 effective `/DEF` 会在 `link.exe` 前被拒绝，而不是依赖 argv ordering。

原生 linker `/ORDER:@<file>` 同样 graph-aware，但还有额外 execution constraint。所有 raw `/ORDER` 都保留在 argv；因为 LINK 采用最后一个 `/ORDER`，MQB 只把**最后一个** order file 跟踪为 effective freshness input。Config/profile/CLI-relative order-file path 在 supplying layer 内归一化。Unchanged warm build 仍可完全跳过 LINK；但只要存在 effective `/ORDER` 且实际需要 link，MQB 就会强制最终 linker recipe 使用 `/INCREMENTAL:NO`，因为 `/ORDER` 与 incremental linking 不兼容。后续 raw `/INCREMENTAL` 不能覆盖这项 safety policy，因为 MQB 会在 raw linker arguments 之后发出权威的 full-link switch。

原生 linker `/STUB:<file>` 使用 generic freshness model，但没有 always-full-link 规则。所有 raw `/STUB` 都保留在 argv，而 MQB 只把**最后一个** effective MS-DOS executable 作为 freshness evidence。Relative stub path 在 supplying config/profile/CLI layer 内归一化。Unchanged effective stub 复用普通 warm link cache；effective stub 的改变、删除或重新解析会使 generic linker-file freshness 失效。更早、已经被覆盖的 stub file 不作为 effective freshness input，因此修改它不会造成 false relink。

原生 linker `/MANIFESTINPUT:<file>` 引入第三种 file-input cardinality：**cumulative**。LINK 会合并每次 manifest input，因此 MQB 保留每一个 raw `/MANIFESTINPUT` argument，并把每个 resolved file 都记录成 effective freshness evidence，而不是把后续 layer 视为替换。Config/profile-relative input 从 project root 解析，CLI-relative input 从 invocation directory 解析。所有 layer 合并后，MQB 验证最终 linker manifest mode：如果有任意 `/MANIFESTINPUT` active，最终 ordered argv 必须选择 `/MANIFEST:EMBED`（包括 `/MANIFEST:EMBED,ID=...`）。因此后续 `/MANIFEST` 或 `/MANIFEST:NO` 会在 LINK 前让组合失效。

原生 linker `/DEFAULTLIB:<library>` 使用另一条 evidence-without-ownership 路径，因为其 payload 是 **library search declaration**，不是显式 input position。Raw `/DEFAULTLIB`、`/NODEFAULTLIB`、`/NODEFAULTLIB:<name>` argv 保持权威，使 LINK 保留原生 search priority。Bare library name 保持 bare search name；只有 path-bearing `/DEFAULTLIB` payload 会按 supplying layer 归一化。所有 linker layer 合并后，MQB 在最终 `/NODEFAULTLIB` policy 下计算 **user-explicit** `/DEFAULTLIB` 的 effective set，只把当前能解析到的 file 作为 freshness evidence。Resolved file 永远不会重新发射成 explicit library argv。

## Native argv token shape

只有 ownership 还不足以解析原生 `cl.exe` command line。CLI 在决定一个 bare token 是否是 positional source 之前，必须知道前一个 MSVC option 是否把该 token 拥有为 operand。

`MsvcParameterEngine::token_shape()` 是这一边界的 single source of truth。CLI 只问它“后面多少个 argv element 属于当前原生 compiler option”；CLI **不负责**决定该 option 是否允许。分组后的 argv 再经过与 config/profile `compiler_args` 和 `--compiler-arg` 相同的 ownership/semantic routing。

当前 public shape model 有意保持很小：

- `none`：当前 argv element 自包含；
- `single`：后面恰好一个 argv element 属于该 option。

这一差异取决于具体 spelling。`/Iinclude`、`/external:Ivendor`、`/FIforced.hpp` 这类 attached form 不消耗额外 token。`/I include`、`/D NAME`、`/U NAME`、`/external:I vendor`、`/FI forced.hpp`、`/Tc file`、`/Tp file`、`/headerName:angle vector`、`/ifcOutput path` 这类精确 split form 消耗一个 token。即使 option 最终会被拒绝，split operand 也要先正确分组；例如 `/analyze:plugin checker.dll` 与 `/experimental:log sarif-out` 应作为完整 option/operand pair 进入 Parameter Engine，而不是把 pathname 错判为 source。

Shape table 遵循真实 MSVC spelling contract，而不是根据 option prefix 猜测。要求 attached pathname 的 output switch 仍保持 self-contained：bare `/Fd`、`/Fi`、`/Fm`、`/Fp` 不消耗后一个 argv element。普通 `/Fo` 同样 self-contained；文档中的 `/Fo: pathname` spelling 则建模成 one-operand form。`/Fe: pathname` 同理。这样 malformed 或 incomplete structural option 不会偷走合法的后续 source token。

当前模型不虚构通用 stateful/variadic mode。未来某个 MSVC syntax 若无法表示成 `none` 或 `single`，必须先得到显式 shape extension，CLI 才能解释它。

## 原生 linker 与 librarian 边界

Public CLI 为下游原生工具提供显式 one-way boundary：

```powershell
mqb build foo.cpp /O2 /link /DEBUG:FULL /STACK:8388608
mqb build foo.cpp --type static /O2 /lib /WX /EXPORT:foo
```

`/link` 把后续 build argv 路由到 `link.exe`。Static target 用 `/lib` 把后续 build argv 路由到 `lib.exe`；也接受 uppercase `/LIB`。Librarian boundary 有意**不提供** single-dash alias。`-lib` 与 `-LIB` 会在 generic `-l...` shorthand 之前被拒绝，避免被解释成 `-l ib`。MQB 把 `-l <name>` 与 `-l<name>` 专门保留为 link-library shorthand。

`/lib` tail 至少必须包含一个 librarian argument。第二个 `/lib` 或 `/LIB` 会被拒绝。提取后，tail 会像 `build.librarian_args` 一样经过 `MsvcParameterEngine::route_librarian()`：Class A structural ownership（如 `/OUT`）仍不开放给用户；Class B `/MACHINE` 与 `/LTCG` 合并到 typed policy；Class C 保持 ordered passthrough；unsupported/wrong-tool option fail closed。

Librarian policy 只有最终 target kind 为 `static` 时才有效。Executable 与 DLL target 会在 product execution 前拒绝非空 effective librarian argument set。反过来，static target 会拒绝 linker-only policy，例如 libraries、library directories、subsystem 与 raw linker arguments。

## Toolchain lifecycle admission

`ToolchainIdentity.version` 记录 discovered `VCToolsVersion`（例如 `14.44.x`、`14.50.x`）。`MsvcParameterCapabilities` 在 discovery 后使用真实 toolset identity，而不是把一个“永远当前”的静态答案写死在 ownership registry 中。

第一批 lifecycle rules：

- `14.50+` 视为 Visual Studio 2026 / v145 boundary；
- compiler `/await` 在旧 toolset 上接受，在 `14.50+` 上接受但给出 MQB deprecation warning；`/await:strict` 不包含在这条规则内；
- linker `/DEBUG:FASTLINK` 在 `14.50` 之前接受，在 `14.50+` 上会在 `link.exe` 前拒绝，并建议使用 `/DEBUG:FULL`；
- 无法解析的 toolset version 只会对 lifecycle 确实依赖 version 的 option fail closed；`/W4` 等普通 option 不受影响。

这套 two-stage design 保留现有早期 config/profile/CLI normalization 与 precedence model，同时让最终 option admission 成为真正执行它的 toolchain 的属性。

## Layering 与 precedence

原生参数在提供它们的 layer 内完成 normalization：

1. `mqb.json` typed fields 与 `compiler_args` / `linker_args` / `librarian_args` 一起归一化。
2. Selected profile 作为自己的 overlay 归一化，包括它自己的 librarian arguments。
3. CLI typed options 与 native/raw arguments 一起归一化；CLI `/lib` tail 在 routing 前成为 CLI-layer librarian arguments。
4. 同一 layer 内 typed/native value 冲突属于错误；tracked file input 跨 effective layers 遵循各自 LINK semantics（`/DEF` single-instance，`/ORDER` 与 `/STUB` last-wins，`/MANIFESTINPUT` cumulative）。Path-bearing `/DEFAULTLIB` declaration 在同一个 supplying layer 归一化，同时继续作为 default-library argv。
5. Project resolver 应用 `built-ins < base mqb.json < selected profile < CLI`。List-valued librarian policy 因而严格按 `build.librarian_args -> profile librarian_args -> CLI /lib tail` 追加。
6. 依赖**最终 merged linker argv**的 requirement/observation 在 layering 后派生：`/MANIFESTINPUT` 要求最终 `/MANIFEST:EMBED`；user-explicit `/DEFAULTLIB` freshness 则由最终 `/NODEFAULTLIB` / `/NODEFAULTLIB:<name>` policy 过滤。
7. MSVC discovery 后，剩余 raw compiler、linker、librarian arguments 再通过 toolchain lifecycle admission。

Relative path-bearing evidence 在 layer 合并前解析：project configuration/profile value 使用 project root；CLI value 使用 invocation directory。这避免 cwd-dependent cache identity，并保证同一个 project 从 child directory 调用时，重写后的 raw argv 仍稳定。

这样可以阻止 argv ordering 变成 typed semantic policy 的第二套 precedence system。Native passthrough option 保留自己的 argv order，不会被静默转换成重新排序的 property list。

## Archive cache identity

Static-library freshness 由完整 effective archive recipe 定义，而不仅仅看 object timestamp。Archive signature 包含 librarian/toolchain identity、target architecture、effective LTCG state、object set/output 与最终 routed librarian argv。

因此 librarian policy 是一等 cache dimension：

```text
unchanged librarian argv -> warm archive cache may be reused
changed librarian argv   -> archive_recipe_changed -> rearchive
```

`build.librarian_args`、profile librarian arguments 与 CLI `/lib` tail 最终都汇聚到同一个 recipe。Profile name 或 configuration-file timestamp 本身不是 cache dimension；只有 effective librarian semantics 是。

## Tool semantics

Compiler option name 按 compiler 自身的 spelling/case semantics 处理。Linker 与 librarian option name 由各自 parameter router 做 case-insensitive normalization。CLI boundary 本身有意只识别公开文档中的 `/lib` 与 `/LIB` spelling；这与 boundary **之后**的 option case normalization 是两回事。

MQB 每个 `cl.exe /c` invocation 只编译一个 TU，并由 MQB 拥有 concurrency，因此拒绝 `/MP`，用 `-j/--jobs` 代替。Compiler `/F` 也被拒绝：它要求 `cl.exe` 控制 linker stack size，但 MQB 拥有独立 `link.exe` invocation；应改用 linker `/STACK`。

Raw PCH switch 继续保留给 MQB 的 first-class PCH pipeline。Response file 会被拒绝，因为它可以把 option 与 input file 隐藏在 ownership、cache、dependency classification 之外。

Raw forced include `/FI` 是 graph-aware 的。Smart discovery 启用时，MQB 会观察有序 `/FI` operand，同时保留原始 raw argv 为权威。每个 forced header 都按 quoted-include semantics 从 selected entry source 开始，再通过 effective include search roots 解析。当前 promotion 要求结果必须是 indexed project header；unresolved、external/unindexed 或 non-header forced target 会 fail closed，而不是创建 compiler-visible/discovery-invisible dependency。Forced header 只挂到 selected entry，因此不会在 MQB 有意使用的 undirected source-selection graph 中变成人工 global hub；随后它的普通 include 与 same-stem ownership edge 扩展 closure。`--no-discover` 仍是显式 source-set-managed escape hatch。Compile freshness 继续由 MSVC `/sourceDependencies` 驱动；native Windows E2E coverage 证明 forced-header mutation 会重建受影响 object 与 executable。Raw `/FU` 继续被拒绝，因为它引入 metadata file input，但该 input 尚未进入 compile freshness identity。

MSVC code-analysis execution 当前也 fail closed。`/analyze` 可以生成 analysis log，`/analyze:*` form 还可能引入 plugin/ruleset input 或显式 log output；这些 artifact 尚未进入 MQB graph。`/analyze-` 是窄范围安全例外，因为它关闭 analysis pipeline。`/experimental:log` 因相同 artifact-ownership 原因被拒绝，即使 token-shape layer 已能正确分组它的 filename/directory operand。

`/DEF:<file>` 是第一种被提升到 MQB generic linker-file freshness path 的 raw LINK file-bearing option。Link cache v4 把 resolved file-input list 与普通 object/library input 和 side output 分开存储。Missing file input、changed path、newer timestamp 或丢失 cache metadata 都会使 reuse 失效。`file_inputs_changed` 是 execution-only evidence：Debug 下它会强制一次 one-shot full link，避免已有 `.ilk` 保留 obsolete export table；仅 object 变化仍使用普通 incremental linking。Raw `/DEF` argument 同时保留在 link signature 与最终 LINK argv，因此纯 option change 与 file-content freshness change 仍可区分。

`/ORDER:@<file>` 复用 generic linker-file freshness path，但 execution semantics 更强。MQB 保留所有 raw `/ORDER` argument，因为 LINK 拥有 order-sensitive last-wins 行为；cache 只跟踪最后一个 effective order file。Missing/changed effective order file 通过普通 `file_inputs` evidence 使 reuse 失效。除此之外，存在 effective `/ORDER` 会设置 execution-only `requires_full_link` evidence。Warm no-op path 仍在调用 LINK 前返回；但任何真实 Debug link——无论由 object change、explicit rebuild、linker-option change 还是 order-file freshness 引起——最终都会在 raw linker arguments 后追加 MQB 权威 `/INCREMENTAL:NO`。这样不需要依赖 LINK 自己诊断并覆盖不兼容的 raw `/INCREMENTAL` 请求。

`/STUB:<file>` 也复用 LinkCache v4 `file_inputs`，但不会永久强制 non-incremental linking。MQB 保留每一个 raw `/STUB` argument，同时只跟踪最后一个 effective MS-DOS executable input。Unchanged input state 保持 zero-LINK warm path。Missing、changed 或 re-resolved effective stub 产生普通 `file_inputs_changed` evidence；现有 Debug one-shot full-link safety 会阻止 stale incremental state 在 stub-only mutation 后继续存活，但不会把以后每次 `/STUB` link 都变成 full link。因为只跟踪最后一个 effective stub，修改更早且已被覆盖的 stub 不会产生 false freshness invalidation。

`/MANIFESTINPUT:<file>` 同样复用 LinkCache v4 `file_inputs`，但每一次 occurrence 都保持 effective。Incremental linker 会重新观察**最终 argv**并分别 snapshot 每个 cumulative manifest input，所以 missing、newer、changed-path 或 re-resolved file 都会使 reuse 失效，而无需新 cache format 或 permanent full-link mode。Unchanged cumulative input 保持普通 zero-LINK warm path；仅 manifest file 变化会使用既有 linker-file change safety path，并且不会重新编译 source TU。最终 manifest-mode validation 与 freshness 分离：除非最终 ordered raw arguments 选择 `/MANIFEST:EMBED`，MQB 会在 LINK 前拒绝 invocation。

User-explicit `/DEFAULTLIB:<library>` 具有 freshness awareness，但不会被提升到 explicit-library argv tier。`MsvcDefaultLibraryPolicy` 保留 raw `/DEFAULTLIB` / `/NODEFAULTLIB` option，只从最终 linker arguments 计算 effective **user-declared** default-library set。`MsvcLibraryResolver::resolve_available()` 使用与 structured library 相同的 `-L -> link working directory -> MSVC LIB` search order，但会跳过当前缺失的 default library，避免 MQB 比 LINK 更严格。Resolved file 只追加到 LinkCache v4 generic `file_inputs`，永远不会插入 `LinkAction.libraries`。Unchanged provider 因而保持 zero-LINK warm path；changed/re-resolved available provider 会使 freshness 失效，并使用既有 one-shot Debug full-link safety。`/NODEFAULTLIB` 与 `/NODEFAULTLIB:<name>` 会同时从 freshness evidence 与剩余 raw LINK argv semantics 中移除被 suppress 的 declaration。

这项 DEFAULTLIB promotion 有意比 LINK 完整的 transitive default-library model 更窄。由 `.obj` 或 `.lib` 的 `.drectve` section 注入的 default library 仍由 LINK 解释，但**还没有**被 MQB freshness 跟踪，除非同一个 library 同时通过 explicit MQB library input 或 user-written `/DEFAULTLIB` 出现。若要把 claim 扩展到 transitive directive，必须解析或通过其他方式观察这些 embedded directive，而不能从最终 executable 猜测。

除已 promotion 的 `/DEF`、`/ORDER`、`/STUB`、`/MANIFESTINPUT`、user-explicit `/DEFAULTLIB` 外，其他会引入 graph input 或 secondary output 的 file-bearing linker mode 继续 fail closed，直到相应 semantics 被建模。`/NATVIS`、`/SOURCELINK`、signing input、PGO input/output 等不会仅因为 generic `file_inputs` 已存在就被隐式放行；影响 PDB/manifest/signing artifact 的 option 在 promotion 前可能还需要额外 output modeling。`/WHOLEARCHIVE:<library>` 是单独的窄例外：当同一 library 也通过 MQB structured library input 声明时可以 passthrough，从而继续以 freshness tracking 为权威。

## Coverage gates

`cpp/tests/msvc/parameters/msvc_parameter_engine_tests.cpp` 是 executable ownership 与 token-shape coverage matrix。它包含当前 registry 使用的 Microsoft reference 中每个 option family 的 representative argv，并要求每个 family 都解析到 A、B、C 或 D，而不是 `unregistered`。它还锁定 split-vs-attached compiler form，避免未来 registry expansion 静默吞掉 positional source。

`cpp/tests/app/cli/build_policy_cli_tests.cpp` 锁定 librarian-facing UX contract：`/lib` 与 `/LIB` 路由 static-target tail；`-l` 保持 link-library shorthand；`-lib` / `-LIB` 会被拒绝并提示 `/lib`；empty/duplicate librarian boundary fail；MQB-owned/wrong-tool librarian option 保持 fail-closed。同一测试还把 `mqb --help` 文本视为 public contract 的一部分。

`cpp/tests/config/resolution/project_options_tests.cpp` 独立证明 librarian list precedence：base `build.librarian_args`、selected-profile librarian arguments、CLI overrides 按 base -> profile -> CLI 顺序追加且不丢失。

`cpp/tests/orchestration/incremental/incremental_archive_coordinator_tests.cpp` 证明 archive cache identity：native librarian argv 到达实际 archive invocation；unchanged recipe 可复用；只修改 librarian argv 会产生 `archive_recipe_changed`，并恰好增加一次 librarian invocation。

`cpp/tests/core/cache/link_state_tests.cpp` 独立于 object/library freshness 锁定 generic linker file-input execution evidence：warm input 可复用；newer/missing/re-resolved input 或 metadata 丢失会使 reuse 失效并标记 `file_inputs_changed`，但不会污染 `library_inputs_changed`。

`cpp/tests/orchestration/incremental/incremental_link_coordinator_tests.cpp` 锁定 `/ORDER` execution semantics：repeated order option 保留 raw argv，但只跟踪最后一个 effective file；malformed `/ORDER` 在 LINK 前失败；warm no-op 跳过 LINK；effective order-file mutation 使 freshness 失效；每次真实 `/ORDER` link 都会在 raw linker arguments 后追加权威 `/INCREMENTAL:NO`。

`cpp/tests/e2e/mqb_dll_target_e2e_tests.cpp` 在真实 Windows/MSVC 上证明 `/DEF` pipeline：cold DLL 暴露第一个 `.def` export；unchanged build 是 link-cache hit；只修改 definition file 时 TU 保持 up-to-date 但 DLL relink；最终 export table 删除旧 symbol 并暴露 replacement。Fixture 还验证从 child invocation directory 调用时，project-root-relative `mqb.json` `/DEF` normalization 正确，并在 LINK 前拒绝第二个 CLI `/DEF`。

`cpp/tests/e2e/mqb_stub_linker_e2e_tests.cpp` 在真实 Windows/MSVC 上证明 `/STUB` pipeline。Fixture 自己创建有效 DOS MZ executable，传入两个 raw `/STUB` argument，并验证最终 PE 包含最后一个 effective stub 的 DOS program，而不是被覆盖的前一个。随后证明 unchanged build 是 zero-LINK cache hit；只修改 overridden stub 不会造成 false relink；只修改 effective stub 时 source TU 保持 up-to-date，但 executable relink 并替换 embedded DOS program；empty `/STUB:` 在 LINK 前失败。

`cpp/tests/e2e/mqb_manifest_input_e2e_tests.cpp` 在真实 Windows/MSVC 上证明 `/MANIFESTINPUT` pipeline。Fixture 在 `/MANIFEST:EMBED` 下给 LINK 两个 manifest file，证明 cold link 成功、unchanged build 成为 zero-LINK cache hit；然后分别修改每个 cumulative input，验证每次 mutation 都会 relink，而 source TU 保持 up-to-date。第二个 project fixture 证明 project-root-relative config input 与 invocation-relative CLI input 跨 layer 保持 cumulative，并能独立使 freshness 失效。它还证明缺少最终 EMBED、后续 `/MANIFEST:NO` 与 empty `/MANIFESTINPUT:` 都会在 LINK 前失败。

`cpp/tests/msvc/linker/library_resolver_tests.cpp` 锁定 explicit-vs-default library resolution policy。Structured library 继续 require-all，而 user DEFAULTLIB evidence 是 available-only；测试还覆盖 case-insensitive `/NODEFAULTLIB:<name>` suppression、optional `.lib` matching、path-bearing layer-base normalization 与 shared library-search precedence。

`cpp/tests/e2e/mqb_default_library_e2e_tests.cpp` 在真实 Windows/MSVC 上证明 user-explicit `/DEFAULTLIB` pipeline。Consumer 只通过 raw DEFAULTLIB 解析 symbol；unchanged build 成为 zero-LINK cache hit；仅 default-library archive mutation 时 TU 保持 up-to-date，但会 relink 并改变 runtime behavior；`/NODEFAULTLIB:<name>` suppress raw default library；structured explicit library 优先于竞争的 DEFAULTLIB。Fixture 还证明 suppressed missing DEFAULTLIB 不会让 MQB 比 LINK 更严格，以及从 nested directory 启动 MQB 时，project-root-relative `mqb.json` DEFAULTLIB path 仍正确。

`cpp/tests/app/cli/mqb_native_msvc_cli_e2e_tests.cpp` 独立验证 CLI boundary 与 native execution path：fixed operand 保持在 ordered compiler argv 中，attached form 不消耗后续 source；`/FI` 等 graph-aware native option 在真实 candidate MQB/MSVC pipeline 下保持 split operand 与 rebuild behavior。

`cpp/tests/msvc/parameters/msvc_parameter_capabilities_tests.cpp` 独立锁定 toolset lifecycle boundary，避免 ownership coverage 静默退化成一套静态、与版本无关的答案。

Reference snapshot lineage：

- Microsoft C/C++ compiler options alphabetical reference metadata date：**2026-05-25**；
- Microsoft LINK options reference：registry 初始实现时的 current 版本；
- Microsoft LIB overview/options reference：registry 初始实现时的 current 版本；
- lifecycle update 依据 discovered `VCToolsVersion` 建模，从 Visual Studio 2026 / v145（`14.50+`）transition 开始。

Registry/capability implementation 位于 `cpp/src/msvc/parameters/`；public API 保持在稳定的 `cpp/include/mqb/msvc/` facade 下。

## 当前 typed-model 限制

Engine 会在 MQB 尚无法保留 semantics 的位置有意 fail closed：

- target architecture：仅 typed x86/x64；
- subsystem：仅 typed console/windows；
- LTCG：boolean on/off policy；specialized `/LTCG:*` mode 不会被静默折叠；
- CLR/Windows Runtime/kernel/driver/ARM64EC target mode：尚无 first-class MQB target model；
- raw forced metadata `/FU`：在其 dependency input 进入 freshness identity 之前拒绝；
- code-analysis 与 diagnostic-log artifact mode（`/analyze`、`/experimental:log`）：在 inputs/outputs 进入 artifact/freshness identity 前拒绝；`/analyze-` 保持 safe disable switch；
- 除已 promotion 的 `/DEF`、`/ORDER`、`/STUB`、`/MANIFESTINPUT`、user-explicit `/DEFAULTLIB` 与 separately constrained `/WHOLEARCHIVE` 外的 file-bearing LINK option：在其 inputs/outputs 能在不削弱 cache/artifact correctness 的前提下表示之前拒绝；
- object/library `.drectve` 中嵌入的 transitive default-library directive：由 LINK 解释，但尚未进入 MQB freshness evidence；
- PGO 与其他 file-producing/file-consuming mode：在其 artifact 进入 freshness/cache identity 前拒绝；
- response file：在 MQB 能安全展开并 classify 内容之前拒绝。

因此，对于原生 argument，Parameter Engine 实际上有四个彼此独立的 correctness question：**哪些 argv token 属于这个 option？**、**谁拥有它的 semantics？**、**MQB 是否需要为其他 subsystem 提取 non-owning evidence？**，以及在需要时，**这个精确 toolset 是否仍然提供该 option？**
