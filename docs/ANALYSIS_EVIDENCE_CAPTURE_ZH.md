# 分析证据采集与最终完整性审查

[English](ANALYSIS_EVIDENCE_CAPTURE.md)

## 问题与证据

本切片处理分析采集器已经暴露的缺口：某一流在即时检查时通过，稍后却丢失原文字节，而驱动仍发布成功完成摘要。目标是在明确限额内保全逐流原文，对采集根目录和流文件实行排他创建，并在报告证据完整之前最终核对所有计划内流。

直接依据是 PR250 显式值成本研究002。其执行账本记录100个进程和500条观察，保留的 `070-history_256-chain-0.stdout` 随后却被发现为空，不再匹配已记录的 SHA-256：`ea375e03db244431e48f4764f5dc90035c6b6f7362c42643e16099428e327735`。现存原件只能独立重建495条观察。已完成的恢复搜索没有找到缺失流的忠实副本，丢失机制和写入者仍未知。

002继续为 **INVALID**，PR250继续 **HOLD / Draft**。改进保全或在新测试中检出该类丢失，均不能恢复那五条观察或改变原决定。Performance944的独立默认路径有限资格仍属于另一份证据。

本片范围见[接手记录6078819232](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078819232)和[证据交付6078869889](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6078869889)。

## 范围与实现状态

新 [helper](../tests/analysis/analysis_evidence_capture.py) 位于 `tests/analysis/`，接收调用者提供的二进制流并记录分析证据。它不启动进程、不运行codec、不构建MQB、不启动MSVC、不采集ETW、不选择性能样本，也不分配测量额度。测试仅使用固定数据或替身输出。

本文记录书面实施计划与helper的API合同。验收需要保留实际本地测试结果，并审阅最终源码；这里不声明通过的测试数量或Windows验收。

本片独立从 main `2762749b27fc89156b724d19e89cce84e388ef52` 开始，不建立在PR250之上。所有原有tracked产品、测试、文档和工作流文件保持原字节，尤其保留51份工作流、native清单、历史fixture、Recorder及001/002证据。

## 书面实施计划

1. 创建采集会话前，冻结调用计划、调用者身份、输出合同和明确资源限额。定义正常替身用例，以及对应后期删除、截断、替换和不完整闭合的失败用例。
2. 先增加这些用例的纯数据失败合同，保存红灯结果作为实现证据；不运行原成本研究，不借其他用例制造缺失输出。
3. 实现排他输出创建、有界分块逐流保全、独立原文字节副本和追加式receipt日志。锁定首错，保留已经观察到的前缀。
4. 实现最终闭合及独立只读审计，重读保存的计划、日志和每对原文/副本；只有准确完整匹配才允许封口为完整证据。
5. 通过纯数据或替身输出验证正常闭合、资源界、非法或不完整数据、短写、重复身份、已有目标及任一流的失败；确认后期丢失不能只凭早先receipt成功而通过。
6. 审阅完整diff、双语说明和实际本地测试原输出；保持产品及历史工作流身份不变，并明确仅本地Linux验证的范围。

## 冻结计划及其权限

`CaptureSession.create(root, *, identity, plan, limits=Limits())` 在任何调用开始前冻结完整调用者身份、有序计划和限额。它将这些输入序列化后重新解析，再从manifest重建私有 `Limits` 实例，因此调用者后来修改自己持有的输入不会改变会话。独立审计再次接收期望输入，不能仅因证据目录里的文件声明另一份计划就接受它。

会话 `identity` 是包含1至32个字符串字段的对象。键长1至128个字符，值长1至4,096个字符。这些是调用者声明的身份，不是独立核实的生产者事实。

计划是非空有序列表，每项准确包含三个键：长1至128个字符且唯一的 `call_id`、有序 `stdout_records` 期望记录身份列表，以及布尔值 `stderr_empty`。每份期望身份包含1至32个字段，键长1至128个字符；值可以是最长4,096字符的字符串、`bit_length()` 不超过64的整数、布尔值或null。同一次调用内重复的期望身份会被拒绝。类型属于身份的一部分，`true` 不等于 `1`。准确标识、记录数量、顺序和stderr空值策略均属于冻结输入。

stdout必须是严格UTF-8 NDJSON：按声明顺序，每份期望记录准确对应一个JSON对象，每条记录都有LF终止符。仅当不期望任何stdout记录时，空stdout才有效。空行、重复JSON键、非法UTF-8、非有限数值、缺少末尾LF及记录数量过多或不足都会验证失败。CRLF原样保留并接受，不重写字节。记录必须包含每个期望身份字段，其值和类型准确匹配；允许附加字段。

身份匹配和字节保全并不证明附加字段的值是有效测量；单位、范围、工作量等价、业务成功和接受门槛应由另行审阅的驱动负责。

