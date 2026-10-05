"""Same-call module/HU capture contracts; not Windows execution or performance."""
import hashlib
from pathlib import Path
import unittest
import accepted_module_cache_evidence_extension as x

ROOT=Path(__file__).resolve().parents[2]
TU='cpp/src/orchestration/modules/MsvcModuleCompileCoordinator.cpp'
HEADER='cpp/include/mqb/orchestration/MsvcModuleCompileCoordinator.hpp'

def read(name):return (ROOT/name).read_text(encoding='utf-8')
def between(text, first, last):return text.split(first,1)[1].split(last,1)[0]

def wiring(text):
    if 'return run_impl<false>(request, nullptr, nullptr);' not in text:
        raise ValueError('default capture must remain disabled')
    if 'run_impl<true>(request, &record, &cache_evidence)' not in text:
        raise ValueError('recorded path disconnected')
    worker=between(text,'                if constexpr (Capture) {','                return attempts[node_index]->has_value();')
    if worker.count('compile_coordinator_.run_recorded(compile_request)')!=1 or worker.count('compile_coordinator_.run(compile_request)')!=1:
        raise ValueError('select exactly one lower call per node')
    if 'captured_nodes[node_index].emplace(std::move(compiled->record));' not in worker:
        raise ValueError('must move the actual returned cache evidence')
    if 'std::unexpected(std::move(compiled.error()))' not in worker:
        raise ValueError('lower error lost')
    for token in ('push_back(', '.resize(', '.reserve(', 'CompileCacheFile::', 'BuildSignature::', '.inspect(', 'capture_compile_cache('):
        if token in worker:raise ValueError('unapproved worker operation: '+token)
    prepare=between(text,'    std::vector<std::optional<CompileCacheEvidence>> captured_nodes;', '    using CompileAttempt')
    for token in ('if constexpr (Capture)', 'captured_nodes.resize(plan.node_count())',
                  'record->compiles.resize(plan.source_count)', 'record->header_unit_compiles.resize(plan.header_count)',
                  'cache_evidence->compiles.reserve(plan.source_count)', 'cache_evidence->header_unit_compiles.reserve(plan.header_count)'):
        if token not in prepare:raise ValueError('missing preallocation boundary')
    for token in ('CompileCacheFile::load(', 'CompileCacheFile::save(', 'BuildSignature::for_', 'std::ifstream', 'std::ofstream'):
        if token in text:raise ValueError('extra I/O or reconstructed evidence')
    for token in ('if (!captured_nodes[index])', 'if (!captured_nodes[node_index])',
                  'cache_evidence->compiles.push_back(std::move(*captured_nodes[index]))',
                  'cache_evidence->header_unit_compiles.push_back(std::move(*captured_nodes[node_index]))'):
        if token not in text:raise ValueError('ordered complete evidence required')

