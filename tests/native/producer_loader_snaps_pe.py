"""Pure bounded PE checks for the attempt-010 child-image DWORD change.

This accepts a narrow AMD64 PE32+ executable profile, not every Windows-loadable
image. It has no file operations and never creates or modifies an executable.
Every call derives the layout from its own immutable input bytes. Physical
identity, held handles, copy/write/flush/readback and actual image observation
belong to the caller, and cannot be inferred from this byte-level result.

Unsupported forms include sub-page section alignment, COFF symbol/relocation
tables, more than 16 directories, nonempty zero-sized directories, directories
in headers or section padding, ambiguous sections, checksummed/certified files,
and load configuration outside one read-only initialized .rdata section.

Format: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
GlobalFlagsSet is the documented DWORD at load-configuration relative offset 16.
"""

import hashlib
import struct


MAX_IMAGE_BYTES = 8 * 1024 * 1024
_UINT32_MAX = 0xFFFFFFFF
_MAX_IMAGE_RVA = 0x80000000
_PATCH_BYTES = b"\x02\x00\x00\x00"


class PeValidationError(ValueError):
    """A stable refusal reason; it carries no execution authority."""

    def __init__(self, reason):
        self.reason = reason
        super().__init__(reason)


def _require(condition, reason):
    if not condition:
        raise PeValidationError(reason)


def _end(start, size, limit, reason):
    # Subtraction avoids treating a PE DWORD addition as wrapping arithmetic.
    _require(0 <= start <= min(limit, _UINT32_MAX)
             and 0 <= size <= _UINT32_MAX
             and size <= limit - start
             and size <= _UINT32_MAX - start, reason)
    return start + size


def _u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _no_overlap(ranges, reason):
    ordered = sorted(ranges)
    for previous, current in zip(ordered, ordered[1:]):
        _require(previous[1] <= current[0], reason)


