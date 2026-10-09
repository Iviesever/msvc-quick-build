# 成本决定准备

[English](ANALYSIS_COST_DECISION_PREPARATION.md)

## 1. 状态与判别目的

[冻结准备数据](ANALYSIS_COST_DECISION_PREPARATION.json) 落实 [接手记录 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173) 中的决定协议准备部分，接续 [PR252](https://github.com/Iviesever/msvc-quick-build/pull/252) 已合并的固定替身采集驱动。其状态始终为 **PREPARATION_ONLY_BLOCKED**。核对准确字节可以验收这份准备记录，不能授权真实负载、接受产品成本或集成 PR250。

判别目的是：通过固定替身，判断成本输出适配器能否执行冻结的 prepared/observation 语义与最终证据完整性要求，并列清后续独立审阅“是否需要任何具有独立理由的真实成本执行”所需的前提。目前尚未选择独立的真实成本问题。替换研究 002 或填补其中缺失的五条观察，不构成新问题。

```json
{
  "format": 1,
  "kind": "cost-decision-preparation",
  "protocol_id": "pr250-cost-semantics-preparation-20261009",
  "status": "PREPARATION_ONLY_BLOCKED",
  "real_execution_authorized": false
}
```

`authorized_real_budgets` 中每一项都是严格整数零。`future_selection` 中每一项都是 null。这些 null 表示尚未选择且构成阻塞，不表示零成本、继承某个旧选择或允许填入后继续。七项阻塞与四项 false 权限也是冻结数据的一部分。这份文档不包含执行状态转换。

## 2. 分析与产品身份的不同角色

| 角色 | Commit | Tree |
|---|---|---|
| 分析开发基线 | `bf5df76079f224ea9bb5e7822be683cb3a2e4d45` | `46007c773858dd50653db0a49ce6861b15733fb2` |
| 历史 PR250 产品候选 | `a26b103685589ab8a5b4df60d1b760ec445efdf5` | `4e777bdfc33b7befaf5e3769e27eb4fe910f9f03` |
| 历史原产品基线 | `2762749b27fc89156b724d19e89cce84e388ef52` | `0a4c2dd76c56309accf58d584a079b4bd483ee49` |

分析开发基线只标识本实现从何处开始，不声称新增文件已经属于该 commit，也不替换原产品比较基准。新增提交源码的身份，必须由实际提交的 tree 及保全的验证证据另行建立。

下列候选、原产品基线、二进制与输入都是历史引用。文件仍然存在，不表示已选择它们再次执行。原基线没有相同的旧 codec；历史 live-model 比较使用候选自身已有的 model 入口，处理相同选中事实。不同操作之间的差异不能称为旧 codec 回退，也不能直接归为隔离出的序列化成本。

保全构建报告 GCC 13.3.0、目标 `x86_64-linux-gnu`、参数 `-std=c++23 -O2 -DNDEBUG`，并分别构建 `COUNT_ALLOCS=0` 与 `COUNT_ALLOCS=1` 二进制。编译使用六个原生产翻译单元；源码身份记录覆盖 232 个生产文件，其中含 135 个头文件。系统、GCC 与 STL headers 未作为完整逐字节运行时包保全。这些是 Linux/GCC 参考身份，不是 Windows/MSVC 资格。[001 登记](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078630647) 记载了这些边界。

## 3. 历史文件身份

下列名称相对于保全的 `pr250-qualification` 证据根目录。它们标识已保留字节，不是可执行入口、必须存在的 checkout 路径或新的执行输入。JSON 内也嵌入相同的完整身份，因此离线阅读者无需持有旧本地目录即可理解这份准备记录。

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `run_cost_study.py` | 10211 | `c13b89f0ab5437986e17012af3a0879566a4c1b475d6d1c81f1c34976a5efaf7` |
| `cost_harness.cpp` | 16500 | `d0b7d1025a2d2d010fc2df562977ca3ec96a478ae8960672503c9847d6ac3632` |
| `cost_fixture.inc` | 16015 | `2774b498d1ba544c18ecc6fb08b7b48f818c0ec5ba6e7616240eda58647563c7` |
| `prepare_cost_harness.py` | 2043 | `fe33f000e4b18b697764f63f937be153d4d56825594c49b0cecef32979124666` |
| `build_cost_harness.py` | 2370 | `f9238b51f5c0ddeae9a569348f3db591a727febf77906ada937bcf90a0a41633` |
| `cost-fixture-source.json` | 782 | `70c632ffec7113f38f2397c857ab381f917ea159996243b2a404e5fa1d3e8309` |
| `cost-final-build-manifest.json` | 4250 | `3929fce949b7c57c29d90dbd682c6c5d89d238ee9473a2754f9f27183ff02fc6` |
| `cost-preparation/manifest.json` | 15338 | `e6d5e0202e708290f63c4c6ca41d24a73d567d282bfc96653eacf237aa9bf6ea` |
| `independent-production-source-identity.json` | 56952 | `223bd1ea0ae060e48a803f1bfd574a2a51eb7796a686a1b53c0da3c0219af0c3` |
| `run_cost_study_002.py` | 10219 | `0a07fc21997cdf0b5d69b08cde290a2e7d9aad0cb7369bc9cabaa971f7705124` |
| `cost-study-frozen.json` | 6757 | `41cdf32e1ee737efe176dde1c1155f68b0f81e542969687f475a546d645faa2d` |
| `cost-study-002-frozen.json` | 7126 | `6632fafcb73fef3e5b1ab5e6c344d8eca05e81c5831e85aad95ee5389c53a766` |
| `cost-final-0` | 729752 | `378be587c5b54fb7ba83fa04e0e3f9b4732d40e9af0ee2c6242a7fba5a571528` |
| `cost-final-1` | 731440 | `544ea84717c7cec9d79c98ab83ad916b7e08cac94b21e1737f5aae746e652dd6` |

[002 登记](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078685272) 将其旧分配、runner 与 manifest 独立保全，与 001 分开。这份准备记录不导入或重播其中任何 runner。

## 4. 输入与 prepared 的关联

每个 case 都有两份历史文件：`cost-preparation/CASE-0.wire` 与 `cost-preparation/CASE-1.wire`。两个 mode 均具有下列 wire 大小与 SHA-256。它们是 synthetic 准备输入，不是真实生命周期持久化得到的原始证据。

| Case | Wire 字节 | Wire SHA-256 |
|---|---:|---|
| `exe` | 3405 | `cdfdcd07edceb308837bd115857dd76fc008a5c10af27dc4dd8ca7006a37225e` |
| `dll` | 3429 | `b65d212ca2b21051ee03a086a16b0ddfe6cfd8a12b5279eb4de63ff30d1be473` |
| `static` | 3174 | `b190a2fff3aadb75e509fe499553cff2e5f30f3a3c003c057bca07f7bd92c58f` |
| `reuse_save_failure` | 6878 | `599fa256096913e4385240d88cde43da96b842ac98309d1dbb9d721cb8ef46a1` |
| `snapshot_128k` | 135422 | `2d43cb09a00c115084c44d6144d2c46391ca3464b49082f7ffb12fd129f9fdfa` |
| `history_256` | 304248 | `070f87a96a4c6f68e2e40a3706d127016a3276baab9222643620a20895f9899e` |
| `text_7m` | 7343780 | `3db6c28c33f4ff9b9386ef1b740042927324e316be9be0fb07668c535f80ef94` |
| `items_60000` | 363373 | `ae55e61087c39579b48703070f2c592128cb47dff01be47261c4b4bd3f41e8b5` |
| `bad_text_tail` | 7343781 | `6af269bef4cd49836b4891149942d7c91798d39f468aacd2ed6797a0f417b0d1` |
| `bad_items_tail` | 363374 | `471f9b987af3256900d9a778c46a8ee077affb972563ee429ffefae2c4bd8719` |

全部 prepared 记录都有 `kind="prepared"`、`public_preparation_calls=5` 与 `model_equal=true`。prepared 对象本身不包含 operation、mode 或 trial；这些身份由外层冻结的 call 契约提供。不同 mode 的 checks 必须分别绑定：

| Case | Records | Normal checks | Instrumented checks |
|---|---:|---:|---:|
| `exe` | 1 | 115 | 118 |
| `dll` | 1 | 118 | 121 |
| `static` | 1 | 106 | 109 |
| `reuse_save_failure` | 2 | 203 | 206 |
| `snapshot_128k` | 1 | 115 | 118 |
| `history_256` | 256 | 8207 | 8210 |
| `text_7m` | 1 | 129 | 132 |
| `items_60000` | 1 | 115 | 118 |
| `bad_text_tail` | 1 | 129 | 132 |
| `bad_items_tail` | 1 | 115 | 118 |

Instrumented checks 包含三个 allocator 控制检查。两个 bad case 的 model equality 针对追加 `!` 以前的合法父 wire，不表示 bad wire 已经成功 decode。错误窗口返回 offset，不能把它解释为模型大小或成功 decode 的声明。

这些固定形状有明确边界：history 是 256 个 executed 单 source records 与一个 retain；大 text 位于 live/model recipe 不包含的 terminal warnings；items 使用短参数；snapshot label 是 ASCII。它们都不能证明全部上限组合、任意 callback、Windows 大 UTF-16 转换或高频使用的资格。prepared 事实与这些覆盖限制来自上文标识的历史 manifest 与 harness。

## 5. 历史消费与当前零预算

研究 001 保持 failed-preflight，没有 measurement 进程或观察。此前 20 个 prepare 进程及其 100 次公开 API 调用仍然已消费。研究 002 依据 [准确 head 的 HOLD 审查](https://github.com/Iviesever/msvc-quick-build/pull/250#pullrequestreview-5468664960) 保持 INVALID。

| 历史项目 | 数量 |
|---|---:|
| 此前 prepare 进程 | 20 |
| 此前 prepare 顶层 API 调用 | 100 |
| 002 执行账本中的进程，全部 exit zero | 100 |
| 这些 measurement 进程内部的准备 API 调用 | 500 |
| 观察窗口顶层 API 调用 | 740 |
| 完整研究顶层 API 消费 | 1340 |
| 执行账本声称的观察 | 500 |
| 可独立重建的 prepared 记录 | 99 |
| 可独立重建的观察 | 495 |
| 可重建 normal / instrumented 观察 | 245 / 250 |
| 上次完整审计时完整的原始流 | 199 / 200 |

原顺序为 case 表，再 live/project/encode/decode/model/chain，再 normal/instrumented；bad case 仅允许 decode。100 个 cell 各使用一个新进程及 trial 0 至 4。这描述的是已消费的旧分配，不是提出新的矩阵。`future_selection.matrix` 仍为 null。

每个 measurement 进程执行五次准备 API。每条 chain 观察调用四次顶层 API，其余每条观察调用一次。这些事实解释了 500 加 740 加此前 100 的账本，不声称普查了内部嵌套调用。每个 instrumented 进程还执行 257 字节普通分配与 129 字节 aligned 分配控制：两次请求，386 requested bytes/peak/live、largest 257，释放后 live 为零。控制调用独立于公开 API 和观察。

缺失原件仍是 `cost-execution-002/070-history_256-chain-0.stdout`：expected SHA-256 为 `ea375e03db244431e48f4764f5dc90035c6b6f7362c42643e16099428e327735`，保全大小为 0，SHA-256 为 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`。其 prepared 记录与全部五条 normal chain 时间保持 null。已完成的忠实副本搜索没有找到匹配原件。缺失证据不能退还调用额度，也不能授权替换、插值、再次搜索或另一项研究。

当前全部真实预算均为零：codec compile、preflight、prepare 和 measurement 进程；公开 API、warmup 和 allocator-control 调用；真实观察；重试；替换进程和观察；MQB 与 MSVC 进程；ETW 会话；性能研究。另行限定的固定替身测试预算属于 synthetic 验证，不是真实执行分配。未来未知的全局 process/API/resource/output/metadata/copy 预算保持未选择。这份准备记录没有新 study 编号或可执行命令。

## 6. 观察窗口与采集开销

JSON 保留有源码依据的窗口定义，即使它不授权任何真实执行。这可以防止后续阅读者给相同字段名称赋予不相容的含义。

| 窗口或量 | 必须采用的解释 |
|---|---|
| Normal `ns` | 非负 steady-clock 纳秒 API/wrapper 返回延迟，包含内部验证、临时值析构和 take/move 包装；排除最终拥有结果的 caller 析构、consume 与 stdout 格式化。 |
| Instrumented allocation | `ns` 为 null。统计本窗口成功 C++ new 的 requested payload 与 callback 活动；排除准备基线及 allocator/OS 额外开销。Requested bytes 不是复制字节、总 heap 或 RSS。 |
| 正向所有权 | 在取得统计前保留返回的 owning value；chain 在返回前同时保留 projected archive、wire、decoded archive 与 model。 |
| Bad-tail decode | 包含拒绝、准确 error-code/零 callback 检查，以及返回 offset 前的 Error 析构。live-at-return 为零不能证明公共 Error 没有分配。 |
| 准备 | 五次公开 API 先于 measurement；全部准备输入表示在 warm trials 期间持续存活。完整 warning 字节/顺序与 PRIVATE 排除在 model equality 之外另行检查。 |
| `process_hwm_before_kib` / `process_hwm_after_kib` | 单位 KiB 的正数 Linux 进程历史 HWM，包含启动、准备、存活输入与之前 trial。after 在 consume 后、当前 stdout 格式化前读取；二者差值不是 op 独占 RSS 或 Windows working set。 |

既有 [英文 driver 契约](ANALYSIS_SUBSTITUTE_DRIVER.md) 与 [中文 driver 契约](ANALYSIS_SUBSTITUTE_DRIVER_ZH.md) 区分 active supervision 和后续工作。pipe 读取与 spool 写入位于 active supervision 中；最终 spool flush/fsync/close 位于其后，记录的 finish 包含后者。bounded spool 读取、BytesIO、raw/backup 复制、parse、hash、record/result 发布、seal 与最终 audit，并非全部受那些 active deadline gates 覆盖。阻塞的 OS 操作也使绝对返回时间保证无法成立。

Spool、raw 与 backup 是同一存储故障域中的三份副本。admitted pipe-byte 限额只计一次输入，不是全部存储、metadata 或临时内存上限。格式化、buffering、pipe backpressure、复制、读取、parse、hash、同步和 audit 都有成本，也可能影响调度/cache 与后续调用。位于 API 时间窗口外，不会使它们免费，也不能证明没有测量扰动。[英文 capture 契约](ANALYSIS_EVIDENCE_CAPTURE.md) 与 [中文 capture 契约](ANALYSIS_EVIDENCE_CAPTURE_ZH.md) 保持各自独立的最终完整性要求。

Whole-child wall 不能替代缺失的 API 延迟。instrumented 值、相邻 trial 与相近 case 都不能填补缺失的五条 normal 时间。

## 7. 七项阻塞前提

`blockers` 中的七个字符串描述下列未解决前提。仅保全历史引用不能满足其中任何一项：

1. 选择独立于替换研究 002 或填补缺失观察的真实成本判别目的，并决定是否确实需要真实执行。
2. 选择准确的未来 candidate/comparator、source/binary/compiler/runtime 与 callback/input 身份，以及准确 invocation。历史身份不能自动完成这些选择。
3. 在新数据产生前固定未来产品成本验收准则或数值预算。原登记没有数值产品阈值；不能从现有结果事后发明一个阈值。
4. 选择完整全局 matrix/order、process/API/warmup/control 账本、resource/output/metadata/copy 界与停止计费规则。闭合 driver 最多允许 8 calls、每 call active supervision 3 秒、overall 20 秒。旧研究是 100 calls、每 child 25 秒、overall 600 秒，另有每 child 1 GiB address space 与 20/21 CPU 秒。若无另行审阅的设计，两种策略不兼容；分批不能静默重置全局预算。
5. 明确格式化、pipe、spool 写入、复制、读取、parse、hash、flush/fsync/close、seal 和 audit 的观察窗口及扰动策略。仅凭它们相对于 API 窗口的位置，不能确定其影响。
6. 验收真实 producer 支持及所需的全局监督、准确 reap 所有权与完整证据绑定。当前 driver 接受固定 Python 替身；schema 与准确固定字节检查不能认证真实输出。
7. 在未来真实启动前取得独立审阅，以及另行冻结的准确协议登记/回读。这份 artifact 在自身格式内没有 approved 或 executable 后继状态。

因此，未来 purpose、candidate、comparator、binaries、inputs、matrix、acceptance criteria、observer、global budgets 与 registration 保持 null。既有证据不能决定这些值，也不能将 `real_execution_authorized` 改为 true。

## 8. 验收与不存在的执行转换

验收分为三个独立范围。第一，离线 verifier 可以凭独立提供的 expected SHA-256，只接受准确的准备字节与闭合状态。从同一可变文件内部取得 hash 并不足够。必须拒绝 malformed 或被改动的数据，包括预算改变、非 null 选择、缺少 blocker 或 authority 改变。

第二，只有 transport、capture 与新鲜的最终 audit 同时通过，完整 typed semantics 和冻结的 call identity/order 才能验收固定替身适配器。Synthetic observation 数字是字面量测试数据，不是成本样本、研究 002 有效性的证明或真实 producer 的证据。

第三，产品成本与 PR250 集成决定保持 HOLD；真实执行保持 BLOCKED。这份准备记录没有数值成本阈值，也没有可编辑 grant、approve/proceed 开关、真实 runner 入口或自动第三次研究分配。填入未知字段会改变并使这份冻结 artifact 失效。任何具有独立理由的后续工作，都需要另一份已审阅 artifact 与决定。

停止规则保留首个 identity、schema/type/value/order、budget、EOF、exit/reap、timeout、stderr、I/O、publication 或 final-integrity 错误，并停止后续调用。可得的有界前缀与已观察消费仍是证据。未知消费保持 unknown，不能变成零。这份准备记录不允许 retry、refund、interpolation 或 replacement。

准备或替身检查通过，不批准 PR250 集成、发布、默认/高频采用、自动持久化、恢复、clean/prune 或 #248 集成。四项权限保持 false：`producer_identity_verified`、`current_content_verified`、`complete_producer_inventory` 和 `deletion_authorized`。

## 9. 保全与资格边界

这份准备记录使 001 继续以 failed-preflight 关闭，002 继续以 INVALID 关闭。PR250 保持 Draft/HOLD。默认 Windows Performance944 仍只是既有默认路径的有限资格，不能修补显式成本证据。819/886/897/907/919/937/944 的全部不利记录与 block variation 继续有效；计数相等不能建立噪声归因。

继承的边界还包括 file_inputs 7-to-1/dependency 缺口、专用 DLL/LIB/PCH/HU 及 Windows/high-frequency/real-wire 成本缺口、different-byte bootstrap、writer/atomicity/reparse/concurrency/recovery、clean-prune/Rium/#164 与 #248 隔离。VERSION 保持 5.6.0；这份准备记录不授权 v5.7.0 发布。这些决定保全于 [接手记录 6081083173](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6081083173) 与 [PR250 HOLD 审查](https://github.com/Iviesever/msvc-quick-build/pull/250#pullrequestreview-5468664960)。

准确字节验证描述的是当次检查所读到的文件，不认证已加载 Python 模块、完整 runtime/kernel、原子 check-to-exec 行为或 producer 真伪。最终 bundle 验证仍是顺序读取区间，不是原子快照或永久完整性保证。这份文档不记录新的真实测量或通过测试数量；实际实现验收属于另行保全的独立验证与审阅。
