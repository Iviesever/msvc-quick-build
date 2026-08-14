#pragma once

#include <expected>
#include <vector>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

struct ModuleTargetScanBatch {
    std::vector<ModuleTargetScanResult> scans;
    std::vector<modules::ScannedModuleUnit> units;
};

[[nodiscard]] std::expected<ModuleTargetScanBatch, IncrementalModuleTargetError>
scan_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

} // namespace mqb::orchestration::detail
