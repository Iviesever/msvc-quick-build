#pragma once

#include <expected>
#include <vector>

#include "ModuleTargetArtifactRegistry.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

// Inspect every currently knowable toolchain-owned std/std.compat provider.
// Returns false when at least one provider scan must execute before the fixed
// point and final module graph can be known.
[[nodiscard]] std::expected<bool, IncrementalModuleTargetError>
inspect_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources,
    std::vector<ModuleTargetScanInspection>& scan_inspections);

[[nodiscard]] std::expected<void, IncrementalModuleTargetError>
inject_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources);

} // namespace mqb::orchestration::detail
