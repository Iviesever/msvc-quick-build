#include "ModuleCompilePlan.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    const std::u8string utf8 = path.lexically_normal().generic_u8string();
    std::string value;
    value.reserve(utf8.size());
    for (const char8_t ch : utf8) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= static_cast<unsigned char>('A')
            && byte <= static_cast<unsigned char>('Z')) {
            value.push_back(static_cast<char>(byte + ('a' - 'A')));
        } else {
            value.push_back(static_cast<char>(byte));
        }
    }
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

[[nodiscard]] std::optional<HeaderUnitLookupMethod> core_lookup_method(
    const modules::LookupMethod lookup_method) {
    switch (lookup_method) {
    case modules::LookupMethod::include_angle:
        return HeaderUnitLookupMethod::angle;
    case modules::LookupMethod::include_quote:
        return HeaderUnitLookupMethod::quote;
    case modules::LookupMethod::by_name:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleCompileError> merge_module_reference(
    std::vector<ModuleReference>& references,
    const ModuleReference& incoming,
    const fs::path& consumer_source) {
    const auto existing = std::find_if(
        references.begin(), references.end(),
        [&](const ModuleReference& reference) {
            return reference.logical_name == incoming.logical_name;
        });
    if (existing == references.end()) {
        references.push_back(incoming);
        return std::nullopt;
    }
    if (windows_path_key(existing->interface_file)
        != windows_path_key(incoming.interface_file)) {
        return failure(
            ModuleCompileErrorCode::duplicate_reference,
            "transitive module reference closure resolves logical name '"
                + incoming.logical_name + "' to different IFC artifacts",
            consumer_source,
            incoming.interface_file,
            incoming.logical_name);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleCompileError> merge_header_reference(
    std::vector<HeaderUnitReference>& references,
    const HeaderUnitReference& incoming,
    const fs::path& consumer_source) {
    const auto existing = std::find_if(
        references.begin(), references.end(),
        [&](const HeaderUnitReference& reference) {
            return reference.header_name == incoming.header_name
                && reference.lookup_method == incoming.lookup_method;
        });
    if (existing == references.end()) {
        references.push_back(incoming);
        return std::nullopt;
    }
    if (windows_path_key(existing->interface_file)
        != windows_path_key(incoming.interface_file)) {
        return failure(
            ModuleCompileErrorCode::duplicate_reference,
            "transitive header-unit reference closure resolves one header identity to different IFC artifacts",
            consumer_source,
            incoming.interface_file,
            incoming.header_name);
    }
    return std::nullopt;
}

} // namespace

std::expected<ModuleCompilePlan, ModuleCompileError>
build_module_compile_plan(const ModuleCompileWaveRequest& request) {
    if (request.sources.empty() && request.header_units.empty()) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::no_sources,
            "module compile wave requires at least one source or header-unit producer"));
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

    ModuleCompilePlan plan{
        .source_count = request.sources.size(),
        .header_count = request.header_units.size(),
    };
    const std::size_t node_count = plan.node_count();

    std::unordered_map<std::string, std::size_t> node_by_key;
    node_by_key.reserve(node_count);
    std::unordered_map<std::string, std::size_t> source_by_key;
    source_by_key.reserve(plan.source_count);
    std::unordered_map<std::string, std::size_t> header_by_key;
    header_by_key.reserve(plan.header_count);
    std::unordered_set<std::string> claimed_artifacts;
    claimed_artifacts.reserve(plan.source_count * 4u + plan.header_count * 3u);

    for (std::size_t index = 0; index < plan.source_count; ++index) {
        const auto& source = request.sources[index];
        const std::string key = windows_path_key(source.source);
        if (!node_by_key.emplace(key, index).second) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_source,
                "module compile request contains the same source more than once",
                source.source));
        }
        source_by_key.emplace(key, index);

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

    for (std::size_t index = 0; index < plan.header_count; ++index) {
        const auto& header = request.header_units[index];
        const std::size_t node_index = plan.source_count + index;
        const std::string key = windows_path_key(header.source);
        if (!node_by_key.emplace(key, node_index).second) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_source,
                "header-unit producer source collides with another compile source",
                header.source));
        }
        header_by_key.emplace(key, index);
        if (header.header_name.empty()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_header_unit,
                "header-unit producer name is empty",
                header.source));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                header.artifacts.dependencies,
                "header-unit source-dependency metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                header.artifacts.compile_cache,
                "header-unit compile-cache metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                header.artifacts.module_interface,
                "header-unit IFC")) {
            return std::unexpected(std::move(*error));
        }
    }

    std::unordered_map<std::string, const modules::PlannedHeaderUnit*> planned_header_by_key;
    planned_header_by_key.reserve(request.plan.header_units.size());
    for (const auto& planned : request.plan.header_units) {
        planned_header_by_key.emplace(windows_path_key(planned.source), &planned);
    }
    if (planned_header_by_key.size() != request.header_units.size()) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::invalid_header_unit,
            "header-unit compile request does not match dependency-plan producer count"));
    }
    for (const auto& header : request.header_units) {
        const auto planned = planned_header_by_key.find(windows_path_key(header.source));
        if (planned == planned_header_by_key.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_header_unit,
                "header-unit compile request contains a provider absent from dependency plan",
                header.source));
        }
        const auto lookup = core_lookup_method(planned->second->lookup_method);
        if (!lookup
            || planned->second->header_name != header.header_name
            || *lookup != header.lookup_method) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_header_unit,
                "header-unit compile request identity does not match dependency plan",
                header.source,
                {},
                header.header_name));
        }
    }

    std::vector<std::size_t> plan_occurrences(node_count, 0);
    plan.level_indices.reserve(request.plan.compile_levels.size());
    for (const auto& level : request.plan.compile_levels) {
        auto& indices = plan.level_indices.emplace_back();
        indices.reserve(level.size());
        for (const auto& source : level) {
            const auto found = node_by_key.find(windows_path_key(source));
            if (found == node_by_key.end()) {
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
            const fs::path source = index < plan.source_count
                ? request.sources[index].source
                : request.header_units[index - plan.source_count].source;
            return std::unexpected(failure(
                ModuleCompileErrorCode::plan_source_unlisted,
                "module compile source is missing from dependency-plan compile levels",
                source));
        }
    }

    plan.module_references.resize(node_count);
    plan.header_references.resize(node_count);
    plan.provider_indices.resize(node_count);

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

        auto& references = plan.module_references[consumer->second];
        const auto duplicate = std::find_if(
            references.begin(), references.end(),
            [&dependency](const ModuleReference& reference) {
                return reference.logical_name == dependency.logical_name;
            });
        if (duplicate != references.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_reference,
                "module dependency plan resolves the same logical name more than once for one consumer",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.logical_name));
        }
        references.push_back(ModuleReference{
            .logical_name = dependency.logical_name,
            .interface_file = provider_request.artifacts.module_interface,
        });
        plan.provider_indices[consumer->second].push_back(provider->second);
    }

    for (const auto& dependency : request.plan.resolved_external_dependencies) {
        const auto consumer = source_by_key.find(windows_path_key(dependency.consumer_source));
        if (consumer == source_by_key.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::plan_source_missing,
                "resolved external module dependency references a consumer absent from the compile request",
                dependency.consumer_source,
                dependency.interface_file,
                dependency.logical_name));
        }
        std::error_code ec;
        const bool regular = fs::is_regular_file(dependency.interface_file, ec);
        if (ec || !regular) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_provider,
                "external/prebuilt named-module provider IFC is not an existing regular file",
                dependency.consumer_source,
                dependency.interface_file,
                dependency.logical_name));
        }

        auto& references = plan.module_references[consumer->second];
        const auto duplicate = std::find_if(
            references.begin(), references.end(),
            [&dependency](const ModuleReference& reference) {
                return reference.logical_name == dependency.logical_name;
            });
        if (duplicate != references.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_reference,
                "module dependency plan resolves the same logical name more than once for one consumer",
                dependency.consumer_source,
                dependency.interface_file,
                dependency.logical_name));
        }
        references.push_back(ModuleReference{
            .logical_name = dependency.logical_name,
            .interface_file = dependency.interface_file,
        });
    }

    for (const auto& dependency : request.plan.resolved_header_unit_dependencies) {
        const auto consumer = source_by_key.find(windows_path_key(dependency.consumer_source));
        const auto provider = header_by_key.find(windows_path_key(dependency.provider_source));
        if (consumer == source_by_key.end() || provider == header_by_key.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::plan_source_missing,
                "resolved header-unit dependency references a source absent from the compile request",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.header_name));
        }
        const auto lookup = core_lookup_method(dependency.lookup_method);
        if (!lookup) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_header_unit,
                "resolved header-unit dependency has a non-header lookup method",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.header_name));
        }
        const auto& provider_request = request.header_units[provider->second];
        if (provider_request.artifacts.module_interface.empty()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::invalid_provider,
                "resolved header-unit provider cannot produce an IFC artifact",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.header_name));
        }

        auto& references = plan.header_references[consumer->second];
        const auto duplicate = std::find_if(
            references.begin(), references.end(),
            [&](const HeaderUnitReference& reference) {
                return reference.header_name == dependency.header_name
                    && reference.lookup_method == *lookup;
            });
        if (duplicate != references.end()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::duplicate_reference,
                "module dependency plan resolves the same header-unit identity more than once for one consumer",
                dependency.consumer_source,
                dependency.provider_source,
                dependency.header_name));
        }
        references.push_back(HeaderUnitReference{
            .header_name = dependency.header_name,
            .lookup_method = *lookup,
            .interface_file = provider_request.artifacts.module_interface,
        });
        plan.provider_indices[consumer->second].push_back(plan.source_count + provider->second);
    }

    // MSVC validates imported IFCs while compiling a downstream module. An IFC
    // can itself import named modules or header units, so a consumer needs the
    // reference closure of its direct providers, not only the references named
    // in the consumer's own P1689 rule. Propagate that closure provider-first
    // using the already validated dependency levels. Keep direct provider
    // indices unchanged: fresh rebuild propagation naturally cascades one level
    // at a time through the same graph.
    for (const auto& level : plan.level_indices) {
        for (const std::size_t consumer_index : level) {
            if (consumer_index >= plan.source_count) continue;
            for (const std::size_t provider_index : plan.provider_indices[consumer_index]) {
                if (provider_index >= plan.source_count) continue;
                for (const auto& reference : plan.module_references[provider_index]) {
                    if (auto error = merge_module_reference(
                            plan.module_references[consumer_index],
                            reference,
                            request.sources[consumer_index].source)) {
                        return std::unexpected(std::move(*error));
                    }
                }
                for (const auto& reference : plan.header_references[provider_index]) {
                    if (auto error = merge_header_reference(
                            plan.header_references[consumer_index],
                            reference,
                            request.sources[consumer_index].source)) {
                        return std::unexpected(std::move(*error));
                    }
                }
            }
        }
    }

    return plan;
}

} // namespace mqb::orchestration::detail