def inspect_loader_snaps_image(data: bytes, *, expected_flags_set: int,
                              max_image_bytes: int) -> dict:
    """Validate an unchanged parent (0) or final child (2), deriving its offset.

    The caller may choose a smaller image limit, but cannot enlarge the fixed
    8 MiB ceiling. Only immutable bytes and exact integer expectations/bounds
    are accepted; bool is not an integer for this contract.
    """
    _require(type(data) is bytes, "INPUT_BYTES_REQUIRED")
    _require(type(expected_flags_set) is int and expected_flags_set in (0, 2),
             "FLAGS_EXPECTATION_INVALID")
    _require(type(max_image_bytes) is int and 0 < max_image_bytes <= MAX_IMAGE_BYTES,
             "IMAGE_LIMIT_INVALID")
    length = len(data)
    _require(0 < length <= max_image_bytes, "IMAGE_SIZE_OUT_OF_RANGE")
    _require(length >= 64, "DOS_HEADER_TRUNCATED")
    _require(data[:2] == b"MZ", "DOS_MAGIC_INVALID")
    pe = _u32(data, 0x3C)
    _require(pe >= 64, "PE_HEADER_RANGE_INVALID")
    _end(pe, 24, length, "PE_HEADER_RANGE_INVALID")
    _require(data[pe:pe + 4] == b"PE\0\0", "PE_SIGNATURE_INVALID")
    _require(_u16(data, pe + 4) == 0x8664, "MACHINE_NOT_AMD64")
    section_count = _u16(data, pe + 6)
    _require(1 <= section_count <= 96, "SECTION_COUNT_INVALID")
    _require(_u32(data, pe + 12) == 0 and _u32(data, pe + 16) == 0,
             "COFF_SYMBOL_TABLE_UNSUPPORTED")
    characteristics = _u16(data, pe + 22)
    _require(characteristics & 0x0002 != 0
             and characteristics & (0x2000 | 0x1000 | 0x0100 | 0x8080) == 0,
             "COFF_IMAGE_PROFILE_INVALID")

    optional_size = _u16(data, pe + 20)
    optional = pe + 24
    _require(112 <= optional_size <= 240, "OPTIONAL_HEADER_RANGE_INVALID")
    section_table = _end(optional, optional_size, length,
                         "OPTIONAL_HEADER_RANGE_INVALID")
    _require(_u16(data, optional) == 0x20B, "OPTIONAL_MAGIC_NOT_PE32_PLUS")
    directory_count = _u32(data, optional + 108)
    _require(11 <= directory_count <= 16, "DIRECTORY_COUNT_UNSUPPORTED")
    _require(112 + directory_count * 8 <= optional_size, "DIRECTORY_TABLE_TRUNCATED")
    section_table_end = _end(section_table, section_count * 40, length,
                             "SECTION_TABLE_TRUNCATED")

    section_alignment = _u32(data, optional + 32)
    file_alignment = _u32(data, optional + 36)
    _require(512 <= file_alignment <= 65536
             and file_alignment & (file_alignment - 1) == 0
             and section_alignment >= 4096
             and section_alignment & (section_alignment - 1) == 0
             and section_alignment >= file_alignment, "ALIGNMENT_UNSUPPORTED")
    image_size = _u32(data, optional + 56)
    headers_size = _u32(data, optional + 60)
    _require(section_table_end <= headers_size <= length
             and headers_size % file_alignment == 0, "HEADERS_RANGE_INVALID")
    _require(headers_size <= image_size <= _MAX_IMAGE_RVA
             and image_size % section_alignment == 0, "IMAGE_RANGE_INVALID")
    _require(_u32(data, optional + 52) == 0 and _u32(data, optional + 104) == 0,
             "OPTIONAL_RESERVED_FIELD_NONZERO")
    checksum = _u32(data, optional + 64)
    _require(checksum == 0, "CHECKSUM_NONZERO")

    sections = []
    raw_ranges = []
    virtual_ranges = []
    virtual_headers_end = (headers_size + section_alignment - 1) // section_alignment
    virtual_headers_end *= section_alignment
    for index in range(section_count):
        offset = section_table + index * 40
        name_bytes = data[offset:offset + 8]
        name_part, _, name_padding = name_bytes.partition(b"\0")
        _require(name_part and all(0x21 <= value <= 0x7E for value in name_part)
                 and not name_part.startswith(b"/") and not any(name_padding),
                 "SECTION_NAME_UNSUPPORTED")
        virtual_size = _u32(data, offset + 8)
        virtual_address = _u32(data, offset + 12)
        raw_size = _u32(data, offset + 16)
        raw_offset = _u32(data, offset + 20)
        section_flags = _u32(data, offset + 36)
        _require(_u32(data, offset + 24) == 0 and _u32(data, offset + 28) == 0
                 and _u16(data, offset + 32) == 0 and _u16(data, offset + 34) == 0,
                 "SECTION_COFF_TABLE_UNSUPPORTED")
        if raw_size:
            _require(raw_offset >= headers_size and raw_offset % file_alignment == 0
                     and raw_size % file_alignment == 0, "SECTION_RAW_RANGE_INVALID")
            raw_end = _end(raw_offset, raw_size, length, "SECTION_RAW_RANGE_INVALID")
            raw_ranges.append((raw_offset, raw_end))
        else:
            _require(raw_offset == 0, "SECTION_RAW_RANGE_INVALID")
        virtual_span = max(virtual_size, raw_size)
        _require(virtual_span > 0 and virtual_address >= virtual_headers_end
                 and virtual_address % section_alignment == 0,
                 "SECTION_VIRTUAL_RANGE_INVALID")
        virtual_end = _end(virtual_address, virtual_span, image_size,
                           "SECTION_VIRTUAL_RANGE_INVALID")
        virtual_ranges.append((virtual_address, virtual_end))
        sections.append({
            "index": index, "name": name_part.decode("ascii"),
            "header_file_offset": offset, "virtual_address": virtual_address,
            "virtual_size": virtual_size, "mapped_span": virtual_span,
            "raw_file_offset": raw_offset, "raw_size": raw_size,
            "characteristics": section_flags,
        })
    _no_overlap(raw_ranges, "SECTION_RAW_OVERLAP")
    _no_overlap(virtual_ranges, "SECTION_VIRTUAL_OVERLAP")

    directories = []
    for index in range(directory_count):
        offset = optional + 112 + index * 8
        rva, size = struct.unpack_from("<II", data, offset)
        entry = {"index": index, "entry_file_offset": offset, "rva": rva, "bytes": size}
        if index == 4:
            _require(rva == 0 and size == 0, "CERTIFICATE_TABLE_PRESENT")
        if index in (7, 15):
            _require(rva == 0 and size == 0, "RESERVED_DIRECTORY_NONZERO")
        if rva == 0 and size == 0:
            directories.append(entry)
            continue
        _require(rva != 0 and size != 0, "DIRECTORY_PAIR_INVALID")
        rva_end = _end(rva, size, image_size, "DIRECTORY_RANGE_INVALID")
        candidates = [section for section in sections
                      if section["virtual_address"] <= rva
                      and rva_end <= section["virtual_address"] + section["mapped_span"]]
        _require(len(candidates) == 1, "DIRECTORY_NOT_FULLY_FILE_BACKED")
        section = candidates[0]
        relative = rva - section["virtual_address"]
        _require(relative + size <= section["virtual_size"]
                 and relative + size <= section["raw_size"],
                 "DIRECTORY_NOT_FULLY_FILE_BACKED")
        mapped_file_offset = section["raw_file_offset"] + relative
        _end(mapped_file_offset, size, length, "DIRECTORY_NOT_FULLY_FILE_BACKED")
        entry.update({"section_index": section["index"], "section_name": section["name"],
                      "file_offset": mapped_file_offset})
        directories.append(entry)

    load = directories[10]
    _require(load["rva"] != 0 and load["bytes"] != 0, "LOAD_CONFIG_MISSING")
    _require(load["bytes"] >= 20, "LOAD_CONFIG_DIRECTORY_TOO_SHORT")
    named_rdata = [section for section in sections if section["name"] == ".rdata"]
    _require(len(named_rdata) <= 1, "LOAD_CONFIG_SECTION_AMBIGUOUS")
    section = sections[load["section_index"]]
    flags = section["characteristics"]
    _require(section["name"] == ".rdata" and flags & 0x40000040 == 0x40000040
             and flags & (0x80000000 | 0x20000000 | 0x02000000 | 0x80 | 0x20) == 0,
             "LOAD_CONFIG_SECTION_INVALID")
    config_offset = load["file_offset"]
    structure_size = _u32(data, config_offset)
    _require(20 <= structure_size <= load["bytes"], "LOAD_CONFIG_STRUCTURE_SIZE_INVALID")
    clear = _u32(data, config_offset + 12)
    set_flags = _u32(data, config_offset + 16)
    _require(clear == 0, "GLOBAL_FLAGS_CLEAR_NONZERO")
    _require(set_flags == expected_flags_set, "GLOBAL_FLAGS_SET_UNEXPECTED")
    field_offset = config_offset + 16
    # The four bytes may not simultaneously belong to another declared data
    # directory. Other directory ranges may legitimately overlap one another;
    # this check is only about the exact bytes that this diagnostic will patch.
    for entry in directories:
        if entry["index"] != 10 and entry["bytes"]:
            first = entry["file_offset"]
            _require(not (first < field_offset + 4 and field_offset < first + entry["bytes"]),
                     "PATCH_FIELD_DIRECTORY_ALIAS")
    return {
        "bytes": length, "sha256": _sha(data),
        "header": {
            "pe_file_offset": pe, "machine": 0x8664,
            "number_of_sections": section_count, "coff_characteristics": characteristics,
            "optional_header_file_offset": optional, "optional_header_size": optional_size,
            "optional_header_magic": 0x20B, "number_of_rva_and_sizes": directory_count,
            "section_table_file_offset": section_table,
            "section_alignment": section_alignment, "file_alignment": file_alignment,
            "size_of_headers": headers_size, "size_of_image": image_size,
            "checksum_file_offset": optional + 64, "checksum": checksum,
            "certificate_directory_file_offset": optional + 112 + 4 * 8,
            "certificate_file_offset": 0, "certificate_bytes": 0,
        },
        "sections": sections, "data_directories": directories,
        "load_config": {
            "rva": load["rva"], "directory_bytes": load["bytes"],
            "structure_bytes": structure_size, "file_offset": config_offset,
            "directory_sha256": _sha(data[config_offset:config_offset + load["bytes"]]),
            "structure_sha256": _sha(data[config_offset:config_offset + structure_size]),
            "section_index": section["index"], "section_name": section["name"],
            "global_flags_clear": clear, "global_flags_set": set_flags,
            "global_flags_clear_file_offset": config_offset + 12,
            "global_flags_set_file_offset": field_offset,
            "global_flags_set_raw_hex": data[field_offset:field_offset + 4].hex(),
            "patch_bytes_hex": _PATCH_BYTES.hex(),
        },
    }


