"""Regression of the observed 012 refusal using synthetic DLL fixtures only.

The exact 36-byte envelope and 12-byte root entry are immutable evidence anchors.
Only the envelope is copied into a synthetic PE; its addresses are not preserved.
Accepted neighbors describe the frozen parser's boundary, not Windows unwind
validity, a compatibility correction, or an explanation of the runtime failure.
"""

import hashlib
import struct
import sys
import unittest

import producer_bcrypt_pe as pe
import producer_bcrypt_refusal_012 as refusal
from test_producer_bcrypt_pe import fixture, put


# report.json SHA256 d747b6dc008b8191e0226282879d3258ee11a645d0066b91f8757cb5af357e86.
OBSERVED_ENVELOPE = bytes.fromhex(
    "19300e6522741a001e6419001a3418001663110112000af008e006d004c00250d8a70000")
OBSERVED_ENVELOPE_SHA256 = "31930bd7175248a6377f6ecde5d9e88a28ba3eb7ded19b14c96ca57ebca7be1c"
OBSERVED_ROOT = bytes.fromhex("30180000be19000058d90100")
OBSERVED_ROOT_SHA256 = "ba99b79b830b4591fe5555471d02cde74fbe404584f6cc0cb46cce398871bd04"
MINIMAL_ENVELOPE = bytes.fromhex("0116016516630000")
SYNTHETIC_UNWIND_RVA = 0x2600
SYNTHETIC_HANDLER_RVA = 0x1100


def synthetic_image(envelope):
    """Keep fixture function ranges; move its first unwind record beyond 0x2510."""
    original, at = fixture()
    data = bytearray(original)
    offset = at["rdata"] + SYNTHETIC_UNWIND_RVA - at["rdata_rva"]
    data[offset:offset + len(envelope)] = envelope
    struct.pack_into("<I", data, at["pdata"] + 8, SYNTHETIC_UNWIND_RVA)
    return bytes(data), at, offset


