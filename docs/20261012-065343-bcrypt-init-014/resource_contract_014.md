# 014 resource and interface contract

Status: DESIGN_REVIEWED_FOR_IMPLEMENTATION_AND_SYNTHETIC_VALIDATION.
Actual tool invocation remains gated by final source/budget review and registration.

## Reviewed choice and authority
The user requested the next handoff after 013 on 2026-10-12. The existing
014 document supplies the exact 391-byte target and gates. Routine choices here
implement that accepted scope. No new native/product/release authority is added.
The installed tool identity is separately recorded in tool-identity-014.json;
the numerical design review is design-review-014.json. Both are audit evidence.

The selected tool is /usr/bin/x86_64-linux-gnu-objdump, 390848 B, SHA256
7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5,
installed Ubuntu binutils-x86-64-linux-gnu 2.42-4ubuntu2.5, Build-ID
d5ff3d831c6e336d73e1db0ef48dcbe86f8831da. Installed package metadata is the
existing trust anchor. Nine static ELF/loader/dependency files total 6268752 B.
No version/help probe, tool installation or binary download is selected.

## Fixed limits
| Value | Limit |
|---|---:|
| owner admission deadline | 12 seconds |
| child CPU soft/hard | 2 seconds |
| child AS soft/hard | 268435456 B |
| child FSIZE soft/hard, per raw regular file | 131072 B |
| child CORE soft/hard | 0 |
| child NOFILE soft/hard | 64 |
| manifest | 32768 B |
| original image | 262144 B |
| admitted index | 327680 B |
| prior runtime admission | 32768 B |
| each and combined identity bytes | 67108864 B |
| streaming identity chunk | 65536 B |
| sources | exactly five, each <=32768 B |
| non-source identities | exactly 14 roles |
| alias checks / absent paths | <=12 / <=8 |
| path text | printable ASCII, <=256 characters |
| tool argv | exactly 18 fixed-format strings |
| owner stderr / stdout | <=65 B / 0 B |
| target | 1..391 B synthetic; exactly 391 B actual |
| instructions / instruction bytes | <=391 / 1..15 |
| raw text line / assembly text | <=512 / <=384 printable ASCII |
| report / marker | 524288 B / 1024 B |
| report wrapper without instruction list payload | <=16384 B |

The fixed environment is exactly LANG=C, LC_ALL=C, TZ=UTC, DEBUGINFOD_URLS="".
PATH is unnecessary because argv[0] is canonical; no environment is inherited.
Flags: -D -z -b binary -m i386:x86-64 -M intel --insn-width=16 --wide
--show-raw-insn --visualize-jumps=off --disassembler-color=off, followed by
--adjust-vma, --start-address, --stop-address and target.bin.
All three addresses derive from the explicit preferred VMA and exact length.

## Producer, capture and persistence proof
A child receives only the exact target.bin, never the original image. stdout
and stderr are exclusive regular files opened by the owner. FSIZE is lowered
in the child before exec. Each captured file is therefore <=131072 B under
the selected kernel contract. A size >=131072 is always a consumed failure,
including a clean exit. Such a file is the complete retained captured prefix;
bytes refused by the kernel are not described as captured output.

The five owner-managed runtime files are consumed.json, target.bin,
tool-stdout.bin, tool-stderr.bin and report.json. Their combined content limit
is 1024+391+131072+131072+524288 = 787847 B. Adding manifest gives 820615 B;
adding the maximum owner diagnostic gives 820680 B. These are content-byte
bounds for the named files, not block allocation or a host-wide write sandbox.
CORE=0 is a child rlimit; host-wide core-pipe behavior is not claimed.

The child must be reaped before raw capture is stable. Unreaped output is not
given a final complete digest. Popen creation, kill, wait and synchronous file
operations are not universally bounded-return OS operations. The single
12-second deadline is checked through the lifecycle and after final durability
readback; late outcomes return nonzero. Full report plus CLI zero is required.

Every file is exclusively created. The directory itself consumes the attempt
even if marker persistence fails. No existing result is overwritten. All
marker/target/raw/report contents and the directory receive fsync and complete
readback before success. Persistence failure may prevent writing a complete
JSON: a fixed <=65 B owner error code and independently captured CLI outcome
then provide the completion refusal. No exception text or traceback is dumped.

## Parent memory and deterministic serialization
The old index is used only after its fixed identity is verified; it is never
regenerated. Its admitted graph has 26721 nodes including keys and 171892
characters. The previous review's conservative 192*N+4*C graph accounting is
5818000 B. New code must separately bound manifest decoding, source loading,
raw capture, listing nodes, wrapper and serialization before encoding.
All large identities use streaming 65536-byte reads rather than retaining
Python or tool/dependency binaries.

