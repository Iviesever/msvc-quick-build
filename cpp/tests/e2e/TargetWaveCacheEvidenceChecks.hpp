#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <tuple>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/orchestration/ArtifactGenerationModel.hpp"
#include "mqb/orchestration/ArtifactGenerationArchive.hpp"
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

// BEGIN generation archive round-trip checks
inline void require_same_generation_model(const RecordedArtifactGenerationModel& before,
                                          const RecordedArtifactGenerationModel& after) {
    const auto label = [](const auto& a, const auto& b) {
        require(a.has_value()==b.has_value(), "archive model label presence");
        if (a) require(a->target==b->target && a->generation==b->generation,
                       "archive model label bytes");
    };
    const auto snapshot = [](const auto& a, const auto& b) {
        require(a.has_value()==b.has_value(), "archive model snapshot presence");
        if (a) {
            const auto x=mqb::encode_link_fact_snapshot(*a), y=mqb::encode_link_fact_snapshot(*b);
            require(x.has_value() && y.has_value() && *x==*y, "archive model complete snapshot");
        }
    };
    const auto identity = [](const auto& a, const auto& b) {
        require(std::tie(a.compiler,a.version,a.binary_stamp)==std::tie(b.compiler,b.version,b.binary_stamp),
                "archive model original toolchain identity");
    };
    const auto& a=before.model; const auto& b=after.model;
    require(a.records.size()==b.records.size(), "archive model record count");
    for (std::size_t i=0; i<a.records.size(); ++i) {
        const auto& x=a.records[i]; const auto& y=b.records[i];
        require(std::tie(x.source_id,x.target,x.generation,x.completion,x.recipe_evidence,x.state,x.origin_record,x.issues)==
                std::tie(y.source_id,y.target,y.generation,y.completion,y.recipe_evidence,y.state,y.origin_record,y.issues),
                "archive model complete record and provenance diagnostics");
        label(x.caller_label,y.caller_label); snapshot(x.snapshot,y.snapshot);
    }
    require(before.compiles.size()==after.compiles.size(), "archive model compile-record count");
    for (std::size_t i=0; i<before.compiles.size(); ++i) {
        require(before.compiles[i].size()==after.compiles[i].size(), "archive model source count");
        for (std::size_t j=0; j<before.compiles[i].size(); ++j) {
            const auto& x=before.compiles[i][j]; const auto& y=after.compiles[i][j];
            identity(x.inspection_toolchain,y.inspection_toolchain);
            identity(x.cache_toolchain,y.cache_toolchain);
            require(x.captured_signature==y.captured_signature && x.toolchains_differ==y.toolchains_differ &&
                    x.force_rebuild==y.force_rebuild && x.save_error.has_value()==y.save_error.has_value() &&
                    x.warnings.size()==y.warnings.size(), "archive model compile outcomes");
            if (x.save_error) {
                const auto& u=*x.save_error; const auto& v=*y.save_error;
                require(std::tie(u.code,u.file,u.offset,u.message)==std::tie(v.code,v.file,v.offset,v.message),
                        "archive model complete save failure");
            }
            for (std::size_t k=0; k<x.warnings.size(); ++k) {
                const auto& u=x.warnings[k]; const auto& v=y.warnings[k];
                require(std::tie(u.code,u.path,u.message)==std::tie(v.code,v.path,v.message),
                        "archive model warning order and bytes");
            }
        }
    }
    const auto& ar=a.references; const auto& br=b.references;
    require(ar.records.size()==br.records.size() && ar.matches.size()==br.matches.size() &&
            ar.row_matches==br.row_matches, "archive model reference collection sizes");
    for (std::size_t i=0; i<ar.records.size(); ++i) {
        const auto& x=ar.records[i]; const auto& y=br.records[i];
        label(x.caller_label,y.caller_label);
        require(x.stages.size()==y.stages.size(), "archive model stage count");
        for (std::size_t j=0; j<x.stages.size(); ++j) {
            const auto& u=x.stages[j]; const auto& v=y.stages[j];
            require(std::tie(u.kind,u.completion,u.cache_state,u.source_has_warnings,u.configuration,
                             u.working_directory,u.toolchain_source)==
                    std::tie(v.kind,v.completion,v.cache_state,v.source_has_warnings,v.configuration,
                             v.working_directory,v.toolchain_source) && u.paths.size()==v.paths.size(),
                    "archive model complete stage and own cwd");
            for (std::size_t k=0; k<u.paths.size(); ++k)
                require(u.paths[k].path==v.paths[k].path && u.paths[k].role==v.paths[k].role,
                        "archive model literal ordered path and role");
        }
    }
    for (std::size_t i=0; i<ar.matches.size(); ++i) {
        const auto& x=ar.matches[i]; const auto& y=br.matches[i];
        require(std::tie(x.record_index,x.stage_index,x.path_index,x.resolved_path,x.state,x.observed_rows,
                         x.same_observed_file_rows,x.observed_identity_metadata_conflict)==
                std::tie(y.record_index,y.stage_index,y.path_index,y.resolved_path,y.state,y.observed_rows,
                         y.same_observed_file_rows,y.observed_identity_metadata_conflict),
                "archive model complete lexical match");
    }
    // Both model entry points deliberately associate an empty observation set.
    // A readback must not manufacture a current filesystem observation.
    require(ar.observation.artifact_root==br.observation.artifact_root &&
            !ar.observation.root_exists && !br.observation.root_exists &&
            ar.observation.entries.empty() && br.observation.entries.empty() &&
            ar.observation.references.empty() && br.observation.references.empty() &&
            ar.observation.issues.empty() && br.observation.issues.empty(),
            "archive model retains empty unverified observation");
    require(a.retention.size()==b.retention.size() && a.paths.size()==b.paths.size(),
            "archive model retention and path-group sizes");
    for (std::size_t i=0; i<a.retention.size(); ++i) {
        const auto& x=a.retention[i]; const auto& y=b.retention[i];
        require(std::tie(x.request,x.state,x.executed_candidates,x.associated_records)==
                std::tie(y.request,y.state,y.executed_candidates,y.associated_records),
                "archive model retention requests and decisions");
    }
    for (std::size_t i=0; i<a.paths.size(); ++i) {
        const auto& x=a.paths[i]; const auto& y=b.paths[i];
        require(std::tie(x.path_key,x.matches,x.records,x.retained_records,x.shared_reference,
                         x.multiple_executed_generations,x.different_target_or_recipe,x.unresolved_members)==
                std::tie(y.path_key,y.matches,y.records,y.retained_records,y.shared_reference,
                         y.multiple_executed_generations,y.different_target_or_recipe,y.unresolved_members),
                "archive model complete path-group diagnostics");
    }
}
inline void generation_archive_round_trip(std::span<const RecordedArtifactGenerationInput> inputs,
                                         const RecordedArtifactGenerationModel& original,
                                         const fs::path& root, const mqb::StoragePathKey& key) {
    auto selected=project_artifact_generation_archive(inputs, {}, root, key);
    if (!selected) throw std::runtime_error("native generation archive projection: "+selected.error().message);
    auto encoded=encode_artifact_generation_archive(*selected,key);
    if (!encoded) throw std::runtime_error("native generation archive encode: "+encoded.error().message);
    auto decoded=decode_artifact_generation_archive(*encoded,key);
    if (!decoded) throw std::runtime_error("native generation archive decode: "+decoded.error().message);
    auto repeated=encode_artifact_generation_archive(*decoded,key);
    require(repeated.has_value() && *repeated==*encoded, "native archive deterministic complete wire round trip");
    auto modeled=model_archived_artifact_generations(*decoded,key);
    if (!modeled) throw std::runtime_error("native generation archive model: "+modeled.error().message);
    require_same_generation_model(original,*modeled);
    static_assert(!ArtifactGenerationArchive::producer_identity_verified &&
                  !ArtifactGenerationArchive::current_content_verified &&
                  !ArtifactGenerationArchive::complete_producer_inventory &&
                  !ArtifactGenerationArchive::deletion_authorized);
    std::cout << "generation-archive phase=" << inputs.front().source_id << " bytes=" << encoded->size()
              << " complete_model_equal=true producer_identity_verified=false current_content_verified=false"
              << " complete_producer_inventory=false deletion_authorized=false\n";
}
// END generation archive round-trip checks

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
    generation_archive_round_trip(inputs, *value, root, key);
}
// END recorded generation consumer checks
} // namespace target_wave_cache_checks
