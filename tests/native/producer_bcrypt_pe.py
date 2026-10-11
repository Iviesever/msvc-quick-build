"""Read-only, bounded static PE index for the diagnostic-011 bcrypt snapshot.

The caller supplies one immutable, exact event-object byte snapshot and the
future run's own (loader init address - load base) RVA. This helper neither
opens files nor executes code; its hash does not establish object identity.
It accepts a deliberately restricted AMD64 PE32+ DLL profile with ordinary
page-aligned sections, v1/v2 unwind envelopes and RVA-form delay descriptors.
Every referenced range must lie wholly in one file-backed section, excluding
raw padding and zero-filled tails. All declared directory ranges are checked;
only imports, delay imports, IAT, exception/unwind and certificate envelopes
are interpreted. Directory ranges may contain one another without creating
an ambiguous section mapping. Certificate addresses are FILE offsets.

Nonzero checksums and normal certificate tables are retained, not verified.
Language-specific unwind data and v2 epilog meaning remain explicit unknowns.
This is not a loader, full unwind interpreter, disassembler, Authenticode
verifier or evidence that any static branch/API actually executed.

Contracts: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
V2 opcode layout: https://llvm.org/doxygen/Win64EH_8h_source.html
"""

import hashlib
import struct


MAX_IMAGE_BYTES = 1024 * 1024
MAX_SECTIONS = 96
MAX_DIRECTORIES = 16
MAX_DIRECTORY_BYTES = 512 * 1024
MAX_IMPORT_DESCRIPTORS = 128
MAX_DELAY_DESCRIPTORS = 128
MAX_SYMBOLS_PER_DLL = 1024
MAX_IMPORT_SYMBOLS = 4096
MAX_IAT_SLOTS = 8192
MAX_STRING_BYTES = 512  # Includes the required NUL byte.
MAX_RUNTIME_FUNCTIONS = 16384
MAX_UNWIND_RECORDS = 16384
MAX_UNWIND_CODE_SLOTS = 65536  # Combined across unique unwind envelopes.
MAX_UNWIND_CHAIN_DEPTH = 32
MAX_CERTIFICATES = 64
MAX_CERTIFICATE_BYTES = 256 * 1024
MAX_WORK_UNITS = 4 * 1024 * 1024
_MAX_RVA = 0x80000000
_ORDINAL_FLAG = 1 << 63
_DIRECTORY_NAMES = (
    "EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "CERTIFICATE", "BASE_RELOCATION",
    "DEBUG", "ARCHITECTURE", "GLOBAL_POINTER", "TLS", "LOAD_CONFIG", "BOUND_IMPORT",
    "IAT", "DELAY_IMPORT", "CLR", "RESERVED",
)


class PeValidationError(ValueError):
    """Stable refusal reason, without any native execution authority."""

    def __init__(self, reason):
        self.reason = reason
        super().__init__(reason)


def _require(condition, reason):
    if not condition:
        raise PeValidationError(reason)


def _end(start, size, limit, reason):
    _require(0 <= start <= limit and 0 <= size <= limit - start
             and start + size <= 0xFFFFFFFF, reason)
    return start + size


def _disjoint(ranges, reason):
    ordered = sorted(ranges)
    for previous, current in zip(ordered, ordered[1:]):
        _require(previous[1] <= current[0], reason)


