"""Pure constructed index/caller/receipt checks; no original inputs are read."""
import copy
import hashlib
import importlib
import posixpath
import unittest
from unittest import mock
from test_bcrypt_disassembly_model_015 import identity, make_manifest, model

try:
    selection = importlib.import_module('bcrypt_selection_015')
except ModuleNotFoundError as error:
    if error.name != 'bcrypt_selection_015':
        raise
    selection = None


def fixture(phase='selection', size=167):
    m = make_manifest(phase)
    t = m['target']
    t['file_end'], t['rva_end'] = t['file_start'] + size, t['rva_start'] + size
    target = b'\x90' * size
    image = b'X' * t['file_start'] + target + b'Y' * 128
    if phase == 'actual':
        t['sha256'] = hashlib.sha256(target).hexdigest()
    index = {'schema': 'bcrypt-pe-index-013-v1', 'runtime_execution_observed': False,
             'sha256': hashlib.sha256(image).hexdigest(), 'bytes': len(image),
             'header': {'machine': 34404, 'optional_header_magic': 523,
                        'address_of_entrypoint': 0x3000, 'image_base': 0x90000000},
             'entrypoint': {'rva': 0x3000, 'runtime_function_index': 99},
             'sections': [{'index': 0, 'name': '.text', 'raw_file_offset': 32,
                           'raw_size': len(image) - 32, 'file_backed_size': len(image) - 32,
                           'virtual_address': 0x2000, 'characteristics': 0x60000020}],
             'runtime_functions': [{'index': 0, 'section_index': 0, 'section_name': '.text',
                                    'begin_rva': t['rva_start'], 'end_rva': t['rva_end'],
                                    'begin_file_offset': t['file_start'], 'end_file_offset': t['file_end'],
                                    'unwind_info_rva': 0x4000, 'table_entry_file_offset': 256}],
             'unwind_info': [{'rva': 0x4000, 'file_offset': 256, 'bytes': 4,
                              'version': 1, 'flags': 0, 'prolog_size': 0, 'code_slots': 0,
                              'frame_register': 0, 'frame_offset': 0, 'raw_hex': '01000000',
                              'handler_rva': None, 'handler_file_offset': None, 'chain': None, 'chain_depth': 1}]}
    report = {'schema': 'bcrypt-pe-index-013-report-v1', 'status': 'complete_index',
              'static_index_admitted': True,
              'core': {'schema': 'bcrypt-pe-core-013-v1', 'status': 'indexed',
                       'static_index_admitted': True, 'index': index}}
    admission = {'schema': 'bcrypt013-root-joint-runtime-admission-v1',
                 'decision': 'ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013',
                 'status': 'COMPLETE_INDEX_INDEPENDENTLY_REVIEWED',
                 'concrete_next_static_target': {'runtime_function_index': 169, 'function_bytes': 391}}
    call_rva = 0xA612
    call_raw = b'\xe8' + (t['rva_start'] - call_rva - 5).to_bytes(4, 'little', signed=True)
    caller = {'schema': 'bcrypt-disassembly-report-014-v1', 'status': 'complete_disassembly',
              'native_execution_performed': False, 'outgoing_targets_followed': False,
              'instructions': [{'file_offset': 123, 'rva': call_rva, 'preferred_vma': 0x90000000 + call_rva,
                                'length': 5, 'raw_hex': call_raw.hex(), 'assembly': 'call 0x90002000'}]}
    values = [image, report, admission, caller]
    return m, values, target


def encode_inputs(m, values):
    raws = [values[0]] + [model.encode_json(v, 327680 if i == 1 else 32768)
                          for i, v in enumerate(values[1:], 1)]
    for role, raw in zip(('image', 'index', 'admission', 'caller'), raws):
        m['identities'][role] = identity(m['identities'][role]['path'], raw)
    return raws


