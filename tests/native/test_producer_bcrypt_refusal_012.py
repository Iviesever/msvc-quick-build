"""Synthetic same-call context tests; never open a retained DLL or run native code."""

import hashlib
import json
from pathlib import Path
import struct
import sys
from types import FunctionType, SimpleNamespace, TracebackType
import unittest
from unittest.mock import patch

import producer_bcrypt_pe as pe
from test_producer_bcrypt_pe import fixture, put

try:
    import producer_bcrypt_refusal_012 as refusal
except ModuleNotFoundError as error:
    if error.name != "producer_bcrypt_refusal_012":
        raise
    refusal = None


class RefusalContextTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(refusal, "012 bounded same-call refusal context is not implemented")
        self.data, self.at = fixture()

    def malformed(self, raw, *, rva=0x2500):
        data = bytearray(self.data)
        offset = self.at["rdata"] + rva - self.at["rdata_rva"]
        data[offset:offset + len(raw)] = raw
        return bytes(data)

    def locate(self, raw, *, rva=0x2500, data=None):
        source = self.malformed(raw, rva=rva) if data is None else data
        result = refusal.diagnose(pe, source, self.at["init_rva"])
        self.assertEqual(result["status"], "located_refusal", result)
        self.assertEqual((result["reason"], result["exception_type"], result["parser_calls"]),
                         ("UNWIND_OPCODE_INVALID", "PeValidationError", 1))
        self.assertFalse(result["static_index_admitted"])
        self.assertIsNone(result["context_unavailable_reason"])
        self.assertLessEqual(result["context_work_units"], 16384)
        encoded = json.dumps(result, ensure_ascii=True, allow_nan=False, separators=(",", ":"))
        self.assertLessEqual(len(encoded.encode("ascii")), 8192)
        context = result["context"]
        envelope = context["envelope"]
        self.assertEqual(envelope["raw_hex"], raw.hex())
        self.assertEqual(envelope["sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(envelope["rva"], rva)
        self.assertEqual(envelope["bytes"], len(raw))
        self.assertEqual(envelope["file_offset"], self.at["rdata"] + rva - self.at["rdata_rva"])
        self.assertEqual(context["views"]["header"], {"offset": 0, "bytes": 4})
        self.assertEqual(context["views"]["slot"], {"offset": 4 + 2 * context["leaf"]["slot"], "bytes": 2})
        return context

    def capture(self, data=None, *, depth=0):
        def nested(left):
            if left:
                return nested(left - 1)
            return pe.inspect_bcrypt_pe(source, self.at["init_rva"])
        source = self.malformed(bytes.fromhex("0104010004210000")) if data is None else data
        try:
            nested(depth)
        except pe.PeValidationError as error:
            return source, error
        self.fail("synthetic input must retain the frozen rejection")

    def private_extract(self, source, error, *, module=pe):
        return refusal._extract(refusal._bind(module), source, self.at["init_rva"], error, refusal._Work())

    def module_view(self, inspect=None):
        return SimpleNamespace(PeValidationError=pe.PeValidationError, _Image=pe._Image, _require=pe._require,
                               inspect_bcrypt_pe=pe.inspect_bcrypt_pe if inspect is None else inspect)

    @staticmethod
    def trace_nodes(error):
        result, node = [], error.__traceback__
        while node is not None:
            result.append((node.tb_frame, node.tb_lasti, node.tb_lineno))
            node = node.tb_next
        return result

    @staticmethod
    def set_trace(error, nodes):
        tail = None
        for frame, lasti, line in reversed(nodes):
            tail = TracebackType(tail, frame, lasti, line)
        error.__traceback__ = tail

    def test_three_same_reason_guards_and_set_fpreg_disjunction(self):
        cases = (("0104010004210000", 427, 1, 2, None),
                 ("0104010004030000", 438, 3, 0, 1),
                 ("0104010504130000", 438, 3, 1, 1),
                 ("01040100042a0000", 440, 10, 2, 1))
        for hex_bytes, line, opcode, info, width in cases:
            with self.subTest(line=line, opcode=opcode, info=info):
                context = self.locate(bytes.fromhex(hex_bytes))
                self.assertEqual(context["guard"]["line"], line)
                self.assertEqual(context["guard"]["require_line"], 64)
                leaf = context["leaf"]
                self.assertEqual((leaf["slot"], leaf["opcode"], leaf["opinfo"], leaf["width"]),
                                 (0, opcode, info, width))
                self.assertEqual(leaf["width_state"], "not_assigned_for_current_slot" if width is None else "assigned_for_current_slot")

    def test_stale_width_three_is_not_current_width(self):
        for operation, line, width in ((0x21, 427, None), (0x03, 438, 1), (0x2A, 440, 1)):
            with self.subTest(line=line):
                raw = bytes((1, 4, 4, 0, 4, 0x11, 255, 255, 255, 255, 3, operation))
                context = self.locate(raw)
                self.assertEqual(context["guard"]["line"], line)
                self.assertEqual((context["leaf"]["slot"], context["leaf"]["width"]), (3, width))
                if line == 427:
                    _, error = self.capture(self.malformed(raw))
                    node = error.__traceback__
                    while node.tb_next is not None:
                        if node.tb_frame.f_code is pe._Image.unwind_info.__code__:
                            self.assertEqual(node.tb_frame.f_locals["width"], 3)
                        node = node.tb_next
                    error.__traceback__ = None

    def test_one_and_two_slot_progression_skips_payload(self):
        for hex_bytes, slot in (("0104020004320321", 1), ("010403000401ffff03210000", 2)):
            with self.subTest(slot=slot):
                context = self.locate(bytes.fromhex(hex_bytes))
                self.assertEqual(context["leaf"]["slot"], slot)
                self.assertIsNone(context["leaf"]["width"])

    def test_version_two_is_observed_without_format_correction(self):
        context = self.locate(bytes.fromhex("02040100042a0000"))
        self.assertEqual(context["leaf"]["version"], 2)

    def test_root_entry_and_first_slot_missing_optional_values(self):
        context = self.locate(bytes.fromhex("0104010004210000"))
        entry = context["root_entry"]
        self.assertEqual((entry["index"], entry["file_offset"], entry["rva"]), (0, 0x1C00, 0x4000))
        self.assertEqual((entry["begin_rva"], entry["end_rva"], entry["unwind_rva"]), (0x1000, 0x1080, 0x2500))
        root_raw = self.data[0x1C00:0x1C0C]
        self.assertEqual(entry["raw_hex"], root_raw.hex())
        self.assertEqual(entry["sha256"], hashlib.sha256(root_raw).hexdigest())
        self.assertEqual(context["chain_rvas"], [0x2500])
        self.assertNotIn("result", context["leaf"])
        self.assertNotIn("info", entry)

    def test_second_table_entry_chain_uses_leaf_not_parent_or_stale_info(self):
        raw = bytes.fromhex("0104010004210000")
        data = bytearray(self.malformed(raw, rva=0x2600))
        struct.pack_into("<BBBBIII", data, self.at["rdata"] + 0x510,
                         0x21, 0, 0, 0, 0x1000, 0x1080, 0x2600)
        context = self.locate(raw, rva=0x2600, data=bytes(data))
        self.assertEqual(context["chain_rvas"], [0x2510, 0x2600])
        self.assertEqual(context["root_entry"]["index"], 1)
        self.assertEqual(context["root_entry"]["file_offset"], 0x1C0C)
        self.assertEqual(context["root_entry"]["unwind_rva"], 0x2510)
        self.assertEqual(context["leaf"]["file_offset"], 0xE00)

    def test_thirty_two_chain_rvas_and_larger_chain_refusal(self):
        for length in (32, 33):
            with self.subTest(length=length):
                data = bytearray(put(self.data, self.at["pdata"] + 8, 0x2600))
                for index in range(length - 1):
                    struct.pack_into("<BBBBIII", data, self.at["rdata"] + 0x600 + index * 16,
                                     0x21, 0, 0, 0, 0x1000, 0x1080, 0x2610 + index * 16)
                rva = 0x2600 + (length - 1) * 16
                raw = bytes.fromhex("0104010004210000")
                offset = self.at["rdata"] + rva - 0x2000
                data[offset:offset + len(raw)] = raw
                if length == 32:
                    context = self.locate(raw, rva=rva, data=bytes(data))
                    self.assertEqual(context["chain_rvas"], list(range(0x2600, rva + 1, 16)))
                else:
                    result = refusal.diagnose(pe, bytes(data), self.at["init_rva"])
                    self.assertEqual(result["reason"], "UNWIND_CHAIN_DEPTH_LIMIT")
                    self.assertEqual(result["status"], "context_unavailable")
                    self.assertIsNone(result["context"])

    def test_maximum_mapped_envelope_adds_no_parent_raw_read(self):
        raw = bytes((0x21, 4, 255, 0, 4, 0x21)) + bytes(528 - 6)
        data = put(self.malformed(raw, rva=0x2600), self.at["pdata"] + 8, 0x2600)
        context = self.locate(raw, rva=0x2600, data=data)
        self.assertEqual(context["envelope"]["bytes"], 528)
        self.assertEqual(len(bytes.fromhex(context["root_entry"]["raw_hex"])), 12)

    def test_failed_envelope_mapping_does_not_read_context_bytes(self):
        data = bytearray(put(self.data, self.at["pdata"] + 8, 0x2FFC))
        data[self.at["rdata"] + 0xFFC:self.at["rdata"] + 0x1000] = bytes((1, 1, 1, 0))
        result = refusal.diagnose(pe, bytes(data), self.at["init_rva"])
        self.assertEqual(result["reason"], "UNWIND_NOT_FILE_BACKED")
        self.assertEqual(result["status"], "context_unavailable")
        self.assertIsNone(result["context"])
        self.assertNotIn("raw_hex", json.dumps(result))

    def test_identical_bytes_different_object_cannot_borrow_traceback(self):
        source, error = self.capture()
        equal_copy = bytes(bytearray(source))
        self.assertIsNot(source, equal_copy)
        with self.assertRaises(refusal._Unavailable):
            self.private_extract(equal_copy, error)
        error.__traceback__ = None

    def test_source_code_lines_and_table_chain_lineage_are_exact(self):
        data = bytearray(self.malformed(bytes.fromhex("0104010004210000"), rva=0x2600))
        struct.pack_into("<BBBBIII", data, 0xD10, 0x21, 0, 0, 0, 0x1000, 0x1080, 0x2600)
        source, error = self.capture(bytes(data))
        _, other_error = self.capture(bytes(data))
        nodes, other_nodes = self.trace_nodes(error), self.trace_nodes(other_error)
        root = next(i for i, n in enumerate(nodes) if n[0].f_code is pe._Image.exceptions.__code__)
        leaf, parent = len(nodes) - 2, len(nodes) - 3
        cases = []
        for index in (root, leaf, len(nodes) - 1):
            def foreign_frame():
                return sys._getframe()
            foreign = FunctionType(foreign_frame.__code__.replace(co_name=nodes[index][0].f_code.co_name), globals())()
            changed = nodes.copy()
            changed[index] = (foreign, foreign.f_lasti, nodes[index][2])
            cases.append(changed)
        for index, line in ((root - 1, 542), (root, 482), (parent, 449), (leaf, 428), (leaf, 438), (leaf, 440)):
            changed = nodes.copy()
            changed[index] = (*changed[index][:2], line)
            cases.append(changed)
        for index in (root, parent, leaf):
            changed = nodes.copy()
            changed[index] = other_nodes[index]  # Same code and bytes, another _Image.
            cases.append(changed)
        cases.extend((nodes[:root] + [nodes[root]] + nodes[root:], nodes[:-1] + [nodes[-2], nodes[-1]],
                      nodes[:parent] + nodes[parent + 1:], nodes[:parent] + [nodes[leaf], nodes[parent], nodes[-1]]))
        for index, forged in enumerate(cases):
            with self.subTest(forgery=index):
                self.set_trace(error, forged)
                with self.assertRaises(refusal._Unavailable):
                    self.private_extract(source, error)
        error.__traceback__ = other_error.__traceback__ = None

    def test_wrong_exception_identity_and_missing_traceback_are_unavailable(self):
        for error in (ValueError("UNWIND_OPCODE_INVALID"), pe.PeValidationError("UNWIND_OPCODE_INVALID")):
            with self.subTest(error_type=type(error).__name__):
                with self.assertRaises(refusal._Unavailable):
                    self.private_extract(self.data, error)

    def test_traceback_walk_has_hard_limit(self):
        source, error = self.capture(depth=65)
        with self.assertRaisesRegex(refusal._Unavailable, "TRACEBACK_LIMIT"):
            self.private_extract(source, error)
        error.__traceback__ = None

    def test_missing_badly_typed_or_huge_mandatory_scalar_is_unavailable(self):
        for value in ({}, {"slot": True}, {"slot": object()}, {"slot": 1 << 200}, {"slot": -1}):
            with self.subTest(value_type=type(value.get("slot")).__name__):
                with self.assertRaises(refusal._Unavailable):
                    refusal._uint(value, "slot", 255, refusal._Work())

    def test_oversized_and_unserializable_result_is_refused_without_repr(self):
        class NoRepr:
            def __repr__(self):
                raise AssertionError("arbitrary repr must not be called")
        for value in ("x" * 8193, NoRepr(), {"cycle": None}):
            if type(value) is dict:
                value["cycle"] = value
            result = refusal._finish({"context": value, "parser_calls": 1}, refusal._Work())
            self.assertEqual(result["status"], "context_unavailable")
            self.assertIsNone(result["context"])
            self.assertLessEqual(len(json.dumps(result).encode()), 8192)

    def test_real_extraction_and_serialization_charge_their_work_boundaries(self):
        source, error = self.capture()
        work = refusal._Work()
        context = refusal._extract(refusal._bind(pe), source, self.at["init_rva"], error, work)
        self.assertGreaterEqual(work.used, 4 * (8 + 12))
        limited = refusal._Work()
        limited.used = 16384 - 1024 - work.used + 1
        with self.assertRaisesRegex(refusal._Unavailable, "CONTEXT_WORK_LIMIT"):
            refusal._extract(refusal._bind(pe), source, self.at["init_rva"], error, limited)
        report = refusal._empty()
        report.update(context=context, parser_calls=1)
        work = refusal._Work()
        finished = refusal._finish(report.copy(), work)
        self.assertGreaterEqual(work.used, 2 * len(json.dumps(finished, separators=(",", ":")).encode()))
        limited = refusal._Work()
        limited.used = 16384 - 1024 - work.used + 1
        stopped = refusal._finish(report.copy(), limited)
        self.assertEqual(stopped["context_unavailable_reason"], "CONTEXT_WORK_LIMIT")
        self.assertIsNone(stopped["context"])
        self.assertLessEqual(stopped["context_work_units"], 16384)
        error.__traceback__ = None

    def test_actual_refusal_calls_once_and_extraction_never_reenters_parser(self):
        state = {"calls": 0, "after_catch": False, "forbidden": 0}
        blocked = (pe.inspect_bcrypt_pe.__code__, pe._Image.map.__code__, pe._Image.read.__code__)
        def profile(frame, event, arg):
            if event != "call":
                return
            if frame.f_code is refusal._extract.__code__:
                state["after_catch"] = True
            if frame.f_code is pe.inspect_bcrypt_pe.__code__:
                state["calls"] += 1
            if state["after_catch"] and any(frame.f_code is code for code in blocked):
                state["forbidden"] += 1
                raise AssertionError("parser reentered after the refusal")
        previous = sys.getprofile()
        try:
            sys.setprofile(profile)
            result = refusal.diagnose(pe, self.malformed(bytes.fromhex("0104010004210000")), self.at["init_rva"])
        finally:
            sys.setprofile(previous)
        self.assertEqual(state, {"calls": 1, "after_catch": True, "forbidden": 0})
        self.assertEqual(result["status"], "located_refusal")

    def test_del_escape_overflow_stops_before_json_serialization(self):
        with patch.object(refusal.json, "dumps", side_effect=AssertionError("over-bound JSON serialization")) as encoder:
            result = refusal._finish({"context": "\x7f" * 2000, "parser_calls": 1}, refusal._Work())
            encoder.assert_not_called()
        self.assertEqual(result["status"], "context_unavailable")
        self.assertEqual(result["context_unavailable_reason"], "OUTPUT_BYTE_LIMIT")

    def test_one_call_unexpected_success_discards_full_index(self):
        calls = []
        def inspect(data, rva):
            calls.append((data, rva))
            return {"forbidden_full_index": "x" * 100000}
        result = refusal.diagnose(self.module_view(inspect=inspect), self.data, self.at["init_rva"])
        self.assertEqual((len(calls), result["parser_calls"]), (1, 1))
        self.assertEqual(result["status"], "unexpected_success")
        self.assertIsNone(result["context"])
        self.assertNotIn("forbidden_full_index", json.dumps(result))

    def test_unexpected_exception_never_serializes_its_message_or_retries(self):
        calls = []
        def inspect(data, rva):
            calls.append(1)
            raise RuntimeError("do-not-copy-this-message" * 10000)
        result = refusal.diagnose(self.module_view(inspect=inspect), self.data, self.at["init_rva"])
        self.assertEqual(calls, [1])
        self.assertEqual(result["status"], "unexpected_exception")
        self.assertEqual(result["exception_type"], "RuntimeError")
        self.assertNotIn("do-not-copy", json.dumps(result))

    def test_invalid_binding_has_zero_calls_and_a_small_result(self):
        result = refusal.diagnose(SimpleNamespace(), self.data, self.at["init_rva"])
        self.assertEqual(result["parser_calls"], 0)
        self.assertEqual(result["status"], "context_unavailable")
        self.assertFalse(result["static_index_admitted"])

    def test_original_parser_and_tests_stay_byte_identical(self):
        root = Path(__file__).parent
        for name, wanted in (("producer_bcrypt_pe.py", "941b506cdf0ff063069d9309e04e517a26b3906deadcf93d3c0a9814141e41ba"),
                             ("test_producer_bcrypt_pe.py", "d96a1ef1535482bec20e012aba2345b8cbf1b742329de435bd8444290c78117d")):
            self.assertEqual(hashlib.sha256((root / name).read_bytes()).hexdigest(), wanted)


if __name__ == "__main__":
    unittest.main()
