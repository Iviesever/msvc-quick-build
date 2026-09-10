# Physical write-domain admission (M1b groundwork)

This is an opt-in Windows primitive, **not a complete project transaction**.
No CLI, `plan` or `compdb` caller adopts it. VERSION stays 5.5.0. The live roadmap
and acceptance decisions remain in issue #164; this file specifies the new API.

## Contract

`WindowsWriteDomain::open(existing_absolute_directory)` holds a non-inheritable
directory handle, reads its volume/128-bit file ID, and creates nothing. Initial
support is local NTFS with a volume-GUID identity; unsupported/unknown identity
fails rather than falling back to a pathname. File identity is meaningful while
the handle is held, not across arbitrary deletion/recreation.

`try_reserve()` atomically creates `.mqb-write-claim-v1` **relative to that handle**
using `NtCreateFile(FILE_CREATE)`. An alias changing after the pin cannot redirect
this create. No string-key mutex, PID lookup, age/timeout or recursive takeover
is involved. Any existing marker blocks admission, even after all its handles
close, even empty or malformed. Its bytes are diagnostic, not a recovery oracle.
An inaccessible or otherwise uncertain marker is also a refusal.

`withdraw_unstarted()` deletes only the owned marker handle, and is permitted
only before `begin_writes()`. The caller must have submitted no writer in the
reserved state. Errors are not successful-withdrawal evidence. Destruction never
calls withdrawal, including normal destruction and move-assignment replacement.
Once `begin_writes()` succeeds there is intentionally **no completion, reset or
recovery API**: disposal leaves the marker unresolved. This conservative building
block cannot yet be adopted by a normal build that needs safe reusable commits.

Only cooperative clients selecting the same authority directory are serialized.
It neither intercepts legacy writers nor establishes recursive containment. ACL
hardening against hostile same-user manipulation, hostile marker removal, remote
filesystems, disk/power loss, volume resets and directory-tree destruction are
outside this contract. `FlushFileBuffers` is not a claim that namespace updates
survive arbitrary storage failure. A process exit or unlocked kernel object is
not evidence that a shared service or detached writer stopped.

## Current writable-state audit and integration blockers

| Current responsibility | Existing source authority and writes | Boundary still required before CLI adoption |
| --- | --- | --- |
| Source discovery | `SourceDiscovery.cpp` prepares the cache hierarchy; `discovery/cache/DiscoveryCache.cpp` writes/renames discovery records. Default is discovery root `.mqb/cache/discovery`; request may supply a cache path. | This happens before the target layout/toolchain is built. Resolve external discovery roots and overridden cache destinations before admission, not afterward. |
| Toolchain discovery | `Application.cpp` selects project `.mqb/cache/toolchain`; `VisualStudioToolchainCache.cpp` also has a standalone cwd-relative default. `VisualStudioEnvironment.cpp` creates/removes an invocation-local script under the OS temp directory. | Distinguish shared project cache from invocation-private temporary files and third-party tool effects. A project marker does not reserve the entire temp directory or VS installation. |
| Compile/scan | `ProjectArtifactLayout.cpp` owns obj/deps/scan/ifc and source caches (including external-source keys); compiler execution prepares output parents; scanners write P1689/dependency files. | Inspect caller-supplied recipes/paths, subdirectory reparse points, compiler side outputs and explicit native output arguments. One directory pin does not close this write set. |
| PCH/modules | PCH coordinator writes the synthetic creator and PCH state; module coordinators use the same typed layouts for provider/IFC/scan/cache artifacts. | All parent/provider stages must be admitted together. Read-only external inputs are not automatically write domains. |
| Link/archive | Linker/librarian create parent directories and target/side outputs; incremental coordinators save link/archive cache files, including temporary replacement files. | The terminal phase must remain inside the same admitted write set, with original failure diagnostics and freshness checks preserved. |
| Foreground artifact | `BuildCompletion` runs only after build-side C++ objects leave scope. | Keep it outside the future write transaction, but do not treat scope exit as external-writer completion. |

Before whole-project admission, compute a complete physical write set, handle
missing-root initialization, shared `.mqb` aliases, overlapping/nested roots and
external/reparse destinations, and define deadlock-free multi-domain acquisition.
Paired native subshards intentionally share a repository `.mqb`; they cannot be
made correct by ignoring busy errors. Read-only operations must not create a
marker. Dirty/commit generations and crash recovery need a separate proven
writer-completion authority. None is supplied by this class.

## Validation scope

The existing Windows process-runner executable contains fixed real NTFS tests:
case/dot/junction aliases, a retargeted junction after pinning, same-domain process
contention, distinct-domain concurrency, moved states, absent/invalid roots,
empty/malformed/directory markers, explicit unstarted withdrawal, no withdrawal
after writes, normal owner abandonment, and abnormal owner death while a detached
child writes afterward. The child does not inherit the marker handle; its exit
alone still does not allow takeover. Test-only tree deletion occurs only after
all known fixture children have exited and is not product recovery behavior.

Those tests do not run actual MSVC under this admission primitive. Existing
MSVC/service/target tests remain separate and do not prove new lease integration.
I/O-fault injection, remote-volume behavior, hostile manipulation, owner power
loss and recursive write-set coverage are not inferred from positive local tests.

## Platform contracts

- [FILE_ID_INFO: identity of held handles](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info)
- [NtCreateFile: user-mode relative RootDirectory create](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile)
- [GetFinalPathNameByHandleW: local volume GUID versus network shares](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)
- [SetFileInformationByHandle: deletion through the held handle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
