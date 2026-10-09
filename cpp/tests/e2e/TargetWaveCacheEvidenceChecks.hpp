#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/orchestration/ArtifactGenerationModel.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

// Test-side persistence only. This is outside the measured product invocation;
// it neither grants production persistence nor changes product cache budgets.
namespace target_wave_cache_checks {
namespace fs = std::filesystem;
using namespace mqb::orchestration;
inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline std::string bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(in.is_open(), "cache evidence input missing");
    std::string result{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    require(!in.bad(), "cache evidence read failed");
    return result;
}
inline void write(const fs::path& path, const std::string& data) {
    require(!fs::exists(path), "new cache evidence output required");
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << data; out.close();
    require(static_cast<bool>(out), "cache evidence output failed");
}
inline fs::path value_path(const fs::path& prefix, std::size_t index) {
    return prefix.string()+".compile-"+std::to_string(index)+".cache";
}
template<class Recorded, class Request>
void save(const fs::path& prefix, const Recorded& recorded, const Request& request) {
    const auto& values=recorded.cache_evidence.compiles;
    require(values.size()==request.sources.size() && values.size()==recorded.result.compiles.size(),
            "one owning compile cache value per input source required");
    for (std::size_t i=0; i<values.size(); ++i) {
        const auto& e=values[i]; const auto& compiled=recorded.result.compiles[i].result;
        require(e.request.unit.source==request.sources[i].source &&
                e.request.cache_file==request.sources[i].artifacts.compile_cache &&
                e.request.source_dependencies_file==request.sources[i].artifacts.dependencies,
                "same-invocation source/cache/dependency order");
        require(e.request.unit.outputs.size()==1 &&
                e.request.unit.outputs[0].path==request.sources[i].artifacts.object &&
                e.cache_entry.outputs.size()==1 && e.cache_entry.outputs[0].path==request.sources[i].artifacts.object,
                "same-invocation object and cache output identity");
        require(e.request.options.configuration==request.compiler_options.configuration &&
                e.request.unit.source==e.cache_entry.source,
                "actual compile request configuration and source retained");
        require(compiled.compiled == (e.state!=CompileCacheEvidenceState::reused),
                "compiled result agrees with captured state");
        require(e.save_error.has_value()==(e.state==CompileCacheEvidenceState::save_failed),
                "typed save failure is neither erased nor fabricated");
        static_assert(!CompileCacheEvidence::producer_identity_verified &&
                      !CompileCacheEvidence::current_content_verified &&
                      !CompileCacheEvidence::complete_producer_inventory &&
                      !CompileCacheEvidence::deletion_authorized);
        const auto file=value_path(prefix,i); require(!fs::exists(file), "new captured cache output required");
        require(mqb::CompileCacheFile::save(file,e.cache_entry).has_value(), "persist test-only captured cache value");
        if (e.state!=CompileCacheEvidenceState::save_failed) {
            const auto disk=bytes(e.request.cache_file);
            require(bytes(file)==disk, "captured cache bytes equal this call's disk value");
            write(file.string()+".disk",disk);
        } else {
            require(e.save_error->file==e.request.cache_file &&
                    e.save_error->code==mqb::CompileCacheFileErrorCode::replace_failed,
                    "original cache-save error path and code retained");
        }
    }
}
template<class Target, class Request>
auto recorded(Target& target, const Request& request, const fs::path& prefix,
              std::optional<mqb::ArtifactGenerationLabel> label=std::nullopt) {
    mqb::performance::EvidenceSnapshot counts;
    auto result=[&] {
        mqb::performance::Collector collector;
        mqb::performance::Activation active{collector};
        auto value=target.run_recorded(request,std::move(label));
        counts=collector.snapshot();
        return value;
    }();
    const auto i=static_cast<std::size_t>(mqb::performance::CacheKind::compile);
    std::ostringstream out;
    out << "scope=ordinary_target_compile\nreads=" << counts.cache_files_opened[i]
        << "\nwrites=" << counts.cache_files_written[i] << "\nsuccess=" << bool(result) << '\n';
    // Preserve actual counters before any test-side assertion can reject them.
    write(prefix.string()+".compile-counts.txt",out.str());
    if (result) {
        if (!result->result.any_compiled) {
            require(counts.cache_files_opened[i]==request.sources.size() && counts.cache_files_written[i]==0,
                    "ordinary target warm call reads exactly once per source and never writes");
        }
        save(prefix,*result,request);
    }
    return result;
}
template<class Recorded>
void history(const fs::path& prefix, const Recorded& recorded) {
    for (std::size_t i=0; i<recorded.cache_evidence.compiles.size(); ++i) {
        const auto file=value_path(prefix,i), after=fs::path(file.string()+".after");
        require(!fs::exists(after), "new historical cache control required");
        require(mqb::CompileCacheFile::save(after,recorded.cache_evidence.compiles[i].cache_entry).has_value(),
                "persist test-only historical value");
        require(bytes(file)==bytes(after), "owned cache evidence survives later overwrites and failures");
    }
}

// BEGIN recorded generation consumer checks
template<class Recorded>
void generation(const Recorded& recorded, const char* phase, const fs::path& root,
                const mqb::StoragePathKey& key) {
    const std::vector<RecordedArtifactGenerationInput> inputs{
        {phase, {"native-fixture", "ordinary-target"}, "explicit-generation",
         std::cref(recorded), std::nullopt}};
    auto value = model_recorded_artifact_generations(inputs, {}, root, key);
    if (!value) throw std::runtime_error("native recorded generation: " + value.error().message);
    require(value->model.records.size()==1 && value->compiles.size()==1 &&
            value->compiles[0].size()==recorded.cache_evidence.compiles.size(),
            "native generation owns every source context");
    for (std::size_t i=0; i<value->compiles[0].size(); ++i) {
        const auto& context=value->compiles[0][i];
        const auto& evidence=recorded.cache_evidence.compiles[i];
        require(context.captured_signature==evidence.cache_entry.signature &&
                context.inspection_toolchain.compiler==evidence.inspection_toolchain.identity.compiler &&
                context.cache_toolchain.compiler==evidence.cache_entry.toolchain.compiler &&
                context.cache_toolchain.version==evidence.cache_entry.toolchain.version &&
                context.cache_toolchain.binary_stamp==evidence.cache_entry.toolchain.binary_stamp &&
                value->model.references.records[0].stages[i].working_directory==evidence.request.working_directory,
                "native generation preserves exact captured identity and compile cwd");
    }
    const auto& claim=value->model.records[0];
    if (claim.completion==mqb::ArtifactCompletion::executed)
        require(claim.state==ArtifactGenerationState::executed_claim, "native executed claim remains unverified but resolvable");
    else
        require(claim.state==ArtifactGenerationState::unresolved && !claim.origin_record,
                "standalone native reuse cannot invent its absent producer");
    std::cout << "recorded-generation phase=" << phase << " source_contexts=" << value->compiles[0].size()
              << " accepted=true deletion_authorized=false\n";
}
// END recorded generation consumer checks
} // namespace target_wave_cache_checks
