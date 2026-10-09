"""Fixed real-pipe wiring tests; no real codec or cost measurement invocation."""
import copy
import hashlib
import importlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


class CostDriverTests(unittest.TestCase):
    def setUp(self):
        self.driver = importlib.import_module("analysis_substitute_driver")
        self.fixture = importlib.import_module("analysis_fixed_substitute")
        self.assertIn("cost_time", self.fixture.MODES, "missing fixed cost semantics integration")
        self.semantics = importlib.import_module("analysis_cost_semantics")
        parent = os.environ.get("MQB_DRIVER_EVIDENCE_ROOT")
        if parent:
            self.root = Path(tempfile.mkdtemp(prefix=self._testMethodName + "-", dir=parent))
        else:
            temporary = tempfile.TemporaryDirectory()
            self.addCleanup(temporary.cleanup)
            self.root = Path(temporary.name)

    def protocol(self, modes):
        return self.driver.freeze_protocol(protocol_id=self._testMethodName,
            calls=[{"call_id": f"cost-{i}", "mode": mode} for i, mode in enumerate(modes)])

    def run_protocol(self, protocol, name="bundle"):
        root = self.root / name
        result = self.driver.run_frozen_substitutes(root, protocol=protocol,
            expected_protocol_sha256=protocol.sha256, seal_path=self.root / (name + ".seal"))
        return root, result

    def audit(self, root, protocol, result):
        return self.driver.audit_driver_evidence(root, protocol=protocol,
            expected_protocol_sha256=protocol.sha256,
            expected_driver_seal_sha256=result["driver_seal_sha256"])

    def test_four_fixed_modes_pass_real_pipes_and_final_business_audit(self):
        modes = ["cost_time", "cost_allocation", "cost_bad_tail_time", "cost_bad_tail_allocation"]
        protocol = self.protocol(modes)
        self.assertIn("cost_semantics", protocol.document()["sources"])
        root, result = self.run_protocol(protocol)
        self.assertTrue(result["complete"], result)
        self.assertEqual(4, result["planned_semantic_calls"])
        self.assertEqual(4, result["verified_semantic_calls"])
        self.assertEqual(20, result["verified_cost_observations"])
        self.assertEqual(4, result["launched_calls"])
        self.assertEqual(4, result["reaped_calls"])
        self.assertTrue(all(value == 0 and type(value) is int
                            for value in result["real_measurement_budget"].values()))
        for i, mode in enumerate(modes):
            fixed = self.fixture.fixture_spec(mode)
            call = protocol.document()["calls"][i]
            self.assertEqual("cost-" + str(i), call["semantic_contract"]["call_id"])
            for role in ("stdout", "stderr"):
                for folder in ("spool", "capture/raw", "capture/backup"):
                    self.assertEqual(fixed[role], (root / folder / f"{i:06d}.{role}.bin").read_bytes())
            record = json.loads((root / "calls" / f"{i:06d}.json").read_bytes())
            self.assertTrue(record["reaped"])
            self.assertEqual(0, record["returncode"])
        self.assertEqual(result, self.audit(root, protocol, result))

    def test_intact_semantically_invalid_bytes_preserve_both_streams_and_stop_next_call(self):
        for mode in ("cost_invalid_time", "cost_invalid_allocation"):
            with self.subTest(mode=mode):
                protocol = self.protocol([mode, "cost_time"])
                root, result = self.run_protocol(protocol, mode)
                self.assertFalse(result["complete"])
                self.assertIn("cost_semantics:", result["first_failure"])
                self.assertEqual(1, result["launch_attempts"])
                self.assertEqual(1, result["launched_calls"])
                self.assertEqual(1, result["reaped_calls"])
                self.assertEqual(0, result["verified_semantic_calls"])
                self.assertFalse((root / "calls/000001.json").exists())
                fixed = self.fixture.fixture_spec(mode)
                for role in ("stdout", "stderr"):
                    for folder in ("spool", "capture/raw", "capture/backup"):
                        self.assertEqual(fixed[role], (root / folder / f"000000.{role}.bin").read_bytes())
                self.assertFalse(self.audit(root, protocol, result)["complete"])

    def test_final_gate_reparses_semantics_even_if_first_validation_was_wrong(self):
        protocol = self.protocol(["cost_invalid_time"])
        original = self.semantics.validate_cost_output
        calls = []

        def admit_once(*args, **kwargs):
            calls.append(1)
            if len(calls) == 1:
                return {"complete": True, "verified_observations": 5}
            return original(*args, **kwargs)

        with patch.object(self.semantics, "validate_cost_output", side_effect=admit_once):
            root, result = self.run_protocol(protocol)
        self.assertGreaterEqual(len(calls), 2)
        self.assertFalse(result["complete"])
        self.assertIn("cost_semantics:", result["first_failure"])
        # Original-pipe transport and the unchanged identity-only capture were
        # both successful. Only the fresh business check can reject this case.
        self.assertTrue(json.loads((root / "calls/000000.json").read_bytes())["transport_ok"])
        self.assertTrue(json.loads((root / "capture/result.json").read_bytes())["complete"])
        self.assertEqual(1, result["launched_calls"])

    def test_rehashed_forged_contract_cannot_relax_the_closed_fixture_specification(self):
        protocol = self.protocol(["cost_time"])
        changed = protocol.document()
        changed["calls"][0]["semantic_contract"]["expected_consumed"] += 1
        raw = self.driver.transport.canonical_bytes(changed)
        forged = self.driver.FrozenProtocol(raw)
        with patch.object(self.driver.transport.subprocess, "Popen", side_effect=AssertionError("launch")):
            with self.assertRaises(self.driver.DriverError):
                self.run_protocol(forged)
        self.assertFalse((self.root / "bundle").exists())

    def test_adapter_source_drift_rejects_before_launch_or_output_creation(self):
        protocol = self.protocol(["cost_time"])
        original = self.driver._source_identities

        def changed_sources():
            value = copy.deepcopy(original())
            value["cost_semantics"]["sha256"] = "0" * 64
            return value

        with patch.object(self.driver, "_source_identities", side_effect=changed_sources), \
                patch.object(self.driver.transport.subprocess, "Popen", side_effect=AssertionError("launch")):
            with self.assertRaises(self.driver.DriverError):
                self.run_protocol(protocol)
        self.assertFalse((self.root / "bundle").exists())

    def test_contract_document_copy_cannot_mutate_frozen_admission_policy(self):
        protocol = self.protocol(["cost_time"])
        before = protocol.canonical
        copy_of_document = protocol.document()
        copy_of_document["calls"][0]["semantic_contract"]["prepared"]["checks"] = 0
        copy_of_document["real_measurement_budget"]["codec"] = 100
        self.assertEqual(before, protocol.canonical)
        self.assertEqual(hashlib.sha256(before).hexdigest(), protocol.sha256)
        self.assertEqual(115, protocol.document()["calls"][0]["semantic_contract"]["prepared"]["checks"])


if __name__ == "__main__":
    unittest.main()
