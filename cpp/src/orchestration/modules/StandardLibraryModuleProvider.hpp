#pragma once

#include <expected>
#include <vector>

#include "ModuleTargetArtifactRegistry.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

[[nodiscard]] std::expected<void, IncrementalModuleTargetError>
inject_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources);

} // namespace mqb::orchestration::detail
