"""Recognize exactly the module-target cache-evidence additive native extension.

Reverse explicit replacements and the pinned suffix, then verify the entire old
program. Neither old nor new assertions can be dropped or wildcard-excluded.
"""
import hashlib
TEST = 'cpp/tests/e2e/mqb_module_target_record_e2e_tests.cpp'
PRIOR = '06a6851c5ddb830ee32c9cdeec0fdbcce58e9b86'
CURRENT = '40c236ae3520b8b68e09e77b4357b2bd985331f7'
REPLACEMENTS = [('using namespace mqb::orchestration;\n', 'using namespace mqb::orchestration;\n#ifdef _WIN32\nvoid save_target_cache_evidence(const fs::path&, const RecordedModuleTargetResult&);\nvoid check_target_cache_history(const fs::path&, const RecordedModuleTargetResult&);\nstd::expected<RecordedModuleTargetResult, IncrementalModuleTargetError> run_target_cache_checked(\n    MsvcModuleTargetCoordinator&, const IncrementalModuleTargetRequest&, ArtifactGenerationLabel, const fs::path&);\n#endif\n'), ('        auto r = harness.target.run_recorded(request, ArtifactGenerationLabel{"fixture", phase});', '        auto r = run_target_cache_checked(harness.target, request, ArtifactGenerationLabel{"fixture", phase}, prefix);'), ('        else { save_success(prefix, *r, copies); check_success(*r, request); }', '        else { save_success(prefix, *r, copies); check_success(*r, request); save_target_cache_evidence(prefix, *r); }'), ('    require(calls.count == 10 && snapshot(tools) == tool_inputs, "fixed mock calls, read-only toolchain/external inputs");', '    check_target_cache_history(evidence / "01-cold", *first);\n    check_target_cache_history(evidence / "02-reuse", *warm);\n    require(first->cache_evidence.compiles.size() == 4 && first->cache_evidence.header_unit_compiles.size() == 1,\n        "target keeps actual injected providers but no fabricated external cache slot");\n    require(bytes(r.target.link_cache / "sentinel") == "preserve obstruction", "terminal save failure preserves original obstruction");\n    require(calls.count == 10 && snapshot(tools) == tool_inputs, "fixed mock calls, read-only toolchain/external inputs");'), ('    require(calls.count == 10 && runner.programs == 2, "fixed real target/program count");', '    check_target_cache_history(evidence / "01-cold", *cold);\n    check_target_cache_history(evidence / "02-reuse", *warm);\n    check_target_cache_history(evidence / "06-standard-cold", *std_cold);\n    check_target_cache_history(evidence / "07-standard-reuse", *std_warm);\n    require(cold->cache_evidence.compiles[0].request.options.configuration == BuildConfiguration::debug &&\n        release->cache_evidence.compiles[0].request.options.configuration == BuildConfiguration::release,\n        "retained target cache request survives configuration overwrite and later errors");\n    require(calls.count == 10 && runner.programs == 2, "fixed real target/program count");')]
BEGIN = "// BEGIN MQB_MODULE_TARGET_CACHE_EVIDENCE_CASES\n"
END = "// END MQB_MODULE_TARGET_CACHE_EVIDENCE_CASES\n"


def git_blob(text):
    raw=text.encode('utf-8')
    return hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()


def original_program(text):
    if git_blob(text)!=CURRENT or text.count(BEGIN)!=1 or text.count(END)!=1 or not text.endswith(END):
        raise ValueError('target cache evidence extension bytes/boundary changed')
    text=text.split(BEGIN,1)[0]
    for before,after in reversed(REPLACEMENTS):
        if text.count(after)!=1: raise ValueError('target evidence anchor changed')
        text=text.replace(after,before,1)
    if git_blob(text)!=PRIOR: raise ValueError('original target assertions changed')
    return text


def legacy_view(values):
    result=dict(values)
    if TEST in result:
        value=values[TEST]; binary=isinstance(value,bytes)
        previous=original_program(value.decode('utf-8') if binary else value)
        result[TEST]=previous.encode('utf-8') if binary else previous
    return result
