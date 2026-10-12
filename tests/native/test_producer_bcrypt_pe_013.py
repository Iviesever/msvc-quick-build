"""Focused synthetic contracts for the versioned 013 SET_FPREG consumer policy.

No actual DLL is read. Accepted bytes establish this indexer's policy boundary,
not Windows unwind semantic validity or the cause of the native init failure.
"""

import hashlib
import struct
import sys
import unittest

import producer_bcrypt_pe as frozen
import test_bcrypt_refusal_regression_012 as anchor
from test_producer_bcrypt_pe import directory, fixture, put

try:
    import producer_bcrypt_pe_013 as pe
except ModuleNotFoundError as error:
    if error.name != "producer_bcrypt_pe_013":
        raise
    pe = None


class BcryptPe013Tests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(pe, "013 versioned parser feature is not implemented")
        self.data, self.at = fixture()

    def index(self, data, at):
        try:
            return pe.inspect_bcrypt_pe(data, at["init_rva"])
        except pe.PeValidationError as error:
            self.fail("013 acceptance expectation refused: " + error.reason)

    def policy(self, result, ignored):
        self.assertEqual(result.get("schema"), "bcrypt-pe-index-013-v1")
        self.assertEqual(result.get("compatibility_policy"), {
            "id": "AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED",
            "nonzero_opinfo_ignored": ignored,
            "notice": "CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED"})

    def same_index(self, data, at):
        expected = frozen.inspect_bcrypt_pe(data, at["init_rva"])
        result = self.index(data, at)
        self.policy(result, False)
        structural = {key: value for key, value in result.items()
                      if key not in ("schema", "compatibility_policy")}
        self.assertEqual(structural, expected)
        return result

    def reject(self, data, reason, *, both=True):
        for module in ((frozen, pe) if both else (pe,)):
            with self.subTest(parser=module.__name__, reason=reason):
                with self.assertRaises(module.PeValidationError) as caught:
                    module.inspect_bcrypt_pe(data, self.at["init_rva"])
                self.assertEqual(caught.exception.reason, reason)

    def tracked_index(self, data, at, offset):
        slots, read_code = [], pe._Image.read.__code__

        def profile(frame, event, arg):
            if event == "call" and frame.f_code is read_code and frame.f_locals["fmt"] == "<BB":
                caller = frame.f_back
                if caller.f_code is pe._Image.unwind_info.__code__ and caller.f_locals["rva"] == 0x2600:
                    slots.append((frame.f_locals["offset"] - offset - 4) // 2)

        previous = sys.getprofile()
        sys.setprofile(profile)
        try:
            result = self.index(data, at)
        finally:
            sys.setprofile(previous)
        return result, slots

    def test_existing_accepted_result_changes_only_versioned_policy_metadata(self):
        variants = [fixture(), fixture(certificate=True),
                    fixture(pe_offset=0x180, raw_start=0x600, rva_shift=0x10000)]
        variants.append((directory(self.data, self.at, 3, 0, 0), self.at))
        epilog, at, _ = anchor.synthetic_image(bytes.fromhex("0204010003160000"))
        variants.append((epilog, at))
        for data, at in variants:
            with self.subTest(sha256=hashlib.sha256(data).hexdigest()):
                self.same_index(data, at)

    def test_v1_all_opinfo_values_keep_single_slots_and_raw_bytes(self):
        for info in range(16):
            with self.subTest(info=info):
                envelope = bytes((1, 22, 2, 0x65, 22, (info << 4) | 3, 20, 0x32))
                data, at, offset = anchor.synthetic_image(envelope)
                before = hashlib.sha256(data).hexdigest()
                result, slots = self.tracked_index(data, at, offset)
                self.policy(result, info != 0)
                self.assertEqual(slots, [0, 1])
                record = next(item for item in result["unwind_info"] if item["rva"] == 0x2600)
                self.assertEqual(record["raw_hex"], envelope.hex())
                self.assertEqual((record["code_slots"], record["frame_register"], record["frame_offset"]), (2, 5, 6))
                self.assertEqual((result["sha256"], hashlib.sha256(data).hexdigest()), (before, before))
                self.assertFalse(result["runtime_execution_observed"])

    def test_nonzero_policy_notice_has_fixed_shape_and_resets_per_input(self):
        envelope = bytes((1, 0, 255, 0x65)) + bytes((0, 0x63)) * 255 + bytes(2)
        data, at, _ = anchor.synthetic_image(envelope)
        result = self.index(data, at)
        self.policy(result, True)
        record = next(item for item in result["unwind_info"] if item["rva"] == 0x2600)
        self.assertEqual(record["raw_hex"], envelope.hex())
        self.assertEqual(record["code_slots"], 255)
        self.same_index(self.data, self.at)

    def test_original_info6_refusal_remains_at_frozen_guard(self):
        data, at, _ = anchor.synthetic_image(anchor.MINIMAL_ENVELOPE)
        self.policy(self.index(data, at), True)
        try:
            frozen.inspect_bcrypt_pe(data, at["init_rva"])
        except frozen.PeValidationError as error:
            self.assertEqual(error.reason, "UNWIND_OPCODE_INVALID")
            node, lines = error.__traceback__, []
            while node is not None:
                if node.tb_frame.f_code is frozen._Image.unwind_info.__code__:
                    lines.append(node.tb_lineno)
                node = node.tb_next
            self.assertEqual(lines, [438])
        else:
            self.fail("frozen parser must retain its original info6 refusal")

    def test_v2_accepts_only_zero_opinfo(self):
        for info in range(16):
            with self.subTest(info=info):
                envelope = bytes((2, 22, 1, 0x65, 22, (info << 4) | 3, 0, 0))
                data, at, _ = anchor.synthetic_image(envelope)
                if info == 0:
                    self.same_index(data, at)
                else:
                    self.reject(data, "UNWIND_OPCODE_INVALID")

    def test_zero_frame_register_keeps_guard_and_header_refusals(self):
        for version in (1, 2):
            for info in (0, 6, 15):
                for frame in (0, 0x60):
                    with self.subTest(version=version, info=info, frame=frame):
                        envelope = bytes((version, 22, 1, frame, 22, (info << 4) | 3, 0, 0))
                        data, _, _ = anchor.synthetic_image(envelope)
                        reason = "UNWIND_FRAME_INVALID" if frame else "UNWIND_OPCODE_INVALID"
                        self.reject(data, reason)

    def test_observed_width2_prefix_and_info6_raw_byte_are_preserved(self):
        self.assertEqual(hashlib.sha256(anchor.OBSERVED_ENVELOPE).hexdigest(), anchor.OBSERVED_ENVELOPE_SHA256)
        self.assertEqual(hashlib.sha256(anchor.OBSERVED_ROOT).hexdigest(), anchor.OBSERVED_ROOT_SHA256)
        # Only this synthetic copy relocates its handler to fixture .text.
        envelope = put(anchor.OBSERVED_ENVELOPE, 32, 0x1100)
        data, at, offset = anchor.synthetic_image(envelope)
        result, slots = self.tracked_index(data, at, offset)
        self.policy(result, True)
        self.assertEqual(slots, [0, 2, 4, 6, 7, 9, 10, 11, 12, 13])
        record = next(item for item in result["unwind_info"] if item["rva"] == 0x2600)
        self.assertEqual(record["raw_hex"], envelope.hex())
        self.assertEqual(data[offset + 16:offset + 18], bytes((0x16, 0x63)))
        self.assertEqual(envelope[:32], anchor.OBSERVED_ENVELOPE[:32])
        self.assertEqual((record["handler_rva"], record["handler_file_offset"]), (0x1100, 0x500))
        self.assertFalse(result["runtime_execution_observed"])

    def test_alloc_large_and_machframe_opinfo_guards_are_unchanged(self):
        for version in (1, 2):
            for opcode in (1, 10):
                for info in range(2, 16):
                    with self.subTest(version=version, opcode=opcode, info=info):
                        envelope = bytes((version, 22, 1, 0, 22, (info << 4) | opcode, 0, 0))
                        data, _, _ = anchor.synthetic_image(envelope)
                        self.reject(data, "UNWIND_OPCODE_INVALID")
                for info in (0, 1):
                    width = (2 if info == 0 else 3) if opcode == 1 else 1
                    envelope = bytes((version, 22, width, 0, 22, (info << 4) | opcode))
                    envelope += bytes(4 + ((width + 1) & ~1) * 2 - len(envelope))
                    data, at, _ = anchor.synthetic_image(envelope)
                    self.same_index(data, at)

    def test_unsupported_headers_opcodes_and_truncated_slots_still_refuse(self):
        changes = [(0, value, "UNWIND_VERSION_UNSUPPORTED") for value in (0, 3, 4, 5, 6, 7)]
        changes += [(0, value, "UNWIND_FLAGS_INVALID") for value in (0x29, 0x41, 0x3A)]
        changes += [(5, value, "UNWIND_OPCODE_UNSUPPORTED") for value in (6, 7, 11, 12, 13, 14, 15)]
        changes += [(5, value, "UNWIND_OPCODE_TRUNCATED") for value in (1, 0x11, 4, 5, 8, 9)]
        for offset, value, reason in changes:
            with self.subTest(offset=offset, value=value):
                envelope = put(anchor.MINIMAL_ENVELOPE, offset, value, "<B")
                data, _, _ = anchor.synthetic_image(envelope)
                self.reject(data, reason)

    def test_code_order_still_refuses_after_a_compatible_operation(self):
        envelope = bytes.fromhex("0116026516031703")
        data, _, _ = anchor.synthetic_image(envelope)
        self.reject(data, "UNWIND_CODE_ORDER_INVALID")
        data, _, _ = anchor.synthetic_image(put(envelope, 5, 0x63, "<B"))
        self.reject(data, "UNWIND_CODE_ORDER_INVALID", both=False)

    def test_chain_cycle_frame_and_target_prolog_checks_are_unchanged(self):
        for frame, end, target, reason in (
            (0, 0x1080, 0x2510, "UNWIND_CHAIN_CYCLE"),
            (5, 0x1080, 0x2500, "UNWIND_CHAIN_FRAME_MISMATCH"),
            (0, 0x1001, 0x2500, "UNWIND_PROLOG_RANGE_INVALID"),
        ):
            with self.subTest(reason=reason):
                data = bytearray(self.data)
                struct.pack_into("<BBBBIII", data, self.at["rdata"] + 0x510,
                                 0x21, 0, 0, frame, 0x1000, end, target)
                self.reject(bytes(data), reason)

    def chain(self, length, leaf=bytes((1, 0, 0, 0)), frame=0):
        data = bytearray(self.data)
        for index in range(length - 1):
            struct.pack_into("<BBBBIII", data, self.at["rdata"] + 0x600 + index * 16,
                             0x21, 0, 0, frame, 0x1000, 0x1080, 0x2600 + (index + 1) * 16)
        offset = self.at["rdata"] + 0x600 + (length - 1) * 16
        data[offset:offset + len(leaf)] = leaf
        struct.pack_into("<I", data, self.at["pdata"] + 8, 0x2600)
        return bytes(data)

    def test_chain_depth_boundary_and_nonzero_policy_in_leaf(self):
        self.same_index(self.chain(32), self.at)
        self.reject(self.chain(33), "UNWIND_CHAIN_DEPTH_LIMIT")
        result = self.index(self.chain(2, anchor.MINIMAL_ENVELOPE, 0x65), self.at)
        self.policy(result, True)
        leaf = next(item for item in result["unwind_info"] if item["rva"] == 0x2610)
        self.assertEqual(leaf["raw_hex"], anchor.MINIMAL_ENVELOPE.hex())

    def test_runtime_and_unwind_ranges_and_overlaps_still_refuse(self):
        for offset, value, reason in (
            (self.at["pdata"] + 4, 0x1000, "RUNTIME_FUNCTION_RANGE_INVALID"),
            (self.at["pdata"] + 12, 0x107F, "RUNTIME_FUNCTION_ORDER_OR_OVERLAP"),
            (self.at["pdata"] + 4, 0x1301, "RUNTIME_FUNCTION_NOT_EXECUTABLE_FILE_BACKED"),
            (self.at["pdata"] + 4, 0x1001, "UNWIND_PROLOG_RANGE_INVALID"),
            (self.at["pdata"] + 8, 0x2501, "UNWIND_ALIGNMENT_INVALID"),
        ):
            self.reject(put(self.data, offset, value), reason)
        data = bytearray(put(self.data, self.at["pdata"] + 20, 0x2504))
        data[self.at["rdata"] + 0x500:self.at["rdata"] + 0x50C] = bytes.fromhex("010104000100000000000000")
        self.reject(bytes(data), "UNWIND_RECORD_OVERLAP")

    def test_virtual_only_and_ambiguous_section_mapping_still_refuse(self):
        for rva in (0x1300, 0x3400):
            self.reject(put(self.data, self.at["pdata"] + 8, rva), "UNWIND_NOT_FILE_BACKED")
        section = self.at["sections"] + 40
        self.reject(put(self.data, section + 20, 0x400), "SECTION_RAW_OVERLAP")
        self.reject(put(self.data, section + 12, 0x1000), "SECTION_VIRTUAL_OVERLAP")
        data = bytearray(put(self.data, self.at["pdata"] + 8, 0x2FFC))
        data[self.at["rdata"] + 0xFFC:self.at["rdata"] + 0x1000] = bytes((1, 1, 1, 0))
        self.reject(bytes(data), "UNWIND_NOT_FILE_BACKED")


if __name__ == "__main__":
    unittest.main()
