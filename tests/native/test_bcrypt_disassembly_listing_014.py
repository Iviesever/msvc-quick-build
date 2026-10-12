"""Constructed GNU-text fixtures only: never invokes a disassembler or reads a PE."""

import hashlib
import unittest

try:
    import bcrypt_disassembly_listing_014 as listing
    from bcrypt_disassembly_model_014 import Refusal
except ModuleNotFoundError as missing:
    listing = None
    MISSING_FEATURE = missing.name


DATA = bytes.fromhex("48 83 ec 28 90 c3")
ROWS = [(DATA[:4], "sub    rsp,0x28"), (DATA[4:5], "nop"), (DATA[5:], "ret")]
FIELDS = {"file_offset", "rva", "preferred_vma", "length", "raw_hex", "assembly"}


def target(data=DATA, **changes):
    result = {"runtime_function_index": 2, "section_index": 1,
              "file_start": 0x40, "file_end": 0x40 + len(data),
              "rva_start": 0x1200, "rva_end": 0x1200 + len(data),
              "preferred_vma_start": 0x180001200,
              "sha256": hashlib.sha256(data).hexdigest()}
    result.update(changes)
    return result


def text(rows=ROWS, spec=None):
    spec = target() if spec is None else spec
    address = spec["preferred_vma_start"]
    lines = ["", "target.bin:     file format binary", "", "",
             "Disassembly of section .data:", "", f"{address:016x} <.data>:"]
    for raw, assembly in rows:
        lines.append(f"   {address:x}:\t{raw.hex(' '):<47}\t{assembly}")
        address += len(raw)
    return ("\n".join(lines) + "\n").encode("ascii")


class ListingTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(listing, "missing listing feature/dependency: " +
                             globals().get("MISSING_FEATURE", ""))

    def parse(self, raw=None, data=DATA, spec=None):
        return listing.parse_listing(text() if raw is None else raw, data,
                                     target(data) if spec is None else spec)

    def refuse(self, raw, data=DATA, spec=None):
        with self.assertRaises(Refusal):
            self.parse(raw, data, spec)

    def test_exact_six_fields_and_separated_address_domains(self):
        records = self.parse()
        self.assertEqual(records, [
            {"file_offset": 0x40, "rva": 0x1200, "preferred_vma": 0x180001200,
             "length": 4, "raw_hex": "4883ec28", "assembly": "sub    rsp,0x28"},
            {"file_offset": 0x44, "rva": 0x1204, "preferred_vma": 0x180001204,
             "length": 1, "raw_hex": "90", "assembly": "nop"},
            {"file_offset": 0x45, "rva": 0x1205, "preferred_vma": 0x180001205,
             "length": 1, "raw_hex": "c3", "assembly": "ret"}])
        self.assertTrue(all(set(record) == FIELDS for record in records))

    def test_column_padding_changes_do_not_change_assembly(self):
        raw = text().replace(b"\t", b"   ")
        raw = raw.replace(b"sub    rsp,0x28\n", b"sub    rsp,0x28   \n")
        self.assertEqual(self.parse(raw)[0]["assembly"], "sub    rsp,0x28")

    def test_assembly_semantic_characters_are_preserved(self):
        assembly = "call   QWORD PTR [rip+0x12]        # 0x180001218"
        raw = text([(DATA, assembly)])
        self.assertEqual(self.parse(raw)[0]["assembly"], assembly)

    def test_zero_bytes_have_explicit_contiguous_coverage(self):
        data = b"\0\0"
        spec = target(data)
        records = self.parse(text([(data, "add    BYTE PTR [rax],al")], spec), data, spec)
        self.assertEqual(records[0]["raw_hex"], "0000")

    def test_maximum_391_one_byte_records(self):
        data = b"\x90" * 391
        spec = target(data)
        records = self.parse(text([(b"\x90", "nop")] * 391, spec), data, spec)
        self.assertEqual(len(records), 391)
        self.assertEqual(records[-1]["rva"], spec["rva_end"] - 1)

    def test_instruction_byte_boundary_15(self):
        data = bytes.fromhex("66 66 66 66 66 66 66 66 66 66 66 66 66 66 90")
        spec = target(data)
        self.assertEqual(self.parse(text([(data, "data16 nop")], spec), data, spec)[0]["length"], 15)

    def test_sixteen_raw_bytes_cannot_become_assembly(self):
        data = b"\x90" * 16
        self.refuse(text([(data, "nop")], target(data)), data)

    def test_missing_each_header_component(self):
        for line in (b"target.bin:     file format binary\n",
                     b"Disassembly of section .data:\n", b"0000000180001200 <.data>:\n"):
            with self.subTest(line=line):
                self.refuse(text().replace(line, b""))

    def test_duplicate_each_header_component(self):
        for line in (b"target.bin:     file format binary\n",
                     b"Disassembly of section .data:\n", b"0000000180001200 <.data>:\n"):
            with self.subTest(line=line):
                self.refuse(text().replace(line, line + line))

    def test_wrong_header_identity(self):
        for before, after in ((b"target.bin:", b"other.bin:"), (b"binary", b"pei-x86-64"),
                              (b"section .data", b"section .text"), (b"<.data>", b"<entry>")):
            with self.subTest(after=after):
                self.refuse(text().replace(before, after))

    def test_wrong_label_address(self):
        self.refuse(text().replace(b"0000000180001200 <", b"0000000180001201 <"))

    def test_extra_nonblank_line_before_after_or_between(self):
        for raw in (b"warning: unexpected\n" + text(), text() + b"extra\n",
                    text().replace(b"\tnop\n", b"\tnop\n???\n")):
            with self.subTest(raw=raw[-30:]):
                self.refuse(raw)

    def test_header_order_is_fixed(self):
        raw = text().replace(b"target.bin:     file format binary", b"PLACEHOLDER")
        raw = raw.replace(b"Disassembly of section .data:", b"target.bin:     file format binary")
        self.refuse(raw.replace(b"PLACEHOLDER", b"Disassembly of section .data:"))

    def test_gap_in_addresses(self):
        self.refuse(text().replace(b"180001204:", b"180001205:"))

    def test_overlap_in_addresses(self):
        self.refuse(text().replace(b"180001204:", b"180001203:"))

    def test_first_address_must_equal_target_start(self):
        self.refuse(text().replace(b"180001200:", b"1800011ff:"))

    def test_file_offset_or_rva_cannot_substitute_for_vma(self):
        for address in (b"40:", b"1200:"):
            self.refuse(text().replace(b"180001200:", address))

    def test_bytes_must_match_target_at_every_offset(self):
        for before, after in ((b"48 83 ec 28", b"48 83 ec 29"), (b"c3 ", b"cc ")):
            self.refuse(text().replace(before, after))

    def test_incomplete_trailing_coverage(self):
        self.refuse(text(ROWS[:-1]))

    def test_instruction_crossing_end_is_refused(self):
        self.refuse(text(), DATA[:-1], target(DATA[:-1]))

    def test_end_cut_in_instruction_with_pseudo_data(self):
        data = b"\xe8\x01\x02"
        rows = [(b"\xe8", ".byte 0xe8"), (b"\x01\x02", "add DWORD PTR [rdx],eax")]
        self.refuse(text(rows, target(data)), data)

    def test_invalid_decode_and_pseudo_data(self):
        for assembly in ("(bad)", ".byte 0x48", ".word 0x8348", ".long 0x28ec8348",
                         ".quad 0x28ec8348", ".zero 6", "db 0x48", "dw 0x8348", "dd 1", "dq 1"):
            with self.subTest(assembly=assembly):
                self.refuse(text([(DATA, assembly)]))

    def test_prefix_only_output_is_incomplete(self):
        for assembly in ("rex", "rex.W", "rex.WRXB", "repz", "repnz", "lock", "data16",
                         "addr32", "cs", "fs", "bnd", "notrack", "xacquire", "xrelease",
                         "data16 cs rex.W", "repz data16"):
            with self.subTest(assembly=assembly):
                self.refuse(text([(DATA, assembly)]))

    def test_prefix_with_an_instruction_is_preserved(self):
        for assembly in ("data16 nop", "rex.W nop", "repz ret", "bnd call 0x180001240"):
            with self.subTest(assembly=assembly):
                self.assertEqual(self.parse(text([(DATA, assembly)]))[0]["assembly"], assembly)

    def test_non_mnemonic_after_prefix_is_refused(self):
        self.refuse(text([(DATA, "rex.W 0x48")]))

    def test_no_instruction_or_empty_assembly(self):
        self.refuse(text([]))
        self.refuse(text([(DATA, "")]))

    def test_uppercase_raw_hex_is_not_fixed_gnu_format(self):
        self.refuse(text().replace(b"ec 28", b"EC 28"))

    def test_address_lexical_format_is_hex_without_0x(self):
        for replacement in (b"0x180001200:", b"+180001200:", b"0180001200000000000:"):
            self.refuse(text().replace(b"180001200:", replacement))

    def test_non_ascii_utf8_is_refused(self):
        self.refuse(text() + "说明\n".encode())

    def test_control_bytes_cannot_be_swallowed_as_line_breaks(self):
        for control in (b"\0", b"\r", b"\v", b"\f", b"\x1b", b"\x1c", b"\x1d", b"\x1e", b"\x7f"):
            with self.subTest(control=control):
                self.refuse(text().replace(b"\tnop\n", b"\tnop" + control + b"\n"))

    def test_tab_inside_assembly_is_refused(self):
        self.refuse(text().replace(b"sub    rsp", b"sub\trsp"))

    def test_assembly_length_384_boundary(self):
        assembly = "nop " + "a" * 380
        self.assertEqual(len(self.parse(text([(DATA, assembly)]))[0]["assembly"]), 384)
        self.refuse(text([(DATA, assembly + "a")]))

    def test_raw_line_512_boundary(self):
        raw = text()
        line = next(line for line in raw.split(b"\n") if b"\tnop" in line)
        padded = line + b" " * (512 - len(line))
        self.assertEqual(len(self.parse(raw.replace(line, padded))), 3)
        self.refuse(raw.replace(line, padded + b" "))

    def test_raw_capture_cap_is_failure_even_if_whitespace(self):
        base = text()
        self.assertEqual(len(self.parse(b"\n" * (131071 - len(base)) + base)), 3)
        self.refuse(b"\n" * (131072 - len(base)) + base)

    def test_missing_final_lf_is_not_complete_output(self):
        self.refuse(text()[:-1])

    def test_exact_bytes_input_types(self):
        for raw in (text().decode(), bytearray(text()), None):
            with self.subTest(raw_type=type(raw).__name__):
                with self.assertRaises(Refusal):
                    listing.parse_listing(raw, DATA, target())
        for data in (bytearray(DATA), DATA.hex(), None, b"", b"\x90" * 392):
            with self.subTest(data_type=type(data).__name__):
                with self.assertRaises(Refusal):
                    listing.parse_listing(text(), data, target())

    def test_exact_target_schema(self):
        for spec in (None, [], dict(target(), extra=1), {k: v for k, v in target().items() if k != "sha256"}):
            with self.subTest(spec=spec):
                with self.assertRaises(Refusal):
                    listing.parse_listing(text(), DATA, spec)

    def test_target_integer_types_and_uint64_bounds(self):
        for key in set(target()) - {"sha256"}:
            for value in (-1, True, "0", 1.0, 2**64):
                with self.subTest(key=key, value=value):
                    self.refuse(text(), spec=target(**{key: value}))

    def test_target_mapping_lengths_must_match(self):
        for key in ("file_start", "file_end", "rva_start", "rva_end"):
            with self.subTest(key=key):
                self.refuse(text(), spec=target(**{key: target()[key] + 1}))

    def test_target_digest_is_bound(self):
        for digest in ("0" * 64, "G" * 64, "f" * 63, 42, None):
            self.refuse(text(), spec=target(sha256=digest))

    def test_target_preferred_end_cannot_overflow_uint64(self):
        spec = target(preferred_vma_start=2**64 - 2)
        self.refuse(text(ROWS, spec), spec=spec)

    def test_maximum_valid_uint64_exclusive_end(self):
        data = b"\x90"
        spec = target(data, preferred_vma_start=2**64 - 2)
        result = self.parse(text([(data, "nop")], spec), data, spec)
        self.assertEqual(result[0]["preferred_vma"], 2**64 - 2)


if __name__ == "__main__":
    unittest.main()
