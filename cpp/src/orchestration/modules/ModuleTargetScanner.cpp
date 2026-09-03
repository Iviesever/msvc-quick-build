#include "ModuleTargetScanner.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"
#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] IncrementalModuleTargetError failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] std::optional<fs::path> working_directory_for(
    const fs::path& requested,
    const fs::path& source) {
    if (!requested.empty()) return requested;
    if (!source.parent_path().empty()) return source.parent_path();
    return std::nullopt;
}

[[nodiscard]] IncrementalModuleScanRequest make_scan_request(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const fs::path& working_directory) {
    return IncrementalModuleScanRequest{
        .source = source.source,
        .module_dependencies_file = source.artifacts.module_dependencies,
        .compile_cache_file = source.artifacts.compile_cache,
        .options = options,
        .kind = source.kind,
        .working_directory = working_directory_for(
            working_directory,
            source.source),
    };
}

[[nodiscard]] IncrementalModuleTargetError scan_failure(
    const fs::path& source,
    msvc::ModuleScanError error) {
    IncrementalModuleTargetError result = failure(
        IncrementalModuleTargetErrorCode::scan_failed,
        "module dependency scan inspection failed",
        source);
    result.scan_error = std::move(error);
    return result;
}

[[nodiscard]] std::expected<modules::ScannedModuleUnit, IncrementalModuleTargetError>
scanned_unit_from(
    const fs::path& source,
    const modules::P1689Document& dependencies,
    const bool toolchain_owned = false) {
    if (dependencies.rules.size() != 1) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::invalid_scan_result,
            "one-source module scan must produce exactly one P1689 rule",
            source));
    }
    return modules::ScannedModuleUnit{
        .source = source,
        .rule = dependencies.rules.front(),
        .toolchain_owned = toolchain_owned,
    };
}

} // namespace

std::expected<IncrementalModuleScanInspection, msvc::ModuleScanError>
inspect_module_source(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const fs::path& working_directory,
    msvc::MsvcModuleDependencyScanner& scanner) {
    MsvcIncrementalModuleScanCoordinator coordinator{scanner};
    return coordinator.inspect(make_scan_request(
        source,
        options,
        working_directory));
}

std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>
scan_module_source(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const fs::path& working_directory,
    msvc::MsvcModuleDependencyScanner& scanner) {
    MsvcIncrementalModuleScanCoordinator coordinator{scanner};
    auto scanned = coordinator.run(make_scan_request(
        source,
        options,
        working_directory));
    if (!scanned) return std::unexpected(scanned.error());
    return std::move(scanned->result);
}

std::expected<ModuleTargetScanInspectionBatch, IncrementalModuleTargetError>
inspect_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner) {
    using InspectionAttempt = std::expected<
        IncrementalModuleScanInspection,
        msvc::ModuleScanError>;
    std::vector<std::optional<InspectionAttempt>> attempts(request.sources.size());

    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_scans,
        ParallelismWorkload::dependency_scan,
        [&](const std::size_t index) {
            attempts[index].emplace(inspect_module_source(
                request.sources[index],
                request.compiler_options,
                request.working_directory,
                scanner));
            return attempts[index]->has_value();
        });
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::scheduling_failed,
            "module scan inspection scheduler failed: " + scheduled.error().message));
    }

    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (attempts[index] && !attempts[index]->has_value()) {
            return std::unexpected(scan_failure(
                request.sources[index].source,
                attempts[index]->error()));
        }
    }

    ModuleTargetScanInspectionBatch batch;
    batch.scans.reserve(request.sources.size());
    batch.units.reserve(request.sources.size() + 2u);

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index]) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::scheduling_failed,
                "module scan inspection scheduler stopped without a recorded failure",
                request.sources[index].source));
        }

        auto inspection = std::move(attempts[index]->value());
        if (inspection.reusable()) {
            auto unit = scanned_unit_from(
                request.sources[index].source,
                *inspection.dependencies);
            if (!unit) return std::unexpected(std::move(unit.error()));
            batch.units.push_back(std::move(*unit));
        } else {
            batch.complete = false;
        }
        batch.scans.push_back(ModuleTargetScanInspection{
            .source = request.sources[index].source,
            .result = std::move(inspection),
            .toolchain_owned = false,
        });
    }

    return batch;
}

std::expected<ModuleTargetScanBatch, IncrementalModuleTargetError>
scan_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner) {
    using ScanAttempt = std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>;
    std::vector<std::optional<ScanAttempt>> attempts(request.sources.size());

    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_scans,
        ParallelismWorkload::dependency_scan,
        [&](const std::size_t index) {
            attempts[index].emplace(scan_module_source(
                request.sources[index],
                request.compiler_options,
                request.working_directory,
                scanner));
            return attempts[index]->has_value();
        });
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::scheduling_failed,
            "module scan scheduler failed: " + scheduled.error().message));
    }

    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (attempts[index] && !attempts[index]->has_value()) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "module dependency scan failed",
                request.sources[index].source);
            error.scan_error = attempts[index]->error();
            return std::unexpected(std::move(error));
        }
    }

    ModuleTargetScanBatch batch;
    batch.scans.reserve(request.sources.size());
    batch.units.reserve(request.sources.size() + 2u);

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index]) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::scheduling_failed,
                "module scan scheduler stopped without a recorded scan failure",
                request.sources[index].source));
        }
        auto scan = std::move(attempts[index]->value());
        auto unit = scanned_unit_from(
            request.sources[index].source,
            scan.dependencies);
        if (!unit) return std::unexpected(std::move(unit.error()));
        batch.units.push_back(std::move(*unit));
        batch.scans.push_back(ModuleTargetScanResult{
            .source = request.sources[index].source,
            .result = std::move(scan),
        });
    }

    return batch;
}

} // namespace mqb::orchestration::detail
