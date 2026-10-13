"""Bounded, pure ELF64/LE program-header metadata reader; never loads code.

Reference: ELF gABI (program header and dynamic section). Unsupported metadata is
refused; this is not an ELF validity certificate or a dynamic-loader simulation.
"""
from __future__ import annotations
import struct

FILE_CAP = 64 * 1024 * 1024
PH_CAP, DYN_CAP, STR_CAP, TEXT_CAP, NEED_CAP, NOTE_CAP = 64, 4096, 1048576, 4096, 64, 256


class Refused(ValueError):
    pass


def need(ok: bool, message: str) -> None:
    if not ok:
        raise Refused(message)


def parse_elf(data: bytes) -> dict:
    need(type(data) is bytes and 64 <= len(data) <= FILE_CAP, 'ELF file size/type')
    need(data[:7] == b'\x7fELF\x02\x01\x01', 'ELF class/endian/version')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    phoff = struct.unpack_from('<Q', data, 32)[0]
    ehsize, phsize, phnum = struct.unpack_from('<HHH', data, 52)
    need(kind in (2, 3) and machine == 62 and version == 1, 'unsupported ELF machine/type')
    need(ehsize == 64 and phsize == 56 and 1 <= phnum <= PH_CAP, 'program-header dimensions')
    need(64 <= phoff <= len(data) and phnum * 56 <= len(data) - phoff, 'program-header bounds')
    loads, dynamic, interpreters, notes = [], [], [], []
    for i in range(phnum):
        ptype, flags, offset, va, _, size, memsize, align = struct.unpack_from('<IIQQQQQQ', data, phoff+i*56)
        need(offset <= len(data) and size <= len(data)-offset, 'segment file bounds')
        if ptype == 1:
            need(size <= memsize and va + memsize <= 2**64, 'LOAD memory bounds')
            need(align in (0, 1) or align & (align-1) == 0, 'LOAD alignment')
            if align > 1:
                need((va-offset) % align == 0, 'LOAD address alignment')
            loads.append((offset, va, size))
        elif ptype == 2:
            dynamic.append((offset, size))
        elif ptype == 3:
            need(1 <= size <= TEXT_CAP, 'INTERP size')
            raw = data[offset:offset+size]
            need(raw[-1:] == b'\0' and b'\0' not in raw[:-1], 'INTERP termination')
            try:
                interpreter = raw[:-1].decode('ascii')
            except UnicodeError as exc:
                raise Refused('INTERP encoding') from exc
            need(interpreter.startswith('/') and '\n' not in interpreter, 'INTERP absolute path')
            interpreters.append(interpreter)
        elif ptype == 4:
            notes.append((offset, size))
    need(loads and len(dynamic) == 1 and len(interpreters) <= 1, 'missing/duplicate ELF segments')
    off, size = dynamic[0]
    need(size % 16 == 0 and 1 <= size // 16 <= DYN_CAP, 'dynamic dimensions')
    tags: dict[int, list[int]] = {}
    terminated = False
    for i in range(size//16):
        tag, value = struct.unpack_from('<qQ', data, off+i*16)
        if tag == 0:
            terminated = True
            break
        tags.setdefault(tag, []).append(value)
    need(terminated, 'dynamic not terminated')
    for tag in (5, 10, 14, 15, 29):
        need(len(tags.get(tag, [])) <= 1, 'duplicate singleton dynamic tag')
    for tag in (0x6ffffefb, 0x6ffffefc, 0x7ffffffd, 0x7fffffff):
        need(tag not in tags, 'unsupported loader audit/filter/auxiliary')
    need(5 in tags and 10 in tags, 'missing dynamic string table')
    address, count = tags[5][0], tags[10][0]
    need(1 <= count <= STR_CAP and address+count <= 2**64, 'string-table size/address')
    matches = [o+address-v for o, v, n in loads if v <= address and count <= n-(address-v)]
    need(len(matches) == 1, 'string-table mapping not unique')
    strings = memoryview(data)[matches[0]:matches[0]+count]

    def text(index: int) -> str:
        need(0 <= index < count, 'dynamic string index')
        raw = bytes(strings[index:min(count, index+TEXT_CAP+1)])
        end = raw.find(b'\0')
        need(0 <= end <= TEXT_CAP, 'dynamic string not bounded/terminated')
        try:
            value = raw[:end].decode('ascii')
        except UnicodeError as exc:
            raise Refused('dynamic string encoding') from exc
        need(all(32 <= ord(ch) < 127 for ch in value), 'dynamic string control character')
        return value

    needed = [text(i) for i in tags.get(1, [])]
    need(len(needed) <= NEED_CAP, 'too many dependencies')
    need(all(x and '/' not in x and '$' not in x for x in needed), 'unsupported dependency name')
    optional = lambda t: text(tags[t][0]) if t in tags else None
    build_ids, note_count = [], 0
    for off, size in notes:
        end, pos = off+size, off
        while pos < end:
            need(end-pos >= 12, 'truncated note header')
            namesz, descsz, ntype = struct.unpack_from('<III', data, pos)
            pos += 12
            name_end = pos+namesz
            desc_start = (name_end+3) & ~3
            desc_end = desc_start+descsz
            nxt = (desc_end+3) & ~3
            need(name_end <= end and desc_end <= end and nxt <= end, 'note bounds')
            note_count += 1
            need(note_count <= NOTE_CAP, 'too many notes')
            if ntype == 3 and data[pos:name_end] == b'GNU\0':
                need(1 <= descsz <= 64, 'build-id size')
                build_ids.append(data[desc_start:desc_end].hex())
            pos = nxt
    need(len(set(build_ids)) <= 1, 'conflicting build ids')
    return dict(schema='elf64-static-metadata-017-v1', machine=machine, elf_type=kind,
                interpreter=interpreters[0] if interpreters else None, needed=needed,
                soname=optional(14), rpath=optional(15), runpath=optional(29),
                gnu_build_id=build_ids[0] if build_ids else None,
                program_headers=phnum, dynamic_entries=i+1,
                additional_dynamic_tags=sorted(t for t in tags if t not in (1, 5, 10, 14, 15, 29)),
                runtime_loading_verified=False)
