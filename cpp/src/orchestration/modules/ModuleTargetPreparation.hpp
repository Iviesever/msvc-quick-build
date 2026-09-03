#pragma once

#include <expected>
#include <optional>
#include <vector>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

struct ModuleTargetPreparation {
    std::vector<ModuleTargetScanResult> scans;
    modules::ModuleDependencyPlan plan;
    ModuleCompileWaveRequest compile_request;
    TargetTimings timings;
};

struct ModuleTargetPreparationInspection {
    std::vector<ModuleTargetScanInspection> scans;
    // Absent when at least one requested or toolchain-owned provider scan must
    // execute before a trustworthy graph can be constructed.
    std::optional<ModuleTargetPreparation> prepared;
};

[[nodiscard]] std::expected<ModuleTargetPreparationInspection, IncrementalModuleTargetError>
inspect_module_target_preparation(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

[[nodiscard]] std::expected<ModuleTargetPreparation, IncrementalModuleTargetError>
prepare_module_target(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

} // namespace mqb::orchestration::detail
