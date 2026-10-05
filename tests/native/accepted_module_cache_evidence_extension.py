"""Recognize exactly the module/HU cache-evidence additive native extension.

Reverse explicit replacements and the pinned suffix, then verify the entire old
program. Neither old nor new assertions can be dropped or wildcard-excluded.
"""
import hashlib
TEST = 'cpp/tests/e2e/mqb_module_record_e2e_tests.cpp'
PRIOR = '90315e5f1a929ad6f85b2ba08e313eab91b50096'
CURRENT = 'f353d5935460baa1a923a2e68767137bb3970650'
REPLACEMENTS = [('#include <chrono>\n', '#include <chrono>\n#include <condition_variable>\n#include <mutex>\n'), ('using namespace mqb::orchestration;\n', 'using namespace mqb::orchestration;\n#ifdef _WIN32\nvoid save_module_cache_evidence(const fs::path&, const RecordedModuleCompileWaveResult&);\nvoid check_module_cache_history(const fs::path&, const RecordedModuleCompileWaveResult&);\nstd::expected<RecordedModuleCompileWaveResult,ModuleCompileError> run_module_cache_checked(\n    MsvcModuleCompileCoordinator&, const ModuleCompileWaveRequest&, ArtifactGenerationLabel, const fs::path&);\n#endif\n'), ('        auto r=wave.run_recorded(input,ArtifactGenerationLabel{"mock-module",phase}); save_call(evidence,phase,r,false); return r;', '        auto r=run_module_cache_checked(wave,input,ArtifactGenerationLabel{"mock-module",phase},evidence/phase);\n        save_call(evidence,phase,r,false); if (r) save_module_cache_evidence(evidence/phase,*r); return r;'), ('        auto r=wave.run_recorded(request,ArtifactGenerationLabel{"native-module",phase}); save_call(evidence,phase,r,true); return r;', '        auto r=run_module_cache_checked(wave,request,ArtifactGenerationLabel{"native-module",phase},evidence/phase);\n        save_call(evidence,phase,r,true); if (r) save_module_cache_evidence(evidence/phase,*r); return r;'), ('    require(calls==10 && external_before==snapshot(root/"external"),"fixed mock calls; external file untouched");', '    check_module_cache_history(evidence/"01-cold",*cold);\n    check_module_cache_history(evidence/"02-reuse",*reuse);\n    for (std::size_t i=0;i<cold->cache_evidence.compiles.size();++i)\n        require(bytes((evidence/"01-cold").string()+".source"+std::to_string(i)+".captured.cache") ==\n            bytes((evidence/"02-reuse").string()+".source"+std::to_string(i)+".captured.cache"), "mock cold/warm accepted values match");\n    require(calls==10 && external_before==snapshot(root/"external"),"fixed mock calls; external file untouched");'), ('    require(calls==6,"fixed native wave budget completed");', '    check_module_cache_history(evidence/"01-cold",*cold);\n    check_module_cache_history(evidence/"02-reuse",*warm);\n    require(cold->cache_evidence.compiles[0].request.options.configuration==BuildConfiguration::debug &&\n        release->cache_evidence.compiles[0].request.options.configuration==BuildConfiguration::release,\n        "old cache request survives same-path configuration overwrite and failure");\n    require(calls==6,"fixed native wave budget completed");'), ('        std::atomic<unsigned> calls{0};\n', '        std::atomic<unsigned> calls{0};\n        std::mutex completion_mutex;\n        std::condition_variable completion_changed;\n        bool header_completed{false};\n        std::vector<std::string> cold_completions;\n'), ('            const auto source=mock_compile_source(spec);\n', '            const auto source=mock_compile_source(spec);\n            if (phase=="01-cold" && source.filename()=="A.ixx") {\n                std::unique_lock lock{completion_mutex};\n                require(completion_changed.wait_for(lock,std::chrono::seconds{10},[&]{return header_completed;}),\n                    "bounded mock ordering requires the independent header worker");\n            }\n'), ('            log_result(prefix,r); return r;\n        }\n    } runner{evidence/"processes"};', '            log_result(prefix,r);\n            if (phase=="01-cold") {\n                std::lock_guard lock{completion_mutex};\n                cold_completions.push_back(text(source.filename()));\n                if (source.filename()=="extra.hpp") { header_completed=true; completion_changed.notify_all(); }\n            }\n            return r;\n        }\n    } runner{evidence/"processes"};'), ('    require(cold->record.dependencies.resolved_external_dependencies.size()==1 && runner.calls==4,"external provider is not compiled");', '    require(cold->record.dependencies.resolved_external_dependencies.size()==1 && runner.calls==4,"external provider is not compiled");\n    require(runner.cold_completions==std::vector<std::string>{"extra.hpp","A.ixx","B.ixx","main.cpp"},\n        "forced mock completion order is opposite the source request order");\n    std::ostringstream completion_log;\n    for (const auto& name:runner.cold_completions) completion_log<<name<<\'\\n\';\n    write(evidence/"cold-completion-order.txt",completion_log.str());')]
BEGIN = '// BEGIN MQB_MODULE_CACHE_EVIDENCE_CASES\n'
END = '// END MQB_MODULE_CACHE_EVIDENCE_CASES\n'


def git_blob(text):
    raw=text.encode('utf-8')
    return hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()


def original_program(text):
    if git_blob(text)!=CURRENT or text.count(BEGIN)!=1 or text.count(END)!=1 or not text.endswith(END):
        raise ValueError('module cache evidence extension bytes/boundary changed')
    text=text.split(BEGIN,1)[0]
    for before,after in reversed(REPLACEMENTS):
        if text.count(after)!=1: raise ValueError('module evidence anchor changed')
        text=text.replace(after,before,1)
    if git_blob(text)!=PRIOR: raise ValueError('original module assertions changed')
    return text


def legacy_view(values):
    result=dict(values)
    if TEST in result:
        value=values[TEST]; binary=isinstance(value,bytes)
        previous=original_program(value.decode('utf-8') if binary else value)
        result[TEST]=previous.encode('utf-8') if binary else previous
    return result