Instruction records have exactly six fields:
file_offset, rva, preferred_vma (uint64); length (1..15);
raw_hex (2*length lowercase hex); assembly (<=384 printable ASCII).
Worst-case compact JSON is <=937 B per record, 366759 B for 391 records;
with wrapper <=16384 B, report <=383143 B, below 524288 B.
The encoder must walk and bound its graph and encoded byte count before
json.dumps, reject cycles/unsupported values/over-depth values, and verify
the final exact encoded count. The final independent review must confirm
allocation and traversal counts from the actual code before registration.

## Shared implementation interfaces
These APIs coordinate the five new source modules; no old source is imported.
- model.Refusal(code), model.require(condition, code), model.LIMITS, model.ENV,
  model.SOURCE_NAMES, model.ACTUAL_IMAGE/INDEX/ADMISSION/TARGET_SHA256.
- model.encode_json(value, cap, max_nodes=32768): bounded canonical compact
  ASCII JSON with final LF; raises Refusal on unsupported values or overflow.
- model.decode_json(raw, cap, max_nodes=32768): bounded depth, strict duplicate
  keys/nonfinite/numeric handling and graph validation; returns parsed JSON.
- model.validate_manifest(manifest): exact schema/fields/limits/roles/paths/
  argv/env and mode validation, returning the same validated dictionary.
- model.tool_argv(tool_path, target): fixed argv from target preferred_vma_start
  and rva_end-rva_start, ending in target.bin.
- model.select_target(manifest, image_bytes, index_bytes, admission_bytes):
  validate the admitted index association and mapping, returning exact slice.
- listing.parse_listing(raw_stdout, target_bytes, target_spec): return the
  exact six-field records after strict GNU header, address and byte coverage.
- io.verify_identity(record, cap, keep=False): bounded regular-file/canonical
  read and full SHA check; returns bytes only with keep=True, else None.
- io.verify_environment(manifest): full identity alias and absent-path checks.
- io.child_limits(): set the six reviewed child limit/signal conditions.
- io.OutputDirectory(path): exclusive mkdir, open directory; methods
  create(name, data, cap, mode=0o600), open_capture(name), read(name, cap),
  durable_readback(expected_bytes_by_name), close(). File descriptors remain
  owned until close; failures never overwrite files. open_capture returns fd.
- owner.observe(manifest, manifest_raw, manifest_path, expected_sha, start_ns):
  owns reservation/tool lifecycle/report; returns (CLI code, fixed diagnostic).
  Bootstrap supplies its pre-manifest clock, and never prints success output.

Manifest exact fields: schema, mode, call_id, source_commit, output_dir,
identities, sources, aliases, absent_paths, target, tool_argv, environment, limits.
Identity records have path/bytes/sha256 only. Non-source roles are image,
index, admission, python, tool, loader_cache, dep01..dep08. Sources use exactly
the five declared filenames. Alias records are path/target; all alias targets
must be a pinned identity path. Target fields are runtime_function_index,
section_index, file_start, file_end, rva_start, rva_end, preferred_vma_start,
sha256. Actual mode fixes 014-init-static-1 and the prior image/index/admission
identities and exact target. Synthetic mode rejects the actual image, target
and index SHA256 values, and uses recorded constructed fixtures only.

## Synthetic scope
Pure model/listing tests precede implementation and retain genuine RED/GREEN.
IO/owner tests use explicitly bounded synthetic Python children or dependency
injection for failure paths. Selected objdump behavior is exercised only by
a separately recorded finite synthetic fixture set under the reviewed owner
and resource limits. No original-image bytes are used in synthetic execution.
#198 actual registration is a later gate; no version/help call is part of tests.

Required cases include separated address domains, exact/end-inside instruction
coverage, zeros, maximum count, raw mismatch, extra lines, invalid decode,
identity/option failure, output caps, timeout/crash/reap, used directory, changed
target, and late write/fsync/readback. A consumed actual failure is not retried.

## Primary references and limits of interpretation
GNU 2.42 objdump manual:
https://sourceware.org/binutils/docs-2.42/binutils/objdump.html
It documents output/address flags; synthetic checks and raw-byte matching
establish the selected output grammar and exact coverage.
Static references do not establish live IAT binding, actual calls, failure
causality, or native qualification. Existing cost/native/HOLD boundaries remain.
