#pragma once

#include <expected>
#include <filesystem>
#include <vector>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

struct ModuleTargetScanBatch {
    std::vector<ModuleTargetScanResult> scans;
    std::vector<modules::ScannedModuleUnit> units;
};

struct ModuleTargetScanInspectionBatch {
    std::vector<ModuleTargetScanInspection> scans;
    // Reusable source scans are projected into units. Callers must not build a
    // graph unless complete is true because pending scans can change topology.
    std::vector<modules::ScannedModuleUnit> units;
    bool complete{true};
};

[[nodiscard]] std::expected<IncrementalModuleScanInspection, msvc::ModuleScanError>
inspect_module_source(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const std::filesystem::path& working_directory,
    msvc::MsvcModuleDependencyScanner& scanner);

[[nodiscard]] std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>
scan_module_source(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const std::filesystem::path& working_directory,
    msvc::MsvcModuleDependencyScanner& scanner);

[[nodiscard]] std::expected<ModuleTargetScanInspectionBatch, IncrementalModuleTargetError>
inspect_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

[[nodiscard]] std::expected<ModuleTargetScanBatch, IncrementalModuleTargetError>
scan_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner);

} // namespace mqb::orchestration::detail
