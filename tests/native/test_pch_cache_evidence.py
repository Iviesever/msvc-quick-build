"""PCH handoff contracts; static/synthetic checks, not native performance tests."""
import hashlib
from pathlib import Path
import unittest
import accepted_pch_cache_evidence_extension as x

ROOT=Path(__file__).resolve().parents[2]
PCH='cpp/src/orchestration/incremental/MsvcIncrementalPchCoordinator.cpp'
COMPILE='cpp/src/orchestration/incremental/MsvcIncrementalCompileCoordinator.cpp'
HEADER='cpp/include/mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp'

def read(path):return (ROOT/path).read_text(encoding='utf-8')
def body(text, begin, end):return text.split(begin,1)[1].split(end,1)[0]

def captured_boundaries(pch, compile_header, compile_source):
    if 'return run_impl<false>(request, nullptr, nullptr);' not in pch:
        raise ValueError('default recording enabled')
    if 'run_impl<true>(request, &record, &cache_evidence)' not in pch:
        raise ValueError('recording not connected')
    if compile_header.index('    inspect_for_pch_record(')<compile_header.index('\nprivate:\n'):
        raise ValueError('public execution handoff')
    if 'friend class MsvcIncrementalPchCoordinator;' not in compile_header:
        raise ValueError('missing private PCH access')
    warm=body(pch,'    auto inspected = [&]() {','    if (!inspected)')
    if warm.count('inspect_for_pch_record(')!=1 or 'if constexpr (Capture)' not in warm:
        raise ValueError('warm inspection is not the original opt-in handoff')
    complete=body(pch,'    auto complete =','    // A warm PCH hit')
    if 'if (!*cache_evidence)' not in complete or 'switch ((*cache_evidence)->state)' not in complete:
        raise ValueError('missing evidence/type check')
    finish=body(pch,'    auto compiled = [&]()','    if (!compiled)')
    if finish.count('run_recorded(')!=1 or finish.count('compile_coordinator_.run(')!=1 or 'if constexpr (Capture)' not in finish:
        raise ValueError('post-materialization path must select exactly one run')
    if pch.index('auto creator = write_creator_if_needed')>pch.index('    auto compiled = [&]()'):
        raise ValueError('compile before creator repair')
    hot=body(pch,'    if (inspected->inspection.compile.plan.empty()) {','    for (const fs::path* artifact')
    if hot.count('complete(result_from_inspection(')!=1 or 'run_recorded' in hot or '.inspect(' in hot:
        raise ValueError('warm hit must not re-inspect/run')
    handoff=compile_source.split('MsvcIncrementalCompileCoordinator::inspect_for_pch_record(',1)[1]
    for token in ('CompileCacheFile::load(', 'CompileCacheFile::save(', 'executor_.execute(', 'BuildSignature::for_'):
        if token in handoff or token in pch:raise ValueError('extra evidence I/O or signature reconstruction')
    if handoff.count('inspect_impl<true>(request, &capture)')!=1 or 'cache_entry = std::move(*capture.entry)' not in handoff:
        raise ValueError('must use original accepted value')

