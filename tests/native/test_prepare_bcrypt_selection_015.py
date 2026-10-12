"""Fake lifecycle tests: no image file, GNU or child process is opened."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

PATH = Path(__file__).with_name('prepare_bcrypt_selection_015.py')


class Output:
    failure = None
    clock = None
    instances = []

    def __init__(self, path):
        if self.failure == 'create_directory':
            raise OSError('constructed directory collision')
        self.files, self.closed = {}, False
        self.instances.append(self)

    def create(self, name, raw, cap):
        if name in self.files or len(raw) > cap:
            raise ValueError('constructed output refusal')
        self.files[name] = raw
        if self.failure == name:
            raise OSError('constructed persistence failure')

    def durable_readback(self, expected):
        if self.failure == 'readback':
            raise OSError('constructed full readback failure')
        if self.files != expected:
            raise ValueError('constructed mismatch')

    def close(self):
        self.closed = True
        if self.failure == 'late_close':
            self.clock[0] = 8_000_000_001


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATH.is_file(), '015 needs a separately consumed selection lifecycle')
        spec = importlib.util.spec_from_file_location('prepare_test_subject_015', PATH)
        self.subject = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.subject)
        self.now = [1]
        Output.failure, Output.clock, Output.instances = None, self.now, []
        self.raws = {'image': b'i' * 200, 'index': b'{}', 'admission': b'{}',
                     'caller': b'{}', 'python': b'python'}
        refs = {name: {'path': '/constructed/' + name, 'bytes': len(raw),
                      'sha256': hashlib.sha256(raw).hexdigest()}
                for name, raw in self.raws.items()}
        self.manifest = {
            'phase': 'selection', 'mode': 'synthetic', 'call_id': '015-synthetic-prepare',
            'baseline_commit': '6' * 40, 'source_commit': None, 'output_dir': '/constructed/out',
            'identities': refs, 'provenance': {}, 'sources': {}, 'aliases': [],
            'absent_paths': [], 'target': {'file_start': 1, 'file_end': 168,
                'rva_start': 2, 'rva_end': 169, 'preferred_vma_start': 3,
                'runtime_function_index': 0, 'section_index': 0, 'sha256': None}}
        self.manifest_raw = self.subject.encode_json(self.manifest, 32768)
        self.manifest_sha = hashlib.sha256(self.manifest_raw).hexdigest()

    def run_case(self, changed_stat=False, identity_failure=False):
        reads, selected = [], []

        def verify(record, cap, keep=False):
            reads.append((record['path'], keep))
            if identity_failure and record['path'].endswith('/image'):
                raise OSError('constructed identity failure')
            role = record['path'].rsplit('/', 1)[-1]
            return self.raws.get(role, b'') if keep else None

        def select(*args):
            selected.append(args)
            return b'N' * 167

        calls = [0]
        def state(_):
            calls[0] += 1
            return ('changed' if changed_stat and calls[0] > 1 else 'same',)

        with patch.object(self.subject, 'validate_manifest', lambda m: m), \
             patch.object(self.subject, 'verify_environment', lambda m: None), \
             patch.object(self.subject, 'verify_identity', side_effect=verify), \
             patch.object(self.subject, 'select_target', side_effect=select), \
             patch.object(self.subject, '_image_state', side_effect=state), \
             patch.object(self.subject, 'PreparationDirectory', Output), \
             patch.object(self.subject.time, 'monotonic_ns', side_effect=lambda: self.now[0]):
            result = self.subject.observe(copy.deepcopy(self.manifest), self.manifest_raw,
                                          '/constructed/manifest', self.manifest_sha, 0)
        return result, reads, selected

    def test_success_selects_same_verified_snapshot_and_reads_image_once(self):
        (code, diagnostic), reads, selected = self.run_case()
        self.assertEqual((code, diagnostic), (0, b''))
        self.assertEqual(sum(p.endswith('/image') for p, _ in reads), 1)
        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0][1], self.raws['image'])
        out = Output.instances[0]
        self.assertTrue(out.closed)
        report = json.loads(out.files['report.json'])
        self.assertEqual(report['original_image_full_reads'], 1)
        self.assertTrue(report['image_snapshot_stat_stable'])
        self.assertTrue(report['identities_after_verified'])
        self.assertEqual(report['status'], 'complete_selection')
        self.assertEqual(report['target']['sha256'], hashlib.sha256(b'N' * 167).hexdigest())

    def test_identity_failure_never_calls_selector(self):
        (code, diagnostic), _, selected = self.run_case(identity_failure=True)
        self.assertNotEqual(code, 0)
        self.assertEqual(selected, [])
        self.assertEqual(diagnostic, b'PREPARATION_REFUSED\n')
        self.assertIn('consumed.json', Output.instances[0].files)

    def test_already_consumed_directory_never_reads_inputs(self):
        Output.failure = 'create_directory'
        (code, _), reads, selected = self.run_case()
        self.assertNotEqual(code, 0)
        self.assertEqual((reads, selected), ([], []))

    def test_changed_image_metadata_refuses_without_second_content_read(self):
        (code, _), reads, _ = self.run_case(changed_stat=True)
        self.assertNotEqual(code, 0)
        self.assertEqual(sum(p.endswith('/image') for p, _ in reads), 1)

    def test_persistence_failure_cannot_be_complete(self):
        for name in ('target.bin', 'report.json', 'readback'):
            with self.subTest(name=name):
                Output.failure, Output.instances = name, []
                (code, diagnostic), _, _ = self.run_case()
                self.assertNotEqual(code, 0)
                self.assertEqual(diagnostic, b'PREPARATION_REFUSED\n')
                self.assertTrue(Output.instances[0].closed)

    def test_late_final_close_overrides_stored_complete_candidate(self):
        Output.failure = 'late_close'
        (code, diagnostic), _, _ = self.run_case()
        self.assertNotEqual(code, 0)
        self.assertEqual(diagnostic, b'PREPARATION_REFUSED\n')
        report = json.loads(Output.instances[0].files['report.json'])
        self.assertEqual(report['status'], 'complete_selection')
        self.assertTrue(report['completion_requires_clean_owner_exit'])


if __name__ == '__main__':
    unittest.main()
