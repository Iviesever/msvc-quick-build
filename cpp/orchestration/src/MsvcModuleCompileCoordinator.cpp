#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] ModuleCompileError failure(
    const ModuleCompileErrorCode code,
    std::string message,
    fs::path source = {},
    fs::path provider_source = {},
    std::string logical_name = {}) {
    return ModuleCompileError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
        .provider_source = std::move(provider_source),
        .logical_name = std::move(logical_name),
    };
}

[[nodiscard]] ModuleCompileError artifact_failure(
    const ModuleCompileErrorCode code,
    std::string message,
    const fs::path& source,
    const fs::path& artifact) {
    return ModuleCompileError{
        .code = code,
        .message = std::move(message),
        .source = source,
        .artifact = artifact,
    };
}

[[nodiscard]] std::optional<ModuleCompileError> claim_artifact(
    std::unordered_set<std::string>& claimed,
    const fs::path& source,
    const fs::path& artifact,
    const std::string_view role) {
    if (artifact.empty()) {
        return artifact_failure(
            ModuleCompileErrorCode::invalid_artifact,
            "module compile " + std::string{role} + " artifact path is empty",
            source,
            artifact);
    }
    if (!claimed.emplace(windows_path_key(artifact)).second) {
        return artifact_failure(
            ModuleCompileErrorCode::artifact_collision,
            "module compile " + std::string{role}
                + " artifact collides with another planned writable artifact",
            source,
            artifact);
    }
    return std::nullopt;
}

[[nodiscard]] IncrementalCompileRequest compile_request_for(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const std::vector<ModuleReference>& references,
    const bool force_rebuild,
    const fs::path& working_directory) {
    TranslationUnit unit;
    unit.source = source.source;
    unit.kind = source.kind;
    unit.module_references = references;
    unit.outputs.push_back(Artifact{
        .path = source.artifacts.object,
        .kind = ArtifactKind::object,
    });
    if (source.kind == TranslationUnitKind::module_interface) {
        unit.outputs.push_back(Artifact{
            .path = source.artifacts.module_interface,
            .kind = ArtifactKind::module_interface,
        });
    }

    return IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = options,
        .cache_file = source.artifacts.compile_cache,
        .source_dependencies_file = source.artifacts.dependencies,
        .working_directory = working_directory.empty()
            ? std::optional<fs::path>{source.source.parent_path()}
            : std::optional<fs::path>{working_directory},
        .force_rebuild = force_rebuild,
    };
}

} // namespace