class ModuleCacheEvidenceContracts(unittest.TestCase):
    def test_single_call_and_worker_ownership_boundaries(self):wiring(read(TU))
    def test_negative_wiring_controls(self):
        t=read(TU)
        variants=[t.replace('run_impl<false>', 'run_impl<true>',1),
          t.replace('compiled->record','CompileCacheEvidence{}',1),
          t.replace('std::unexpected(std::move(compiled.error()))','std::unexpected(IncrementalCompileError{})',1),
          t.replace('captured_nodes.resize(plan.node_count());','',1),
          t.replace('if (!captured_nodes[index])','if (false)',1),
          t.replace('captured_nodes[node_index].emplace(std::move(compiled->record));','captured_nodes.push_back(std::move(compiled->record));'),
          t+'\nCompileCacheFile::load(p);',t+'\nBuildSignature::for_compile(u,t,o);']
        for index,v in enumerate(variants):
            with self.subTest(index=index),self.assertRaises(ValueError):wiring(v)
    def test_success_payload_is_owning_and_nonoptional(self):
        h=read(HEADER)
        value=between(h,'struct ModuleCompileWaveCacheEvidence {','\n};')
        self.assertIn('std::vector<CompileCacheEvidence> compiles;',value)
        self.assertIn('std::vector<CompileCacheEvidence> header_unit_compiles;',value)
        self.assertNotIn('optional',value)
        result=between(h,'struct RecordedModuleCompileWaveResult {','\n};')
        for field in ('ModuleCompileWaveResult result;','ModuleCompileWaveArtifactRecord record;',
                      'ModuleCompileWaveCacheEvidence cache_evidence;'):self.assertIn(field,result)
    def test_failed_wave_does_not_publish_partial_record(self):
        entry=between(read(TU),'MsvcModuleCompileCoordinator::run_recorded(', 'template<bool Capture>')
        self.assertLess(entry.index('if (!result) return std::unexpected'),entry.index('return RecordedModuleCompileWaveResult'))
        run=read(TU).split('MsvcModuleCompileCoordinator::run_impl(',1)[1]
        self.assertLess(run.index('"module compile scheduler failed: "'),run.index('ModuleCompileWaveResult result;'))
        self.assertLess(run.index('ModuleCompileErrorCode::compile_failed'),run.index('ModuleCompileWaveResult result;'))
    def test_existing_native_assertions_preserved_exactly(self):
        self.assertEqual(x.PRIOR,x.git_blob(x.original_program(read(x.TEST))))
    def test_old_and_new_assertion_mutations_rejected(self):
        t=read(x.TEST)
        for token in ('"external provider is not compiled"','"real module reuse is read-only"',
                      'e.save_error->code==CompileCacheFileErrorCode::replace_failed',
                      'count.cache_files_opened[ci]==limit','"forced mock completion order is opposite the source request order"',
                      'bytes(stem+".history.cache")==bytes(stem+".captured.cache")'):
            self.assertIn(token,t)
            with self.subTest(token=token),self.assertRaises(ValueError):x.original_program(t.replace(token,'',1))
    def test_extension_mapping_keeps_types_and_other_files(self):
        for binary in (False,True):
            value=read(x.TEST);value=value.encode() if binary else value
            before={x.TEST:value,'unrelated':value};after=x.legacy_view(before)
            self.assertIsNot(before,after);self.assertEqual(before[x.TEST],value)
            self.assertEqual(after['unrelated'],value);self.assertIsInstance(after[x.TEST],type(value))
    def test_inspect_and_projection_semantics_unchanged(self):
        t=read(TU)
        # Pins are filled from verified main, not from the changed implementation.
        parts=[between(t,'[[nodiscard]] ModuleCompileArtifactRecord capture_compile(', '} // namespace'),
               between(t,'MsvcModuleCompileCoordinator::inspect(', 'std::expected<ModuleCompileWaveResult, ModuleCompileError>')]
        self.assertEqual(PART_PINS,[hashlib.sha256(p.encode()).hexdigest() for p in parts])
        self.assertEqual(CORE_PIN,hashlib.sha256(read('cpp/include/mqb/core/BuildArtifactRecord.hpp').encode()).hexdigest())
    def test_provider_force_and_graph_result_order_preserved(self):
        t=read(TU)
        self.assertIn('provider_work_planned(plan, compiled_this_run, node_index)',t)
        self.assertIn('compiled_this_run[node_index] = attempts[node_index]->value().compiled;',t)
        self.assertIn('if constexpr (Capture) record->dependencies = request.plan;',t)
        self.assertIn('for (std::size_t index = 0; index < plan.source_count; ++index)',t)
        self.assertIn('for (std::size_t index = 0; index < plan.header_count; ++index)',t)
    def test_existing_native_call_budget_unchanged(self):
        t=read(x.TEST)
        for token in ('++calls<=10','++calls<=6','n<=16','++compiles<=24',
                      '"header unit contributes no guessed link object"','ran->exit_code==13'):
            self.assertIn(token,t)
        helper=t.split(x.BEGIN,1)[1]
        self.assertEqual(1,helper.count('wave.run_recorded('))
        self.assertNotIn('wave.run(',helper)
        self.assertEqual(96,len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))))
        self.assertEqual(51,len(list((ROOT/'.github/workflows').glob('*.yml'))))
    def test_real_evidence_counts_are_outside_serialization(self):
        helper=read(x.TEST).split(x.BEGIN,1)[1]
        call=helper.split('run_module_cache_checked(',1)[1]
        self.assertLess(call.index('performance::Activation active{collector}'),call.index('wave.run_recorded('))
        self.assertLess(call.index('}();'),call.index('collector.snapshot()'))
        self.assertNotIn('CompileCacheFile::save',call)
        self.assertNotIn('environment[0].value',helper)
    def test_default_adoption_has_not_spread(self):
        allowed={TU,'cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp',
                 'cpp/src/orchestration/incremental/MsvcIncrementalPchCoordinator.cpp'}
        for path in (ROOT/'cpp/src').rglob('*'):
            if path.suffix not in ('.cpp','.hpp') or path.relative_to(ROOT).as_posix() in allowed:continue
            t=path.read_text();self.assertNotIn('CompileCacheEvidence',t,str(path));self.assertNotIn('ModuleCompileWaveCacheEvidence',t,str(path))
    def test_bilingual_docs_and_unreleased_version(self):
        a=read('docs/MODULE_CACHE_EVIDENCE.md');b=read('docs/MODULE_CACHE_EVIDENCE_ZH.md')
        self.assertEqual(a.count('\n## '),b.count('\n## '))
        self.assertIn('MODULE_CACHE_EVIDENCE_ZH.md',a);self.assertIn('MODULE_CACHE_EVIDENCE.md',b)
        self.assertEqual('5.6.0',read('VERSION').strip())

PART_PINS = ['6d58970c394fd29947f32110706936c9a66967ee5c4e3fabc5fb2060af621c68', '2b7e1446d7ed1cbca665313db74a728eb65f5d374154533c828918241dfc197b']
CORE_PIN = 'c0e7d0d7342647d9fb2b4fb9d4ebecaa0fe392f1123a2bfd54497ef3c39a75e4'
if __name__=='__main__':unittest.main(verbosity=2)