传入的 `identity` 和退出码是调用者声明。helper可以保全并比较这些声明，不能独立证明哪个生产者执行过、哪份binary产生了流，或所声明的进程退出确实发生。

## 读取前确定资源界

`Limits` 是不可变dataclass。每个字段必须为正整数，布尔值会被拒绝。默认值同时也是绝对上限，调用者只能调低；实际传入的准确数值会纳入冻结manifest。

| 字段 | 默认值及绝对上限 | 适用对象 |
|---|---:|---|
| `max_calls` | 256 | 计划调用数 |
| `max_records_per_call` | 256 | 每次调用期望stdout记录数 |
| `max_stream_bytes` | 1,048,576字节（1 MiB） | 单个stdout或stderr流的已准入字节 |
| `max_total_bytes` | 67,108,864字节（64 MiB） | 全部stdout和stderr流的已准入字节总数 |
| `chunk_bytes` | 65,536字节（64 KiB） | 每次有界读取请求 |
| `max_record_bytes` | 65,536字节（64 KiB） | 单条stdout记录或编码后的期望身份，含LF |
| `max_plan_bytes` | 1,048,576字节（1 MiB） | 编码后的manifest和最终result，各自含LF |
| `max_journal_line_bytes` | 65,536字节（64 KiB） | 单条编码后的journal事件，含LF |
| `max_journal_bytes` | 8,388,608字节（8 MiB） | 整份journal |

逐流和总量限额在写入任一副本前，对逻辑输入字节计数一次，不因保存两份文件而重复计数。元数据限额针对编码后的表示。因此，即使调用数量很少，调低某项限额也可能令计划被拒绝或令结果无法发布。

所提供reader的 `read(n)` 必须返回不超过 `n` 字节的 `bytes` 对象；只有实际返回 `b""` 才标记EOF。到达逐流或总量上限时，helper最多再读一个字节，以区分恰好EOF和溢出；该字节只保存在receipt的 `overflow_probe_hex` 中，不计入已准入字节哈希和计数，并导致失败，不能被静默舍弃后当作成功的较短流。

超限、读取错误、非法记录或写入错误都保持失败。短写不会令receipt缩小到恰好匹配较短的保存文件。helper不重读源流以取得更方便的字节。reader返回错误类型或过多字节会被拒绝，但helper不能控制所提供reader内部的分配或阻塞行为。

这些限额描述helper接受和保留的数据，不是codec成本预算、总heap或进程RSS保证、OS整体资源限额，也不是消费另一个研究额度的许可。

## 保全原文字节与receipt

对于已开始的调用，`preserve_stream(index, role, source)` 消费所提供的 `BinaryIO`，源流仍由调用者拥有。角色为 `"stdout"` 和 `"stderr"`。helper先打开backup文件，再打开raw文件，两者均使用排他二进制创建（`xb`）。对于每个已准入块，先更新receipt，再写backup，随后写raw。目标已存在即失败，不覆盖已有文件。

写入前，helper检查root、raw和backup仍为真实目录而非符号链接，且设备号/inode身份与创建时记录一致。稳定存在的替换会在尝试写流之前被拒绝。这些检查不构成原子路径事务，也不保证免疫于并发恶意替换。

| 证据 | 用途 |
|---|---|
| `manifest.json` | 规范编码的格式版本1、调用者身份、有序计划和限额 |
| `journal.jsonl` | 连续生命周期和receipt记录，由单一会话追加并sync |
| `backup/000000.stdout.bin`及对应stderr文件 | 独立、有界的原文字节副本 |
| `raw/000000.stdout.bin`及对应stderr文件 | 以固定调用序号和角色标识的原始流 |
| `result.json` | 最终完整性判定；可封存无效采集，也可封存完整采集 |

序号从零开始，以六位十进制数字表示。每次完成的调用具有四个有序journal事件：`start`、两个不同的 `stream` 事件及 `finish`。两种流角色均须准确尝试一次，任一角色均可先进行。

| receipt字段 | 含义 |
|---|---|
| `byte_count` | 已准入输入字节数，在写入任一副本前计数 |
| `sha256` | 这些已准入字节的SHA-256 |
| `lf_count` | 这些字节中 `0x0a` 出现的次数 |
| `ends_with_lf` | 已准入前缀是否以 `0x0a` 结束；空输入为false |
| `eof` | 源流是否实际返回过 `b""` |
| `capture_error` | 该流首个采集错误，最多256字符，或null |
| `overflow_probe_hex` | 空字符串，或单个溢出字节对应的两位小写十六进制数字 |

receipt记录reader已经准入的输入，不声明每次写入均已成功。发生I/O错误时，文件可能只保存较短前缀，与receipt不一致仍属于无效证据。哈希和计数针对原始字节，包括CRLF、NUL及缺少末尾换行。解析从不归一化或重建这些文件；保全非法stdout也不会使其成为有效NDJSON。

