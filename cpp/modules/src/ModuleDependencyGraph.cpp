#include "mqb/modules/ModuleDependencyGraph.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mqb::modules {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_key(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] ModuleGraphError graph_failure(
    const ModuleGraphErrorCode code,
    std::string message,
    fs::path source = {},
    std::string logical_name = {}) {
    return ModuleGraphError{
        .code = code,
        .source = std::move(source),
        .logical_name = std::move(logical_name),
        .message = std::move(message),
    };
}

struct ProviderCandidate {
    std::size_t unit_index{};
    bool is_interface{true};
};

[[nodiscard]] std::expected<std::size_t, ModuleGraphError> select_provider(
    const std::string& logical_name,
    const std::vector<ProviderCandidate>& candidates,
    const std::vector<ScannedModuleUnit>& units) {
    std::vector<std::size_t> interface_candidates;
    for (const auto& candidate : candidates) {
        if (candidate.is_interface) interface_candidates.push_back(candidate.unit_index);
    }

    if (interface_candidates.size() > 1) {
        return std::unexpected(graph_failure(
            ModuleGraphErrorCode::duplicate_interface_provider,
            "multiple module interface units provide logical module '" + logical_name + "'",
            units[interface_candidates.front()].source,
            logical_name));
    }
    if (interface_candidates.size() == 1) return interface_candidates.front();
    if (candidates.size() == 1) return candidates.front().unit_index;

    return std::unexpected(graph_failure(
        ModuleGraphErrorCode::ambiguous_named_provider,
        "multiple non-interface units provide logical module '" + logical_name
            + "' without a unique interface provider",
        units[candidates.front().unit_index].source,
        logical_name));
}

[[nodiscard]] bool same_header_identity(
    const PlannedHeaderUnit& planned,
    const RequiredModule& required) {
    return planned.header_name == required.logical_name
        && planned.lookup_method == required.lookup_method;
}

} // namespace