class _Image:
    def __init__(self, data):
        self.data = data
        self.work = 0
        self.sections = []
        self.directories = []
        self.unknowns = {
            "ACTUAL_EXECUTION_AND_FAILURE_CAUSE_UNKNOWN", "CHECKSUM_NOT_VERIFIED",
            "UNWIND_STACK_EFFECTS_NOT_VERIFIED",
        }
        self.iat_index = []
        self.iat_slots = set()
        self.metadata_ranges = []
        self.unwind = {}
        self.unwind_slots = 0
        self.spend(len(data))  # One full raw-byte SHA-256 pass, not a PE digest.

    def spend(self, units):
        _require(units <= MAX_WORK_UNITS - self.work, "PARSE_WORK_LIMIT")
        self.work += units

    def read(self, fmt, offset):
        size = struct.calcsize(fmt)
        _end(offset, size, len(self.data), "FILE_READ_OUT_OF_RANGE")
        self.spend(size)
        return struct.unpack_from(fmt, self.data, offset)

    def map(self, rva, size, reason, *, executable=False):
        _require(size > 0, reason)
        last = _end(rva, size, self.header["size_of_image"], reason)
        self.spend(len(self.sections))
        candidates = [section for section in self.sections
                      if section["virtual_address"] <= rva
                      and last <= section["virtual_address"] + section["file_backed_size"]]
        _require(len(candidates) == 1, reason)
        section = candidates[0]
        _require(not executable or section["characteristics"] & 0x20000000, reason)
        offset = section["raw_file_offset"] + rva - section["virtual_address"]
        _end(offset, size, len(self.data), reason)
        return offset, section

    def string(self, rva):
        offset, section = self.map(rva, 1, "STRING_NOT_FILE_BACKED")
        available = section["raw_file_offset"] + section["file_backed_size"] - offset
        size = min(available, MAX_STRING_BYTES)
        self.spend(size)
        raw = self.data[offset:offset + size]
        end = raw.find(b"\0")
        _require(end > 0, "STRING_UNTERMINATED_OR_LIMIT")
        _require(all(0x20 <= byte <= 0x7E for byte in raw[:end]), "STRING_NOT_ASCII")
        return raw[:end].decode("ascii"), offset

    def headers(self):
        length = len(self.data)
        _require(length >= 64, "DOS_HEADER_TRUNCATED")
        _require(self.data[:2] == b"MZ", "DOS_MAGIC_INVALID")
        pe, = self.read("<I", 0x3C)
        _require(pe >= 64 and pe % 4 == 0, "PE_HEADER_RANGE_INVALID")
        _end(pe, 24, length, "PE_HEADER_RANGE_INVALID")
        _require(self.data[pe:pe + 4] == b"PE\0\0", "PE_SIGNATURE_INVALID")
        machine, count, stamp, symbols, symbol_count, optional_size, flags = self.read("<HHIIIHH", pe + 4)
        _require(machine == 0x8664, "MACHINE_NOT_AMD64")
        _require(1 <= count <= MAX_SECTIONS, "SECTION_COUNT_INVALID")
        _require(symbols == 0 and symbol_count == 0, "COFF_SYMBOL_TABLE_UNSUPPORTED")
        _require(flags & 0x2002 == 0x2002 and not flags & 0x8180, "DLL_IMAGE_PROFILE_INVALID")
        optional = pe + 24
        _require(112 <= optional_size <= 240, "OPTIONAL_HEADER_RANGE_INVALID")
        section_table = _end(optional, optional_size, length, "OPTIONAL_HEADER_RANGE_INVALID")
        magic, = self.read("<H", optional)
        _require(magic == 0x20B, "OPTIONAL_MAGIC_NOT_PE32_PLUS")
        directories, = self.read("<I", optional + 108)
        _require(directories <= MAX_DIRECTORIES, "DIRECTORY_COUNT_UNSUPPORTED")
        _require(112 + directories * 8 <= optional_size, "DIRECTORY_TABLE_TRUNCATED")
        table_end = _end(section_table, count * 40, length, "SECTION_TABLE_TRUNCATED")
        entry, = self.read("<I", optional + 16)
        image_base, = self.read("<Q", optional + 24)
        section_alignment, file_alignment = self.read("<II", optional + 32)
        image_size, headers_size, checksum = self.read("<III", optional + 56)
        _require(512 <= file_alignment <= 65536
                 and file_alignment & (file_alignment - 1) == 0
                 and section_alignment >= 4096
                 and section_alignment & (section_alignment - 1) == 0
                 and section_alignment >= file_alignment, "ALIGNMENT_UNSUPPORTED")
        _require(table_end <= headers_size <= length and headers_size % file_alignment == 0,
                 "HEADERS_RANGE_INVALID")
        _require(headers_size <= image_size <= _MAX_RVA and image_size % section_alignment == 0,
                 "IMAGE_RANGE_INVALID")
        _require(image_base % 65536 == 0 and image_base + image_size <= (1 << 64),
                 "IMAGE_BASE_INVALID")
        _require(self.read("<I", optional + 52)[0] == 0
                 and self.read("<I", optional + 104)[0] == 0,
                 "OPTIONAL_RESERVED_FIELD_NONZERO")
        self.header = {
            "pe_file_offset": pe, "machine": machine, "timestamp": stamp,
            "number_of_sections": count, "coff_characteristics": flags,
            "optional_header_file_offset": optional, "optional_header_size": optional_size,
            "optional_header_magic": magic, "number_of_rva_and_sizes": directories,
            "section_table_file_offset": section_table, "image_base": image_base,
            "section_alignment": section_alignment, "file_alignment": file_alignment,
            "size_of_headers": headers_size, "size_of_image": image_size,
            "checksum": checksum, "checksum_file_offset": optional + 64,
            "address_of_entrypoint": entry,
        }
        raw_ranges, virtual_ranges = [], []
        virtual_headers_end = (headers_size + section_alignment - 1) & -section_alignment
        for index in range(count):
            offset = section_table + index * 40
            name, virtual_size, rva, raw_size, raw_offset, reloc, lines, nreloc, nlines, flags = self.read(
                "<8sIIIIIIHHI", offset)
            name, _, padding = name.partition(b"\0")
            _require(name and not name.startswith(b"/") and not any(padding)
                     and all(0x21 <= byte <= 0x7E for byte in name), "SECTION_NAME_UNSUPPORTED")
            _require(reloc == lines == nreloc == nlines == 0, "SECTION_COFF_TABLE_UNSUPPORTED")
            if raw_size:
                _require(raw_offset >= headers_size and raw_offset % file_alignment == 0
                         and raw_size % file_alignment == 0, "SECTION_RAW_RANGE_INVALID")
                raw_end = _end(raw_offset, raw_size, length, "SECTION_RAW_RANGE_INVALID")
                raw_ranges.append((raw_offset, raw_end))
            else:
                _require(raw_offset == 0, "SECTION_RAW_RANGE_INVALID")
            span = max(virtual_size, raw_size)
            _require(virtual_size > 0 and rva >= virtual_headers_end
                     and rva % section_alignment == 0, "SECTION_VIRTUAL_RANGE_INVALID")
            virtual_end = _end(rva, span, image_size, "SECTION_VIRTUAL_RANGE_INVALID")
            virtual_ranges.append((rva, virtual_end))
            self.sections.append({
                "index": index, "name": name.decode("ascii"), "header_file_offset": offset,
                "virtual_address": rva, "virtual_size": virtual_size, "mapped_span": span,
                "raw_file_offset": raw_offset, "raw_size": raw_size,
                "file_backed_size": min(virtual_size, raw_size), "characteristics": flags,
            })
        _disjoint(raw_ranges, "SECTION_RAW_OVERLAP")
        _disjoint(virtual_ranges, "SECTION_VIRTUAL_OVERLAP")
        for index in range(MAX_DIRECTORIES):
            offset = optional + 112 + index * 8
            address, size = self.read("<II", offset) if index < directories else (0, 0)
            entry = {"index": index, "name": _DIRECTORY_NAMES[index],
                     "declared": index < directories, "entry_file_offset": offset if index < directories else None,
                     "address": address, "rva": None if index == 4 else address,
                     "address_kind": "FILE_OFFSET" if index == 4 else "RVA", "bytes": size,
                     "file_offset": None, "section_index": None, "section_name": None}
            if index in (7, 15):
                _require(address == size == 0, "RESERVED_DIRECTORY_NONZERO")
            if index == 8:
                _require(address == size == 0, "GLOBAL_POINTER_DIRECTORY_UNSUPPORTED")
            if address or size:
                _require(address != 0 and size != 0, "DIRECTORY_PAIR_INVALID")
                _require(size <= MAX_DIRECTORY_BYTES, "DIRECTORY_BYTES_LIMIT")
                if index == 4:
                    _require(address % 8 == 0 and size % 8 == 0, "CERTIFICATE_RANGE_INVALID")
                    end = _end(address, size, length, "CERTIFICATE_RANGE_INVALID")
                    _require(address >= headers_size
                             and all(end <= start or finish <= address for start, finish in raw_ranges),
                             "CERTIFICATE_IMAGE_ALIAS")
                    entry["file_offset"] = address
                else:
                    file_offset, section = self.map(address, size, "DIRECTORY_NOT_FILE_BACKED")
                    entry.update(file_offset=file_offset, section_index=section["index"],
                                 section_name=section["name"])
                if index not in (1, 3, 4, 12, 13):
                    self.unknowns.add("OTHER_DIRECTORY_CONTENTS_NOT_INTERPRETED")
            self.directories.append(entry)
        iat = self.directories[12]
        if iat["bytes"]:
            _require(iat["rva"] % 8 == 0 and iat["bytes"] % 8 == 0,
                     "IAT_DIRECTORY_ALIGNMENT_INVALID")
            _require(iat["bytes"] // 8 <= MAX_IAT_SLOTS, "IAT_DIRECTORY_SLOT_LIMIT")

    def certificates(self):
        directory = self.directories[4]
        size = directory["bytes"]
        if not size:
            return []
        _require(size <= MAX_CERTIFICATE_BYTES, "CERTIFICATE_BYTES_LIMIT")
        self.unknowns.add("CERTIFICATE_TRUST_NOT_VERIFIED")
        cursor, end = directory["file_offset"], directory["file_offset"] + size
        result = []
        while cursor < end:
            _require(len(result) < MAX_CERTIFICATES, "CERTIFICATE_COUNT_LIMIT")
            _require(end - cursor >= 8, "CERTIFICATE_RECORD_INVALID")
            length, revision, kind = self.read("<IHH", cursor)
            padded = (length + 7) & ~7
            _require(8 <= length <= end - cursor and padded <= end - cursor,
                     "CERTIFICATE_RECORD_INVALID")
            self.spend(padded)
            _require(not any(self.data[cursor + length:cursor + padded]), "CERTIFICATE_PADDING_NONZERO")
            result.append({"file_offset": cursor, "bytes": length, "padded_bytes": padded,
                           "revision": revision, "certificate_type": kind})
            cursor += padded
        return result

    def thunks(self, lookup, iat, dll, descriptor_index, *, delay, bound_without_lookup, bound):
        source = lookup or iat
        _require(source and iat and source % 8 == 0 and iat % 8 == 0,
                 "IMPORT_TABLE_ALIGNMENT_INVALID")
        if bound_without_lookup:
            self.unknowns.add("BOUND_IAT_WITHOUT_LOOKUP_NAMES_UNKNOWN")
        symbols = []
        for index in range(MAX_SYMBOLS_PER_DLL + 1):
            lookup_rva, iat_rva = source + index * 8, iat + index * 8
            lookup_offset, _ = self.map(lookup_rva, 8, "IMPORT_TABLE_NOT_FILE_BACKED")
            iat_offset, _ = self.map(iat_rva, 8, "IMPORT_TABLE_NOT_FILE_BACKED")
            value, = self.read("<Q", lookup_offset)
            iat_value, = self.read("<Q", iat_offset)
            _require((value == 0) == (iat_value == 0), "IAT_LOOKUP_LENGTH_MISMATCH")
            _require(iat_rva not in self.iat_slots, "IAT_SLOT_ALIAS")
            self.iat_slots.add(iat_rva)  # Includes terminator slots.
            if value == 0:
                break
            _require(index < MAX_SYMBOLS_PER_DLL, "IMPORT_SYMBOLS_PER_DLL_LIMIT")
            _require(len(self.iat_index) < MAX_IMPORT_SYMBOLS, "IMPORT_SYMBOL_COUNT_LIMIT")
            name = hint = ordinal = name_offset = hint_rva = hint_offset = None
            if not bound_without_lookup:
                if value & _ORDINAL_FLAG:
                    _require(value & ~(_ORDINAL_FLAG | 0xFFFF) == 0, "IMPORT_ORDINAL_ENCODING_INVALID")
                    ordinal = value & 0xFFFF
                else:
                    _require(0 < value <= 0x7FFFFFFF, "IMPORT_NAME_RVA_INVALID")
                    hint_rva = value
                    hint_offset, _ = self.map(value, 2, "IMPORT_NAME_NOT_FILE_BACKED")
                    hint, = self.read("<H", hint_offset)
                    name, name_offset = self.string(value + 2)
                    self.map(value, 2 + len(name) + 1, "IMPORT_NAME_NOT_FILE_BACKED")
            if not delay and not bound:
                _require(iat_value == value, "IAT_LOOKUP_CONTENT_MISMATCH")
            symbol = {
                "kind": "delay" if delay else "import", "dll": dll,
                "descriptor_index": descriptor_index, "index": index,
                "name": name, "hint": hint, "ordinal": ordinal,
                "hint_name_rva": hint_rva, "hint_name_file_offset": hint_offset,
                "name_file_offset": name_offset, "lookup_rva": lookup_rva,
                "lookup_file_offset": lookup_offset, "lookup_raw_value": value,
                "iat_rva": iat_rva, "iat_file_offset": iat_offset, "iat_raw_value": iat_value,
                "resolved_runtime_target": None,
            }
            symbols.append(symbol)
            self.iat_index.append(symbol)
        size = (len(symbols) + 1) * 8
        for rva in {source, iat}:
            offset, _ = self.map(rva, size, "IMPORT_TABLE_NOT_FILE_BACKED")
            # OFT=0 (or an identical same-descriptor ILT/IAT) is one IAT claim.
            # A later descriptor's LOOKUP cannot borrow that writable claim.
            role = "IAT" if rva == iat else "LOOKUP"
            self.metadata_ranges.append((offset, offset + size, role,
                                         ("DELAY" if delay else "IMPORT", descriptor_index)))
        directory = self.directories[12]
        if not delay and directory["bytes"]:
            _require(directory["rva"] <= iat and iat + size <= directory["rva"] + directory["bytes"],
                     "IMPORT_IAT_OUTSIDE_DIRECTORY")
        return symbols

    def imports(self, *, delay=False):
        directory = self.directories[13 if delay else 1]
        if not directory["bytes"]:
            return []
        width = 32 if delay else 20
        maximum = MAX_DELAY_DESCRIPTORS if delay else MAX_IMPORT_DESCRIPTORS
        prefix = "DELAY" if delay else "IMPORT"
        result = []
        for index in range(maximum + 1):
            _require((index + 1) * width <= directory["bytes"], prefix + "_DESCRIPTOR_UNTERMINATED")
            offset = directory["file_offset"] + index * width
            values = self.read("<8I" if delay else "<5I", offset)
            owner = (prefix, index)
            self.metadata_ranges.append((offset, offset + width, "DESCRIPTOR", owner))
            if not any(values):
                return result
            _require(index < maximum, prefix + "_DESCRIPTOR_LIMIT")
            if delay:
                attributes, name_rva, module_rva, iat, lookup, bound_iat, unload_iat, stamp = values
                _require(attributes & ~1 == 0, "DELAY_ATTRIBUTES_INVALID")
                _require(attributes == 1, "DELAY_VA_FORM_UNSUPPORTED")
                _require(name_rva and module_rva and iat and lookup, "DELAY_DESCRIPTOR_INVALID")
                module_offset, _ = self.map(module_rva, 8, "DELAY_MODULE_HANDLE_NOT_FILE_BACKED")
                self.metadata_ranges.append((module_offset, module_offset + 8, "MODULE", owner))
            else:
                lookup, stamp, forwarder, name_rva, iat = values
                _require(name_rva and iat, "IMPORT_DESCRIPTOR_INVALID")
            dll, name_offset = self.string(name_rva)
            symbols = self.thunks(lookup, iat, dll, index, delay=delay,
                                  bound_without_lookup=bool(not delay and not lookup and stamp),
                                  bound=bool(stamp))
            descriptor = {"index": index, "descriptor_rva": directory["rva"] + index * width,
                          "descriptor_file_offset": offset, "dll": dll, "name_rva": name_rva,
                          "name_file_offset": name_offset, "lookup_table_rva": lookup,
                          "iat_rva": iat, "timestamp": stamp, "symbols": symbols}
            if delay:
                size = (len(symbols) + 1) * 8
                for address, kind in ((bound_iat, "BOUND"), (unload_iat, "UNLOAD")):
                    if not address:
                        continue
                    _require(address % 8 == 0, "DELAY_OPTIONAL_TABLE_ALIGNMENT_INVALID")
                    table_offset, _ = self.map(address, size, "DELAY_OPTIONAL_TABLE_NOT_FILE_BACKED")
                    self.metadata_ranges.append((table_offset, table_offset + size, kind, owner))
                    _require(self.read("<Q", table_offset + size - 8)[0] == 0,
                             "DELAY_OPTIONAL_TABLE_UNTERMINATED")
                    self.spend(size)
                    if kind == "UNLOAD":
                        iat_offset, _ = self.map(iat, size, "IMPORT_TABLE_NOT_FILE_BACKED")
                        _require(self.data[table_offset:table_offset + size] == self.data[iat_offset:iat_offset + size],
                                 "DELAY_UNLOAD_IAT_MISMATCH")
                descriptor.update(attributes=attributes, module_handle_rva=module_rva,
                                  module_handle_file_offset=module_offset,
                                  bound_iat_rva=bound_iat, unload_iat_rva=unload_iat)
            else:
                descriptor["forwarder_chain"] = forwarder
            result.append(descriptor)
        raise PeValidationError(prefix + "_DESCRIPTOR_UNTERMINATED")

    def runtime_function(self, begin, end, unwind, offset):
        _require(0 < begin < end <= self.header["size_of_image"], "RUNTIME_FUNCTION_RANGE_INVALID")
        file_offset, section = self.map(begin, end - begin,
                                        "RUNTIME_FUNCTION_NOT_EXECUTABLE_FILE_BACKED", executable=True)
        _require(unwind > 0 and unwind % 4 == 0, "UNWIND_ALIGNMENT_INVALID")
        return {"begin_rva": begin, "end_rva": end,
                "begin_file_offset": file_offset, "end_file_offset": file_offset + end - begin,
                "section_index": section["index"], "section_name": section["name"],
                "unwind_info_rva": unwind, "table_entry_file_offset": offset}

    def unwind_info(self, rva, path=()):
        _require(rva not in path, "UNWIND_CHAIN_CYCLE")
        _require(len(path) < MAX_UNWIND_CHAIN_DEPTH, "UNWIND_CHAIN_DEPTH_LIMIT")
        if rva in self.unwind:
            result = self.unwind[rva]
            _require(len(path) + result["chain_depth"] <= MAX_UNWIND_CHAIN_DEPTH,
                     "UNWIND_CHAIN_DEPTH_LIMIT")
            return result
        _require(len(self.unwind) + len(path) < MAX_UNWIND_RECORDS, "UNWIND_RECORD_COUNT_LIMIT")
        _require(rva > 0 and rva % 4 == 0, "UNWIND_ALIGNMENT_INVALID")
        offset, _ = self.map(rva, 4, "UNWIND_NOT_FILE_BACKED")
        vf, prolog, count, frame = self.read("<4B", offset)
        version, flags = vf & 7, vf >> 3
        _require(version in (1, 2), "UNWIND_VERSION_UNSUPPORTED")
        _require(flags & ~7 == 0 and not (flags & 4 and flags & 3), "UNWIND_FLAGS_INVALID")
        _require(frame & 15 or frame == 0, "UNWIND_FRAME_INVALID")
        _require(count <= MAX_UNWIND_CODE_SLOTS - self.unwind_slots, "UNWIND_CODE_SLOT_LIMIT")
        self.unwind_slots += count
        body_size = 4 + ((count + 1) & ~1) * 2
        size = body_size + (12 if flags & 4 else 4 if flags & 3 else 0)
        self.map(rva, size, "UNWIND_NOT_FILE_BACKED")
        self.spend(size)
        slot, previous_offset = 0, prolog
        while slot < count:
            code_offset, operation = self.read("<BB", offset + 4 + slot * 2)
            opcode, info = operation & 15, operation >> 4
            _require(opcode in (0, 1, 2, 3, 4, 5, 8, 9, 10) or (opcode == 6 and version == 2),
                     "UNWIND_OPCODE_UNSUPPORTED")
            if opcode == 1:
                _require(info in (0, 1), "UNWIND_OPCODE_INVALID")
                width = 2 if info == 0 else 3
            else:
                width = {4: 2, 5: 3, 8: 2, 9: 3}.get(opcode, 1)
            _require(slot + width <= count, "UNWIND_OPCODE_TRUNCATED")
            if opcode == 6:
                self.unknowns.add("UNWIND_V2_EPILOG_SEMANTICS_NOT_INTERPRETED")
            else:
                _require(code_offset <= previous_offset, "UNWIND_CODE_ORDER_INVALID")
                previous_offset = code_offset
            if opcode == 3:
                _require(info == 0 and frame & 15 != 0, "UNWIND_OPCODE_INVALID")
            if opcode == 10:
                _require(info in (0, 1), "UNWIND_OPCODE_INVALID")
            slot += width
        result = {"rva": rva, "file_offset": offset, "bytes": size,
                  "version": version, "flags": flags, "prolog_size": prolog,
                  "code_slots": count, "frame_register": frame & 15, "frame_offset": frame >> 4,
                  "raw_hex": self.data[offset:offset + size].hex(),
                  "handler_rva": None, "handler_file_offset": None, "chain": None, "chain_depth": 1}
        if flags & 4:
            begin, end, chained_rva = self.read("<III", offset + body_size)
            chain = self.runtime_function(begin, end, chained_rva, offset + body_size)
            target = self.unwind_info(chained_rva, path + (rva,))
            _require(target["prolog_size"] <= end - begin, "UNWIND_PROLOG_RANGE_INVALID")
            _require(target["frame_register"] == result["frame_register"]
                     and target["frame_offset"] == result["frame_offset"], "UNWIND_CHAIN_FRAME_MISMATCH")
            result.update(chain=chain, chain_depth=1 + target["chain_depth"])
        elif flags & 3:
            handler, = self.read("<I", offset + body_size)
            handler_offset, _ = self.map(handler, 1, "UNWIND_HANDLER_NOT_EXECUTABLE_FILE_BACKED", executable=True)
            result.update(handler_rva=handler, handler_file_offset=handler_offset,
                          language_specific_data_rva=rva + size)
            self.unknowns.add("LANGUAGE_SPECIFIC_UNWIND_DATA_NOT_INTERPRETED")
        self.unwind[rva] = result
        self.metadata_ranges.append((offset, offset + size, "UNWIND", ("UNWIND", rva)))
        return result

    def exceptions(self):
        directory = self.directories[3]
        if not directory["bytes"]:
            return []
        _require(directory["rva"] % 4 == 0 and directory["bytes"] % 12 == 0,
                 "EXCEPTION_DIRECTORY_ALIGNMENT_INVALID")
        count = directory["bytes"] // 12
        _require(count <= MAX_RUNTIME_FUNCTIONS, "RUNTIME_FUNCTION_COUNT_LIMIT")
        self.metadata_ranges.append((directory["file_offset"], directory["file_offset"] + directory["bytes"],
                                     "EXCEPTION_TABLE", ("EXCEPTION", 0)))
        result, previous_end = [], 0
        for index in range(count):
            offset = directory["file_offset"] + index * 12
            begin, end, unwind = self.read("<III", offset)
            function = self.runtime_function(begin, end, unwind, offset)
            _require(begin >= previous_end, "RUNTIME_FUNCTION_ORDER_OR_OVERLAP")
            previous_end = end
            function["index"] = index
            info = self.unwind_info(unwind)
            _require(info["prolog_size"] <= end - begin, "UNWIND_PROLOG_RANGE_INVALID")
            result.append(function)
        ranges = [(record["rva"], record["rva"] + record["bytes"]) for record in self.unwind.values()]
        _disjoint(ranges, "UNWIND_RECORD_OVERLAP")
        for first, last in ranges:
            _require(last <= directory["rva"] or directory["rva"] + directory["bytes"] <= first,
                     "UNWIND_EXCEPTION_TABLE_ALIAS")
        return result

    def unknown_iat_slots(self):
        directory = self.directories[12]
        result = []
        for delta in range(0, directory["bytes"], 8):
            rva, offset = directory["rva"] + delta, directory["file_offset"] + delta
            value, = self.read("<Q", offset)
            if value and rva not in self.iat_slots:
                self.metadata_ranges.append((offset, offset + 8, "IAT", ("UNINDEXED_IAT", delta // 8)))
                result.append({"iat_rva": rva, "iat_file_offset": offset, "iat_raw_value": value,
                               "name": None, "resolved_runtime_target": None})
        if result:
            self.unknowns.add("UNINDEXED_IAT_SLOT_NAMES_UNKNOWN")
        return result

    def check_metadata_ranges(self):
        # Only read-only lookup arrays may be shared by different descriptors.
        # Keep every writable/source-copy/structure claim, even equal ranges;
        # general directory containers are deliberately not exclusive claims.
        count = len(self.metadata_ranges)
        self.spend(count * (count.bit_length() + 2))
        lookups, ranges = set(), []
        for claim in self.metadata_ranges:
            if claim[2] == "LOOKUP":
                key = claim[:2]
                if key in lookups:
                    continue
                lookups.add(key)
            ranges.append(claim)
        _disjoint(ranges, "IMPORT_TABLE_OVERLAP")


def inspect_bcrypt_pe(data: bytes, init_rva: int) -> dict:
    """Index one <=1 MiB DLL, or refuse with PeValidationError(reason).

    Limits cannot be enlarged through arguments. Work units charge full-input
    hashing, every section probe, fixed record reads, bounded string scans and
    certificate/unwind envelope bytes; independent caps also bound sorting,
    recursion, output counts and every individual string/record.
    """
    _require(type(data) is bytes, "INPUT_BYTES_REQUIRED")
    _require(type(init_rva) is int and 0 < init_rva <= 0xFFFFFFFF, "INIT_RVA_INVALID")
    _require(0 < len(data) <= MAX_IMAGE_BYTES, "IMAGE_SIZE_OUT_OF_RANGE")
    image = _Image(data)
    image.headers()
    entry_rva = image.header["address_of_entrypoint"]
    entry_offset, section = image.map(entry_rva, 1, "ENTRYPOINT_NOT_EXECUTABLE_FILE_BACKED", executable=True)
    _require(entry_rva == init_rva, "INIT_ENTRYPOINT_MISMATCH")
    certificates = image.certificates()
    imports = image.imports()
    delay_imports = image.imports(delay=True)
    functions = image.exceptions()
    containing = [entry["index"] for entry in functions if entry["begin_rva"] <= init_rva < entry["end_rva"]]
    if not containing:
        image.unknowns.add("ENTRYPOINT_FUNCTION_BOUNDARY_UNKNOWN")
    unknown_iat = image.unknown_iat_slots()
    image.check_metadata_ranges()
    return {
        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "header": image.header, "sections": image.sections, "data_directories": image.directories,
        "entrypoint": {"rva": entry_rva, "file_offset": entry_offset,
                       "section_index": section["index"], "section_name": section["name"],
                       "loader_init_rva": init_rva, "runtime_function_index": containing[0] if containing else None},
        "certificates": certificates, "imports": imports, "delay_imports": delay_imports,
        "iat_index": image.iat_index, "unknown_iat_slots": unknown_iat,
        "runtime_functions": functions, "unwind_info": [image.unwind[rva] for rva in sorted(image.unwind)],
        "runtime_execution_observed": False, "unknowns": sorted(image.unknowns), "work_units": image.work,
        "limits": {"image_bytes": MAX_IMAGE_BYTES, "sections": MAX_SECTIONS, "directories": MAX_DIRECTORIES,
                   "directory_bytes": MAX_DIRECTORY_BYTES, "import_descriptors": MAX_IMPORT_DESCRIPTORS,
                   "delay_descriptors": MAX_DELAY_DESCRIPTORS, "symbols_per_dll": MAX_SYMBOLS_PER_DLL,
                   "import_symbols": MAX_IMPORT_SYMBOLS, "iat_slots": MAX_IAT_SLOTS,
                   "string_bytes": MAX_STRING_BYTES, "runtime_functions": MAX_RUNTIME_FUNCTIONS,
                   "unwind_records": MAX_UNWIND_RECORDS, "unwind_code_slots": MAX_UNWIND_CODE_SLOTS,
                   "unwind_slots_per_record": 255, "unwind_chain_depth": MAX_UNWIND_CHAIN_DEPTH,
                   "certificates": MAX_CERTIFICATES, "certificate_bytes": MAX_CERTIFICATE_BYTES,
                   "work_units": MAX_WORK_UNITS},
    }