class BcryptRefusalRegression012Tests(unittest.TestCase):
    def locate(self, data, at, slot):
        result = refusal.diagnose(pe, data, at["init_rva"])
        self.assertEqual(result["status"], "located_refusal")
        self.assertEqual(result["reason"], "UNWIND_OPCODE_INVALID")
        self.assertEqual(result["exception_type"], "PeValidationError")
        self.assertEqual(result["parser_calls"], 1)
        self.assertFalse(result["static_index_admitted"])
        context = result["context"]
        self.assertEqual(context["guard"], {
            "function": "_Image.unwind_info", "line": 438, "require_line": 64,
            "predicate": "info == 0 and frame & 15 != 0", "predicate_value": False})
        leaf = context["leaf"]
        self.assertEqual(tuple(leaf[key] for key in
                               ("version", "frame", "opcode", "opinfo", "slot", "width")),
                         (1, 0x65, 3, 6, slot, 1))
        self.assertEqual(leaf["width_state"], "assigned_for_current_slot")
        self.assertEqual(leaf["frame"] & 15, 5)  # Nonzero: only info == 0 is false.
        return context

    def test_exact_observed_anchors_and_guard_operands(self):
        self.assertEqual(len(OBSERVED_ENVELOPE), 36)
        self.assertEqual(hashlib.sha256(OBSERVED_ENVELOPE).hexdigest(), OBSERVED_ENVELOPE_SHA256)
        self.assertEqual(len(OBSERVED_ROOT), 12)
        self.assertEqual(hashlib.sha256(OBSERVED_ROOT).hexdigest(), OBSERVED_ROOT_SHA256)
        self.assertEqual(struct.unpack("<III", OBSERVED_ROOT), (0x1830, 0x19BE, 0x1D958))
        self.assertEqual(0x22084 - 11 * 12, 0x22000)  # Observed root index and table RVA.
        vf, prolog, count, frame = OBSERVED_ENVELOPE[:4]
        self.assertEqual((vf & 7, vf >> 3, prolog, count, frame & 15, frame >> 4),
                         (1, 3, 48, 14, 5, 6))
        self.assertEqual(OBSERVED_ENVELOPE[4 + 6 * 2:4 + 7 * 2], bytes((0x16, 0x63)))
        self.assertEqual(struct.unpack_from("<I", OBSERVED_ENVELOPE, 32)[0], 0xA7D8)

    def test_minimal_info6_refusal_and_single_byte_info0_neighbor(self):
        # Synthetic reduction: flags=0, prolog=0x16, count=1; original version,
        # frame=0x65 and offending code-offset/operation remain unchanged.
        data, at, offset = synthetic_image(MINIMAL_ENVELOPE)
        context = self.locate(data, at, 0)
        self.assertEqual(context["envelope"]["raw_hex"], MINIMAL_ENVELOPE.hex())
        self.assertEqual(context["leaf"]["code_offset"], 0x16)
        neighbor = put(data, offset + 5, 0x03, "<B")
        self.assertEqual([i for i, (a, b) in enumerate(zip(data, neighbor)) if a != b],
                         [offset + 5])
        result = pe.inspect_bcrypt_pe(neighbor, at["init_rva"])
        record = next(item for item in result["unwind_info"] if item["rva"] == SYNTHETIC_UNWIND_RVA)
        self.assertEqual((record["version"], record["frame_register"], record["frame_offset"]), (1, 5, 6))
        self.assertEqual(record["raw_hex"], "0116016516030000")
        self.assertFalse(result["runtime_execution_observed"])

    def test_exact_envelope_reaches_slot6_after_original_width2_prefix(self):
        data, at, offset = synthetic_image(OBSERVED_ENVELOPE)
        reached, code = [], pe._Image.unwind_info.__code__

        def trace(frame, event, arg):
            if frame.f_code is code:
                if event == "line" and frame.f_lineno == 422 and frame.f_locals["rva"] == SYNTHETIC_UNWIND_RVA:
                    reached.append((frame.f_locals["slot"], frame.f_locals["previous_offset"]))
                return trace
            return None

        previous_trace = sys.gettrace()
        sys.settrace(trace)
        try:
            context = self.locate(data, at, 6)
        finally:
            sys.settrace(previous_trace)
        self.assertEqual(reached, [(0, 48), (2, 0x22), (4, 0x1E), (6, 0x1A)])
        self.assertEqual(OBSERVED_ENVELOPE[4:16].hex(), "22741a001e6419001a341800")
        self.assertEqual(context["leaf"]["code_offset"], 0x16)
        self.assertEqual(context["views"]["slot"], {"offset": 16, "bytes": 2})
        self.assertEqual(context["envelope"]["raw_hex"], OBSERVED_ENVELOPE.hex())
        self.assertEqual(context["envelope"]["sha256"], OBSERVED_ENVELOPE_SHA256)
        self.assertEqual((context["envelope"]["rva"], context["envelope"]["file_offset"]), (0x2600, 0xE00))
        self.assertEqual((context["root_entry"]["index"], context["root_entry"]["unwind_rva"]), (0, 0x2600))
        self.assertEqual(data[offset:offset + 36], OBSERVED_ENVELOPE)
        self.assertEqual(data[at["rdata"] + 0x510:at["rdata"] + 0x514], b"\x01\x00\x00\x00")

    def test_full_neighbor_uses_explicit_synthetic_handler_relocation(self):
        # The observed handler RVA is outside the fixture's .text. Relocate it
        # only in this synthetic copy; leave the exact 36-byte anchor untouched.
        relocated = put(OBSERVED_ENVELOPE, 32, SYNTHETIC_HANDLER_RVA)
        data, at, offset = synthetic_image(relocated)
        self.locate(data, at, 6)  # Relocation alone does not remove the refusal.
        neighbor = put(data, offset + 17, 0x03, "<B")
        raw = neighbor[offset:offset + 36]
        self.assertEqual([i for i, (a, b) in enumerate(zip(data, neighbor)) if a != b], [offset + 17])
        self.assertEqual([i for i, (a, b) in enumerate(zip(OBSERVED_ENVELOPE, raw)) if a != b], [17, 32, 33])
        self.assertEqual(raw[4:16], OBSERVED_ENVELOPE[4:16])
        result = pe.inspect_bcrypt_pe(neighbor, at["init_rva"])
        record = next(item for item in result["unwind_info"] if item["rva"] == SYNTHETIC_UNWIND_RVA)
        self.assertEqual(tuple(record[key] for key in
                               ("version", "flags", "prolog_size", "code_slots", "frame_register", "frame_offset")),
                         (1, 3, 48, 14, 5, 6))
        self.assertEqual((record["handler_rva"], record["handler_file_offset"]), (0x1100, 0x500))
        self.assertEqual(len(result["runtime_functions"]), 2)
        self.assertEqual(hashlib.sha256(OBSERVED_ENVELOPE).hexdigest(), OBSERVED_ENVELOPE_SHA256)
        self.assertFalse(result["runtime_execution_observed"])


if __name__ == "__main__":
    unittest.main()