std::expected<ModuleCompileWaveResult, ModuleCompileError>
MsvcModuleCompileCoordinator::run(const ModuleCompileWaveRequest& request) const {
    if (request.sources.empty()) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::no_sources,
            "module compile wave requires at least one source"));
    }
    if (request.max_parallel_compiles == 0) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::invalid_parallelism,
            "module compile parallelism must be at least one"));
    }
    if (!request.plan.unresolved_requirements.empty()) {
        const auto& unresolved = request.plan.unresolved_requirements.front();
        return std::unexpected(failure(
            ModuleCompileErrorCode::unresolved_requirement,
            "module compile wave contains a requirement that this milestone cannot yet satisfy",
            unresolved.consumer_source,
            {},
            unresolved.requirement.logical_name));
    }
    if (!request.plan.header_units.empty()) {
        const auto& header = request.plan.header_units.front();
        const fs::path consumer = request.plan.resolved_header_unit_dependencies.empty()
            ? fs::path{}
            : request.plan.resolved_header_unit_dependencies.front().consumer_source;
        return std::unexpected(failure(
            ModuleCompileErrorCode::unresolved_requirement,
            "module dependency plan resolved a project-local header unit, but header-unit wave execution is not wired yet",
            consumer,
            header.source,
            header.header_name));
    }

    std::unordered_map<std::string, std::size_t> source_by_key;
    source_by_key.reserve(request.sources.size());
    std::unordered_set<std::string> claimed_artifacts;
    claimed_artifacts.reserve(request.sources.size() * 4u);

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        const auto& source = request.sources[index];
        const std::string key = windows_path_key(source.source);
        if (!source_by_key.emplace(key, index).second) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_source,
                "module compile request contains the same source more than once",
                source.source));
        }

        if (auto error = claim_artifact(
                claimed_artifacts, source.source, source.artifacts.object, "object")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                source.source,
                source.artifacts.dependencies,
                "source-dependency metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                source.source,
                source.artifacts.compile_cache,
                "compile-cache metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (source.kind == TranslationUnitKind::module_interface) {
            if (auto error = claim_artifact(
                    claimed_artifacts,
                    source.source,
                    source.artifacts.module_interface,
                    "IFC")) {
                return std::unexpected(std::move(*error));
            }
        }
    }

    std::vector<std::size_t> plan_occurrences(request.sources.size(), 0);
    std::vector<std::vector<std::size_t>> level_indices;
    level_indices.reserve(request.plan.compile_levels.size());
    for (const auto& level : request.plan.compile_levels) {
        auto& indices = level_indices.emplace_back();
        indices.reserve(level.size());
        for (const auto& source : level) {
            const auto found = source_by_key.find(windows_path_key(source));
            if (found == source_by_key.end()) {
                return std::unexpected(failure(
                    ModuleCompileErrorCode::plan_source_missing,
                    "module dependency plan references a source absent from the compile request",
                    source));
            }
            if (++plan_occurrences[found->second] != 1) {
                return std::unexpected(failure(
                    ModuleCompileErrorCode::plan_source_duplicate,
                    "module dependency plan schedules a source more than once",
                    source));
            }
            indices.push_back(found->second);
        }
    }
    for (std::size_t index = 0; index < plan_occurrences.size(); ++index) {
        if (plan_occurrences[index] == 0) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::plan_source_unlisted,
                "module compile source is missing from dependency-plan compile levels",
                request.sources[index].source));
        }
    }

    std::vector<std::vector<ModuleReference>> references(request.sources.size());
    std::vector<std::vector<std::size_t>> provider_indices(request.sources.size());
    for (const auto& dependency : request.plan.resolved_dependencies) {
        const auto consumer = source_by_key.find(windows_path_key(dependency.consumer_source));
        const auto provider = source_by_key.find(windows_path_key(dependency.provider_source));
        if (consumer == source_by_key.end() || provider == source_by_key.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::plan_source_missing,
                "resolved module dependency references a source absent from the compile request",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.logical_name));
        }

        const auto& provider_request = request.sources[provider->second];
        if (provider_request.kind != TranslationUnitKind::module_interface
            || provider_request.artifacts.module_interface.empty()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_provider,
                "resolved named-module provider cannot produce an IFC artifact",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.logical_name));
        }

        auto& consumer_references = references[consumer->second];
        const auto duplicate = std::find_if(
            consumer_references.begin(),
            consumer_references.end(),
            [&dependency](const ModuleReference& reference) {
                return reference.logical_name == dependency.logical_name;
            });
        if (duplicate != consumer_references.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_reference,
                "module dependency plan resolves the same logical name more than once for one consumer",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.logical_name));
        }

        consumer_references.push_back(ModuleReference{
            .logical_name = dependency.logical_name,
            .interface_file = provider_request.artifacts.module_interface,
        });
        provider_indices[consumer->second].push_back(provider->second);
    }

    using CompileAttempt = std::expected<IncrementalCompileResult, IncrementalCompileError>;
    std::vector<std::optional<CompileAttempt>> attempts(request.sources.size());
    std::vector<bool> compiled_this_run(request.sources.size(), false);

    for (const auto& level : level_indices) {
        const auto scheduled = BoundedWorkScheduler::run(
            level.size(),
            request.max_parallel_compiles,
            [&](const std::size_t level_index) {
                const std::size_t source_index = level[level_index];
                bool force_rebuild = false;
                for (const auto provider_index : provider_indices[source_index]) {
                    force_rebuild = force_rebuild || compiled_this_run[provider_index];
                }

                auto compile_request = compile_request_for(
                    request.sources[source_index],
                    request.compiler_options,
                    references[source_index],
                    force_rebuild,
                    request.working_directory);
                attempts[source_index].emplace(compile_coordinator_.run(compile_request));
                return attempts[source_index]->has_value();
            });
        if (!scheduled) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile scheduler failed: " + scheduled.error().message));
        }

        for (const auto source_index : level) {
            if (!attempts[source_index]) continue;
            if (!attempts[source_index]->has_value()) {
                ModuleCompileError error = failure(
                    ModuleCompileErrorCode::compile_failed,
                    "module translation unit compilation failed",
                    request.sources[source_index].source);
                error.compile_error = attempts[source_index]->error();
                return std::unexpected(std::move(error));
            }
        }

        for (const auto source_index : level) {
            if (!attempts[source_index]) {
                return std::unexpected(failure(
                    ModuleCompileErrorCode::scheduling_failed,
                    "module scheduler stopped without a recorded compile failure",
                    request.sources[source_index].source));
            }
            compiled_this_run[source_index] = attempts[source_index]->value().compiled;
        }
    }

    ModuleCompileWaveResult result;
    result.compiles.reserve(request.sources.size());
    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index] || !attempts[index]->has_value()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile result is missing after all dependency levels completed",
                request.sources[index].source));
        }
        auto compiled = std::move(attempts[index]->value());
        result.any_compiled = result.any_compiled || compiled.compiled;
        result.compiles.push_back(ModuleCompileResult{
            .source = request.sources[index].source,
            .result = std::move(compiled),
        });
    }
    return result;
}

} // namespace mqb::orchestration
