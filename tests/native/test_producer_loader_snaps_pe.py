"""Byte-contract tests; no Windows calls, executable writes, or native runs."""

import hashlib
import struct
import unittest

try:
    import producer_loader_snaps_pe as pe
except ModuleNotFoundError as error:
    if error.name != "producer_loader_snaps_pe":
        raise
    pe = None


LIMIT = 8 * 1024 * 1024


def fixture(*, pe_offset=0x80, raw_offset=0x400, rdata_rva=0x2000,
            config_delta=0x20, directory_count=16, optional_size=240):
    """Independent, deliberately small AMD64 PE with two file-backed sections."""
    section_table = pe_offset + 24 + optional_size
    headers = (section_table + 80 + 511) & ~511
    data = bytearray(raw_offset + 0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset:pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, pe_offset + 4,
                     0x8664, 2, 0, 0, 0, optional_size, 0x22)
    optional = pe_offset + 24
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 16, 0x1000)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", data, optional + 56,
                     (rdata_rva + 0xFFF + 0x400) & ~0xFFF, headers)
    struct.pack_into("<H", data, optional + 68, 3)
    struct.pack_into("<I", data, optional + 108, directory_count)
    struct.pack_into("<II", data, optional + 112 + 10 * 8,
                     rdata_rva + config_delta, 0x140)
    struct.pack_into("<8sIIIIIIHHI", data, section_table,
                     b".text\0\0\0", 0x180, 0x1000, 0x200, headers,
                     0, 0, 0, 0, 0x60000020)
    struct.pack_into("<8sIIIIIIHHI", data, section_table + 40,
                     b".rdata\0\0", 0x300, rdata_rva, 0x400, raw_offset,
                     0, 0, 0, 0, 0x40000040)
    data[headers:headers + 0x200] = bytes(range(256)) * 2
    data[raw_offset:raw_offset + 0x400] = bytes(range(256)) * 4
    config = raw_offset + config_delta
    struct.pack_into("<I", data, config, 0x140)
    struct.pack_into("<II", data, config + 12, 0, 0)
    return bytes(data), {
        "pe": pe_offset, "optional": optional, "sections": section_table,
        "headers": headers, "raw": raw_offset, "rva": rdata_rva,
        "config": config, "field": config + 16,
        "directory": optional + 112 + 10 * 8,
        "certificate": optional + 112 + 4 * 8,
    }


def put(data, offset, value, format="<I"):
    changed = bytearray(data)
    struct.pack_into(format, changed, offset, value)
    return bytes(changed)


def patched(data, field):
    return put(data, field, 2)


class LoaderSnapsPeTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(pe, "bounded PE helper is not implemented")
        self.data, self.at = fixture()

    def inspect(self, data=None, expected=0, limit=LIMIT):
        return pe.inspect_loader_snaps_image(
            self.data if data is None else data,
            expected_flags_set=expected, max_image_bytes=limit)

    def reject(self, data, reason):
        with self.assertRaises(pe.PeValidationError) as caught:
            self.inspect(data)
        self.assertEqual(caught.exception.reason, reason)

    def test_valid_layout_yields_derived_offset_and_full_fingerprints(self):
        result = self.inspect()
        self.assertEqual(result["bytes"], len(self.data))
        self.assertEqual(result["sha256"], hashlib.sha256(self.data).hexdigest())
        config = result["load_config"]
        self.assertEqual(config["global_flags_set_file_offset"], self.at["field"])
        self.assertEqual(config["file_offset"], self.at["config"])
        self.assertEqual(config["section_name"], ".rdata")
        self.assertEqual(config["global_flags_clear"], 0)
        self.assertEqual(config["global_flags_set"], 0)
        self.assertEqual(config["global_flags_set_raw_hex"], "00000000")
        self.assertEqual(config["patch_bytes_hex"], "02000000")
        self.assertEqual(config["structure_sha256"], hashlib.sha256(
            self.data[self.at["config"]:self.at["config"] + 0x140]).hexdigest())

    def test_relocated_headers_and_sections_do_not_reuse_previous_offset(self):
        other, where = fixture(pe_offset=0x180, raw_offset=0x800,
                               rdata_rva=0x6000, config_delta=0x80,
                               directory_count=11, optional_size=200)
        first = self.inspect()
        second = self.inspect(other)
        self.assertNotEqual(first["load_config"]["global_flags_set_file_offset"],
                            second["load_config"]["global_flags_set_file_offset"])
        self.assertEqual(second["load_config"]["global_flags_set_file_offset"],
                         where["field"])
        self.assertEqual(second["header"]["optional_header_size"], 200)
        self.assertEqual(second["header"]["number_of_rva_and_sizes"], 11)

    def test_input_and_bound_types_are_strict(self):
        for data in (bytearray(self.data), memoryview(self.data), None):
            with self.subTest(kind=type(data).__name__):
                with self.assertRaises(pe.PeValidationError) as caught:
                    pe.inspect_loader_snaps_image(
                        data, expected_flags_set=0, max_image_bytes=LIMIT)
                self.assertEqual(caught.exception.reason, "INPUT_BYTES_REQUIRED")
        for value in (True, -1, 1, 3, "0"):
            with self.subTest(expected=value):
                with self.assertRaises(pe.PeValidationError) as caught:
                    self.inspect(expected=value)
                self.assertEqual(caught.exception.reason, "FLAGS_EXPECTATION_INVALID")
        for value in (True, 0, -1, LIMIT + 1, "2048"):
            with self.subTest(limit=value):
                with self.assertRaises(pe.PeValidationError) as caught:
                    self.inspect(limit=value)
                self.assertEqual(caught.exception.reason, "IMAGE_LIMIT_INVALID")

    def test_hard_image_limit_has_no_implicit_expansion(self):
        self.inspect(limit=len(self.data))
        with self.assertRaises(pe.PeValidationError) as caught:
            self.inspect(limit=len(self.data) - 1)
        self.assertEqual(caught.exception.reason, "IMAGE_SIZE_OUT_OF_RANGE")

    def test_short_dos_or_wrong_magic_is_rejected(self):
        self.reject(b"MZ", "DOS_HEADER_TRUNCATED")
        self.reject(b"ZZ" + self.data[2:], "DOS_MAGIC_INVALID")

    def test_pe_offset_cannot_point_inside_dos_or_past_file(self):
        for offset in (0, 0x3C, 0xFFFFFFF0):
            with self.subTest(offset=offset):
                self.reject(put(self.data, 0x3C, offset), "PE_HEADER_RANGE_INVALID")

    def test_signature_architecture_and_executable_profile_are_checked(self):
        self.reject(put(self.data, self.at["pe"], 0), "PE_SIGNATURE_INVALID")
        self.reject(put(self.data, self.at["pe"] + 4, 0x14C, "<H"),
                    "MACHINE_NOT_AMD64")
        self.reject(put(self.data, self.at["optional"], 0x10B, "<H"),
                    "OPTIONAL_MAGIC_NOT_PE32_PLUS")
        for value in (0x20, 0x2022, 0x1022, 0x122):
            with self.subTest(characteristics=value):
                self.reject(put(self.data, self.at["pe"] + 22, value, "<H"),
                            "COFF_IMAGE_PROFILE_INVALID")

    def test_section_count_and_symbol_table_are_bounded(self):
        for value in (0, 97):
            with self.subTest(count=value):
                self.reject(put(self.data, self.at["pe"] + 6, value, "<H"),
                            "SECTION_COUNT_INVALID")
        self.reject(put(self.data, self.at["pe"] + 12, 1),
                    "COFF_SYMBOL_TABLE_UNSUPPORTED")

    def test_declared_optional_header_and_directory_ranges_are_checked(self):
        for size in (0, 111, 0xFFFF):
            with self.subTest(size=size):
                self.reject(put(self.data, self.at["pe"] + 20, size, "<H"),
                            "OPTIONAL_HEADER_RANGE_INVALID")
        for count in (0, 10, 17, 0xFFFFFFFF):
            with self.subTest(count=count):
                self.reject(put(self.data, self.at["optional"] + 108, count),
                            "DIRECTORY_COUNT_UNSUPPORTED")
        self.reject(put(self.data, self.at["pe"] + 20, 200, "<H"),
                    "DIRECTORY_TABLE_TRUNCATED")

    def test_truncated_section_table_is_not_accepted_as_zero_sections(self):
        self.reject(self.data[:self.at["sections"] + 79],
                    "SECTION_TABLE_TRUNCATED")

    def test_alignment_header_and_image_bounds_are_checked(self):
        for relative, value, reason in (
            (36, 1000, "ALIGNMENT_UNSUPPORTED"),
            (32, 0x800, "ALIGNMENT_UNSUPPORTED"),
            (60, 0x1FF, "HEADERS_RANGE_INVALID"),
            (60, len(self.data) + 0x200, "HEADERS_RANGE_INVALID"),
            (56, 0x3001, "IMAGE_RANGE_INVALID"),
            (56, 0x1000, "SECTION_VIRTUAL_RANGE_INVALID"),
            (56, 0xFFFFF000, "IMAGE_RANGE_INVALID"),
            (52, 1, "OPTIONAL_RESERVED_FIELD_NONZERO"),
            (104, 1, "OPTIONAL_RESERVED_FIELD_NONZERO"),
        ):
            with self.subTest(relative=relative, value=value):
                self.reject(put(self.data, self.at["optional"] + relative, value), reason)

    def test_raw_ranges_must_be_file_backed_and_outside_headers(self):
        section = self.at["sections"] + 40
        for relative, value in ((20, 0), (20, 0xFFFFFE00),
                                (16, 0xFFFFFE00), (20, len(self.data))):
            with self.subTest(relative=relative, value=value):
                self.reject(put(self.data, section + relative, value),
                            "SECTION_RAW_RANGE_INVALID")

    def test_raw_overlap_is_rejected_before_rva_mapping(self):
        self.reject(put(self.data, self.at["sections"] + 40 + 20,
                        self.at["headers"]), "SECTION_RAW_OVERLAP")

    def test_virtual_overlap_and_rva_overflow_are_rejected(self):
        self.reject(put(self.data, self.at["sections"] + 40 + 12, 0x1000),
                    "SECTION_VIRTUAL_OVERLAP")
        self.reject(put(self.data, self.at["sections"] + 40 + 12, 0xFFFFF000),
                    "SECTION_VIRTUAL_RANGE_INVALID")
        self.reject(put(self.data, self.at["sections"] + 40 + 8, 0xFFFFF000),
                    "SECTION_VIRTUAL_RANGE_INVALID")

    def test_each_declared_nonempty_directory_needs_a_full_mapping(self):
        export = self.at["optional"] + 112
        malformed = put(put(self.data, export, 0x70000000), export + 4, 0x20)
        self.reject(malformed, "DIRECTORY_RANGE_INVALID")
        malformed = put(put(self.data, export, 0xFFFFFFF0), export + 4, 0x40)
        self.reject(malformed, "DIRECTORY_RANGE_INVALID")
        self.reject(put(self.data, export, 0x1000), "DIRECTORY_PAIR_INVALID")

    def test_reserved_directories_and_embedded_certificates_are_refused(self):
        for index in (7, 15):
            with self.subTest(index=index):
                address = self.at["optional"] + 112 + index * 8
                self.reject(put(self.data, address, 0x1000),
                            "RESERVED_DIRECTORY_NONZERO")
        for relative in (0, 4):
            with self.subTest(certificate_relative=relative):
                self.reject(put(self.data, self.at["certificate"] + relative, 8),
                            "CERTIFICATE_TABLE_PRESENT")

    def test_checksum_is_not_silently_recalculated(self):
        self.reject(put(self.data, self.at["optional"] + 64, 1),
                    "CHECKSUM_NONZERO")

    def test_missing_or_partially_present_load_config_is_rejected(self):
        absent = put(put(self.data, self.at["directory"], 0),
                     self.at["directory"] + 4, 0)
        self.reject(absent, "LOAD_CONFIG_MISSING")
        self.reject(put(self.data, self.at["directory"] + 4, 0),
                    "DIRECTORY_PAIR_INVALID")
        self.reject(put(self.data, self.at["directory"] + 4, 19),
                    "LOAD_CONFIG_DIRECTORY_TOO_SHORT")

    def test_config_structure_size_must_cover_flags_and_fit_directory(self):
        for size in (0, 19, 0x141, 0xFFFFFFFF):
            with self.subTest(size=size):
                self.reject(put(self.data, self.at["config"], size),
                            "LOAD_CONFIG_STRUCTURE_SIZE_INVALID")

    def test_directory_cannot_use_raw_padding_outside_virtual_size(self):
        changed = put(self.data, self.at["sections"] + 40 + 8, 0x100)
        self.reject(changed, "DIRECTORY_NOT_FULLY_FILE_BACKED")

    def test_directory_cannot_use_zero_filled_virtual_tail(self):
        changed = put(self.data, self.at["sections"] + 40 + 16, 0x200)
        changed = put(changed, self.at["directory"], self.at["rva"] + 0x100)
        self.reject(changed, "DIRECTORY_NOT_FULLY_FILE_BACKED")

    def test_load_config_requires_read_only_initialized_rdata(self):
        section = self.at["sections"] + 40
        renamed = bytearray(self.data)
        renamed[section:section + 8] = b".other\0\0"
        self.reject(bytes(renamed), "LOAD_CONFIG_SECTION_INVALID")
        for flags in (0xC0000040, 0x60000040, 0x40000000,
                      0x40000080, 0x42000040):
            with self.subTest(flags=flags):
                self.reject(put(self.data, section + 36, flags),
                            "LOAD_CONFIG_SECTION_INVALID")

    def test_duplicate_rdata_name_is_ambiguous_even_without_overlap(self):
        changed = bytearray(self.data + bytes(0x200))
        struct.pack_into("<H", changed, self.at["pe"] + 6, 3)
        struct.pack_into("<I", changed, self.at["optional"] + 56, 0x4000)
        struct.pack_into("<8sIIIIIIHHI", changed, self.at["sections"] + 80,
                         b".rdata\0\0", 0x100, 0x3000, 0x200, 0x800,
                         0, 0, 0, 0, 0x40000040)
        self.reject(bytes(changed), "LOAD_CONFIG_SECTION_AMBIGUOUS")

    def test_flag_baselines_are_explicit_and_cannot_be_merged(self):
        self.reject(put(self.data, self.at["config"] + 12, 1),
                    "GLOBAL_FLAGS_CLEAR_NONZERO")
        self.reject(put(self.data, self.at["field"], 2),
                    "GLOBAL_FLAGS_SET_UNEXPECTED")
        self.reject(put(self.data, self.at["field"], 4),
                    "GLOBAL_FLAGS_SET_UNEXPECTED")
        with self.assertRaises(pe.PeValidationError) as caught:
            self.inspect(expected=2)
        self.assertEqual(caught.exception.reason, "GLOBAL_FLAGS_SET_UNEXPECTED")

    def test_patch_field_cannot_alias_another_declared_directory(self):
        field_rva = self.at["rva"] + self.at["field"] - self.at["raw"]
        entry = self.at["optional"] + 112
        for start, size in ((field_rva, 4), (field_rva - 8, 12),
                            (field_rva - 1, 2), (field_rva + 3, 1)):
            with self.subTest(start=start, size=size):
                changed = put(put(self.data, entry, start), entry + 4, size)
                self.reject(changed, "PATCH_FIELD_DIRECTORY_ALIAS")
        # Disjoint adjacent ranges do not alias the modified field. This is
        # deliberately not a blanket prohibition on overlapping PE directories.
        for start, size in ((field_rva - 4, 4), (field_rva + 4, 4)):
            self.inspect(put(put(self.data, entry, start), entry + 4, size))

    def test_valid_single_dword_pair_reparses_and_preserves_all_other_bytes(self):
        child = patched(self.data, self.at["field"])
        original = self.data
        result = pe.verify_loader_snaps_pair(
            self.data, child, max_image_bytes=LIMIT)
        self.assertEqual(self.data, original)
        self.assertEqual(result["field_bytes"], 4)
        self.assertEqual(result["global_flags_set_file_offset"], self.at["field"])
        self.assertEqual(result["before_hex"], "00000000")
        self.assertEqual(result["after_hex"], "02000000")
        self.assertTrue(result["outside_field_equal"])
        self.assertEqual(result["parent_sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(result["child_sha256"], hashlib.sha256(child).hexdigest())
        self.assertEqual(result["child_pe"]["load_config"]["global_flags_set"], 2)

    def test_pair_refuses_adjacent_and_distant_unrelated_mutation(self):
        child = patched(self.data, self.at["field"])
        for offset in (self.at["field"] + 4, self.at["config"] + 88,
                       self.at["headers"] + 47, len(child) - 1):
            with self.subTest(offset=offset):
                changed = bytearray(child)
                changed[offset] ^= 0x80
                with self.assertRaises(pe.PeValidationError) as caught:
                    pe.verify_loader_snaps_pair(
                        self.data, bytes(changed), max_image_bytes=LIMIT)
                self.assertEqual(caught.exception.reason, "PAIR_OUTSIDE_FIELD_CHANGED")

    def test_pair_refuses_size_change_and_wrong_patch(self):
        child = patched(self.data, self.at["field"])
        for changed in (child + b"\0", child[:-1]):
            with self.subTest(length=len(changed)):
                with self.assertRaises(pe.PeValidationError) as caught:
                    pe.verify_loader_snaps_pair(
                        self.data, changed, max_image_bytes=LIMIT)
                self.assertEqual(caught.exception.reason, "PAIR_SIZE_CHANGED")
        with self.assertRaises(pe.PeValidationError) as caught:
            pe.verify_loader_snaps_pair(self.data, self.data, max_image_bytes=LIMIT)
        self.assertEqual(caught.exception.reason, "GLOBAL_FLAGS_SET_UNEXPECTED")


if __name__ == "__main__":
    unittest.main()
