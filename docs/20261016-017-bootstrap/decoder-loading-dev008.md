# 017: real decoder loading development evidence (dev008)

Base `ba39f933103c07946d0fe6a8e32371d0ec831121`, 2026-10-16.
This is a **two-process, synthetic-input development check**, not preparation or
observation of the original 103-byte function. It does not rerun dev004–dev007.

## What is actually exercised

The baseline invokes the unchanged `PreparedNativeBackend`, original exclusive
`OutputDirectory`, child limits, real GNU objdump, and original listing parser.
Input is exactly 102 newly generated `90` bytes followed by `c3` (102 NOPs and a
RET). Those bytes are passed as data to objdump, never executed as machine code.
Expected coverage is 103 one-byte instructions at the already fixed display RVA.
This synthetic match says nothing about the original target's instructions.

A second, separate directory uses the baseline process's exact observed argv and
adds **only `LD_DEBUG=libs`** to the fixed environment. Its actual loader stderr
and stdout are retained. The projection requires one PID, bounded ASCII records,
complete initialization/transfer/finalization, no unfamiliar messages, and exact
library identity correspondence to the decoder's declared dependency closure.
`trying file` and other search messages are never treated as loaded objects.
The two complete disassembly byte streams and instruction lists must be equal.

This is loader diagnostic evidence from the second run, **not `/proc` mapping
proof for the baseline**. Diagnostics alter the environment and can alter behavior.
Equal outputs do not prove identical side effects, every possible dlopen path,
absence of external writers, or the complete formal bootstrap module set.
There is no new allowlist, writer authority, accepted preparation or target SHA.

## Identity, process and resource contract

The registration fixes all 12 files from the successful dev007 static toolchain,
including Python 3.12.3, objdump and loader, by path/length/SHA256. Runtime inode
and timestamp state are newly recorded, not copied from the expired runner.
Static graph collection before and after the pair must match, including aliases,
absence checks and cache. Library bytes are hashed, not uploaded.

The original subreaper scope activates before each launch and drains after the
root is actually reaped. Any orphan, signal/cleanup failure or unknown process
outcome refuses the pair; cleanup does not turn it into successful evidence.
No guessed-PID fallback or process scanning. The supervisor must have the
already-required own-task/children/pidfd interfaces. It must run with `-I -S -B`.
The known unsupported local container is not used for this live pair.

At most two decoder launches, no retries or follow-up version/help commands.
Per child: original CPU 2 s, AS 256 MiB, output file 131072 B, core 0, 64 FDs.
Acceptance is strictly below each capture cap. One 3 s direct wait and at most
one 1 s cleanup wait; the original scope has its own 1 s drain. Baseline failure
prevents diagnostic launch. Partial files remain; candidate report precedes close,
and separate case-result records the post-close outcome. A failed close is refusal.

Two static graph passes retain their original 20 s cooperative deadline and
192 MiB read allowance EACH (worst-case aggregate 384 MiB, not hidden under one
192 MiB cap). Each graph internally rehashes its files. Output records are bounded
by the reused encoder; loader parsing permits at most 1024 lines/68 events under
the capture cap. Actual file snapshots, libc preexec behavior and synchronous I/O
still depend on a trusted single-threaded host, not an OS hard-time guarantee.
The outer supervisor has 60 s plus 3 s termination wait, step 3 min/job 5 min.
A dead supervisor/kernel stall can prevent complete descendant cleanup; such a
run is failed, never retrospectively accepted from partial output.

## CI and review scope

A separately registered first push/first attempt on the existing feature runs
367 explicit unit methods (original 345 unchanged plus 22 new), then one isolated
supervisor and the fixed pair. Sources and interpreter are checked before launch;
a started record exists before later checks. Read-only token, pinned actions,
no credentials persisted, no sudo/package installation/loader hooks changed.
The registered workflow is single-use; no Run/Rerun or renamed copy of this slot.

New unit cases use finite subprocess/scope stand-ins; the original listing parser
also consumes manufactured text in a unit test. Live GNU success must come from
the new run's original files, not these tests. No second independent reviewer is
claimed. The formal owner/BoundObservation/controller invocation, preparation
receipts, private-writer premise, and semantic acceptance remain separate tasks.

Sources used for protocol review:
- GNU objdump options: https://sourceware.org/binutils/docs/binutils/objdump.html
- GNU loader diagnostics: https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Environment-Variables.html
- glibc initializer reporting: https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-init.c

Main, existing HOLD/DEFER, old evidence, VERSION5.6.0, deletion authority and
release status remain unchanged. This is not a v5.7.0 release gate bypass.
