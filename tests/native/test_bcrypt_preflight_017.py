"""Pure synthetic metadata tests; no image, decoder, fixture or network access."""
from __future__ import annotations

import copy
import hashlib
import unittest
from unittest import mock

import bcrypt_preflight_017 as m


def fixture():
    row = dict(begin_file_offset=m.START, begin_rva=m.START, end_file_offset=m.END,
               end_rva=m.END, index=302, section_index=0, section_name=".text",
               table_entry_file_offset=142888, unwind_info_rva=m.UNWIND)
    unwind = dict(bytes=8, chain=None, chain_depth=1, code_slots=1, file_offset=m.UNWIND,
                  flags=0, frame_offset=0, frame_register=0, handler_file_offset=None,
                  handler_rva=None, prolog_size=4, raw_hex="0104010004420000", rva=m.UNWIND, version=1)
    rows = [dict(begin_rva=4096 + n * 2, end_rva=4097 + n * 2) for n in range(390)]
    rows[302] = row
    sections = [dict(virtual_address=4096, virtual_size=106133, raw_size=106496,
                     file_backed_size=106133, raw_file_offset=4096, index=0,
                     name=".text", characteristics=0x60000020),
                dict(virtual_address=0x1B000, virtual_size=0x6000, raw_size=0x6000,
                     file_backed_size=0x6000, raw_file_offset=0x1B000, index=1,
                     name=".rdata", characteristics=0x40000040)]
    index = dict(schema="bcrypt-pe-index-013-v1", bytes=m.IMAGE_BYTES, sha256=m.IMAGE_SHA256,
                 header=dict(image_base=m.IMAGE_BASE, machine=0x8664, optional_header_magic=0x20B),
                 sections=sections, runtime_functions=rows, unwind_info=[unwind],
                 data_directories=[{}, {}, {}, dict(name="EXCEPTION", file_offset=139264, bytes=4680)])
    # These are previously admitted 016 records, not new 017 machine-code bytes.
    hexes = ["41b988070000", "4c8d0597e80000", "488d15c8e80000", "8bcf", "e8297bffff",
             "8bdf", "41b97c000000", "eb0e", "41b985000000", "eb06", "41b98e000000",
             "488d1546e40000", "8bcf", "4c8d0591e70000", "e8fc7affff", "e8f3350000",
             "90", "e9567dffff"]
    records, cursor = [], 0xDB3C
    for raw in hexes:
        records.append(dict(raw_hex=raw, length=len(raw)//2, rva=cursor,
                            file_offset=cursor, preferred_vma=m.IMAGE_BASE+cursor))
        cursor += len(raw)//2
    old_target = dict(file_start=0xDB3C, file_end=0xDB8F, rva_start=0xDB3C, rva_end=0xDB8F,
                      section_index=0, runtime_function_index=252, preferred_vma_start=m.IMAGE_BASE+0xDB3C,
                      sha256=hashlib.sha256(bytes.fromhex("".join(hexes))).hexdigest())
    caller = dict(status="complete_disassembly", source_commit=m.SOURCE_016, child_returncode=0,
                  child_reaped=True, native_execution_performed=False, instructions=records, target=old_target)
    runtime = dict(decision="ADMIT_ACTUAL_STATIC_RESULT_016", source_commit=m.SOURCE_016,
                   static_result_admitted=True, open_findings=dict(C=0, I=0, M=0),
                   instructions=copy.deepcopy(records), target=copy.deepcopy(old_target))
    closure = dict(decision="ADMIT_FINAL_CLOSURE_016", final_feature_commit=m.BASELINE,
                   source_commit=m.SOURCE_016, open_findings=dict(C=0, I=0, M=0),
                   final_feature_tree="2a6a31926ca9b12039b31bcc6f7f26fe7d409d13",
                   runtime_review_admission=dict(sha256=m.PINS["runtime"][1]))
    proposal = dict(exact_runtime_function_row=copy.deepcopy(row), exact_unwind_record=copy.deepcopy(unwind),
                    proposed_target=dict(file_start=m.START, file_end=m.END, rva_start=m.START,
                        rva_end=m.END, section_index=0, runtime_function_index=302,
                        preferred_vma_start=m.IMAGE_BASE+m.START, sha256=None),
                    proposed_target_bytes=103, selection_identity_admitted=False, new_execution_admitted=False,
                    source_call_instruction=dict(raw_hex=m.CALL_HEX, rva=m.CALL_RVA, destination_rva=m.START))
    return dict(index=dict(schema="bcrypt-pe-index-013-report-v1", status="complete_index",
                          static_index_admitted=True, core=dict(index=index, status="indexed", static_index_admitted=True)),
                caller=caller, runtime=runtime, closure=closure, proposal=proposal)


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.d = fixture()
        self.i = self.d["index"]["core"]["index"]

    def reject(self):
        with self.assertRaises(m.Refused):
            m._validate(self.d)

    def test_103_byte_extent_not_old_83(self):
        r = m._validate(self.d)
        self.assertEqual(r["candidate_bytes"], 103)
        self.assertEqual(r["required_future_snapshot_checks"][1]["raw_hex"], "7c110100e31101004cdd0100")

    def test_positive_never_grants_execution(self):
        r = m._validate(self.d)
        for key in ("execution_admitted", "selection_identity_admitted", "native_execution_admitted"):
            self.assertIs(r[key], False)
        self.assertIsNone(r["candidate_sha256"])
        self.assertEqual(r["image_content_reads"], 0)

    def test_source_only_baseline_refused(self):
        self.d["closure"]["final_feature_commit"] = m.SOURCE_016; self.reject()

    def test_wrong_closure_tree_refused(self):
        self.d["closure"]["final_feature_tree"] = "0"*40; self.reject()

    def test_open_prior_finding_refused(self):
        self.d["runtime"]["open_findings"]["I"] = 1; self.reject()

    def test_boolean_cannot_impersonate_zero_exit(self):
        self.d["caller"]["child_returncode"] = False; self.reject()

    def test_missing_reap_refused(self):
        self.d["caller"]["child_reaped"] = False; self.reject()

    def test_caller_raw_byte_corruption_refused(self):
        for key in ("caller", "runtime"):
            self.d[key]["instructions"][15]["raw_hex"] = "e863360000"
        self.reject()

    def test_caller_gap_refused(self):
        for key in ("caller", "runtime"):
            self.d[key]["instructions"][4]["rva"] += 1
        self.reject()

    def test_caller_address_domains_not_interchangeable(self):
        for key in ("caller", "runtime"):
            self.d[key]["instructions"][0]["preferred_vma"] = 0xDB3C
        self.reject()

    def test_runtime_disagreement_refused(self):
        self.d["runtime"]["instructions"][0]["length"] = 5; self.reject()

    def test_adjacent_111ec_not_selected(self):
        self.i["runtime_functions"][302]["begin_rva"] = 0x111EC; self.reject()

    def test_widened_extent_refused(self):
        self.i["runtime_functions"][302]["end_rva"] += 1; self.reject()

    def test_ambiguous_containment_refused(self):
        self.i["runtime_functions"][303] = dict(begin_rva=m.START, end_rva=m.END); self.reject()

    def test_zero_fill_not_file_backed(self):
        self.i["sections"][0]["file_backed_size"] = m.START-4096; self.reject()

    def test_mapping_alias_refused(self):
        self.i["sections"].append(copy.deepcopy(self.i["sections"][0])); self.reject()

    def test_section_not_executable(self):
        self.i["sections"][0]["characteristics"] = 0x40000040; self.reject()

    def test_bad_pdata_offset(self):
        self.i["runtime_functions"][302]["table_entry_file_offset"] += 12; self.reject()

    def test_short_pdata_table(self):
        self.i["data_directories"][3]["bytes"] = 302*12; self.reject()

    def test_old_chaininfo_profile_refused(self):
        self.i["unwind_info"][0]["flags"] = 4; self.reject()

    def test_handler_refused(self):
        self.i["unwind_info"][0]["handler_rva"] = 123; self.reject()

    def test_duplicate_unwind_refused(self):
        self.i["unwind_info"].append(copy.deepcopy(self.i["unwind_info"][0])); self.reject()

    def test_unwind_byte_corruption_refused(self):
        self.i["unwind_info"][0]["raw_hex"] = "0104010004520000"; self.reject()

    def test_unreviewed_target_hash_refused(self):
        self.d["proposal"]["proposed_target"]["sha256"] = "0"*64; self.reject()

    def test_proposal_execution_flag_refused(self):
        self.d["proposal"]["new_execution_admitted"] = True; self.reject()

    def test_archived_runtime_join_refused(self):
        self.d["closure"]["runtime_review_admission"]["sha256"] = "0"*64; self.reject()

    def test_public_api_checks_pins_before_json(self):
        inputs = {role: b" "*size for role, (size, _) in m.PINS.items()}
        with mock.patch.object(m, "strict_json") as decode:
            with self.assertRaises(m.Refused): m.preflight(inputs)
            decode.assert_not_called()

    def test_extra_image_role_refused(self):
        with self.assertRaises(m.Refused): m.preflight({"image": b""})

    def test_strict_duplicate_json(self):
        with self.assertRaises(m.Refused): m.strict_json(b'{"x":1,"x":2}')

    def test_strict_nan_json(self):
        with self.assertRaises(m.Refused): m.strict_json(b'{"x":NaN}')

    def test_json_exponent_overflow_is_nonfinite(self):
        with self.assertRaises(m.Refused): m.strict_json(b'{"x":1e9999}')

    def test_strict_non_object_json(self):
        with self.assertRaises(m.Refused): m.strict_json(b'[]')

    def test_nested_typed_equality(self):
        with self.assertRaises(m.Refused): m.same({"x": [True]}, {"x": [1]}, "typed")


if __name__ == "__main__":
    unittest.main()