class PchCacheEvidenceContracts(unittest.TestCase):
    def test_private_same_invocation_handoff(self):
        captured_boundaries(read(PCH),read(HEADER),read(COMPILE))
    def test_negative_handoff_controls(self):
        p,h,c=read(PCH),read(HEADER),read(COMPILE)
        variants=[(p.replace('run_impl<false>', 'run_impl<true>',1),h,c),
            (p.replace('if (!*cache_evidence)', 'if (false)',1),h,c),
            (p.replace('switch ((*cache_evidence)->state)', 'switch (CompileCacheEvidenceState::saved)',1),h,c),
            (p.replace('return complete(result_from_inspection(inspected->inspection.compile));',
                       'compile_coordinator_.run_recorded(inspected->compile_request);\nreturn complete(result_from_inspection(inspected->inspection.compile));'),h,c),
            (p,h.replace('\nprivate:\n','\npublic:\n',1),c),
            (p,h,c+'\nCompileCacheFile::load(request.cache_file);'),
            (p,h,c.replace('cache_entry = std::move(*capture.entry)', 'cache_entry = CompileCacheEntry{}'))]
        for variant in variants:
            with self.subTest(case=variants.index(variant)),self.assertRaises((ValueError,IndexError)):
                captured_boundaries(*variant)
    def test_mandatory_evidence_and_legacy_projection_unchanged(self):
        public=read('cpp/include/mqb/orchestration/MsvcIncrementalPchCoordinator.hpp')
        record=body(public,'struct RecordedPchResult {','\n};')
        self.assertIn('CompileCacheEvidence cache_evidence;',record)
        self.assertNotIn('optional<CompileCacheEvidence>',record)
        legacy=read('cpp/include/mqb/core/BuildArtifactRecord.hpp')
        self.assertEqual(hashlib.sha256(legacy.encode()).hexdigest(),
            'c0e7d0d7342647d9fb2b4fb9d4ebecaa0fe392f1123a2bfd54497ef3c39a75e4')
    def test_inspect_stays_readonly_and_uncaptured(self):
        method=body(read(PCH),'MsvcIncrementalPchCoordinator::inspect(',
                    'MsvcIncrementalPchCoordinator::run(')
        self.assertIn('inspect_pch(request, compile_coordinator_)',method)
        self.assertNotIn('record',method)
        self.assertNotIn('write_creator',method)
    def test_creator_semantic_force_and_error_paths_remain(self):
        text=read(PCH)
        for marker in ('forced_request.force_rebuild = true;', 'compile_coordinator.inspect(forced_request)',
                       'state.force_compile_after_materialization = true;', 'BuildReason::source_changed',
                       'compile_request.force_rebuild = true;', 'compiled->plan = inspected->inspection.compile.plan;',
                       'compiled->warnings.insert(', 'if (!regular_file(request.artifacts.precompiled_header))'):
            self.assertIn(marker,text)
        error=body(text,'    if (!compiled) {','    if (inspected->force_compile_after_materialization)')
        self.assertIn('compiled.error()',error)
    def test_no_other_source_adopts_capture(self):
        for path in (ROOT/'cpp/src').rglob('*'):
            if path.suffix not in ('.hpp','.cpp') or path.relative_to(ROOT).as_posix() in (PCH,COMPILE,
                'cpp/src/orchestration/modules/MsvcModuleCompileCoordinator.cpp'):continue
            text=path.read_text()
            self.assertNotIn('CompileCacheEvidence',text,str(path))
            self.assertNotIn('inspect_for_pch_record',text,str(path))
    def test_original_pch_native_program_preserved(self):
        self.assertEqual(x.PRIOR,x.git_blob(x.original_program(read(x.TEST))))
    def test_changed_old_or_new_native_assertion_rejected(self):
        text=read(x.TEST)
        for marker in ('"PCH reuse is read-only"', '"same-timestamp creator repaired without false reuse"',
                       'count.cache_files_opened[ci] == 1', 'e.save_error->code == CompileCacheFileErrorCode::replace_failed',
                       'bytes(after) == bytes(prefix.string()+".captured.cache")', 'run_pch_cache_checked(pch,request'):
            self.assertIn(marker,text)
            with self.subTest(marker=marker),self.assertRaises(ValueError):x.original_program(text.replace(marker,'',1))
    def test_extension_keeps_bytes_text_and_source_map(self):
        for binary in (False,True):
            value=read(x.TEST);value=value.encode() if binary else value
            before={x.TEST:value,'untouched':value};result=x.legacy_view(before)
            self.assertIsNot(before,result);self.assertEqual(before[x.TEST],value)
            self.assertEqual(result['untouched'],value);self.assertIsInstance(result[x.TEST],type(value))
    def test_no_native_call_budget_increase(self):
        self.assertEqual(96,len(list((ROOT/'cpp/tests').rglob('*_tests.cpp'))))
        text=read(x.TEST)
        self.assertIn('calls == 10 && runner.calls == 6',text);self.assertIn('calls==6',text)
        helper=text.split(x.BEGIN,1)[1]
        self.assertEqual(1,helper.count('pch.run_recorded('))
        self.assertNotIn('pch.run(',helper)
        self.assertNotIn('environment[0].value',helper)
        self.assertIn('maximum_original_payload_opens=',helper)
        self.assertEqual(51,len(list((ROOT/'.github/workflows').glob('*.yml'))))
    def test_context_captured_before_inspection_and_no_ticket(self):
        helper=read(COMPILE).split('MsvcIncrementalCompileCoordinator::inspect_for_pch_record(',1)[1]
        self.assertLess(helper.index('auto captured_toolchain = toolchain_;'),helper.index('inspect_impl<true>'))
        self.assertLess(helper.index('accepted.reset();'),helper.index('inspect_impl<true>'))
        self.assertNotIn('execute_inspected',helper)
    def test_bilingual_documentation_and_version(self):
        en=read('docs/PCH_CACHE_EVIDENCE.md');zh=read('docs/PCH_CACHE_EVIDENCE_ZH.md')
        self.assertEqual(en.count('\n## '),zh.count('\n## '))
        self.assertIn('PCH_CACHE_EVIDENCE_ZH.md',en);self.assertIn('PCH_CACHE_EVIDENCE.md',zh)
        self.assertEqual('5.6.0',read('VERSION').strip())

if __name__=='__main__':unittest.main(verbosity=2)