stdout遵循冻结的NDJSON身份合同。`stderr_empty` 为false时，stderr可以是不透明二进制，仍须逐字节保全和检查。空stderr也是一份真实记录的流，不能省略成没有artifact。

两份副本改善保全并便于检出不一致，但同一证据目录中的副本不是独立存储故障域，也不能保证字节以后不被修改。

## 全部流终检后才报告完整

生命周期为 `start_call(index)`、两个 `preserve_stream` 尝试、`finish_call(index, *, exit_code, failure=None)` 和 `finalize()`。调用须遵循冻结计划中从零开始的顺序，同时最多一个活跃调用。`exit_code` 是必填关键字参数，接受null或 `[-2**31, 2**32 - 1]` 内的严格整数，拒绝布尔值和超范围数值并立即锁存失败。成功调用要求退出码为零且没有failure字符串。所提供failure字符串最长256字符。`finish_call` 执行即时检查，但不能代替最终闭合。

记录流的receipt事件前，helper先尝试flush、sync并关闭每个writer，任何错误都会使采集无效。最终闭合尝试关闭journal并重读落盘证据，核对准确目录成员、期望manifest、完整有序journal、全部raw与backup字节、receipt锚点、字节/行数、流结束状态及计划内stdout记录。读取拒绝非普通文件、硬链接文件，以及读取期间观察到的替换或元数据变化。某次调用早先验证通过并不充分。

`finalize()` 永久关闭会话。结果发布成功时，即使终检发现问题，也会返回判定。调用方必须检查 `complete` 与 `integrity_status`：只有 `complete == true` 且 `integrity_status == "VERIFIED"` 才表示证据完整。门禁失败则返回 `complete == false` 和 `"INVALID"`。`execution_finished` 只描述有序生命周期闭合，在某个流缺失或损坏时仍可为true。账本计数或内存摘要不能覆盖该判定。

result通过排他创建写入，随后flush、sync、关闭并读回。发布失败抛出 `CaptureError`，helper不会返回成功结果或重新打开会话。`finalize()` 额外返回准确 `result.json` 字节的 `seal_sha256`；该摘要不写进它自身所哈希的文件，也不单独表示判定已经成功。

调用者必须在被审证据之外保留期望seal。`audit_existing(root, *, identity, plan, limits=Limits(), expected_seal_sha256)` 强制要求该外部seal，再次冻结全部期望输入，只读核对已有证据，不执行工具、不修复文件。调用方同样必须检查其返回判定的 `complete`。只从同一目录中的可变文件取得摘要，不能建立独立可信锚点；已经封存为无效的结果不能通过此次审计转为有效。

`VERIFIED` 总结最终逐文件有界读取区间，不是任一单一时刻全部文件的原子快照，也不保证结果发布或函数返回时全部文件仍未改变。后续写入可以令该判断失效，因此使用保留证据时，调用者必须依据外部保留的seal重新审计。flush/sync和排他创建不建立永久writer租约、文件系统事务语义、断电恢复保证，也不证明审计之后一直保持完整。helper不会反复扫描，直到并发修改变得不可能。

## 保留首错及失败前缀

发生失败后，会话禁止开始新调用，也不允许重采失败角色或替换前缀。当前调用尚未尝试的另一流可以继续保全，避免stdout失败连带抹去可得的stderr，反向亦然。调用者可为当前调用尝试 `finish_call` 并提供失败信息，该调用仍会以无效状态抛错；随后可尝试 `finalize` 保留失败判定。

最早记住的失败不会被后续错误替换。结果中的 `capture_failures` 保留最早最多32条采集、flush、close等异常文本，每条最多256字符。终检问题另有 `failure_count` 和 `failures` 列表，同样最多保留32条、每条最多256字符的诊断字符串。这是有界诊断保留，并非每个次生异常的无损完整轨迹。journal写入失败后禁止进一步追加，已有字节及尝试过的流文件仍保留。可读journal前缀也不能证明计划实验完成。

创建时要求root尚不存在且其父目录已经存在；已有root或输出身份会被拒绝。创建过程中失败可能留下部分root，它会保留而不是被清理。helper不会删除、移动、截断、重新标记或自动恢复已有证据，也不会把backup字节提升为损坏的raw文件。任何后续取证恢复均须使用准确保全副本，并继续保留失败原件和原判定。

## 纯数据使用顺序

在Linux上从仓库根目录运行下面示例。它仅使用 `io.BytesIO`，其中包含固定stdout NDJSON和固定stderr字节，包括一个不透明NUL，不启动生产者进程。它保留新建的临时目录及其中任何失败前缀，并在开始采集前打印路径。

