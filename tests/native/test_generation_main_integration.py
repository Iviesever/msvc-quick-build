"""Reject one-sided resolutions of the model/reporting integration.

Use the actual combined historical guard on isolated source-map copies. No
native executable, benchmark, workflow or filesystem mutation is performed.
"""
from pathlib import Path
import generation_archive_extension as archive_extension
import unittest

import accepted_reporting_extension as reporting
import test_observed_link_isolation as isolation

ROOT = Path(__file__).resolve().parents[2]
LAYOUT = 'tests/native/assert_cpp_layout.ps1'
DRIVER = 'tests/native/run_native_tests.ps1'


class GenerationMainIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = isolation.read_tree(ROOT)

    def test_exact_combined_source_retains_both_additions(self):
        before = dict(self.source)
        result = isolation.audit(self.source)
        self.assertEqual(96, result['native_programs'])
        self.assertEqual(96, result['production_tus'])
        self.assertFalse(result['clears_hold'])
        self.assertEqual(before, self.source)

    def test_missing_model_program_is_not_hidden_by_legacy_view(self):
        changed = dict(self.source)
        del changed[next(iter(isolation.MODEL_TESTS))]
        with self.assertRaisesRegex(ValueError, 'native inventory'):
            isolation.audit(changed)

    def test_unregistered_extra_program_is_not_filtered_out(self):
        changed = dict(self.source)
        changed['cpp/tests/unregistered_tests.cpp'] = 'int main() {}\n'
        with self.assertRaisesRegex(ValueError, 'native inventory'):
            isolation.audit(changed)

    def test_original_reporting_assertions_still_pinned(self):
        changed = dict(self.source)
        changed[reporting.REPORT] += '\n// altered original test\n'
        with self.assertRaisesRegex(ValueError, 'reporting extension bytes'):
            isolation.audit(changed)

    def test_added_reporting_assertions_still_pinned(self):
        changed = dict(self.source)
        changed[reporting.HELPER] += '\n// altered format cases\n'
        with self.assertRaisesRegex(ValueError, 'reporting extension bytes'):
            isolation.audit(changed)

    def test_reporting_extension_cannot_be_dropped_or_duplicated(self):
        for line in reporting.LINES:
            for value in (self.source[reporting.REPORT].replace(line, ''),
                          self.source[reporting.REPORT] + line):
                changed = dict(self.source)
                changed[reporting.REPORT] = value
                with self.subTest(line=line, value=value[-80:]):
                    with self.assertRaisesRegex(ValueError, 'reporting extension bytes'):
                        isolation.audit(changed)

    def test_both_layout_registrations_remain_mandatory_and_unique(self):
        anchors = ((", 'storage_report_format_cases.hpp'", 'reporting layout'),
                   ("\n        'ArtifactGenerationModel.cpp',", 'snapshot layout'),
                   ("\n        'artifact_generation_model_tests.cpp',", 'snapshot layout'))
        for anchor, reason in anchors:
            self.assertEqual(1, self.source[LAYOUT].count(anchor))
            for value in (self.source[LAYOUT].replace(anchor, ''),
                          self.source[LAYOUT] + anchor):
                changed = dict(self.source)
                changed[LAYOUT] = value
                with self.subTest(anchor=anchor, value=value[-80:]):
                    with self.assertRaisesRegex(ValueError, reason):
                        isolation.audit(changed)

    def test_driver_cannot_silently_drop_the_model_program(self):
        changed = dict(self.source)
        changed[DRIVER] = changed[DRIVER].replace('97', '95')
        with self.assertRaisesRegex(ValueError, 'native driver policy'):
            isolation.audit(changed)


if __name__ == '__main__':
    unittest.main(verbosity=2)
