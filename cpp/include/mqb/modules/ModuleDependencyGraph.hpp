#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/DependencyGraph.hpp"
#include "mqb/modules/P1689.hpp"

namespace mqb::modules {

struct ScannedModuleUnit {
    std::filesystem::path source;
    P1689Rule rule;
};

enum class UnresolvedRequirementKind {
    named_module,
    header_unit,
};

struct UnresolvedModuleRequirement {
    std::filesystem::path consumer_source;
    RequiredModule requirement;
    UnresolvedRequirementKind kind{UnresolvedRequirementKind::named_module};
};

struct ResolvedModuleDependency {
    std::filesystem::path consumer_source;
    std::filesystem::path provider_source;
    std::string logical_name;
};

struct ResolvedExternalModuleDependency {
    std::filesystem::path consumer_source;
    std::string logical_name;
    std::filesystem::path interface_file;
};

struct PlannedHeaderUnit {
    std::filesystem::path source;
    std::string header_name;
    LookupMethod lookup_method{LookupMethod::include_quote};
};

struct ResolvedHeaderUnitDependency {
    std::filesystem::path consumer_source;
    std::filesystem::path provider_source;
    std::string header_name;
    LookupMethod lookup_method{LookupMethod::include_quote};
};

struct ModuleDependencyPlan {
    // Each inner vector may be compiled concurrently. Earlier levels must
    // complete before later levels begin. Resolved project-local header-unit
    // producer paths participate as first-class nodes alongside scanned TUs.
    std::vector<std::vector<std::filesystem::path>> compile_levels;
    // Named-module imports that were resolved to another source in this plan.
    std::vector<ResolvedModuleDependency> resolved_dependencies;
    // Explicit read-only external/prebuilt providers selected for consumers.
    // They do not become compile-level nodes because MQB does not own them.
    std::vector<ResolvedExternalModuleDependency> resolved_external_dependencies;
    // Unique project-local header-unit producer recipes selected from P1689
    // source-path identities. Execution assigns artifacts in a later layer.
    std::vector<PlannedHeaderUnit> header_units;
    // Consumer -> header-unit provider edges, preserving import spelling and
    // quote/angle lookup semantics for later /headerUnit routing.
    std::vector<ResolvedHeaderUnitDependency> resolved_header_unit_dependencies;
    std::vector<UnresolvedModuleRequirement> unresolved_requirements;
};

enum class ModuleGraphErrorCode {
    empty_source,
    duplicate_source,
    duplicate_interface_provider,
    ambiguous_named_provider,
    invalid_external_provider,
    duplicate_external_provider,
    toolchain_owned_provider,
    header_unit_source_conflict,
    conflicting_header_unit_identity,
    dependency_cycle,
};

struct ModuleGraphError {
    ModuleGraphErrorCode code{ModuleGraphErrorCode::empty_source};
    std::filesystem::path source;
    std::string logical_name;
    std::string message;
    std::optional<DependencyGraphError> graph_error;
};

class ModuleDependencyGraphBuilder {
public:
    [[nodiscard]] static std::expected<ModuleDependencyPlan, ModuleGraphError>
    build(
        const std::vector<ScannedModuleUnit>& units,
        std::span<const ExternalModuleProvider> external_providers = {});
};

} // namespace mqb::modules
