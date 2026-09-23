# No-op causal trace: three arms, original binaries

**Proposal and implementation, not a study allocation. / 方案与实现，不是执行分配。**

Refs #198 / #207; continue completed study002 (run35809135700), not a rerun.
The original #701 hard failure and all001/002 evidence remain unchanged.
No product, original collector, performance threshold or prior study admission changes.

## Question / 要回答的问题

Separate time spent by the parent PowerShell (running, ready but not scheduled,
or blocked) from child MQB lifetime, scheduling and file I/O. A slow parent/child
interval is not by itself a causal explanation. The existing
`pdb_file_event_probe.cpp` records file/process/thread events, not CSwitch or
ReadyThread; repeating its coarse CPU totals cannot resolve this question.

Use Windows' existing WPR rather than extend another generic recorder. The
profile requests strict ProcessThread, Loader, CSwitch, ReadyThread, FileIO,
FileIOInit and Filename events, with switch/ready stacks. It does **not** use
sampled CPU as a substitute for scheduling evidence or modify sampling intervals.

## Fixed proposed experiment / 固定的拟议实验

|Arm|Call order|Reason|
|---|---|---|
|O / omit|prime → final-off|002's two-call order|
|U / middle_off|prime → middle-off → final-off|Controls the extra invocation itself|
|T / middle_on|prime → middle-on → final-off|Restores #701's timings-enabled middle call|

For every arm: one A→B pair and one B→A pair; reverse arm order in block2.
This is **12 new fixtures / 32 total MQB calls**, including12 primes,8 middle
calls and12 finals. Exactly four middle calls use `--timings=json`. No B/B,
extra warmup, adaptive replication or standard19-scenario matrix. The previously
recorded42 calls remain historical; the proposed cumulative ceiling would be74.
Two pairs per arm support event inspection, **not** statistical equivalence.

T minus O confounds timing with an extra call. Compare T with U for the timing
switch, and U with O for the added invocation; inspect both binaries and both
orders. Never subtract the old002 medians or treat tracing scores as untraced
acceptance. The original #701 ZIP and A/B identities are checked using the
existing verifier. No rebuild or current-main replacement is allowed.

## Minimal implementation / 最小实现

`noop_causal_trace.py` creates the fixed plan and original binary/fixture copies.
`trace_noop_causal.ps1` imports only four exact existing functions, including
`Invoke-LegacyBoundary`; it never calls the old40-call entry. This preserves
PowerShell `&`, merged lines and Push/Pop-Location, not the Process method.

WPR cell markers bracket each complete2/3-call sequence. All original outputs and
QPC boundaries are retained in memory until the cell ends (or fails), then saved.
A common post-prime file manifest and final manifest check byte/metadata stability;
**there is no Python process, hash, WPR utility or disk journal write between the
middle and final calls**. In-memory validation/output processing, the post-prime
manifest, markers and ETW still perturb conditions. The middle-on timing JSON is
parsed/checked, but this does not reproduce every operation of the full historical
benchmark. Report this limitation; do not claim identical historical host state.

The proposed32 calls also entail24 cell-marker WPR commands, one start, one final
status and one stop, excluding read-only setup. They are observer operations,
not hidden MQB warmups. Root PIDs and native thread identities must be resolved
from ETL process events and the recorded parent PID, native TIDs and intervals;
never infer root PID from an executable name alone. Child output remains merged
PowerShell lines, not separately captured original stdout/stderr bytes.

## Safety and evidence / 停止条件与证据

Use only a separately approved disposable Windows/PowerShell7 runner, with a clean
exact reviewed checkout, an external15-minute step /20-minute job limit and at
least4GiB free workspace (`MQB_CAUSAL_DISPOSABLE_HOST=1`). The synchronous legacy launcher cannot interrupt an
individual hung call. Freeze final commit, profile hash, original input and one
execution opportunity in #198 **before** arranging execution. A switch is a guard,
not an approval service. No dispatch workflow is added in this PR; do not rename
or reset001/002's consumed workflow, or run this from its old JSON.

Each capture owns a unique WPR instance. All WPR control commands use that instance;
start refusal never authorizes stopping someone else's session. No cancel/restart,
priority changes, antivirus/service changes, symbol download or cache purge.
The kernel trace is sequential and stops at256MB, never circularly replacing the
beginning. This is **not** a256MB cap on all merged/temp evidence or all WPR
metadata. Retain temp files if stop fails; do not claim guaranteed survival after
runner loss/hard kill. A partially buffered cell may lack saved output after a
hard stop; missing records must fail acceptance, not be reconstructed.

First unexpected call, state, marker, trace or write error stops remaining cells.
Do not retry or refill. No cleaning user `.mqb`, and no running fixture EXEs.
ETW is system-wide: use the dedicated runner, and review paths/process metadata
before sharing; it is not suitable for collecting a user's desktop activity.

## Event-level acceptance / 事件级验收门槛

The Python audit is intentionally named **journal_complete_trace_unreviewed**.
It cannot decode ETL, prove event loss is zero, or infer a cause. A nonempty ETL
plus a green job is not acceptance. Before making any scheduling/I/O inference:

1. Preserve raw ETL, WPR status/control logs and source identity. Use Microsoft's
   WPA/WPAExporter or another separately reviewed decoder; record its version.
   Confirm schema, timebase/frequency, no lost events/buffers, coverage of all cell
   markers and all32 expected MQB process lifetimes. Missing coverage stops review.
2. Resolve process-instance identity (PID **and creation time**), parent relation,
   executable/command line and thread lifetimes. Correlate QPC using the ETL clock
   metadata; never subtract raw QPC from FILETIME/UTC. Unknown mappings stay unknown.
3. Within each native envelope, partition parent/child **per-thread** running,
   ready and blocked intervals using CSwitch/ReadyThread. Inspect switch-out and
   wake stacks, module+offset when symbols are absent. Do not add overlapping
   thread CPU times into wall time. A wait reason alone does not identify its cause.
4. Correlate I/O begin/end, issuing thread and file identity/path. I/O duration is
   not necessarily disk-service time; filename frequency is not latency causation.
   No-op compiler/linker children, unmatched I/O or missing stacks remain explicit.

Expected discriminators: parent-only scheduling/capture delay; extra child CPU;
child blocked I/O; or reproducible middle-on versus middle-off activity changes.
Require agreement with the actual events, directions and controlled order. A
trace association is not proof of a stable regression or reason to waive #207.
If inconclusive, report that; no automatic next experiment is authorized.

## 本轮与后续的明确边界

本轮仅提交三组计划、Windows事件配置、薄执行脚本与静态/合成测试。
自动CI只运行Python契约、PowerShell语法检查及`wpr -profiles`解析；不启动ETW或原A/B。
未批准003或32次新调用；原#207继续Draft/HOLD。实际事件解码、丢失检查、父子关联
与等待/I/O原因审阅仍是执行后的必要工作，不能以本轮工具绿色代替。

## Microsoft references

- [WPR commands and named instances](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)
- [System keywords](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/keyword--in-systemprovider-)
- [Provider definitions and switch stacks](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/2-system-and-event-provider-definitions)
- [Sequential MaximumFileSize](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/maximumfilesize)
- [Lost events and recording privacy](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-how-to-topics)
