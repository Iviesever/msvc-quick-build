# 精确 MSVC 参数清单

**[English](MSVC_PARAMETER_INVENTORY.md) | 简体中文**

MQB 的 Parameter Engine coverage gate 来自不可变的 MicrosoftDocs Git blob snapshot，而不是手工维护的一小组 representative list。

`tests/native/verify_msvc_parameter_inventory.ps1` 会解析官方 compiler、LINK 与 LIB option table，展开 reference 中明确枚举的 option variant（例如 `[-]`、`/favor:<...>`、`/vd{...}`、`/ZH:[...]`），并补充独立文档记录的 command-line form，例如 LINK `/DEBUG:FULL`、`/DEBUG:NONE`、`/DEBUG:FASTLINK`、LIB response file 与 `/WX:NO`。

当前 denominator 为：

- compiler：**309** 个 canonical entries，MicrosoftDocs snapshot 日期为 2026-05-25；
- linker：**114** 个 canonical entries，另加 2025-09-08 的 `/DEBUG` syntax snapshot；
- librarian：**21** 个 canonical entries；
- 总计：**444** 个 canonical entries。

Snapshot Git blob SHA 与 expected count 都属于 verifier contract。更新官方 reference 时，因此必须经过有意的 code review，而不能静默改变 denominator。

对于每个 canonical entry，verifier 都会针对候选 MQB product library 构建并运行一个 C++ probe，并调用真实的 `MsvcParameterEngine::classify()`。它先尝试 bare family name；如果文档中的 family 需要 colon payload，而 bare name 未注册，则会尝试一个中性的 `:mqb_probe` payload。任何最终仍解析为 registry `unregistered` 的 entry 都会让 Native C++ gate 失败。

生成的 `msvc-parameter-inventory.tsv` 会记录每个 entry 的 tool、canonical spelling、concrete classification probe 与最终 ownership class。它随 Native C++ build artifact 一起上传，因此可以从 CI 审计精确的 A/B/C/D routing matrix。

Exact inventory coverage 有意与 toolchain lifecycle admission 分离。例如 `/DEBUG:FASTLINK` 仍然保持 registered passthrough ownership result，以便 VS 2026 之前的 toolset 可以接受它；而 `MsvcParameterCapabilities` 会在 VS 2026 toolset boundary 上拒绝它。同样，inventory 固定 `/DEBUG:NONE` 为 passthrough，并固定 obsolete compiler 拼写 `/Zc:trigraphs` 为 unsupported。

当 canonical inventory 暴露 classifier/router mismatch 时，gate 也会锁定 ownership。C language standard mode `/std:c11`、`/std:c17`、`/std:clatest` 属于 Class C passthrough：它们会被验证并原样保留，但不会填充 MQB 仅面向 C++ 的 `CppStandard` semantic policy。C++ `/std:c++*` mode 仍然属于 semantic。

聚焦的 behavioral tests 继续负责 semantic normalization、conflict handling、token shape、graph-aware file inputs、cache identity 与 lifecycle boundaries。Exact inventory gate 回答的是一个更窄但更强的问题：**固定的官方 reference 中，每一个 canonical option 是否都有确定的 MQB ownership 结果，而不存在未知黑洞？**
