"""Read-only source and exact-history controls, not Windows/MSVC execution."""
import copy
from pathlib import Path
import unittest
from unittest.mock import patch
import link_observation_role_contract as roles
import recorded_storage_projection_contract as storage

ROOT = Path(__file__).resolve().parents[2]
E2E = 'cpp/tests/e2e/mqb_artifact_record_e2e_tests.cpp'
LINKER = 'cpp/src/msvc/linker/MsvcLinker.cpp'


def read(path):
    return (ROOT/path).read_text(encoding='utf-8')


class LinkObservationRoleContracts(unittest.TestCase):
    def test_exact_inverse_retains_the_complete_predecessor(self):
        self.assertEqual(roles.SPEC['base'], '399e06fa43d2052ac0d452e1320438c015c65145')
        self.assertEqual(set(roles.SPEC['files']), {E2E,
            'cpp/tests/e2e/mqb_dll_target_e2e_tests.cpp',
            'cpp/tests/msvc/linker/linker_arguments_tests.cpp'})
        for path, rule in roles.SPEC['files'].items():
            self.assertEqual(roles.digest(roles.legacy_text(path, read(path))), rule['prior'])
        self.assertEqual(roles.digest(roles.legacy_text(E2E, read(E2E))),
                         'b771b6dbb1cd03ccceaf85cdb2b9e92646e0eae4a91376af32a5f0e9628bec18')
        self.assertEqual(roles.digest(storage.legacy_text(E2E, read(E2E))),
                         storage.SPEC['files'][E2E]['prior'])

    def test_deleted_or_extra_native_controls_are_rejected(self):
        for text in (read(E2E)+'\n', read(E2E).replace('api_calls==9', 'api_calls<=9'),
                     read(E2E).replace('!contains_import(file_inputs)', 'true'),
                     roles.legacy_text(E2E, read(E2E))):
            with self.assertRaises(ValueError):
                roles.legacy_text(E2E, text)

    def test_wrong_prior_or_anchor_is_rejected(self):
        for mode in ('prior', 'anchor', 'cuts'):
            spec=copy.deepcopy(roles.SPEC)
            if mode=='prior': spec['files'][E2E]['prior']='0'*64
            elif mode=='anchor': spec['files'][E2E]['replacements'][0]['after']='ABSENT ANCHOR'
            else: spec['files'][E2E]['cuts'][0][0]='ABSENT BOUNDARY'
            with patch.object(roles,'SPEC',spec), self.assertRaises(ValueError):
                roles.legacy_text(E2E,read(E2E))

    def test_old_storage_pins_are_unchanged(self):
        self.assertEqual(roles.digest(read('tests/native/recorded_storage_projection_extension.json')),
                         '9dbd17a8ec9a483d0b7ee55148c52984a4e79868d0afce285eb9d46d357b9332')

    def test_no_migration_or_path_blacklist_was_added_to_coordinator(self):
        self.assertEqual(roles.digest(read('cpp/src/orchestration/incremental/MsvcIncrementalLinkCoordinator.cpp')),
                         'e403c70c90ca35a954e969055eac2a487008a86008cd784bd103a0ea6019260c')
        self.assertEqual(roles.digest(read('cpp/src/core/cache/LinkCacheFile.cpp')),
                         '12eb3c02a977409da94eeffcaeaf068d40ed0918a157f572a718c63c97384f74')

    def test_filter_is_at_the_message_boundary(self):
        s=read(LINKER)
        self.assertIn('is_library_creation_message(line) ? line.size() : 0',s)
        helper=s.split('bool is_library_creation_message',1)[1].split('bool has_user_progress_output',1)[0]
        for token in ('Creating library ', ' and object ', '".lib"', '".exp"', 'ascii_iequals', 'return false'):
            self.assertIn(token,helper)
        for token in ('fs::', 'paths.erase', 'remove_if', 'LinkCacheFile', 'getenv', 'directory_iterator'):
            self.assertNotIn(token,helper)

    def test_parser_controls_keep_real_reads_unknown_lines_and_unicode(self):
        s=read('cpp/tests/msvc/linker/linker_arguments_tests.cpp')
        for token in ('creation_only.empty()', 'search_before.size() == 1',
                      'separate_search.size() == 2', '未知消息', 'warning LNK9999',
                      'unicode_input.size() == 2', 'observed_libraries.size() == 2'):
            self.assertIn(token,s)

    def test_dedicated_model_budget_and_failure_are_explicit(self):
        s=read(E2E).split('// BEGIN link observation role compatibility controls',1)[1].split('// END link observation role compatibility controls',1)[0]
        self.assertIn('api_calls==9 && runner.calls==6',s)
        for phase in ('01-cold','02-clean-reuse','03-legacy-reuse','04-forced-failure',
                      '05-forced-rebuild','06-repaired-reuse','07-input-change',
                      '08-side-repair','09-genuine-read'):
            self.assertEqual(s.count('run("'+phase+'"'),1)
        self.assertIn('bytes(request.cache_file)==old_bytes',s)
        self.assertIn('contains(old->record.association.file_inputs,import)',s)
        self.assertIn('RecordedStorageProjectionIssue::path_conflict',s)
        self.assertIn('external_processes=0',s)

    def test_real_dll_changes_only_its_bug_dependent_projection_expectation(self):
        s=read(E2E)
        start=s.index('auto run = [&](const char* phase)')
        end=s.index('const auto cold = run("01-cold")',start)
        call=s[start:end]
        self.assertIn('!contains_import(file_inputs) && contains_import(side_outputs)',call)
        self.assertIn('result=accepted build_record_retained=true',call)
        self.assertLess(call.index('".record.txt"'),call.index('project_storage_references(*result,'))
        self.assertIn('require(completed_calls == 7,',s)
        # All original pure conflict controls still run against real product code.
        pure=read('cpp/tests/e2e/mqb_storage_association_e2e_tests.cpp')
        self.assertIn('dll_admitted == 2 && dll_refused == 4',pure)
        self.assertIn('rejected==22',pure)

    def test_existing_cli_lifecycle_checks_roles_without_extra_invocations(self):
        s=read('cpp/tests/e2e/mqb_dll_target_e2e_tests.cpp')
        self.assertEqual(s.count('verify_library_roles();'),4)
        self.assertIn('!contains_import(value.file_inputs) && contains_import(value.side_outputs)',s)
        self.assertEqual(len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))),96)
        self.assertEqual(len(list((ROOT/'cpp/src').rglob('*.cpp'))),96)

    def test_bilingual_boundaries_remain_explicit(self):
        en=read('docs/RECORDED_STORAGE_PROJECTION.md')
        zh=read('docs/RECORDED_STORAGE_PROJECTION_ZH.md')
        self.assertEqual(en.count('\n## '),zh.count('\n## '))
        for s in (en,zh):
            for token in ('Creating library','path_conflict','clean/prune','v5.7.0'):
                self.assertIn(token,s)

    def test_public_parser_shape_and_default_scope_are_unchanged(self):
        self.assertIn('observed_library_paths(std::string_view stdout_text);',
                      read('cpp/include/mqb/msvc/MsvcLinker.hpp'))
        self.assertEqual(len(list((ROOT/'.github/workflows').glob('*.yml'))),51)
        self.assertEqual(read('VERSION').strip(),'5.6.0')


if __name__=='__main__':
    unittest.main(verbosity=2)