def verify_loader_snaps_pair(parent: bytes, child: bytes, *, max_image_bytes: int) -> dict:
    """Reparse both images and prove the exact isolated DWORD byte contract."""
    parent_pe = inspect_loader_snaps_image(
        parent, expected_flags_set=0, max_image_bytes=max_image_bytes)
    _require(type(child) is bytes, "INPUT_BYTES_REQUIRED")
    _require(len(parent) == len(child), "PAIR_SIZE_CHANGED")
    child_pe = inspect_loader_snaps_image(
        child, expected_flags_set=2, max_image_bytes=max_image_bytes)
    field = parent_pe["load_config"]["global_flags_set_file_offset"]
    _require(child_pe["load_config"]["global_flags_set_file_offset"] == field,
             "PAIR_FIELD_OFFSET_CHANGED")
    _require(parent[field:field + 4] == bytes(4) and child[field:field + 4] == _PATCH_BYTES,
             "PAIR_FIELD_BYTES_UNEXPECTED")
    _require(memoryview(parent)[:field] == memoryview(child)[:field]
             and memoryview(parent)[field + 4:] == memoryview(child)[field + 4:],
             "PAIR_OUTSIDE_FIELD_CHANGED")
    return {
        "bytes": len(parent), "parent_sha256": parent_pe["sha256"],
        "child_sha256": child_pe["sha256"], "global_flags_set_file_offset": field,
        "field_bytes": 4, "before_hex": parent[field:field + 4].hex(),
        "after_hex": child[field:field + 4].hex(), "outside_field_equal": True,
        "parent_pe": parent_pe, "child_pe": child_pe,
    }
