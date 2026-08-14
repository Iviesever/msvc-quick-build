#include "ModuleTargetScanner.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"

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

} // namespace

std::expected<ModuleTargetScanBatch, IncrementalModuleTargetError>
scan_requested_module_sources(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner) {
    using ScanAttempt = std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>;
    std::vector<std::optional<ScanAttempt>> attempts(request.sources.size());

    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(), request.scan_parallelism,
        [&](const std::size_t index) {
            const auto& source = request.sources[index];
            msvc::ModuleScanInvocation invocation{
                .source = source.source,
                .output_file = source.artifacts.module_dependencies,
                .options = request.compiler_options,
                .kind = source.kind,
                .working_directory = working_directory_for(
                    request.working_directory,
                    source.source),
            };
            attempts[index].emplace(scanner.scan(invocation));
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
        if (scan.dependencies.rules.size() != 1) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_scan_result,
                "one-source module scan must produce exactly one P1689 rule",
                request.sources[index].source));
        }
        batch.units.push_back(modules::ScannedModuleUnit{
            .source = request.sources[index].source,
            .rule = scan.dependencies.rules.front(),
        });
        batch.scans.push_back(ModuleTargetScanResult{
            .source = request.sources[index].source,
            .result = std::move(scan),
        });
    }

    return batch;
}

} // namespace mqb::orchestration::detail
