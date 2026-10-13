"""Synthetic ELF metadata only: no code loading or tool invocation."""
import struct
import unittest
from bcrypt_elf_inventory_017 import parse_elf, Refused


def specimen(needed=('libc.so.6',), runpath=None, interp='/lib64/ld-linux-x86-64.so.2', extra=()):
    data = bytearray(8192)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHI', data, 16, 3, 62, 1)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HHH', data, 52, 64, 56, 4)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0x400000, 0, len(data), len(data), 4096)
    strings = bytearray(b'\0')
    tags = [(5, 0x400400)]
    for name in needed:
        tags.append((1, len(strings))); strings.extend(name.encode()+b'\0')
    if runpath is not None:
        tags.append((29, len(strings))); strings.extend(runpath.encode()+b'\0')
    tags += [(10, len(strings)), *extra, (0, 0)]
    assert 0x800+len(tags)*16 <= 0x1800, 'synthetic sections overlap'
    data[0x400:0x400+len(strings)] = strings
    struct.pack_into('<IIQQQQQQ', data, 120, 2, 6, 0x800, 0x400800, 0, len(tags)*16, len(tags)*16, 8)
    for i, pair in enumerate(tags): struct.pack_into('<qQ', data, 0x800+i*16, *pair)
    note = struct.pack('<III', 4, 4, 3)+b'GNU\0'+b'abcd'
    data[0x1800:0x1800+len(note)] = note
    struct.pack_into('<IIQQQQQQ', data, 176, 4, 4, 0x1800, 0x401800, 0, len(note), len(note), 4)
    if interp:
        raw=interp.encode()+b'\0'; data[0x1c00:0x1c00+len(raw)] = raw
        struct.pack_into('<IIQQQQQQ', data, 232, 3, 4, 0x1c00, 0x401c00, 0, len(raw), len(raw), 1)
    return data


class ElfTests(unittest.TestCase):
    def reject(self, data):
        with self.assertRaises(Refused): parse_elf(bytes(data))

    def test_good_metadata(self):
        r=parse_elf(bytes(specimen(runpath='$ORIGIN/../lib')))
        self.assertEqual(r['needed'], ['libc.so.6'])
        self.assertEqual(r['runpath'], '$ORIGIN/../lib')
        self.assertEqual(r['gnu_build_id'], '61626364')
        self.assertIs(r['runtime_loading_verified'], False)

    def test_no_section_table_needed(self):
        b=specimen(); struct.pack_into('<Q', b, 40, 2**64-1)
        self.assertEqual(parse_elf(bytes(b))['machine'], 62)

    def test_no_interpreter_dso(self):
        self.assertIsNone(parse_elf(bytes(specimen(interp=None)))['interpreter'])

    def test_wrong_class(self):
        b=specimen(); b[4]=1; self.reject(b)

    def test_big_endian(self):
        b=specimen(); b[5]=2; self.reject(b)

    def test_wrong_machine(self):
        b=specimen(); struct.pack_into('<H', b, 18, 183); self.reject(b)

    def test_truncated_header(self): self.reject(specimen()[:63])

    def test_program_table_overflow(self):
        b=specimen(); struct.pack_into('<Q', b, 32, 2**64-8); self.reject(b)

    def test_excess_headers(self):
        b=specimen(); struct.pack_into('<H', b, 56, 65); self.reject(b)

    def test_truncated_segment(self):
        b=specimen(); struct.pack_into('<Q', b, 120+32, 8192); self.reject(b)

    def test_bad_load_memsize(self):
        b=specimen(); struct.pack_into('<Q', b, 64+40, 1); self.reject(b)

    def test_load_alignment(self):
        b=specimen(); struct.pack_into('<Q', b, 64+48, 3); self.reject(b)

    def test_dynamic_unterminated(self):
        b=specimen(); struct.pack_into('<qQ', b, 0x830, 99, 0); self.reject(b)

    def test_dynamic_duplicate(self): self.reject(specimen(extra=((5, 0x400400),)))

    def test_dynamic_audit_filter(self):
        for tag in (0x6ffffefb,0x6ffffefc,0x7ffffffd,0x7fffffff):
            with self.subTest(tag=tag): self.reject(specimen(extra=((tag,1),)))

    def test_zero_fill_strtab(self):
        b=specimen(); struct.pack_into('<Q', b, 0x808, 0x405000); self.reject(b)

    def test_ambiguous_mapping(self):
        b=specimen(); b[232:288]=b[64:120]; self.reject(b)

    def test_string_size_cap(self):
        b=specimen(); struct.pack_into('<Q', b, 0x828, 1048577); self.reject(b)

    def test_string_index(self):
        b=specimen(); struct.pack_into('<Q', b, 0x818, 1000); self.reject(b)

    def test_string_termination(self):
        b=specimen(); b[0x40a]=65; self.reject(b)

    def test_dependency_with_slash(self): self.reject(specimen(needed=('/tmp/libc.so.6',)))

    def test_dependency_empty(self): self.reject(specimen(needed=('',)))

    def test_dependency_count(self): self.reject(specimen(needed=tuple('x' for _ in range(65))))

    def test_bad_interpreter(self): self.reject(specimen(interp='relative-loader'))

    def test_note_overflow(self):
        b=specimen(); struct.pack_into('<I', b, 0x1804, 1000); self.reject(b)


if __name__=='__main__': unittest.main(verbosity=2)