def receipts(m):
    inputs = {r: copy.deepcopy(m['identities'][r]) for r in ('image', 'index', 'admission', 'caller')}
    primary = {'schema': 'bcrypt-selection-report-015-v1', 'status': 'complete_selection',
               'call_id': '015-synthetic-primary', 'mode': m['mode'], 'baseline_commit': m['baseline_commit'],
               'source_commit': None, 'manifest_sha256': '2' * 64, 'target': copy.deepcopy(m['target']),
               'input_identities': inputs, 'consumed': True, 'original_image_full_reads': 1,
               'selection_from_verified_snapshot': True, 'tool_processes': 0,
               'identities_before_verified': True, 'identities_after_verified': True,
               'image_snapshot_stat_stable': True, 'completion_requires_clean_owner_exit': True}
    raw = model.encode_json(primary, 8192)
    m['identities']['selection'] = identity(m['identities']['selection']['path'], raw)
    review = {'schema': 'bcrypt-selection-review-015-v1', 'decision': 'ADMIT_015_SELECTION_IDENTITY',
              'call_id': '015-synthetic-independent', 'mode': m['mode'], 'baseline_commit': m['baseline_commit'],
              'primary_report': copy.deepcopy(m['identities']['selection']), 'target': copy.deepcopy(m['target']),
              'input_identities': inputs, 'original_image_full_reads': 1,
              'selection_from_verified_snapshot': True, 'independent_of_subject_selector': True,
              'image_snapshot_stat_stable': True, 'primary_owner_exit': 0, 'primary_outer_exit': 0,
              'primary_owner_reaped': True, 'primary_complete_files_verified': True,
              'open_findings': {'C': 0, 'I': 0, 'M': 0}, 'native_execution_performed': False}
    source_dir = posixpath.dirname(m['sources']['bcrypt_disassembly_model_015.py']['path'])
    review['primary_manifest_ref'] = {'path': '/synthetic/selection-manifest-015.json',
                                      'bytes': 1000, 'sha256': primary['manifest_sha256']}
    review['primary_manifest_sha256'] = primary['manifest_sha256']
    review['primary_sources'] = {n: copy.deepcopy(m['sources'][n]) if n in m['sources']
                                 else identity(source_dir + '/' + n, b'constructed preparation source')
                                 for n in model.source_names('selection')}
    review['primary_provenance'] = copy.deepcopy(m['provenance'])
    review['primary_outer_source'] = identity(source_dir + '/run_bounded_cli_015.py', b'constructed outer source')
    review['primary_cli_receipt'] = identity('/synthetic/primary-cli-receipt.json', b'constructed transport receipt')
    owner_dir = posixpath.dirname(m['identities']['selection']['path'])
    outer_dir = '/synthetic/cli-selection'
    target_ref = {'path': owner_dir + '/target.bin',
                  'bytes': m['target']['rva_end'] - m['target']['rva_start'], 'sha256': m['target']['sha256']}
    review['primary_files'] = {
        'owner_consumed': identity(owner_dir + '/consumed.json', b'constructed marker'),
        'owner_target': target_ref, 'owner_report': copy.deepcopy(m['identities']['selection']),
        'outer_consumed': identity(outer_dir + '/consumed.json', b'constructed outer marker'),
        'outer_stdout': identity(outer_dir + '/tool-stdout.bin', b''),
        'outer_stderr': identity(outer_dir + '/tool-stderr.bin', b''),
        'outer_report': identity(outer_dir + '/report.json', b'constructed outer report'),
    }
    review['primary_cli_raw'] = {'stdout': identity('/synthetic/primary-cli-stdout.bin', b'constructed summary\n'),
                                 'stderr': identity('/synthetic/primary-cli-stderr.bin', b'')}
    review['completion_requires_clean_verifier_exit'] = True
    return primary, review


