"""Synthetic byte contracts for diagnostic 011; no DLL or native execution."""

import hashlib
import struct
import unittest
from unittest.mock import patch

try:
    import producer_bcrypt_pe as pe
except ModuleNotFoundError as error:
    if error.name != "producer_bcrypt_pe":
        raise
    pe = None


def fixture(*, pe_offset=0x80, raw_start=0x400, rva_shift=0,
            rdata_size=0x1000, data_size=0x400, pdata_size=0x200,
            certificate=False):
    """Independent PE32+ DLL with names, ordinals, delay IAT and two functions."""
    optional = pe_offset + 24
    sections = optional + 240
    text_rva = 0x1000 + rva_shift
    rdata_rva = 0x2000 + rva_shift
    data_rva = rdata_rva + ((rdata_size + 0xFFF) & ~0xFFF)
    data_span = max(data_size, 0x600)
    pdata_rva = data_rva + ((data_span + 0xFFF) & ~0xFFF)
    rdata_raw = raw_start + 0x400
    data_raw = rdata_raw + rdata_size
    pdata_raw = data_raw + data_size
    data = bytearray(pdata_raw + pdata_size)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset:pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, pe_offset + 4,
                     0x8664, 4, 0x65432100, 0, 0, 240, 0x2022)
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<IIQII", data, optional + 16,
                     text_rva + 0x10, text_rva, 0x180000000, 0x1000, 0x200)
    struct.pack_into("<III", data, optional + 56,
                     (pdata_rva + pdata_size + 0xFFF) & ~0xFFF, 0x400, 0x12345678)
    struct.pack_into("<H", data, optional + 68, 2)
    struct.pack_into("<I", data, optional + 108, 16)
    for index, values in enumerate((
        (b".text", 0x300, text_rva, 0x400, raw_start, 0x60000020),
        (b".rdata", rdata_size, rdata_rva, rdata_size, rdata_raw, 0x40000040),
        (b".data", data_span, data_rva, data_size, data_raw, 0xC0000040),
        (b".pdata", pdata_size, pdata_rva, pdata_size, pdata_raw, 0x40000040),
    )):
        name, virtual_size, rva, raw_size, raw, flags = values
        struct.pack_into("<8sIIIIIIHHI", data, sections + index * 40,
                         name, virtual_size, rva, raw_size, raw, 0, 0, 0, 0, flags)
    data[raw_start:raw_start + 0x300] = b"\x90" * 0x300
    for index, rva, size in ((1, rdata_rva, 40), (3, pdata_rva, 24),
                             (12, data_rva, 24), (13, rdata_rva + 0x300, 64)):
        struct.pack_into("<II", data, optional + 112 + index * 8, rva, size)
    struct.pack_into("<IIIII", data, rdata_raw,
                     rdata_rva + 0x100, 0, 0, rdata_rva + 0x80, data_rva)
    data[rdata_raw + 0x80:rdata_raw + 0x80 + 13] = b"KERNEL32.dll\0"
    for at in (rdata_raw + 0x100, data_raw):
        struct.pack_into("<QQQ", data, at, rdata_rva + 0x200, (1 << 63) | 7, 0)
    struct.pack_into("<H", data, rdata_raw + 0x200, 9)
    data[rdata_raw + 0x202:rdata_raw + 0x202 + 13] = b"GetLastError\0"
    struct.pack_into("<IIIIIIII", data, rdata_raw + 0x300,
                     1, rdata_rva + 0x380, data_rva + 0x80,
                     data_rva + 0x40, rdata_rva + 0x400, 0, 0, 0)
    data[rdata_raw + 0x380:rdata_raw + 0x380 + 13] = b"ADVAPI32.dll\0"
    struct.pack_into("<QQ", data, rdata_raw + 0x400, rdata_rva + 0x4C0, 0)
    struct.pack_into("<QQ", data, data_raw + 0x40, 0x180000000 + text_rva + 0x100, 0)
    struct.pack_into("<H", data, rdata_raw + 0x4C0, 2)
    data[rdata_raw + 0x4C2:rdata_raw + 0x4C2 + 14] = b"RegOpenKeyExW\0"
    struct.pack_into("<IIIIII", data, pdata_raw,
                     text_rva, text_rva + 0x80, rdata_rva + 0x500,
                     text_rva + 0x80, text_rva + 0x100, rdata_rva + 0x510)
    data[rdata_raw + 0x500:rdata_raw + 0x508] = bytes((1, 4, 1, 0, 4, 0x32, 0, 0))
    data[rdata_raw + 0x510:rdata_raw + 0x514] = bytes((1, 0, 0, 0))
    at = {"pe": pe_offset, "optional": optional, "sections": sections,
          "text": raw_start, "text_rva": text_rva,
          "rdata": rdata_raw, "rdata_rva": rdata_rva,
          "data": data_raw, "data_rva": data_rva,
          "pdata": pdata_raw, "pdata_rva": pdata_rva,
          "init_rva": text_rva + 0x10}
    if certificate:
        at["certificate"] = len(data)
        data += struct.pack("<IHH", 11, 0x200, 2) + b"abc" + bytes(5)
        struct.pack_into("<II", data, optional + 112 + 4 * 8, at["certificate"], 16)
    return bytes(data), at


