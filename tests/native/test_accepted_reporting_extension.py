"""Negative controls for PR236's exact addition, not relaxation of old fingerprints."""
from pathlib import Path
import unittest
import accepted_reporting_extension as a

ROOT=Path(__file__).resolve().parents[2]


class AcceptedExtension(unittest.TestCase):
    def setUp(self):
        self.text=(ROOT/a.REPORT).read_text(encoding='utf-8')
        self.helper=(ROOT/a.HELPER).read_text(encoding='utf-8')

    def test_whole_accepted_file_restores_exact_prior_assertions(self):
        values={a.REPORT:self.text,'other':'unchanged'};before=dict(values)
        result=a.legacy_view(values,self.helper)
        self.assertEqual(a.PRIOR,a.digest(result[a.REPORT]))
        self.assertEqual(values,before);self.assertEqual('unchanged',result['other'])

    def test_bytes_keep_bytes(self):
        result=a.legacy_view({a.REPORT:self.text.encode()},self.helper)
        self.assertIsInstance(result[a.REPORT],bytes)
        self.assertEqual(a.PRIOR,a.digest(result[a.REPORT].decode()))

    def test_mutated_original_test_is_not_ignored(self):
        with self.assertRaises(ValueError):a.legacy_view({a.REPORT:self.text+'// mutation'},self.helper)

    def test_mutated_new_assertions_are_refused(self):
        with self.assertRaises(ValueError):a.legacy_view({a.REPORT:self.text},self.helper+'// mutation')

    def test_missing_or_duplicate_addition_is_refused(self):
        for line in a.LINES:
            for text in (self.text.replace(line,''),self.text+line):
                with self.subTest(line=line),self.assertRaises(ValueError):a.legacy_view({a.REPORT:text},self.helper)

    def test_unextended_old_file_is_not_accepted_as_current(self):
        old=self.text
        for line in a.LINES:old=old.replace(line,'')
        with self.assertRaises(ValueError):a.legacy_view({a.REPORT:old},self.helper)

    def test_wrong_helper_is_refused(self):
        with self.assertRaises(ValueError):a.legacy_view({a.REPORT:self.text},'')


if __name__=='__main__':unittest.main(verbosity=2)