```sh
PYTHONPATH=tests/analysis PYTHONDONTWRITEBYTECODE=1 python -B - <<'PY'
import io
from pathlib import Path
import tempfile

from analysis_evidence_capture import CaptureSession, Limits, audit_existing

identity = {"protocol": "synthetic-demo-v1", "producer": "io.BytesIO"}
plan = [{
    "call_id": "fixed-0",
    "stdout_records": [{"call_id": "fixed-0", "record": 0}],
    "stderr_empty": False,
}]
limits = Limits(
    max_calls=1,
    max_records_per_call=1,
    max_stream_bytes=4096,
    max_total_bytes=8192,
    chunk_bytes=32,
    max_record_bytes=512,
    max_plan_bytes=4096,
    max_journal_line_bytes=2048,
    max_journal_bytes=8192,
)
directory = Path(tempfile.mkdtemp(prefix="analysis-capture-demo-"))
root = directory / "capture"
print(f"Retained synthetic evidence: {directory}", flush=True)
session = CaptureSession.create(root, identity=identity, plan=plan, limits=limits)
session.start_call(0)
session.preserve_stream(
    0, "stdout", io.BytesIO(b'{"call_id":"fixed-0","record":0,"value":"fixed"}\r\n')
)
session.preserve_stream(0, "stderr", io.BytesIO(b"synthetic diagnostic\x00\n"))
session.finish_call(0, exit_code=0)
result = session.finalize()

# Retain the expected seal outside the audited capture root.
seal_path = directory / "capture-result.sha256"
with seal_path.open("x", encoding="ascii") as destination:
    destination.write(result["seal_sha256"] + "\n")
if not result["complete"] or result["integrity_status"] != "VERIFIED":
    raise RuntimeError(f"Invalid capture: {result['first_failure']}")

review = audit_existing(
    root, identity=identity, plan=plan, limits=limits,
    expected_seal_sha256=seal_path.read_text(encoding="ascii").strip(),
)
if not review["complete"] or review["integrity_status"] != "VERIFIED":
    raise RuntimeError(f"Invalid current evidence: {review['first_failure']}")
print("Synthetic capture and independent read-only audit: VERIFIED")
PY
```

同级seal文件仅示范如何提供单独保留的期望摘要，不提供认证存储或独立故障域；真实调用者须建立自己的保留与信任策略。负控测试在独立替身fixture中修改或删除保存字节，并要求无效判定，不修复历史证据，也不启动替代测量。

## 本地验证与CI范围

本地Linux测试命令为：

```sh
PYTHONDONTWRITEBYTECODE=1 python -B -m unittest discover -s tests/analysis -p 'test_*.py' -v
```

任何会导入仓内源码的Python替身子进程也应使用相同bytecode策略，避免留下未跟踪的 `__pycache__`，导致以后clean-worktree身份预检失败。保留实际测试原输出和源码身份，不能因为文档中写了命令就推断测试通过数量。

现有CI**不会自动执行**新的纯Python合同。本地Linux结果不证明Windows文件系统行为，也不代表在Windows运行过该模块。两份新双语文档须由本片检查共同审阅，现有[Documentation工作流](../.github/workflows/docs-ci.yml)仍执行[原22对维护文档的checker](../tests/docs/verify_bilingual_docs.ps1)。

当本片只有新分析文件及这两份独立文档时，预期既有PR检查为[Native classifier/gate](../.github/workflows/native-ci.yml)正常运行且真实build/shards跳过、Documentation，以及非 `perf:` 题名下跳过的[Performance Evidence](../.github/workflows/performance-evidence.yml)比较。分支名不会令该比较准入；`perf:` 题名或显式dispatch则会改变边界。本片不新增workflow、不修改[native-impact分类器](../tests/ci/classify_native_ci_impact.ps1)；尤其修改旧文档索引或双语checker会启动无关的真实build-system工作，新增第52份workflow会破坏现有准确workflow清单合同。

## 采用限制与下一接手

本片实现保全和检出，不诊断002流为何变空，不恢复缺失观察，不批准PR250集成，也不分配替代实验。001和002按原状态与已消费预算保持关闭。

真实驱动采用本helper需要后续独立协议：准确源码/binary身份、调用计划、输入输出合同、限额、时钟与观察边界、测量目的、完整预算、停止规则、最终验收和原件保全。新增副本、解析、sync和审计可能影响未来观察，不能从旧分数中相减或称其免费；本实现不开放第三次研究、不增加五个替代trial，也不授权codec/MQB/MSVC/ETW执行。

产品的producer identity、current content、complete inventory及deletion authority四项标志继续false。没有默认CLI接入、产物writer协议、自动持久化、clean/prune、恢复、发布或高频采用授权。VERSION仍5.6.0，v5.7.0未发布。既有HOLD、不利矩阵、默认路径尾部、依赖缺口、Stage1/Stage2差异，以及writer/原子性/重解析点/并发/Rium/#164限制继续保留在原证据中。
