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
        if (candidate.is_interface) {
            interface_candidates.push_back(candidate.unit_index);
        }
    }

    if (interface_candidates.size() > 1) {
        return std::unexpected(graph_failure(
            ModuleGraphErrorCode::duplicate_interface_provider,
            "multiple module interface units provide logical module '" + logical_name + "'",
            units[interface_candidates.front()].source,
            logical_name));
    }
    if (interface_candidates.size() == 1) {
        return interface_candidates.front();
    }
    if (candidates.size() == 1) {
        return candidates.front().unit_index;
    }

    return std::unexpected(graph_failure(
        ModuleGraphErrorCode::ambiguous_named_provider,
        "multiple non-interface units provide logical module '" + logical_name
            + "' without a unique interface provider",
        units[candidates.front().unit_index].source,
        logical_name));
}

} // namespace

std::expected<ModuleDependencyPlan, ModuleGraphError>
ModuleDependencyGraphBuilder::build(const std::vector<ScannedModuleUnit>& units) {
    DependencyGraph graph;
    std::unordered_map<std::string, std::size_t> unit_by_key;
    unit_by_key.reserve(units.size());

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
            if (provided.unique_on_source_path) {
                // Header-unit identity is source-based and is retained as an
                // unresolved typed requirement until header-unit compilation is
                // implemented. It must not collide with the named-module map.
                continue;
            }
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
    for (std::size_t consumer_index = 0; consumer_index < units.size(); ++consumer_index) {
        const std::string consumer_key = path_key(units[consumer_index].source);
        for (const auto& required : units[consumer_index].rule.required_modules) {
            const bool header_unit = required.unique_on_source_path
                || required.lookup_method != LookupMethod::by_name;
            if (header_unit) {
                plan.unresolved_requirements.push_back(UnresolvedModuleRequirement{
                    .consumer_source = units[consumer_index].source,
                    .requirement = required,
                    .kind = UnresolvedRequirementKind::header_unit,
                });
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

            if (provider->second == consumer_index) {
                // A source may mention its own module identity in scan metadata.
                // It does not create a useful build-order edge.
                continue;
            }

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
            const auto unit = unit_by_key.find(key);
            if (unit != unit_by_key.end()) {
                output_level.push_back(units[unit->second].source);
            }
        }
    }

    return plan;
}

} // namespace mqb::modules