std::expected<ModuleDependencyPlan, ModuleGraphError>
ModuleDependencyGraphBuilder::build(const std::vector<ScannedModuleUnit>& units) {
    DependencyGraph graph;
    std::unordered_map<std::string, std::size_t> unit_by_key;
    std::unordered_map<std::string, fs::path> source_by_key;
    unit_by_key.reserve(units.size());
    source_by_key.reserve(units.size());

    for (std::size_t index = 0; index < units.size(); ++index) {
        if (units[index].source.empty()) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::empty_source,
                "module graph unit source path is empty"));
        }
        const std::string key = path_key(units[index].source);
        if (unit_by_key.contains(key)) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::duplicate_source,
                "module graph contains the same source more than once",
                units[index].source));
        }
        unit_by_key.emplace(key, index);
        source_by_key.emplace(key, units[index].source);
        auto added = graph.add_node(key);
        if (!added) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::duplicate_source,
                "module graph contains the same source more than once",
                units[index].source));
        }
    }

    std::unordered_map<std::string, std::vector<ProviderCandidate>> provider_candidates;
    for (std::size_t index = 0; index < units.size(); ++index) {
        for (const auto& provided : units[index].rule.provided_modules) {
            if (provided.unique_on_source_path) continue;
            provider_candidates[provided.logical_name].push_back(ProviderCandidate{
                .unit_index = index,
                .is_interface = provided.is_interface,
            });
        }
    }

    std::unordered_map<std::string, std::size_t> provider_by_name;
    provider_by_name.reserve(provider_candidates.size());
    for (const auto& [logical_name, candidates] : provider_candidates) {
        auto provider = select_provider(logical_name, candidates, units);
        if (!provider) return std::unexpected(provider.error());
        provider_by_name.emplace(logical_name, *provider);
    }

    ModuleDependencyPlan plan;
    std::unordered_map<std::string, std::size_t> header_unit_by_key;

    for (std::size_t consumer_index = 0; consumer_index < units.size(); ++consumer_index) {
        const std::string consumer_key = path_key(units[consumer_index].source);
        for (const auto& required : units[consumer_index].rule.required_modules) {
            const bool header_unit = required.unique_on_source_path
                || required.lookup_method != LookupMethod::by_name;
            if (header_unit) {
                if (!required.source_path || required.source_path->empty()) {
                    plan.unresolved_requirements.push_back(UnresolvedModuleRequirement{
                        .consumer_source = units[consumer_index].source,
                        .requirement = required,
                        .kind = UnresolvedRequirementKind::header_unit,
                    });
                    continue;
                }

                const fs::path header_source = required.source_path->lexically_normal();
                const std::string header_key = path_key(header_source);
                if (unit_by_key.contains(header_key)) {
                    return std::unexpected(graph_failure(
                        ModuleGraphErrorCode::header_unit_source_conflict,
                        "header-unit source path collides with a scanned translation unit",
                        header_source,
                        required.logical_name));
                }

                auto header = header_unit_by_key.find(header_key);
                if (header == header_unit_by_key.end()) {
                    auto added = graph.add_node(header_key);
                    if (!added) {
                        return std::unexpected(graph_failure(
                            ModuleGraphErrorCode::header_unit_source_conflict,
                            "header-unit source path collides with another graph node",
                            header_source,
                            required.logical_name));
                    }
                    source_by_key.emplace(header_key, header_source);
                    const std::size_t header_index = plan.header_units.size();
                    plan.header_units.push_back(PlannedHeaderUnit{
                        .source = header_source,
                        .header_name = required.logical_name,
                        .lookup_method = required.lookup_method,
                    });
                    header = header_unit_by_key.emplace(header_key, header_index).first;
                } else if (!same_header_identity(plan.header_units[header->second], required)) {
                    return std::unexpected(graph_failure(
                        ModuleGraphErrorCode::conflicting_header_unit_identity,
                        "the same header-unit source path is required with conflicting import identity",
                        header_source,
                        required.logical_name));
                }

                plan.resolved_header_unit_dependencies.push_back(ResolvedHeaderUnitDependency{
                    .consumer_source = units[consumer_index].source,
                    .provider_source = header_source,
                    .header_name = required.logical_name,
                    .lookup_method = required.lookup_method,
                });

                auto dependency = graph.add_dependency(consumer_key, header_key);
                if (!dependency) {
                    ModuleGraphError error = graph_failure(
                        ModuleGraphErrorCode::dependency_cycle,
                        "failed to add header-unit dependency edge",
                        units[consumer_index].source,
                        required.logical_name);
                    error.graph_error = dependency.error();
                    return std::unexpected(std::move(error));
                }
                continue;
            }

            const auto provider = provider_by_name.find(required.logical_name);
            if (provider == provider_by_name.end()) {
                plan.unresolved_requirements.push_back(UnresolvedModuleRequirement{
                    .consumer_source = units[consumer_index].source,
                    .requirement = required,
                    .kind = UnresolvedRequirementKind::named_module,
                });
                continue;
            }

            if (provider->second == consumer_index) continue;

            plan.resolved_dependencies.push_back(ResolvedModuleDependency{
                .consumer_source = units[consumer_index].source,
                .provider_source = units[provider->second].source,
                .logical_name = required.logical_name,
            });

            auto dependency = graph.add_dependency(
                consumer_key,
                path_key(units[provider->second].source));
            if (!dependency) {
                ModuleGraphError error = graph_failure(
                    ModuleGraphErrorCode::dependency_cycle,
                    "failed to add module dependency edge",
                    units[consumer_index].source,
                    required.logical_name);
                error.graph_error = dependency.error();
                return std::unexpected(std::move(error));
            }
        }
    }

    auto levels = graph.topological_levels();
    if (!levels) {
        ModuleGraphError error = graph_failure(
            ModuleGraphErrorCode::dependency_cycle,
            "module dependency graph contains a cycle");
        error.graph_error = levels.error();
        return std::unexpected(std::move(error));
    }

    plan.compile_levels.reserve(levels->size());
    for (const auto& level : *levels) {
        auto& output_level = plan.compile_levels.emplace_back();
        output_level.reserve(level.size());
        for (const auto& key : level) {
            const auto source = source_by_key.find(key);
            if (source != source_by_key.end()) output_level.push_back(source->second);
        }
    }

    return plan;
}

} // namespace mqb::modules