def put(data, offset, value, fmt="<I"):
    changed = bytearray(data)
    struct.pack_into(fmt, changed, offset, value)
    return bytes(changed)


def directory(data, at, index, rva, size):
    offset = at["optional"] + 112 + index * 8
    return put(put(data, offset, rva), offset + 4, size)


class BcryptPeTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(pe, "diagnostic 011 bounded PE inspector is not implemented")
        self.data, self.at = fixture()

    def inspect(self, data=None, init_rva=None):
        return pe.inspect_bcrypt_pe(
            self.data if data is None else data,
            self.at["init_rva"] if init_rva is None else init_rva)

    def reject(self, data, reason, *, init_rva=None):
        with self.assertRaises(pe.PeValidationError) as caught:
            self.inspect(data, init_rva)
        self.assertEqual(caught.exception.reason, reason)

    def test_complete_dll_preserves_hash_and_entrypoint_evidence(self):
        result = self.inspect()
        self.assertEqual(result["bytes"], len(self.data))
        self.assertEqual(result["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(result["entrypoint"]["rva"], 0x1010)
        self.assertEqual(result["entrypoint"]["file_offset"], 0x410)
        self.assertEqual(result["entrypoint"]["runtime_function_index"], 0)
        self.assertEqual(result["header"]["checksum"], 0x12345678)
        self.assertFalse(result["runtime_execution_observed"])
        self.assertIn("ACTUAL_EXECUTION_AND_FAILURE_CAUSE_UNKNOWN", result["unknowns"])
        self.assertLessEqual(result["work_units"], result["limits"]["work_units"])

    def test_import_names_ordinals_and_iat_addresses_are_distinct(self):
        result = self.inspect()
        imports = result["imports"]
        self.assertEqual(imports[0]["dll"], "KERNEL32.dll")
        named, ordinal = imports[0]["symbols"]
        self.assertEqual((named["name"], named["hint"]), ("GetLastError", 9))
        self.assertEqual((named["iat_rva"], named["iat_file_offset"]), (0x3000, 0x1800))
        self.assertEqual((named["lookup_rva"], named["lookup_file_offset"]), (0x2100, 0x900))
        self.assertEqual(ordinal["ordinal"], 7)
        self.assertIsNone(ordinal["name"])
        delay = result["delay_imports"][0]
        self.assertEqual(delay["dll"], "ADVAPI32.dll")
        self.assertEqual(delay["symbols"][0]["name"], "RegOpenKeyExW")
        self.assertEqual(delay["symbols"][0]["iat_rva"], 0x3040)
        self.assertEqual(delay["symbols"][0]["iat_raw_value"], 0x180001100)
        self.assertEqual(len(result["iat_index"]), 3)
        self.assertTrue(all(item["resolved_runtime_target"] is None for item in result["iat_index"]))

    def test_runtime_function_ranges_and_unwind_bytes_are_indexed(self):
        result = self.inspect()
        first, second = result["runtime_functions"]
        self.assertEqual((first["begin_rva"], first["end_rva"]), (0x1000, 0x1080))
        self.assertEqual((first["begin_file_offset"], first["end_file_offset"]), (0x400, 0x480))
        self.assertEqual(first["table_entry_file_offset"], 0x1C00)
        self.assertEqual(second["unwind_info_rva"], 0x2510)
        unwind = result["unwind_info"][0]
        self.assertEqual((unwind["rva"], unwind["file_offset"]), (0x2500, 0xD00))
        self.assertEqual(unwind["raw_hex"], "0104010004320000")

    def test_every_layout_is_derived_from_its_own_bytes(self):
        other, where = fixture(pe_offset=0x180, raw_start=0x600, rva_shift=0x10000)
        first = self.inspect()
        second = pe.inspect_bcrypt_pe(other, where["init_rva"])
        self.assertEqual(second["entrypoint"]["rva"], 0x11010)
        self.assertEqual(second["entrypoint"]["file_offset"], 0x610)
        self.assertNotEqual(first["sha256"], second["sha256"])
        self.assertEqual(second["imports"][0]["symbols"][0]["iat_file_offset"], 0x1A00)

    def test_certificate_table_is_file_offset_and_included_in_raw_hash(self):
        data, where = fixture(certificate=True)
        result = self.inspect(data)
        cert = result["certificates"][0]
        self.assertEqual((cert["file_offset"], cert["bytes"], cert["padded_bytes"]),
                         (where["certificate"], 11, 16))
        self.assertEqual((cert["revision"], cert["certificate_type"]), (0x200, 2))
        self.assertEqual(result["data_directories"][4]["address_kind"], "FILE_OFFSET")
        self.assertIsNone(result["data_directories"][4]["rva"])
        self.assertIn("CERTIFICATE_TRUST_NOT_VERIFIED", result["unknowns"])
        self.assertIn("CHECKSUM_NOT_VERIFIED", result["unknowns"])
        self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())
        self.assertNotEqual(result["sha256"], hashlib.sha256(data[:-16]).hexdigest())

    def test_input_types_size_and_init_rva_are_strict(self):
        for data in (None, bytearray(self.data), memoryview(self.data)):
            with self.assertRaises(pe.PeValidationError) as caught:
                pe.inspect_bcrypt_pe(data, self.at["init_rva"])
            self.assertEqual(caught.exception.reason, "INPUT_BYTES_REQUIRED")
        for value in (True, False, 0, -1, 1 << 32, "4112"):
            self.reject(self.data, "INIT_RVA_INVALID", init_rva=value)
        self.reject(b"", "IMAGE_SIZE_OUT_OF_RANGE")
        self.reject(self.data + bytes(1024 * 1024), "IMAGE_SIZE_OUT_OF_RANGE")

    def test_truncated_headers_are_refused(self):
        for data, reason in ((b"MZ", "DOS_HEADER_TRUNCATED"),
                             (self.data[:0x90], "PE_HEADER_RANGE_INVALID"),
                             (self.data[:0x100], "OPTIONAL_HEADER_RANGE_INVALID"),
                             (self.data[:0x220], "SECTION_TABLE_TRUNCATED")):
            with self.subTest(length=len(data)):
                self.reject(data, reason)
        self.reject(put(self.data, 0x3C, 0xFFFFFFF0), "PE_HEADER_RANGE_INVALID")

    def test_machine_magic_and_dll_profile_are_checked(self):
        for offset, value, fmt, reason in (
            (0, 0, "<H", "DOS_MAGIC_INVALID"),
            (self.at["pe"], 0, "<I", "PE_SIGNATURE_INVALID"),
            (self.at["pe"] + 4, 0x14C, "<H", "MACHINE_NOT_AMD64"),
            (self.at["optional"], 0x10B, "<H", "OPTIONAL_MAGIC_NOT_PE32_PLUS"),
            (self.at["pe"] + 22, 0x22, "<H", "DLL_IMAGE_PROFILE_INVALID"),
            (self.at["pe"] + 22, 0x2122, "<H", "DLL_IMAGE_PROFILE_INVALID"),
        ):
            with self.subTest(reason=reason):
                self.reject(put(self.data, offset, value, fmt), reason)

    def test_section_and_directory_counts_have_fixed_ceilings(self):
        for value in (0, 97):
            self.reject(put(self.data, self.at["pe"] + 6, value, "<H"), "SECTION_COUNT_INVALID")
        self.reject(put(self.data, self.at["optional"] + 108, 17), "DIRECTORY_COUNT_UNSUPPORTED")
        self.reject(put(self.data, self.at["pe"] + 20, 200, "<H"), "DIRECTORY_TABLE_TRUNCATED")
        self.reject(put(self.data, self.at["pe"] + 12, 1), "COFF_SYMBOL_TABLE_UNSUPPORTED")

    def test_image_headers_alignment_and_reserved_fields_are_validated(self):
        for relative, value, reason in (
            (36, 1000, "ALIGNMENT_UNSUPPORTED"),
            (32, 0x200, "ALIGNMENT_UNSUPPORTED"),
            (60, 0x200, "HEADERS_RANGE_INVALID"),
            (56, 0x5001, "IMAGE_RANGE_INVALID"),
            (52, 1, "OPTIONAL_RESERVED_FIELD_NONZERO"),
            (104, 1, "OPTIONAL_RESERVED_FIELD_NONZERO"),
        ):
            self.reject(put(self.data, self.at["optional"] + relative, value), reason)

    def test_raw_and_virtual_section_conflicts_are_rejected(self):
        section = self.at["sections"] + 40
        self.reject(put(self.data, section + 20, 0x400), "SECTION_RAW_OVERLAP")
        self.reject(put(self.data, section + 12, 0x1000), "SECTION_VIRTUAL_OVERLAP")
        self.reject(put(self.data, section + 16, 0xFFFFFF00), "SECTION_RAW_RANGE_INVALID")
        self.reject(put(self.data, section + 8, 0xFFFFFF00), "SECTION_VIRTUAL_RANGE_INVALID")
        self.reject(self.data[:-1], "SECTION_RAW_RANGE_INVALID")

    def test_all_directories_require_unique_file_backing(self):
        for rva, size in ((0x5000, 4), (0x33F8, 16), (0x1300, 4), (0xFFFFFFFC, 8)):
            with self.subTest(rva=rva, size=size):
                self.reject(directory(self.data, self.at, 0, rva, size), "DIRECTORY_NOT_FILE_BACKED")
        self.reject(directory(self.data, self.at, 0, 0x2000, 0), "DIRECTORY_PAIR_INVALID")
        self.reject(directory(self.data, self.at, 7, 0x2000, 4), "RESERVED_DIRECTORY_NONZERO")

    def test_directory_containment_is_not_ambiguous_section_mapping(self):
        data = directory(self.data, self.at, 0, 0x2000, 0x100)
        result = self.inspect(data)
        self.assertEqual(result["data_directories"][0]["file_offset"], 0x800)
        self.assertEqual(result["data_directories"][1]["file_offset"], 0x800)
        self.assertIn("OTHER_DIRECTORY_CONTENTS_NOT_INTERPRETED", result["unknowns"])

    def test_entrypoint_must_match_this_run_and_be_executable_file_bytes(self):
        self.reject(self.data, "INIT_ENTRYPOINT_MISMATCH", init_rva=0x1011)
        for rva in (0, 0x2000, 0x1300, 0x3400):
            changed = put(self.data, self.at["optional"] + 16, rva)
            self.reject(changed, "ENTRYPOINT_NOT_EXECUTABLE_FILE_BACKED", init_rva=rva or 0x1010)
        changed = put(self.data, self.at["sections"] + 36, 0x40000020)
        self.reject(changed, "ENTRYPOINT_NOT_EXECUTABLE_FILE_BACKED")

    def test_import_descriptor_terminator_is_required_inside_directory(self):
        self.reject(directory(self.data, self.at, 1, 0x2000, 20), "IMPORT_DESCRIPTOR_UNTERMINATED")
        self.reject(put(self.data, self.at["rdata"] + 12, 0), "IMPORT_DESCRIPTOR_INVALID")

    def test_import_strings_require_bounded_ascii_and_file_backing(self):
        data = bytearray(self.data)
        data[self.at["rdata"] + 0x600:self.at["rdata"] + 0x800] = b"a" * 512
        struct.pack_into("<I", data, self.at["rdata"] + 12, 0x2600)
        self.reject(bytes(data), "STRING_UNTERMINATED_OR_LIMIT")
        self.reject(put(self.data, self.at["rdata"] + 0x80, 0xFF, "<B"), "STRING_NOT_ASCII")
        self.reject(put(self.data, self.at["rdata"] + 12, 0x3400), "STRING_NOT_FILE_BACKED")

    def test_import_thunk_encoding_iat_length_and_content_are_checked(self):
        self.reject(put(self.data, self.at["rdata"] + 0x100, 1 << 40, "<Q"), "IMPORT_NAME_RVA_INVALID")
        self.reject(put(self.data, self.at["rdata"] + 0x108, (1 << 63) | 0x10000, "<Q"),
                    "IMPORT_ORDINAL_ENCODING_INVALID")
        self.reject(put(self.data, self.at["data"] + 8, 0, "<Q"), "IAT_LOOKUP_LENGTH_MISMATCH")
        self.reject(put(self.data, self.at["data"], 0x2201, "<Q"), "IAT_LOOKUP_CONTENT_MISMATCH")
        self.reject(put(self.data, self.at["rdata"] + 16, 0x3001), "IMPORT_TABLE_ALIGNMENT_INVALID")

    def test_original_first_thunk_zero_is_used_only_when_names_are_available(self):
        data = put(self.data, self.at["rdata"], 0)
        result = self.inspect(data)
        self.assertEqual(result["imports"][0]["symbols"][0]["name"], "GetLastError")
        bound = put(put(data, self.at["rdata"] + 4, 123), self.at["data"], 0x7FFA00001234, "<Q")
        result = self.inspect(bound)
        self.assertIsNone(result["imports"][0]["symbols"][0]["name"])
        self.assertIn("BOUND_IAT_WITHOUT_LOOKUP_NAMES_UNKNOWN", result["unknowns"])

    def test_import_iat_slots_cannot_be_claimed_by_two_descriptors(self):
        data = bytearray(directory(self.data, self.at, 1, 0x2000, 60))
        data[self.at["rdata"] + 20:self.at["rdata"] + 40] = data[self.at["rdata"]:self.at["rdata"] + 20]
        self.reject(bytes(data), "IAT_SLOT_ALIAS")

    def test_bound_iat_cannot_alias_an_entire_delay_descriptor(self):
        data = put(put(self.data, self.at["rdata"], 0), self.at["rdata"] + 4, 1)
        data = put(data, self.at["rdata"] + 16, 0x2300)
        data = directory(data, self.at, 12, 0, 0)
        # Three unknown bound addresses plus a terminator happen to be the
        # same 32 bytes as a valid delay descriptor. Equal intervals do not
        # authorize different structure kinds to claim the same bytes.
        self.reject(data, "IMPORT_TABLE_OVERLAP")

    def second_import(self, lookup_rva):
        data = directory(self.data, self.at, 1, 0x2000, 60)
        data = bytearray(directory(data, self.at, 12, 0x3000, 0x38))
        struct.pack_into("<IIIII", data, self.at["rdata"] + 20,
                         lookup_rva, 0, 0, 0x2380, 0x3020)
        data[self.at["data"] + 0x20:self.at["data"] + 0x38] = self.data[self.at["data"]:self.at["data"] + 24]
        return bytes(data)

    def test_lookup_cannot_reuse_another_descriptors_writable_iat(self):
        self.reject(self.second_import(0x3000), "IMPORT_TABLE_OVERLAP")

    def test_two_descriptors_may_share_a_read_only_lookup_array(self):
        result = self.inspect(self.second_import(0x2100))
        self.assertEqual(len(result["imports"]), 2)
        self.assertEqual(result["imports"][1]["symbols"][0]["name"], "GetLastError")

    def test_delay_storage_and_copy_tables_cannot_alias_iat_bytes(self):
        for relative, rva in ((0x308, 0x3000), (0x314, 0x3008), (0x318, 0x3040)):
            with self.subTest(relative=relative, rva=rva):
                self.reject(put(self.data, self.at["rdata"] + relative, rva), "IMPORT_TABLE_OVERLAP")

    def test_bound_iat_cannot_claim_exception_or_unwind_records(self):
        for rva in (0x4000, 0x2500):
            with self.subTest(rva=rva):
                data = put(put(self.data, self.at["rdata"], 0), self.at["rdata"] + 4, 1)
                data = put(data, self.at["rdata"] + 16, rva)
                self.reject(directory(data, self.at, 12, 0, 0), "IMPORT_TABLE_OVERLAP")

    def test_unindexed_iat_slots_cannot_claim_exception_records(self):
        data = directory(directory(self.data, self.at, 1, 0, 0), self.at, 13, 0, 0)
        data = directory(data, self.at, 12, 0x4000, 32)
        self.reject(data, "IMPORT_TABLE_OVERLAP")

    def test_iat_directory_must_cover_ordinary_import_slots(self):
        self.reject(directory(self.data, self.at, 12, 0x3008, 16), "IMPORT_IAT_OUTSIDE_DIRECTORY")
        self.reject(directory(self.data, self.at, 12, 0x3000, 23), "IAT_DIRECTORY_ALIGNMENT_INVALID")

    def test_delay_descriptor_form_and_optional_arrays_are_checked(self):
        self.reject(put(self.data, self.at["rdata"] + 0x300, 0), "DELAY_VA_FORM_UNSUPPORTED")
        self.reject(put(self.data, self.at["rdata"] + 0x300, 3), "DELAY_ATTRIBUTES_INVALID")
        self.reject(directory(self.data, self.at, 13, 0x2300, 32), "DELAY_DESCRIPTOR_UNTERMINATED")
        self.reject(put(self.data, self.at["rdata"] + 0x308, 0x3400), "DELAY_MODULE_HANDLE_NOT_FILE_BACKED")
        data = put(self.data, self.at["rdata"] + 0x318, 0x2700)
        self.reject(data, "DELAY_UNLOAD_IAT_MISMATCH")
        copied = bytearray(data)
        copied[self.at["rdata"] + 0x700:self.at["rdata"] + 0x710] = self.data[self.at["data"] + 0x40:self.at["data"] + 0x50]
        self.assertEqual(self.inspect(bytes(copied))["delay_imports"][0]["unload_iat_rva"], 0x2700)

    def test_exception_table_requires_complete_ordered_nonoverlapping_ranges(self):
        self.reject(directory(self.data, self.at, 3, 0x4000, 23), "EXCEPTION_DIRECTORY_ALIGNMENT_INVALID")
        self.reject(put(self.data, self.at["pdata"] + 4, 0x1000), "RUNTIME_FUNCTION_RANGE_INVALID")
        self.reject(put(self.data, self.at["pdata"] + 12, 0x107F), "RUNTIME_FUNCTION_ORDER_OR_OVERLAP")
        self.reject(put(self.data, self.at["pdata"] + 4, 0x1301), "RUNTIME_FUNCTION_NOT_EXECUTABLE_FILE_BACKED")
        self.reject(put(self.data, self.at["pdata"] + 8, 0x2501), "UNWIND_ALIGNMENT_INVALID")

    def test_unwind_fixed_header_slots_and_flags_are_validated(self):
        unwind = self.at["rdata"] + 0x500
        self.reject(put(self.data, unwind, 3, "<B"), "UNWIND_VERSION_UNSUPPORTED")
        self.reject(put(self.data, unwind, 0x29, "<B"), "UNWIND_FLAGS_INVALID")
        self.reject(put(self.data, unwind + 5, 1, "<B"), "UNWIND_OPCODE_TRUNCATED")
        self.reject(put(self.data, unwind + 5, 7, "<B"), "UNWIND_OPCODE_UNSUPPORTED")
        self.reject(put(self.data, self.at["pdata"] + 8, 0x2FFC), "UNWIND_VERSION_UNSUPPORTED")
        data = bytearray(put(self.data, self.at["pdata"] + 8, 0x2FFC))
        data[self.at["rdata"] + 0xFFC:self.at["rdata"] + 0x1000] = bytes((1, 1, 1, 0))
        self.reject(bytes(data), "UNWIND_NOT_FILE_BACKED")

    def test_unwind_v2_epilog_index_retains_explicit_semantic_unknown(self):
        data = bytearray(self.data)
        data[self.at["rdata"] + 0x500:self.at["rdata"] + 0x508] = bytes((2, 4, 1, 0, 3, 0x16, 0, 0))
        result = self.inspect(bytes(data))
        self.assertEqual(result["unwind_info"][0]["version"], 2)
        self.assertIn("UNWIND_V2_EPILOG_SEMANTICS_NOT_INTERPRETED", result["unknowns"])

    def test_unwind_handler_has_mapping_but_language_payload_is_unknown(self):
        data = bytearray(self.data)
        data[self.at["rdata"] + 0x510:self.at["rdata"] + 0x518] = struct.pack("<BBBBI", 9, 0, 0, 0, 0x1100)
        result = self.inspect(bytes(data))
        self.assertEqual(result["unwind_info"][1]["handler_rva"], 0x1100)
        self.assertEqual(result["unwind_info"][1]["handler_file_offset"], 0x500)
        self.assertIn("LANGUAGE_SPECIFIC_UNWIND_DATA_NOT_INTERPRETED", result["unknowns"])
        self.reject(put(bytes(data), self.at["rdata"] + 0x514, 0x2000),
                    "UNWIND_HANDLER_NOT_EXECUTABLE_FILE_BACKED")

    def test_unwind_chain_is_indexed_without_recursing_into_code(self):
        data = bytearray(self.data)
        data[self.at["rdata"] + 0x510:self.at["rdata"] + 0x520] = struct.pack(
            "<BBBBIII", 0x21, 0, 0, 0, 0x1000, 0x1080, 0x2500)
        result = self.inspect(bytes(data))
        self.assertEqual(result["unwind_info"][1]["chain"]["unwind_info_rva"], 0x2500)
        self.assertEqual(len(result["unwind_info"]), 2)
        self.reject(put(bytes(data), self.at["rdata"] + 0x51C, 0x2510), "UNWIND_CHAIN_CYCLE")

    def test_unwind_chain_depth_has_no_implicit_growth(self):
        data = bytearray(self.data)
        for index in range(33):
            rva = 0x2600 + index * 16
            offset = self.at["rdata"] + 0x600 + index * 16
            struct.pack_into("<BBBBIII", data, offset, 0x21, 0, 0, 0,
                             0x1000, 0x1080, rva + 16)
        data[self.at["rdata"] + 0x810:self.at["rdata"] + 0x814] = bytes((1, 0, 0, 0))
        struct.pack_into("<I", data, self.at["pdata"] + 8, 0x2600)
        self.reject(bytes(data), "UNWIND_CHAIN_DEPTH_LIMIT")

    def test_unwind_chain_target_prolog_must_fit_its_declared_range(self):
        data = bytearray(self.data)
        data[self.at["rdata"] + 0x510:self.at["rdata"] + 0x520] = struct.pack(
            "<BBBBIII", 0x21, 0, 0, 0, 0x1000, 0x1001, 0x2500)
        self.reject(bytes(data), "UNWIND_PROLOG_RANGE_INVALID")

    def test_missing_exception_directory_does_not_invent_function_boundaries(self):
        result = self.inspect(directory(self.data, self.at, 3, 0, 0))
        self.assertEqual(result["runtime_functions"], [])
        self.assertIsNone(result["entrypoint"]["runtime_function_index"])
        self.assertIn("ENTRYPOINT_FUNCTION_BOUNDARY_UNKNOWN", result["unknowns"])

    def test_certificate_bounds_alignment_records_and_padding_are_checked(self):
        data, where = fixture(certificate=True)
        self.reject(data[:-1], "CERTIFICATE_RANGE_INVALID")
        self.reject(directory(data, where, 4, where["certificate"] + 1, 15), "CERTIFICATE_RANGE_INVALID")
        self.reject(directory(data, where, 4, 0x800, 16), "CERTIFICATE_IMAGE_ALIAS")
        self.reject(put(data, where["certificate"], 7), "CERTIFICATE_RECORD_INVALID")
        self.reject(put(data, where["certificate"], 24), "CERTIFICATE_RECORD_INVALID")
        self.reject(data[:-1] + b"x", "CERTIFICATE_PADDING_NONZERO")

    def test_certificate_count_is_independently_bounded(self):
        data = self.data + struct.pack("<IHH", 8, 0x200, 2) * 65
        data = directory(data, self.at, 4, len(self.data), 65 * 8)
        self.reject(data, "CERTIFICATE_COUNT_LIMIT")

    def test_import_descriptor_count_is_independently_bounded(self):
        data, at = fixture(rdata_size=0x4000, data_size=0x800)
        data = bytearray(directory(data, at, 1, at["rdata_rva"] + 0x1000, 130 * 20))
        for index in range(129):
            struct.pack_into("<IIIII", data, at["rdata"] + 0x1000 + index * 20,
                             0, 0, 0, at["rdata_rva"] + 0x80, at["data_rva"] + 0x100 + index * 8)
        data = directory(bytes(data), at, 12, 0, 0)
        self.reject(data, "IMPORT_DESCRIPTOR_LIMIT")

    def test_thunk_count_is_independently_bounded(self):
        data, at = fixture(rdata_size=0x4000, data_size=0x4000)
        data = bytearray(directory(data, at, 12, 0, 0))
        struct.pack_into("<I", data, at["rdata"], at["rdata_rva"] + 0x1000)
        struct.pack_into("<I", data, at["rdata"] + 16, at["data_rva"] + 0x1000)
        values = struct.pack("<Q", (1 << 63) | 7) * 1025 + bytes(8)
        data[at["rdata"] + 0x1000:at["rdata"] + 0x1000 + len(values)] = values
        data[at["data"] + 0x1000:at["data"] + 0x1000 + len(values)] = values
        self.reject(bytes(data), "IMPORT_SYMBOLS_PER_DLL_LIMIT")

    def test_exception_count_is_independently_bounded(self):
        data, at = fixture(pdata_size=0x30200)
        data = directory(data, at, 3, at["pdata_rva"], 16385 * 12)
        self.reject(data, "RUNTIME_FUNCTION_COUNT_LIMIT")

    def test_import_symbol_count_is_bounded_across_dlls(self):
        data, at = fixture(rdata_size=0x10000, data_size=0x10000)
        data = directory(directory(data, at, 12, 0, 0), at, 13, 0, 0)
        data = bytearray(directory(data, at, 1, at["rdata_rva"] + 0x1000, 6 * 20))
        values = struct.pack("<Q", (1 << 63) | 7) * 1024 + bytes(8)
        for index in range(5):
            lookup_delta, iat_delta = 0x2000 + index * 0x2100, 0x1000 + index * 0x2100
            struct.pack_into("<IIIII", data, at["rdata"] + 0x1000 + index * 20,
                             at["rdata_rva"] + lookup_delta, 0, 0,
                             at["rdata_rva"] + 0x80, at["data_rva"] + iat_delta)
            for start in (at["rdata"] + lookup_delta, at["data"] + iat_delta):
                data[start:start + len(values)] = values
        self.reject(bytes(data), "IMPORT_SYMBOL_COUNT_LIMIT")

    def test_total_work_budget_is_enforced_at_parse_operations(self):
        with patch.object(pe, "MAX_WORK_UNITS", len(self.data) + 512):
            self.reject(self.data, "PARSE_WORK_LIMIT")


if __name__ == "__main__":
    unittest.main()