def encode_receipts(m, primary, review):
    one = model.encode_json(primary, 8192)
    two = model.encode_json(review, 16384)
    for role, raw in (('selection', one), ('selection_review', two)):
        m['identities'][role] = identity(m['identities'][role]['path'], raw)
    return one, two


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(model, '015 manifest implementation is absent')
        self.assertIsNotNone(selection, '015 pure selector implementation is absent')

    def test_helper_is_not_entrypoint_and_keeps_old_admission_target(self):
        m, values, target = fixture()
        before = copy.deepcopy(m)
        raws = encode_inputs(m, values)
        frozen = copy.deepcopy(m)
        self.assertEqual(selection.select_target(m, *raws), target)
        self.assertEqual(m, frozen)
        self.assertIsNone(m['target']['sha256'])
        self.assertEqual(values[2]['concrete_next_static_target']['runtime_function_index'], 169)
        self.assertEqual(before['target'], m['target'])

    def test_actual_digest_matches_same_exact_snapshot(self):
        m, values, target = fixture('actual')
        raws = encode_inputs(m, values)
        self.assertEqual(selection.select_target(m, *raws), target)
        m['target']['sha256'] = '0' * 64
        with self.assertRaises(model.Refusal):
            selection.select_target(m, *raws)

    def test_input_hash_failure_precedes_json_decode(self):
        m, values, _ = fixture()
        raws = encode_inputs(m, values)
        raws[0] += b'changed'
        with mock.patch.object(selection, 'decode_json', side_effect=AssertionError('decoded before hashes')):
            with self.assertRaises(model.Refusal):
                selection.select_target(m, *raws)

    def test_exact_167_end_and_separated_file_rva_vma(self):
        m, values, target = fixture()
        observed = selection.select_target(m, *encode_inputs(m, values))
        self.assertIsInstance(observed, bytes)
        self.assertEqual(len(observed), 167)
        self.assertNotEqual(m['target']['file_start'], m['target']['rva_start'])
        self.assertNotEqual(m['target']['rva_start'], m['target']['preferred_vma_start'])
        self.assertEqual(target[-1:], b'\x90')

    def test_broken_section_runtime_unwind_and_header_refuse(self):
        changes = [('header', 'image_base', 0), ('header', 'machine', 0),
                   ('section', 'virtual_address', 0), ('section', 'characteristics', 0),
                   ('function', 'end_rva', 0x20A8), ('function', 'section_index', 1),
                   ('unwind', 'raw_hex', '010000'), ('unwind', 'handler_rva', 123),
                   ('unwind', 'code_slots', True)]
        for group, key, value in changes:
            m, values, _ = fixture()
            index = values[1]['core']['index']
            obj = {'header': index['header'], 'section': index['sections'][0],
                   'function': index['runtime_functions'][0], 'unwind': index['unwind_info'][0]}[group]
            obj[key] = value
            with self.subTest(group=group, key=key), self.assertRaises(model.Refusal):
                selection.select_target(m, *encode_inputs(m, values))

    def test_full_index_admission_and_image_association_are_required(self):
        for case in range(5):
            m, values, _ = fixture()
            if case == 0: values[1]['static_index_admitted'] = False
            elif case == 1: values[1]['core']['static_index_admitted'] = False
            elif case == 2: values[1]['core']['index']['sha256'] = '0' * 64
            elif case == 3: values[1]['core']['index']['runtime_execution_observed'] = True
            else: values[2]['decision'] = 'PENDING'
            with self.subTest(case=case), self.assertRaises(model.Refusal):
                selection.select_target(m, *encode_inputs(m, values))

    def test_direct_call_destination_reconstructed_from_raw_displacement(self):
        for case in range(4):
            m, values, _ = fixture()
            instruction = values[3]['instructions'][0]
            if case == 0: instruction['raw_hex'] = 'e800000000'
            elif case == 1: instruction['preferred_vma'] += 1
            elif case == 2: instruction['length'] = 4
            else: values[3]['instructions'].append(dict(instruction))
            with self.subTest(case=case), self.assertRaises(model.Refusal):
                selection.select_target(m, *encode_inputs(m, values))

    def test_selection_evidence_binds_primary_and_independent_inputs(self):
        m, values, _ = fixture('actual')
        encode_inputs(m, values)
        primary, review = receipts(m)
        raws = encode_receipts(m, primary, review)
        self.assertEqual(selection.validate_selection_evidence(m, *raws), primary)

    def test_transport_receipt_is_separate_from_outer_report(self):
        m, values, _ = fixture('actual')
        encode_inputs(m, values)
        primary, review = receipts(m)
        review['primary_cli_receipt'] = identity('/synthetic/primary-cli-receipt.json',
                                                 b'constructed transport receipt')
        self.assertNotEqual(review['primary_cli_receipt'], review['primary_files']['outer_report'])
        try:
            accepted = selection.validate_selection_evidence(m, *encode_receipts(m, primary, review))
        except model.Refusal as error:
            self.fail('separate transport receipt path must be admitted: ' + error.code)
        self.assertEqual(accepted, primary)

    def test_false_clean_exit_or_unresolved_independent_findings_refuse(self):
        for key, value in (('primary_owner_exit', 1), ('primary_outer_exit', True),
                           ('primary_owner_reaped', False), ('independent_of_subject_selector', False),
                           ('open_findings', {'C': 0, 'I': 1, 'M': 0})):
            m, values, _ = fixture('actual')
            encode_inputs(m, values)
            primary, review = receipts(m)
            review[key] = value
            with self.subTest(key=key), self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m, *encode_receipts(m, primary, review))

    def test_unmatched_primary_report_or_target_refuses(self):
        for case in range(3):
            m, values, _ = fixture('actual')
            encode_inputs(m, values)
            primary, review = receipts(m)
            if case == 0: review['primary_report']['sha256'] = '0' * 64
            elif case == 1: review['target']['sha256'] = '0' * 64
            else: primary['original_image_full_reads'] = 2
            with self.subTest(case=case), self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m, *encode_receipts(m, primary, review))

    def test_manifest_source_provenance_and_complete_files_join_is_required(self):
        changes = [lambda r: r.__setitem__('primary_manifest_sha256', '3' * 64),
                   lambda r: r['primary_manifest_ref'].__setitem__('sha256', '3' * 64),
                   lambda r: r['primary_sources']['bcrypt_disassembly_model_015.py'].__setitem__('sha256', '3' * 64),
                   lambda r: r['primary_sources']['prepare_bcrypt_selection_015.py'].__setitem__('path', '/elsewhere/prepare_bcrypt_selection_015.py'),
                   lambda r: r['primary_sources'].pop('prepare_bcrypt_selection_015.py'),
                   lambda r: r['primary_provenance']['runtime014'].__setitem__('sha256', '3' * 64),
                   lambda r: r.__setitem__('completion_requires_clean_verifier_exit', False),
                   lambda r: r['primary_outer_source'].__setitem__('path', '/elsewhere/outer.py'),
                   lambda r: r['primary_files']['owner_report'].__setitem__('sha256', '3' * 64),
                   lambda r: r['primary_files']['owner_target'].__setitem__('bytes', 168),
                   lambda r: r['primary_files']['outer_report'].__setitem__('path', '/elsewhere/report.json'),
                   lambda r: r['primary_cli_raw']['stderr'].__setitem__('bytes', 1)]
        for number, change in enumerate(changes):
            m, values, _ = fixture('actual')
            encode_inputs(m, values)
            primary, review = receipts(m)
            change(review)
            with self.subTest(number=number), self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m, *encode_receipts(m, primary, review))


if __name__ == '__main__':
    unittest.main()
