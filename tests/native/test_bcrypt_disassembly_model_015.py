"""Pure 015 manifest assertions; no input files, owner or tool is opened."""
import copy
import hashlib
import importlib
import unittest

try:
    model = importlib.import_module('bcrypt_disassembly_model_015')
except ModuleNotFoundError as error:
    if error.name != 'bcrypt_disassembly_model_015':
        raise
    model = None


def identity(path, raw=b'constructed'):
    return {'path': path, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def make_manifest(phase='selection', mode='synthetic'):
    m = model
    names = m.source_names(phase)
    roles = ('image', 'index', 'admission', 'caller', 'python')
    if phase == 'actual':
        roles += ('tool', 'loader_cache', *(f'dep{i:02d}' for i in range(1, 9)),
                  'selection', 'selection_review')
    records = {r: identity('/synthetic/identities/' + r, r.encode()) for r in roles}
    sources = {n: identity('/synthetic/source/' + n, n.encode()) for n in names}
    for name, pin in m.FROZEN_SOURCES.items():
        if name in sources:
            sources[name].update(pin)
    target = {'runtime_function_index': 0, 'section_index': 0,
              'file_start': 32, 'file_end': 199, 'rva_start': 0x2000,
              'rva_end': 0x20A7, 'preferred_vma_start': 0x90002000,
              'sha256': None if phase == 'selection' else hashlib.sha256(b'\x90' * 167).hexdigest()}
    result = {'schema': 'bcrypt-disassembly-manifest-015-v1', 'phase': phase,
              'mode': mode, 'call_id': '015-synthetic-unit',
              'baseline_commit': m.BASELINE_COMMIT,
              'source_commit': None if phase == 'selection' else '1' * 40,
              'output_dir': '/synthetic/output', 'identities': records,
              'provenance': {r: identity('/synthetic/provenance/' + r, r.encode())
                             for r in m.PROVENANCE}, 'sources': sources,
              'aliases': [], 'absent_paths': [], 'target': target,
              'tool_argv': [], 'environment': dict(m.ENV), 'limits': m.limits_for(phase)}
    if mode == 'actual':
        result['identities'].update(copy.deepcopy({k: v for k, v in m.FIXED_IDENTITIES.items() if k in roles}))
        result['provenance'] = copy.deepcopy(m.PROVENANCE)
        result['target'] = dict(m.ACTUAL_TARGET, sha256=target['sha256'])
        result['output_dir'] = m.SELECTION_OUTPUT_DIR if phase == 'selection' else m.ACTUAL_OUTPUT_DIR
        result['call_id'] = '015-selection-primary-1' if phase == 'selection' else '015-helper-static-1'
        if phase == 'actual':
            result['identities']['selection']['path'] = m.SELECTION_OUTPUT_DIR + '/report.json'
            result['identities']['selection_review']['path'] = m.EVIDENCE_ROOT + '/evidence-015/selection-independent-015/review.json'
    if phase == 'actual':
        result['tool_argv'] = m.tool_argv(result['identities']['tool']['path'], result['target'])
    return result


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(model, '015 manifest implementation is absent')

    def refused(self, value):
        with self.assertRaises(model.Refusal):
            model.validate_manifest(value)

    def test_both_phases_validate_without_mutation(self):
        for phase in ('selection', 'actual'):
            m = make_manifest(phase)
            original = copy.deepcopy(m)
            self.assertIs(model.validate_manifest(m), m)
            self.assertEqual(m, original)

    def test_actual_phase_identities_are_exactly_pinned(self):
        for phase in ('selection', 'actual'):
            m = make_manifest(phase, 'actual')
            self.assertIs(model.validate_manifest(m), m)
            for role in ('image', 'index', 'admission', 'caller', 'python'):
                bad = copy.deepcopy(m)
                bad['identities'][role]['sha256'] = '0' * 64
                self.refused(bad)

    def test_source_sets_exclude_old_owner_and_run(self):
        self.assertEqual(len(model.source_names('selection')), 6)
        self.assertEqual(len(model.source_names('actual')), 7)
        for phase in ('selection', 'actual'):
            self.assertFalse({'bcrypt_disassembly_owner_014.py', 'run_bcrypt_disassembly_014.py'} & set(model.source_names(phase)))
            bad = make_manifest(phase)
            bad['sources']['run_bcrypt_disassembly_014.py'] = identity('/synthetic/source/run_bcrypt_disassembly_014.py')
            self.refused(bad)

    def test_reused_source_hashes_cannot_drift(self):
        for name in model.FROZEN_SOURCES:
            bad = make_manifest('actual')
            bad['sources'][name]['sha256'] = '0' * 64
            self.refused(bad)

    def test_phase_limits_are_independent_fresh_dicts(self):
        selection, actual = model.limits_for('selection'), model.limits_for('actual')
        self.assertEqual((selection['owner_seconds'], actual['owner_seconds']), (8, 12))
        self.assertEqual((selection['report_bytes'], actual['report_bytes']), (8192, 262144))
        self.assertEqual((selection['identity_roles'], actual['identity_roles']), (5, 17))
        actual['target_bytes'] = 999
        self.assertEqual(model.limits_for('actual')['target_bytes'], 167)

    def test_unknown_phase_schema_mode_and_fields_refuse(self):
        for field, value in (('phase', 'old'), ('mode', 'trial'), ('schema', 'bcrypt-disassembly-manifest-014-v1'), ('extra', 1)):
            bad = make_manifest()
            bad[field] = value
            self.refused(bad)

    def test_baseline_source_commit_and_call_ids_are_bound(self):
        for phase in ('selection', 'actual'):
            for field, value in (('baseline_commit', '0' * 40), ('call_id', '014-init-static-1'),
                                 ('source_commit', '1' * 40 if phase == 'selection' else None)):
                bad = make_manifest(phase)
                bad[field] = value
                self.refused(bad)

    def test_selection_has_no_tool_alias_or_absence_operation(self):
        for field, value in (('tool_argv', ['/bin/true']), ('aliases', [{'path': '/alias', 'target': '/synthetic/identities/python'}]), ('absent_paths', ['/unused'])):
            bad = make_manifest()
            bad[field] = value
            self.refused(bad)

    def test_selection_digest_is_null_actual_digest_is_nonnull(self):
        for phase, value in (('selection', '0' * 64), ('actual', None), ('actual', 'g' * 64)):
            bad = make_manifest(phase)
            bad['target']['sha256'] = value
            self.refused(bad)

    def test_exact_target_length_and_uint64_domains(self):
        for field, value in (('file_end', 200), ('rva_end', 0x20A8), ('file_start', True),
                             ('preferred_vma_start', (1 << 64) - 1), ('section_index', -1)):
            bad = make_manifest()
            bad['target'][field] = value
            self.refused(bad)

    def test_actual_candidate_cannot_be_old_entrypoint(self):
        bad = make_manifest(mode='actual')
        bad['target']['runtime_function_index'] = 169
        self.refused(bad)

    def test_actual_output_reservation_paths_are_fixed(self):
        for phase in ('selection', 'actual'):
            bad = make_manifest(phase, 'actual')
            bad['output_dir'] += '-retry'
            self.refused(bad)

    def test_actual_selection_receipt_paths_cannot_be_replaced(self):
        for role in ('selection', 'selection_review'):
            bad = make_manifest('actual', 'actual')
            bad['identities'][role]['path'] += '-retry'
            self.refused(bad)

    def test_synthetic_inputs_reject_original_image_index_caller(self):
        for forbidden in ('image', 'index', 'caller'):
            bad = make_manifest()
            bad['identities']['admission']['sha256'] = model.FIXED_IDENTITIES[forbidden]['sha256']
            self.refused(bad)

    def test_exact_roles_and_provenance_identities(self):
        bad = make_manifest(mode='actual')
        bad['provenance']['runtime014']['bytes'] += 1
        self.refused(bad)
        bad = make_manifest('actual')
        del bad['identities']['selection_review']
        self.refused(bad)

    def test_identity_alias_path_and_total_caps(self):
        for value in ('relative', '/two/../one', '//two', '/bad\npath'):
            bad = make_manifest()
            bad['identities']['image']['path'] = value
            self.refused(bad)
        bad = make_manifest()
        bad['identities']['caller']['path'] = bad['identities']['image']['path']
        self.refused(bad)
        bad = make_manifest('actual')
        bad['identities']['python']['bytes'] = bad['limits']['identity_total_bytes']
        self.refused(bad)

    def test_tool_options_are_exact_and_no_alternative_is_accepted(self):
        m = make_manifest('actual')
        self.assertEqual(len(m['tool_argv']), 18)
        self.assertEqual(m['tool_argv'][-1], 'target.bin')
        self.assertEqual(m['tool_argv'][-2], '--stop-address=0x900020a7')
        m['tool_argv'].insert(1, '--version')
        self.refused(m)

    def test_environment_and_limits_cannot_be_inherited_or_widened(self):
        for field, key, value in (('environment', 'PATH', '/bin'), ('limits', 'report_bytes', 524288), ('limits', 'target_bytes', True)):
            bad = make_manifest('actual')
            bad[field][key] = value
            self.refused(bad)


if __name__ == '__main__':
    unittest.main()
