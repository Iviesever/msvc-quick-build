"""Synthetic one-call observation and bounded same-failure context contracts."""

import json
import struct
import sys
import unittest
from types import SimpleNamespace, TracebackType
from unittest.mock import patch

import producer_bcrypt_pe_013 as pe
import test_bcrypt_refusal_regression_012 as anchor
from test_producer_bcrypt_pe import fixture, put

try:
    import bcrypt_index_observation_013 as observation
except ModuleNotFoundError as error:
    if error.name != "bcrypt_index_observation_013":
        raise
    observation = None


def refused_input():
    return anchor.synthetic_image(put(anchor.MINIMAL_ENVELOPE, 0, 2, "<B"))


def capture(data, at):
    try:
        pe.inspect_bcrypt_pe(data, at["init_rva"])
    except pe.PeValidationError as error:
        return error
    raise AssertionError("synthetic refusal expected")


def nodes(error):
    result, node = [], error.__traceback__
    while node is not None:
        result.append(node)
        node = node.tb_next
    return result


def relink(items):
    result = None
    for item in reversed(items):
        result = TracebackType(result, item.tb_frame, item.tb_lasti, item.tb_lineno)
    return result


class Observation013Tests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(observation, "013 one-call observation feature is not implemented")

    def observed(self, data, at, parser=pe):
        calls, returned, code = [], [], parser.inspect_bcrypt_pe.__code__

        def profile(frame, event, arg):
            if frame.f_code is code:
                if event == "call":
                    calls.append(1)
                elif event == "return":
                    returned.append(arg)

        previous = sys.getprofile()
        sys.setprofile(profile)
        try:
            result = observation.observe(parser, data, at["init_rva"])
        finally:
            sys.setprofile(previous)
        self.assertEqual(calls, [1])
        self.assertEqual(result["parser_calls"], 1)
        self.assertEqual(set(result), {"schema", "status", "parser_calls", "reason", "exception_type",
                                       "refusal_context", "static_index_admitted", "index"})
        self.assertEqual(result["schema"], "bcrypt-pe-core-013-v1")
        return result, returned

    def policy(self, context, ignored):
        self.assertEqual(context["compatibility_policy"], {
            "id": "AMD64_UNWIND_V1_SET_FPREG_RESERVED_OPINFO_IGNORED",
            "nonzero_opinfo_ignored": ignored,
            "notice": "CONSUMER_COMPATIBILITY_ONLY_WINDOWS_BUILD_UNVERIFIED"})
        self.assertIs(type(context["compatibility_policy"]["nonzero_opinfo_ignored"]), bool)

    def test_success_retains_complete_same_index_object_with_one_call(self):
        data, at = fixture(certificate=True)
        result, returned = self.observed(data, at)
        self.assertEqual(result["status"], "indexed")
        self.assertIs(result["index"], returned[0])
        self.assertTrue(result["static_index_admitted"])
        self.assertEqual((result["reason"], result["exception_type"], result["refusal_context"]), (None, None, None))
        self.assertIs(result["index"]["iat_index"][0], result["index"]["imports"][0]["symbols"][0])
        self.assertEqual(len(result["index"]["runtime_functions"]), 2)
        self.assertEqual(len(result["index"]["certificates"]), 1)

    def test_early_refusal_never_guesses_an_unavailable_image_policy(self):
        _, at = fixture()
        result, _ = self.observed(b"", at)
        self.assertEqual(result["status"], "parser_refused")
        self.assertEqual(result["reason"], "IMAGE_SIZE_OUT_OF_RANGE")
        self.assertEqual(result["exception_type"], "PeValidationError")
        self.assertIsNone(result["refusal_context"])
        self.assertIsNone(result["index"])
        self.assertFalse(result["static_index_admitted"])
        result, _ = self.observed(b"MZ", at)
        self.assertEqual(result["reason"], "DOS_HEADER_TRUNCATED")
        context = result["refusal_context"]
        self.policy(context, False)
        self.assertEqual((context["root"], context["unwind"]), (None, None))

    def test_v2_info6_refusal_has_exact_bound_root_and_leaf_locals(self):
        data, at, offset = refused_input()
        result, _ = self.observed(data, at)
        self.assertEqual((result["status"], result["reason"]), ("parser_refused", "UNWIND_OPCODE_INVALID"))
        context = result["refusal_context"]
        self.assertEqual(context["root"], {"index": 0, "offset": at["pdata"], "begin": 0x1000,
                                           "end": 0x1080, "unwind": 0x2600})
        self.assertEqual(context["unwind"], {"rva": 0x2600, "offset": offset, "version": 2, "flags": 0,
                                             "count": 1, "frame": 0x65, "slot": 0, "code_offset": 22,
                                             "operation": 0x63, "opcode": 3, "info": 6})
        self.assertEqual([site["function"] for site in context["sites"]],
                         ["inspect_bcrypt_pe", "_Image.exceptions", "_Image.unwind_info", "_require"])
        self.assertEqual(context["traceback_nodes"], 5)
        self.policy(context, False)
        self.assertNotIn("width", context["unwind"])

    def test_later_guard_records_compatibility_use_from_the_same_image(self):
        data, at, _ = anchor.synthetic_image(bytes.fromhex("0116026516631703"))
        result, _ = self.observed(data, at)
        self.assertEqual(result["reason"], "UNWIND_CODE_ORDER_INVALID")
        context = result["refusal_context"]
        self.policy(context, True)
        self.assertEqual((context["unwind"]["slot"], context["unwind"]["info"]), (1, 0))
        self.assertFalse(result["static_index_admitted"])

    def test_chain_keeps_outer_root_and_last_unwind_frame(self):
        data, at = fixture()
        changed = bytearray(put(data, at["pdata"] + 8, 0x2600))
        struct.pack_into("<BBBBIII", changed, at["rdata"] + 0x600,
                         0x21, 0, 0, 0x65, 0x1000, 0x1080, 0x2610)
        changed[at["rdata"] + 0x610:at["rdata"] + 0x618] = put(anchor.MINIMAL_ENVELOPE, 0, 2, "<B")
        result, _ = self.observed(bytes(changed), at)
        context = result["refusal_context"]
        self.assertEqual(context["root"]["unwind"], 0x2600)
        self.assertEqual(context["unwind"]["rva"], 0x2610)
        self.assertEqual(context["unwind"]["version"], 2)
        self.assertEqual(sum(site["function"] == "_Image.unwind_info" for site in context["sites"]), 2)
        self.policy(context, False)

    @staticmethod
    def raising_parser(error):
        def inspect_bcrypt_pe(data, init_rva):
            raise error
        return SimpleNamespace(inspect_bcrypt_pe=inspect_bcrypt_pe,
                               PeValidationError=pe.PeValidationError, _Image=pe._Image)

    def test_unexpected_exception_text_is_never_formatted_or_retained(self):
        class OpaqueError(Exception):
            def __str__(self):
                raise AssertionError("arbitrary exception text must not be touched")
        data, at = fixture()
        result, _ = self.observed(data, at, self.raising_parser(OpaqueError("secret" * 10000)))
        self.assertEqual((result["status"], result["reason"], result["exception_type"]),
                         ("parser_error", "PARSER_EXCEPTION", "Exception"))
        self.assertEqual((result["index"], result["refusal_context"]), (None, None))
        self.assertFalse(result["static_index_admitted"])
        self.assertLess(len(json.dumps(result)), 512)

    def test_reason_allows_only_bounded_exact_ascii_uppercase_tokens(self):
        data, at = fixture()
        for reason in ("", "lowercase", "HAS SPACE", "\x7f", "A" * 65, "A" * 10000, True, object()):
            error = pe.PeValidationError(reason)
            result, _ = self.observed(data, at, self.raising_parser(error))
            self.assertEqual((result["status"], result["reason"]), ("parser_error", "PARSER_EXCEPTION"))
            self.assertIsNone(result["refusal_context"])
        reason = "_9" * 32
        result, _ = self.observed(data, at, self.raising_parser(pe.PeValidationError(reason)))
        self.assertEqual((result["status"], result["reason"], result["exception_type"]),
                         ("parser_refused", reason, "PeValidationError"))
        self.assertIsNone(result["refusal_context"])

    def test_extraction_has_no_second_inspection_mapping_or_read(self):
        original = observation._context

        def guarded(*args):
            with patch.object(pe, "inspect_bcrypt_pe", side_effect=AssertionError("second inspection")) as inspect:
                with patch.object(pe._Image, "map", side_effect=AssertionError("second map")) as mapping:
                    with patch.object(pe._Image, "read", side_effect=AssertionError("second read")) as read:
                        context = original(*args)
                        self.assertEqual((inspect.call_count, mapping.call_count, read.call_count), (0, 0, 0))
                        return context

        data, at, _ = refused_input()
        with patch.object(observation, "_context", side_effect=guarded):
            result, _ = self.observed(data, at)
        self.assertIsNotNone(result["refusal_context"])

    def test_context_exception_falls_back_without_another_call(self):
        data, at, _ = refused_input()
        with patch.object(observation, "_context", side_effect=RuntimeError("private" * 10000)):
            result, _ = self.observed(data, at)
        self.assertEqual((result["status"], result["reason"]), ("parser_refused", "UNWIND_OPCODE_INVALID"))
        self.assertIsNone(result["refusal_context"])

    def test_context_requires_exact_input_identity_and_bool_policy_state(self):
        data, at, _ = refused_input()
        error, binding = capture(data, at), observation._bind(pe)
        self.assertIsNotNone(observation._context(binding, data, error))
        self.assertIsNone(observation._context(binding, bytes(bytearray(data)), error))
        image = next(node.tb_frame.f_locals["image"] for node in nodes(error)
                     if node.tb_frame.f_code is pe.inspect_bcrypt_pe.__code__)
        image.v1_set_fpreg_nonzero_opinfo_ignored = 1
        self.assertIsNone(observation._context(binding, data, error))
        image.v1_set_fpreg_nonzero_opinfo_ignored = False
        self.assertIsNotNone(observation._context(binding, data, error))

    def test_unrecognized_code_wrong_line_and_duplicate_inspect_are_unavailable(self):
        data, at, _ = refused_input()
        error, binding = capture(data, at), observation._bind(pe)
        original = nodes(error)
        index = next(i for i, node in enumerate(original) if node.tb_frame.f_code is pe._Image.unwind_info.__code__)

        def unwind_info(self):
            return sys._getframe()

        foreign = unwind_info(original[index].tb_frame.f_locals["self"])
        fake = TracebackType(None, foreign, foreign.f_lasti, foreign.f_lineno)
        bad_line = TracebackType(None, original[index].tb_frame, original[index].tb_lasti, 1)
        for replacement in (fake, bad_line):
            changed = original[:index] + [replacement] + original[index + 1:]
            error.__traceback__ = relink(changed)
            self.assertIsNone(observation._context(binding, data, error))
        inspector = next(node for node in original if node.tb_frame.f_code is pe.inspect_bcrypt_pe.__code__)
        error.__traceback__ = relink(original[:index] + [inspector] + original[index:])
        self.assertIsNone(observation._context(binding, data, error))

    def test_traceback_cap_and_complete_metadata_shape_have_an_8kib_bound(self):
        data, at, _ = refused_input()
        error, binding = capture(data, at), observation._bind(pe)
        original = nodes(error)
        leaf = next(node for node in original if node.tb_frame.f_code is pe._Image.unwind_info.__code__)
        padded = original[:-1] + [leaf] * (64 - len(original)) + original[-1:]
        error.__traceback__ = relink(padded)
        context = observation._context(binding, data, error)
        self.assertEqual(context["traceback_nodes"], 64)
        self.assertEqual(len(context["sites"]), 63)
        metadata = {"schema": "bcrypt-pe-core-013-v1", "status": "parser_refused", "parser_calls": 1,
                    "reason": "A" * 64, "exception_type": "PeValidationError", "refusal_context": context,
                    "static_index_admitted": False, "index": None}
        context["sites"] = [{"function": "f" * 32, "line": 0xFFFFFFFF} for _ in range(64)]
        for snapshot in (context["root"], context["unwind"]):
            snapshot.update((key, (1 << 64) - 1) for key in snapshot)
        encoded = json.dumps(metadata, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        self.assertLessEqual(len(encoded), 6100)
        self.assertEqual(observation.MAX_METADATA_BYTES, 8192)
        error.__traceback__ = relink(padded + [leaf])
        self.assertIsNone(observation._context(binding, data, error))

    def test_snapshot_whitelist_uses_only_exact_uint64_scalars(self):
        for value in (True, False, -1, 1 << 64, "1", 1.0, object()):
            self.assertEqual(observation._snapshot({"index": value, "data": object()}, ("index",)), {})
        self.assertEqual(observation._snapshot({"index": (1 << 64) - 1, "other": 0}, ("index",)),
                         {"index": (1 << 64) - 1})


if __name__ == "__main__":
    unittest.main()
