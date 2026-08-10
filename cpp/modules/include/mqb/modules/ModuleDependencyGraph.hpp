#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

struct ModuleDependencyPlan {
    // Each inner vector may be compiled concurrently. Earlier levels must
    // complete before later levels begin.
    std::vector<std::vector<std::filesystem::path>> compile_levels;
    // Named-module imports that were resolved to another source in this plan.
    // Orchestration uses these exact edges both for /reference wiring and for
    // downstream rebuild propagation; provider selection must not be repeated.
    std::vector<ResolvedModuleDependency> resolved_dependencies;
    std::vector<UnresolvedModuleRequirement> unresolved_requirements;
};

enum class ModuleGraphErrorCode {
    empty_source,
    duplicate_source,
    duplicate_interface_provider,
    ambiguous_named_provider,
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
    build(const std::vector<ScannedModuleUnit>& units);
};

} // namespace mqb::modules
