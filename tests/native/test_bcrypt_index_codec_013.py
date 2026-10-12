"""Synthetic tests only; never imports a PE inspector or opens image bytes."""

import json
import sys
import unittest
from unittest import mock

try:
    import bcrypt_index_codec_013 as codec
except ModuleNotFoundError as error:
    if error.name != "bcrypt_index_codec_013":
        raise
    codec = None


class CodecTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(codec, "013 bounded JSON codec is not implemented")

    def profile(self, **changes):
        return dict(codec.CORE_LIMITS, **changes)

    def refused(self, reason, function, *args, **kwargs):
        with self.assertRaises(codec.CodecError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.reason, reason)

    def roundtrip(self, value, **changes):
        limits = self.profile(**changes)
        raw = codec.encode(value, limits=limits)
        self.assertEqual(raw, json.dumps(value, ensure_ascii=True, allow_nan=False,
                                        sort_keys=True, separators=(",", ":")).encode("ascii"))
        self.assertEqual(codec.decode(raw, limits=limits), value)
        return raw

    def test_sorted_compact_json_roundtrip(self):
        self.assertEqual(self.roundtrip({"z": [None, True, False], "a": 7}),
                         b'{"a":7,"z":[null,true,false]}')

    def test_integer_endpoints_and_boolean_distinction(self):
        self.roundtrip([-(1 << 63), (1 << 64) - 1, 0, True, False])

    def test_integer_out_of_range_is_refused_both_directions(self):
        for value in (-(1 << 63) - 1, 1 << 64):
            with self.subTest(value=value):
                self.refused("JSON_INTEGER_RANGE", codec.encode, value)
                self.refused("JSON_INTEGER_RANGE", codec.decode, str(value).encode())

    def test_float_and_other_python_types_are_refused(self):
        for value in (0.5, float("nan"), float("inf"), (), b"x", set(), object()):
            with self.subTest(type=type(value).__name__):
                self.refused("JSON_TYPE_INVALID", codec.encode, value)

    def test_subclasses_and_non_string_keys_are_refused(self):
        class CustomDict(dict):
            pass
        class CustomInt(int):
            pass
        for value in (CustomDict(a=1), CustomInt(1), {1: "x"}):
            with self.subTest(value=repr(value)):
                self.refused("JSON_TYPE_INVALID", codec.encode, value)

    def test_duplicate_keys_are_refused_before_json_loads(self):
        with mock.patch.object(codec.json, "loads", side_effect=AssertionError("must not allocate")):
            self.refused("JSON_DUPLICATE_KEY", codec.decode, b'{"x":1,"x":2}')

    def test_escaped_duplicate_keys_are_refused(self):
        self.refused("JSON_DUPLICATE_KEY", codec.decode, b'{"a":1,"\\u0061":2}')

    def test_noncanonical_key_order_and_escape_are_refused(self):
        for raw in (b'{"z":0,"a":1}', b'"\\/"', b'"\\u0061"', b'-0'):
            with self.subTest(raw=raw):
                self.refused("JSON_NON_CANONICAL", codec.decode, raw)

    def test_whitespace_and_invalid_json_are_refused(self):
        for raw in (b' null', b'null ', b'[1,]', b'{"a":}', b'{1:2}', b'01', b'1.0',
                    b'1e2', b'NaN', b'Infinity', b'{}{}', b'"\\x00"', b'"\x01"'):
            with self.subTest(raw=raw):
                self.refused("JSON_SYNTAX", codec.decode, raw)

    def test_decode_requires_bytes_and_ascii_wire(self):
        self.refused("JSON_BYTES_REQUIRED", codec.decode, "null")
        self.refused("JSON_BYTES_REQUIRED", codec.decode, bytearray(b"null"))
        self.refused("JSON_SYNTAX", codec.decode, b'"\xff"')

    def test_direct_and_mutual_cycles_are_refused(self):
        direct = []
        direct.append(direct)
        first, second = {}, {}
        first["next"], second["next"] = second, first
        for value in (direct, first):
            self.refused("JSON_CYCLE", codec.encode, value)

    def test_shared_symbol_is_serialized_twice_and_charged_twice(self):
        symbol = {"name": "x"}
        self.assertEqual(self.roundtrip([symbol, symbol], nodes=7),
                         b'[{"name":"x"},{"name":"x"}]')
        self.refused("JSON_NODE_LIMIT", codec.encode, [symbol, symbol],
                     limits=self.profile(nodes=6))

    def test_exact_node_limit_includes_object_keys(self):
        self.roundtrip({"a": [1, 2]}, nodes=5)
        for function, value in ((codec.encode, {"a": [1, 2]}),
                                (codec.decode, b'{"a":[1,2]}')):
            self.refused("JSON_NODE_LIMIT", function, value, limits=self.profile(nodes=4))

    def test_node_limit_precedes_json_object_allocation(self):
        with mock.patch.object(codec.json, "loads", side_effect=AssertionError("must not allocate")):
            self.refused("JSON_NODE_LIMIT", codec.decode, b'[0,0,0,0]',
                         limits=self.profile(nodes=4))

    def test_exact_character_limit_counts_keys(self):
        self.roundtrip({"ab": "xy"}, characters=4)
        for function, value in ((codec.encode, {"ab": "xy"}), (codec.decode, b'{"ab":"xy"}')):
            self.refused("JSON_CHARACTER_LIMIT", function, value,
                         limits=self.profile(characters=3))

    def test_exact_encoded_byte_limit_includes_escaping(self):
        raw = b'"\\u007f"'
        self.assertEqual(self.roundtrip("\x7f", bytes=len(raw)), raw)
        for function, value in ((codec.encode, "\x7f"), (codec.decode, raw)):
            self.refused("JSON_BYTE_LIMIT", function, value,
                         limits=self.profile(bytes=len(raw) - 1))

    def test_del_controls_bmp_and_astral_escape_widths(self):
        cases = (("\x7f", b'"\\u007f"'), ("\x00", b'"\\u0000"'),
                 ("\n", b'"\\n"'), ("\u00e9", b'"\\u00e9"'),
                 ("\U0001f600", b'"\\ud83d\\ude00"'), ("\ud800", b'"\\ud800"'))
        for value, expected in cases:
            with self.subTest(value=repr(value)):
                self.assertEqual(self.roundtrip(value), expected)

    def test_all_ascii_characters_match_standard_canonical_encoding(self):
        self.roundtrip("".join(map(chr, range(128))))

    def test_depth_boundary_both_directions(self):
        self.roundtrip([[[0]]], depth=4)
        for function, value in ((codec.encode, [[[0]]]), (codec.decode, b'[[[0]]]')):
            self.refused("JSON_DEPTH_LIMIT", function, value, limits=self.profile(depth=3))

    def test_depth_guard_precedes_json_object_allocation(self):
        with mock.patch.object(codec.json, "loads", side_effect=AssertionError("must not allocate")):
            self.refused("JSON_DEPTH_LIMIT", codec.decode, b'[' * 100 + b'0' + b']' * 100)

    def test_individual_string_boundary(self):
        self.roundtrip("x" * 4096)
        self.refused("JSON_STRING_LIMIT", codec.encode, "x" * 4097)
        self.refused("JSON_STRING_LIMIT", codec.decode, b'"' + b'x' * 4097 + b'"')

    def test_oversized_token_stops_before_json_object_allocation(self):
        with mock.patch.object(codec.json, "loads", side_effect=AssertionError("must not allocate")):
            self.refused("JSON_STRING_LIMIT", codec.decode, b'"' + b'x' * 100000 + b'"')

    def test_key_string_boundary(self):
        self.roundtrip({"k" * 64: 0})
        for function, value in ((codec.encode, {"k" * 65: 0}),
                                (codec.decode, b'{"' + b'k' * 65 + b'":0}')):
            self.refused("JSON_KEY_LIMIT", function, value)

    def test_small_value_string_profile_keeps_independent_key_limit(self):
        try:
            self.roundtrip({"k" * 64: "x"}, string_characters=1)
        except codec.CodecError as error:
            self.fail("independent key limit must remain supported: " + error.reason)

    def test_object_member_boundary(self):
        self.roundtrip({str(i): i for i in range(64)})
        value = {str(i): i for i in range(65)}
        self.refused("JSON_MEMBER_LIMIT", codec.encode, value)
        raw = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
        self.refused("JSON_MEMBER_LIMIT", codec.decode, raw)

    def test_array_item_boundary(self):
        self.roundtrip([0] * 3, array_items=3)
        for function, value in ((codec.encode, [0] * 4), (codec.decode, b'[0,0,0,0]')):
            self.refused("JSON_ITEM_LIMIT", function, value, limits=self.profile(array_items=3))

    def test_public_maximum_array_boundary(self):
        self.roundtrip([0] * 65536)
        self.refused("JSON_ITEM_LIMIT", codec.encode, [0] * 65537)

    def test_work_guard_and_decode_reencode_share_one_budget(self):
        # Encoding null: two node visits plus four byte-buffer passes = 18 units.
        self.assertEqual(codec.encode(None, limits=self.profile(work_units=18)), b'null')
        self.refused("JSON_WORK_LIMIT", codec.encode, None,
                     limits=self.profile(work_units=17))
        # Decoding null adds three four-byte passes plus two node visits.
        self.assertIsNone(codec.decode(b'null', limits=self.profile(work_units=32)))
        self.refused("JSON_WORK_LIMIT", codec.decode, b'null',
                     limits=self.profile(work_units=31))

    def test_structure_guard_is_before_materialization(self):
        limits = self.profile(structure_bytes=1)
        self.refused("JSON_STRUCTURE_LIMIT", codec.encode, {}, limits=limits)
        with mock.patch.object(codec.json, "loads", side_effect=AssertionError("must not allocate")):
            self.refused("JSON_STRUCTURE_LIMIT", codec.decode, b'{}', limits=limits)

    def test_encode_cannot_delegate_to_unbounded_json_dumps(self):
        with mock.patch.object(codec.json, "dumps", side_effect=AssertionError("unbounded encoding")):
            self.assertEqual(codec.encode({"a": [1]}), b'{"a":[1]}')

    def test_limits_require_exact_known_positive_integers(self):
        invalid = [self.profile(bytes=0), self.profile(bytes=True), self.profile(bytes=-1),
                   self.profile(bytes=codec.REPORT_LIMITS["bytes"] + 1),
                   self.profile(extra=1), {"bytes": 100}]
        for limits in invalid:
            with self.subTest(limits=limits):
                self.refused("JSON_LIMITS_INVALID", codec.encode, None, limits=limits)

    def test_four_fixed_profiles_and_report_headroom(self):
        self.assertEqual(codec.CORE_LIMITS["bytes"], 67108864)
        self.assertEqual(codec.REPORT_LIMITS["bytes"], 67125248)
        self.assertEqual(codec.MANIFEST_LIMITS["bytes"], 32768)
        self.assertEqual(codec.MARKER_LIMITS["bytes"], 1024)
        for limits in (codec.CORE_LIMITS, codec.REPORT_LIMITS,
                       codec.MANIFEST_LIMITS, codec.MARKER_LIMITS):
            self.assertEqual(codec.decode(codec.encode({"a": 1}, limits=limits), limits=limits), {"a": 1})

    def test_utf16_pair_spelling_is_canonicalized_without_data_loss(self):
        raw = codec.encode("\U0001f600")
        self.assertEqual(raw, b'"\\ud83d\\ude00"')
        self.refused("JSON_NON_CANONICAL", codec.decode, b'"\\uD83D\\uDE00"')

    def test_supported_cpython_object_layout_bounds(self):
        self.assertEqual((sys.implementation.name, sys.version_info[:2]), ("cpython", (3, 12)))
        for n in range(65):
            self.assertLessEqual(sys.getsizeof({str(i): i for i in range(n)}), 128 + 64 * n)
        growing = []
        for n in range(65537):
            self.assertLessEqual(sys.getsizeof(growing), 128 + 16 * n)
            growing.append(None)
        for value in ("", "x" * 4096, "\U0001f600" * 4096, "\ud800" * 4096):
            self.assertLessEqual(sys.getsizeof(value), 80 + 4 * len(value))


if __name__ == "__main__":
    unittest.main()
