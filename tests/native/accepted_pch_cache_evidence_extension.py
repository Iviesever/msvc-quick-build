"""Exact reversible PCH native-test extension; not a wildcard exclusion.
The original 10 mock and 6 real calls/assertions remain; extra checks inspect the
same returned values. Full current and predecessor Git blobs are both pinned.
"""
import hashlib
TEST = 'cpp/tests/e2e/mqb_pch_record_e2e_tests.cpp'
CURRENT = '8273230bb3e7db4f2f6c8eca49b37790d86b8539'
PRIOR = '741219cdb39a77f46c5aed5db10e65253d6d40e0'
REPLACEMENTS = [('void save_call(const fs::path& evidence, const char* phase,', 'void save_pch_cache_evidence(const fs::path&, const RecordedPchResult&);\nvoid check_pch_cache_history(const fs::path&, const RecordedPchResult&);\nstd::expected<RecordedPchResult, IncrementalPchError> run_pch_cache_checked(\n    MsvcIncrementalPchCoordinator&, const IncrementalPchRequest&,\n    ArtifactGenerationLabel, const fs::path&);\nvoid save_call(const fs::path& evidence, const char* phase,'), ('    write(prefix.string()+".record.txt", describe(*r));', '    write(prefix.string()+".record.txt", describe(*r));\n    save_pch_cache_evidence(prefix, *r);'), ('    msvc::MsvcCompileExecutor executor{toolchain, runner};', '    toolchain.environment = {{"MQB_PCH_OWNERSHIP", "first"}, {"MQB_PCH_OWNERSHIP", "second"}};\n    msvc::MsvcCompileExecutor executor{toolchain, runner};'), ('        auto r = pch.run_recorded(input, ArtifactGenerationLabel{"mock-pch",phase});', '        auto r = run_pch_cache_checked(pch, input, ArtifactGenerationLabel{"mock-pch",phase}, evidence/phase);'), ('        auto r=pch.run_recorded(request,ArtifactGenerationLabel{"native-pch",phase});', '        auto r=run_pch_cache_checked(pch,request,ArtifactGenerationLabel{"native-pch",phase},evidence/phase);'), ('    require(calls == 10 && runner.calls == 6, "mock fixed budget completed");', '    toolchain.environment[0].value = "changed after failures";\n    require(cold->cache_evidence.inspection_toolchain.environment.size() == 2 &&\n        cold->cache_evidence.inspection_toolchain.environment[0].value == "first" &&\n        cold->cache_evidence.inspection_toolchain.environment[1].value == "second",\n        "owning environment preserves order and duplicate names without serialization");\n    check_pch_cache_history(evidence/"01-cold", *cold);\n    check_pch_cache_history(evidence/"02-reuse", *warm);\n    require(calls == 10 && runner.calls == 6, "mock fixed budget completed");'), ('    require(calls==6,"six native PCH calls completed");', '    check_pch_cache_history(evidence/"01-cold", *cold);\n    check_pch_cache_history(evidence/"02-reuse", *reuse);\n    require(cold->cache_evidence.request.options.configuration == BuildConfiguration::debug &&\n        release->cache_evidence.request.options.configuration == BuildConfiguration::release,\n        "recorded effective compiler options survive configuration change and later failure");\n    require(calls==6,"six native PCH calls completed");')]
BEGIN = '// BEGIN MQB_PCH_CACHE_EVIDENCE_CASES\n'
END = '// END MQB_PCH_CACHE_EVIDENCE_CASES\n'


def git_blob(text):
    raw=text.encode('utf-8')
    return hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()


def original_program(text):
    if git_blob(text)!=CURRENT or text.count(BEGIN)!=1 or text.count(END)!=1 or not text.endswith(END):
        raise ValueError('PCH cache evidence extension bytes/boundary changed')
    text=text.split(BEGIN,1)[0]
    for before,after in reversed(REPLACEMENTS):
        if text.count(after)!=1: raise ValueError('PCH evidence anchor changed')
        text=text.replace(after,before,1)
    if git_blob(text)!=PRIOR: raise ValueError('original PCH assertions changed')
    return text


def legacy_view(values):
    result=dict(values)
    if TEST in result:
        value=result[TEST]; binary=isinstance(value,bytes)
        previous=original_program(value.decode('utf-8') if binary else value)
        result[TEST]=previous.encode('utf-8') if binary else previous
    return result
