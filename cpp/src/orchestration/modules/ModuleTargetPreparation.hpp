#pragma once

#include <expected>
#include <vector>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

struct ModuleTargetPreparation {
    std::vector<ModuleTargetScanResult> scans;
    modules::ModuleDependencyPlan plan;
    ModuleCompileWaveRequest compile_request;
};

[[nodiscard]] std::expected<ModuleTargetPreparation, IncrementalModuleTargetError>
prepare_module_target(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

} // namespace mqb::orchestration::detail
