#include "ModuleTargetScanner.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/modules/P1689.hpp"
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

[[nodiscard]] std::optional<FileSnapshot> snapshot_path(
    const fs::path& path,
    const bool require_regular_file) {
    if (path.empty()) return std::nullopt;
    std::error_code error_code;
    const auto status = fs::status(path, error_code);
    if (error_code) return std::nullopt;
    const bool supported_type = require_regular_file
        ? fs::is_regular_file(status)
        : (fs::is_regular_file(status) || fs::is_directory(status));
    if (!supported_type) return std::nullopt;
    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) return std::nullopt;
    return FileSnapshot{
        .path = path,
        .exists = true,
        .modified = modified,
    };
}

[[nodiscard]] std::optional<FileSnapshot> snapshot_regular_file(const fs::path& path) {
    return snapshot_path(path, true);
}

[[nodiscard]] std::optional<FileSnapshot> snapshot_file_or_directory(const fs::path& path) {
    return snapshot_path(path, false);
}

[[nodiscard]] std::optional<std::string> read_text_file(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return std::nullopt;
    std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (stream.bad()) return std::nullopt;
    return text;
}

[[nodiscard]] std::optional<msvc::ModuleScanResult> try_reuse_scan(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    msvc::MsvcModuleDependencyScanner& scanner) {
    auto loaded = CompileCacheFile::load(source.artifacts.compile_cache);
    if (!loaded || !*loaded || !(**loaded).module_scan) {
        return std::nullopt;
    }
    const auto& evidence = *(**loaded).module_scan;

    const BuildSignature signature = BuildSignature::for_module_scan(
        source.source,
        source.kind,
        scanner.toolchain().identity,
        options);
    auto source_snapshot = snapshot_regular_file(source.source);
    auto output_snapshot = snapshot_regular_file(source.artifacts.module_dependencies);
    if (!source_snapshot || !output_snapshot) {
        return std::nullopt;
    }

    std::vector<FileSnapshot> dependency_snapshots;
    dependency_snapshots.reserve(evidence.dependencies.size());
    for (const auto& dependency : evidence.dependencies) {
        auto snapshot = snapshot_file_or_directory(dependency.path);
        if (!snapshot) return std::nullopt;
        dependency_snapshots.push_back(std::move(*snapshot));
    }

    if (!ModuleScanEvidenceValidator::reusable(
            evidence,
            signature,
            *source_snapshot,
            *output_snapshot,
            dependency_snapshots)) {
        return std::nullopt;
    }

    auto text = read_text_file(source.artifacts.module_dependencies);
    if (!text) return std::nullopt;
    auto dependencies = modules::P1689Parser::parse(*text);
    if (!dependencies) return std::nullopt;

    return msvc::ModuleScanResult{
        .process = process::ProcessResult{},
        .dependencies = std::move(*dependencies),
        .reused = true,
    };
}

} // namespace

std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>
scan_module_source(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const fs::path& working_directory,
    msvc::MsvcModuleDependencyScanner& scanner) {
    if (auto reused = try_reuse_scan(source, options, scanner)) {
        return std::move(*reused);
    }

    return scanner.scan(msvc::ModuleScanInvocation{
        .source = source.source,
        .output_file = source.artifacts.module_dependencies,
        .options = options,
        .kind = source.kind,
        .working_directory = working_directory_for(working_directory, source.source),
    });
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

    for (std::size_t index = 0; index < attempts.size(); ++index) {
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
