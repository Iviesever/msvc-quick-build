"""Offline metadata regressions; synthetic byte fixtures, no native launcher."""
from pathlib import Path
import tempfile
import unittest

import noop_identity_slots as d
import test_noop_identity_slots as controls


def replace_slot_sizes(value, sizes):
    """Keep redundant synthetic records consistent so only byte identity fails."""
    if isinstance(value, dict):
        if {'size', 'creation_ticks', 'mtime_ticks', 'sha256'} <= value.keys():
            value['size'] = sizes[value['sha256']]
        for child in value.values():
            replace_slot_sizes(child, sizes)
    elif isinstance(value, list):
        for child in value:
            replace_slot_sizes(child, sizes)


def change_claim(root, field, value):
    p = d.b.load(root/'plan.json')
    p[field] = value
    controls.write(root/'plan.json', p)
    host = d.b.load(root/'host.json')
    host[field] = value
    host['plan_sha256'] = d.digest(root/'plan.json')
    controls.write(root/'host.json', host)


class SlotEvidenceValidation(unittest.TestCase):
    def assert_size_rejected(self, value):
        with controls.fixture() as (root, _):
            sizes = {sha: value for sha in d.IMAGES.values()}
            for path in (root/'blocks').glob('*.json'):
                record = d.b.load(path)
                replace_slot_sizes(record, sizes)
                controls.write(path, record)
            with self.assertRaisesRegex(ValueError, 'slot (metadata|size)'):
                d.audit(root)

    def test_zero_sizes_rejected_even_with_consistent_redundant_records(self):
        self.assert_size_rejected(0)

    def test_nonzero_wrong_sizes_rejected(self):
        self.assert_size_rejected(12345)

    def test_boolean_size_rejected(self):
        self.assert_size_rejected(True)

    def test_string_size_rejected(self):
        self.assert_size_rejected('18')

    def test_each_slot_size_is_bound_to_its_own_image(self):
        with controls.fixture() as (root, _):
            left_size = (root/'inputs/baseline.exe').stat().st_size
            right_size = (root/'inputs/candidate.exe').stat().st_size
            self.assertNotEqual(left_size, right_size)
            sizes = {sha: left_size for sha in d.IMAGES.values()}
            for path in (root/'blocks').glob('*.json'):
                record = d.b.load(path)
                replace_slot_sizes(record, sizes)
                controls.write(path, record)
            with self.assertRaisesRegex(ValueError, 'slot size'):
                d.audit(root)

    def test_valid_byte_sizes_keep_all_calls_and_contrasts(self):
        with controls.fixture() as (root, _):
            result = d.audit(root)
            self.assertEqual(48, len(result['calls']))
            self.assertEqual(16, len(result['contrasts']))
            self.assertEqual('HOLD', result['original_decision'])
            self.assertIs(result['may_clear_hold'], False)
            self.assertIs(result['gate_replacement'], False)

    def test_audit_rejects_malformed_review_even_when_host_agrees(self):
        for value in ('NOT_A_COMMIT', 'A'*40, 'a'*39, 'a'*41, '', 'a'*40+'\n'):
            with self.subTest(value=value), controls.fixture() as (root, _):
                change_claim(root, 'reviewed_commit', value)
                with self.assertRaisesRegex(ValueError, 'reviewed commit'):
                    d.audit(root)

    def test_audit_rejects_non_string_review(self):
        for value in (None, True, 123, ['a'*40], {'sha': 'a'*40}):
            with self.subTest(value=value), controls.fixture() as (root, _):
                change_claim(root, 'reviewed_commit', value)
                with self.assertRaisesRegex(ValueError, 'reviewed commit'):
                    d.audit(root)

    def test_audit_rejects_malformed_allocation_even_when_host_agrees(self):
        for value in ('reuse-819', 'pr232-slot-01', 'pr232-slot-0001', '', 'pr232-slot-001\n'):
            with self.subTest(value=value), controls.fixture() as (root, _):
                change_claim(root, 'allocation_label', value)
                with self.assertRaisesRegex(ValueError, 'allocation label'):
                    d.audit(root)

    def test_audit_rejects_non_string_allocation(self):
        for value in (None, True, 123, ['pr232-slot-001']):
            with self.subTest(value=value), controls.fixture() as (root, _):
                change_claim(root, 'allocation_label', value)
                with self.assertRaisesRegex(ValueError, 'allocation label'):
                    d.audit(root)

    def test_prepare_rejects_typed_claims_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)/'not-created'
            for review, label in ((True, 'pr232-slot-001'), ('a'*40, True)):
                with self.subTest(review=review, label=label), self.assertRaises(ValueError):
                    d.prepare(Path(temp)/'unused.zip', root, controls.ROOT, review, label)
                self.assertFalse(root.exists())

    def test_well_formed_claim_is_not_execution_authorization(self):
        with controls.fixture() as (root, _):
            change_claim(root, 'reviewed_commit', 'b'*40)
            change_claim(root, 'allocation_label', 'pr232-slot-002')
            result = d.audit(root)
            self.assertEqual('HOLD', result['original_decision'])
            self.assertIs(result['may_clear_hold'], False)
            self.assertIs(d.b.load(root/'plan.json')['execution_allocated'], False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
