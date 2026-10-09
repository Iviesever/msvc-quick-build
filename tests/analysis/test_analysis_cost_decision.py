"""The separately frozen preparation is a refusal of real execution."""
import hashlib
import importlib
import json
from pathlib import Path
import unittest


class CostDecisionTests(unittest.TestCase):
    def setUp(self):
        try:
            self.decision = importlib.import_module("analysis_cost_decision")
        except ModuleNotFoundError as error:
            if error.name != "analysis_cost_decision":
                raise
            self.fail("missing independent frozen decision preparation verifier")
        self.raw = (Path(__file__).resolve().parents[2] /
                    "docs/ANALYSIS_COST_DECISION_PREPARATION.json").read_bytes()
        self.sha = hashlib.sha256(self.raw).hexdigest()

    def test_exact_frozen_preparation_verifies_without_granting_execution(self):
        result = self.decision.audit_preparation(self.raw, expected_sha256=self.sha)
        self.assertTrue(result["preparation_valid"])
        self.assertEqual("PREPARATION_ONLY_BLOCKED", result["status"])
        self.assertIs(result["real_execution_authorized"], False)
        self.assertEqual("HOLD", result["product_cost_status"])
        self.assertEqual(self.sha, result["protocol_sha256"])
        self.assertTrue(result["blockers"])
        self.assertTrue(all(type(n) is int and n == 0 for n in result["authorized_real_budgets"].values()))

    def test_a_new_hash_cannot_turn_this_preparation_into_an_execution_protocol(self):
        original = json.loads(self.raw)
        changes = [lambda x: x.update(real_execution_authorized=True),
                   lambda x: x.update(status="READY"),
                   lambda x: x["authorized_real_budgets"].update(codec_measurement_processes=100),
                   lambda x: x.update(blockers=[]),
                   lambda x: x["future_selection"].update(purpose="repeat missing five")]
        for change in changes:
            with self.subTest(change=change):
                value = json.loads(self.raw)
                change(value)
                raw = json.dumps(value).encode()
                with self.assertRaises(self.decision.DecisionPreparationError):
                    self.decision.audit_preparation(raw, expected_sha256=hashlib.sha256(raw).hexdigest())
        self.assertEqual(original, json.loads(self.raw))

    def test_external_hash_raw_type_and_input_limit_are_required(self):
        for raw, expected in [(self.raw, "0" * 64), (self.raw, True), (self.raw, "ABC"),
                              (self.raw.decode(), self.sha), (b"x" * (32768 + 1), self.sha)]:
            with self.subTest(raw_type=type(raw), expected=expected):
                with self.assertRaises((ValueError, self.decision.DecisionPreparationError)):
                    self.decision.audit_preparation(raw, expected_sha256=expected)

    def test_returned_lists_and_budgets_are_copies_of_the_frozen_preparation(self):
        first = self.decision.audit_preparation(self.raw, expected_sha256=self.sha)
        first["blockers"].clear()
        first["authorized_real_budgets"]["codec_measurement_processes"] = 100
        later = self.decision.audit_preparation(self.raw, expected_sha256=self.sha)
        self.assertTrue(later["blockers"])
        self.assertEqual(0, later["authorized_real_budgets"]["codec_measurement_processes"])
        self.assertIs(later["real_execution_authorized"], False)


if __name__ == "__main__":
    unittest.main()
