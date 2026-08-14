#include "mqb/modules/ModuleDependencyGraph.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::modules {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string graph_node_key(const fs::path& path) {
    const std::u8string utf8 = path.lexically_normal().generic_u8string();
    std::string key;
    key.reserve(utf8.size());
    for (const char8_t ch : utf8) {
        key.push_back(static_cast<char>(ch));
    }
    return key;
}

[[nodiscard]] std::string path_identity_key(const fs::path& path) {
    return mqb::platform::windows::path_identity_key(path);
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

[[nodiscard]] bool toolchain_owned_standard_module(const std::string_view logical_name) noexcept {
    return logical_name == "std" || logical_name == "std.compat";
}

} // namespace

std::expected<ModuleDependencyPlan, ModuleGraphError>
ModuleDependencyGraphBuilder::build(
    const std::vector<ScannedModuleUnit>& units,
    const std::span<const ExternalModuleProvider> external_providers) {
    DependencyGraph graph;
    std::unordered_map<std::string, std::size_t> unit_by_identity;
    std::unordered_map<std::string, fs::path> source_by_graph_key;
    unit_by_identity.reserve(units.size());
    source_by_graph_key.reserve(units.size());

    for (std::size_t index = 0; index < units.size(); ++index) {
        if (units[index].source.empty()) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::empty_source,
                "module graph unit source path is empty"));
        }
        const std::string identity = path_identity_key(units[index].source);
        if (unit_by_identity.contains(identity)) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::duplicate_source,
                "module graph contains the same source more than once",
                units[index].source));
        }
        unit_by_identity.emplace(identity, index);

        const std::string graph_key = graph_node_key(units[index].source);
        source_by_graph_key.emplace(graph_key, units[index].source);
        auto added = graph.add_node(graph_key);
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
            if (toolchain_owned_standard_module(provided.logical_name)
                && !units[index].toolchain_owned) {
                return std::unexpected(graph_failure(
                    ModuleGraphErrorCode::toolchain_owned_provider,
                    "standard-library module '" + provided.logical_name
                        + "' may only be provided by the selected MSVC toolchain",
                    units[index].source,
                    provided.logical_name));
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

    std::unordered_map<std::string, fs::path> external_provider_by_name;
    external_provider_by_name.reserve(external_providers.size());
    for (const auto& provider : external_providers) {
        if (provider.logical_name.empty() || provider.interface_file.empty()) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::invalid_external_provider,
                "external/prebuilt module provider requires a non-empty logical name and IFC path",
                provider.interface_file,
                provider.logical_name));
        }
        if (toolchain_owned_standard_module(provider.logical_name)) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::toolchain_owned_provider,
                "standard-library module '" + provider.logical_name
                    + "' is toolchain-owned and cannot be supplied through external module configuration",
                provider.interface_file,
                provider.logical_name));
        }
        if (provider_by_name.contains(provider.logical_name)) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::ambiguous_named_provider,
                "external/prebuilt provider conflicts with a project-local provider for logical module '"
                    + provider.logical_name + "'",
                provider.interface_file,
                provider.logical_name));
        }
        const auto [_, inserted] = external_provider_by_name.emplace(
            provider.logical_name,
            provider.interface_file.lexically_normal());
        if (!inserted) {
            return std::unexpected(graph_failure(
                ModuleGraphErrorCode::duplicate_external_provider,
                "multiple external/prebuilt providers are configured for logical module '"
                    + provider.logical_name + "'",
                provider.interface_file,
                provider.logical_name));
        }
    }

    ModuleDependencyPlan plan;
    std::unordered_map<std::string, std::size_t> header_unit_by_identity;

    for (std::size_t consumer_index = 0; consumer_index < units.size(); ++consumer_index) {
        const std::string consumer_graph_key = graph_node_key(units[consumer_index].source);
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
                const std::string header_identity = path_identity_key(header_source);
                if (unit_by_identity.contains(header_identity)) {
                    return std::unexpected(graph_failure(
                        ModuleGraphErrorCode::header_unit_source_conflict,
                        "header-unit source path collides with a scanned translation unit",
                        header_source,
                        required.logical_name));
                }

                auto header = header_unit_by_identity.find(header_identity);
                std::string header_graph_key;
                if (header == header_unit_by_identity.end()) {
                    header_graph_key = graph_node_key(header_source);
                    auto added = graph.add_node(header_graph_key);
                    if (!added) {
                        return std::unexpected(graph_failure(
                            ModuleGraphErrorCode::header_unit_source_conflict,
                            "header-unit source path collides with another graph node",
                            header_source,
                            required.logical_name));
                    }
                    source_by_graph_key.emplace(header_graph_key, header_source);
                    const std::size_t header_index = plan.header_units.size();
                    plan.header_units.push_back(PlannedHeaderUnit{
                        .source = header_source,
                        .header_name = required.logical_name,
                        .lookup_method = required.lookup_method,
                    });
                    header = header_unit_by_identity.emplace(header_identity, header_index).first;
                } else if (!same_header_identity(plan.header_units[header->second], required)) {
                    return std::unexpected(graph_failure(
                        ModuleGraphErrorCode::conflicting_header_unit_identity,
                        "the same header-unit source path is required with conflicting import identity",
                        header_source,
                        required.logical_name));
                }
                if (header_graph_key.empty()) {
                    header_graph_key = graph_node_key(plan.header_units[header->second].source);
                }

                plan.resolved_header_unit_dependencies.push_back(ResolvedHeaderUnitDependency{
                    .consumer_source = units[consumer_index].source,
                    .provider_source = header_source,
                    .header_name = required.logical_name,
                    .lookup_method = required.lookup_method,
                });

                auto dependency = graph.add_dependency(consumer_graph_key, header_graph_key);
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
                const auto external = external_provider_by_name.find(required.logical_name);
                if (external != external_provider_by_name.end()) {
                    plan.resolved_external_dependencies.push_back(ResolvedExternalModuleDependency{
                        .consumer_source = units[consumer_index].source,
                        .logical_name = required.logical_name,
                        .interface_file = external->second,
                    });
                    continue;
                }
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
                consumer_graph_key,
                graph_node_key(units[provider->second].source));
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
            const auto source = source_by_graph_key.find(key);
            if (source != source_by_graph_key.end()) output_level.push_back(source->second);
        }
    }

    return plan;
}

} // namespace mqb::modules
