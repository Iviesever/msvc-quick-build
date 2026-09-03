#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/msvc/MsvcIncludeSearchFreshness.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

void add_reason(
    std::vector<ModuleScanReason>& reasons,
    const ModuleScanReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

[[nodiscard]] bool same_path(
    const fs::path& left,
    const fs::path& right) {
    if (mqb::platform::windows::path_identity_key(left)
        == mqb::platform::windows::path_identity_key(right)) {
        return true;
    }

    // Existing scan evidence may have been recorded through a junction or
    // symlink spelling. Physical equivalence is only a compatibility fallback;
    // all normal cache identity remains owned by PathIdentity.
    std::error_code error_code;
    return fs::equivalent(left, right, error_code) && !error_code;
}

[[nodiscard]] bool same_ordered_paths(
    const std::span<const fs::path> left,
    const std::span<const fs::path> right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_path(left[index], right[index])) return false;
    }
    return true;
}

[[nodiscard]] bool same_snapshot(
    const FileSnapshot& left,
    const FileSnapshot& right) {
    return same_path(left.path, right.path)
        && left.exists == right.exists
        && (!left.exists || left.modified == right.modified);
}

[[nodiscard]] std::optional<FileSnapshot> snapshot_regular_file(
    const fs::path& path) {
    if (path.empty()) return std::nullopt;
    std::error_code error_code;
    const auto status = fs::status(path, error_code);
    if (error_code || !fs::is_regular_file(status)) return std::nullopt;
    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) return std::nullopt;
    return FileSnapshot{
        .path = path,
        .exists = true,
        .modified = modified,
    };
}

[[nodiscard]] std::optional<FileSnapshot> snapshot_file_or_directory(
    const fs::path& path) {
    if (path.empty()) return std::nullopt;
    std::error_code error_code;
    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) return std::nullopt;
    return FileSnapshot{
        .path = path,
        .exists = true,
        .modified = modified,
    };
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

} // namespace

std::string_view to_string(const ModuleScanReason reason) noexcept {
    switch (reason) {
    case ModuleScanReason::missing_cache_entry:
        return "missing cache entry";
    case ModuleScanReason::missing_scan_evidence:
        return "missing module scan evidence";
    case ModuleScanReason::include_search_roots_changed:
        return "include search roots changed";
    case ModuleScanReason::scan_signature_changed:
        return "module scan recipe changed";
    case ModuleScanReason::source_changed:
        return "module scan source changed";
    case ModuleScanReason::output_changed:
        return "module dependency output changed";
    case ModuleScanReason::dependency_changed:
        return "module scan dependency changed";
    case ModuleScanReason::dependency_metadata_invalid:
        return "module dependency metadata invalid";
    }
    return "unknown module scan reason";
}

std::expected<IncrementalModuleScanInspection, msvc::ModuleScanError>
MsvcIncrementalModuleScanCoordinator::inspect(
    const IncrementalModuleScanRequest& request) const {
    const msvc::ModuleScanInvocation invocation{
        .source = request.source,
        .output_file = request.module_dependencies_file,
        .options = request.options,
        .kind = request.kind,
        .working_directory = request.working_directory,
    };
    auto recipe = msvc::MsvcModuleDependencyScanner::build_recipe(
        scanner_.toolchain(),
        invocation);
    if (!recipe) return std::unexpected(recipe.error());

    IncrementalModuleScanInspection inspection{
        .recipe = std::move(*recipe),
    };

    auto loaded = CompileCacheFile::load(request.compile_cache_file);
    if (!loaded || !*loaded) {
        add_reason(inspection.reasons, ModuleScanReason::missing_cache_entry);
        return inspection;
    }

    const CompileCacheEntry& entry = **loaded;
    if (!entry.module_scan) {
        add_reason(inspection.reasons, ModuleScanReason::missing_scan_evidence);
        return inspection;
    }
    const ModuleScanEvidence& evidence = *entry.module_scan;

    const auto current_roots = msvc::include_search_roots(
        request.options,
        scanner_.toolchain().environment,
        request.working_directory);
    if (!same_ordered_paths(entry.include_search_roots, current_roots)) {
        add_reason(
            inspection.reasons,
            ModuleScanReason::include_search_roots_changed);
    }

    const BuildSignature current_signature = BuildSignature::for_module_scan(
        request.source,
        request.kind,
        scanner_.toolchain().identity,
        request.options);
    if (evidence.signature != current_signature) {
        add_reason(inspection.reasons, ModuleScanReason::scan_signature_changed);
    }

    const auto source_snapshot = snapshot_regular_file(request.source);
    if (!source_snapshot || !same_snapshot(evidence.source, *source_snapshot)) {
        add_reason(inspection.reasons, ModuleScanReason::source_changed);
    }

    const auto output_snapshot = snapshot_regular_file(
        request.module_dependencies_file);
    if (!output_snapshot || !same_snapshot(evidence.output, *output_snapshot)) {
        add_reason(inspection.reasons, ModuleScanReason::output_changed);
    }

    std::vector<FileSnapshot> dependency_snapshots;
    dependency_snapshots.reserve(evidence.dependencies.size());
    bool dependency_snapshots_complete = true;
    for (const auto& dependency : evidence.dependencies) {
        auto snapshot = snapshot_file_or_directory(dependency.path);
        if (!snapshot) {
            dependency_snapshots_complete = false;
            add_reason(inspection.reasons, ModuleScanReason::dependency_changed);
            continue;
        }
        if (!same_snapshot(dependency, *snapshot)) {
            add_reason(inspection.reasons, ModuleScanReason::dependency_changed);
        }
        dependency_snapshots.push_back(std::move(*snapshot));
    }

    if (inspection.reasons.empty()
        && source_snapshot
        && output_snapshot
        && dependency_snapshots_complete
        && !ModuleScanEvidenceValidator::reusable(
            evidence,
            current_signature,
            *source_snapshot,
            *output_snapshot,
            dependency_snapshots)) {
        // Preserve fail-closed behavior if the core validator gains an
        // additional invariant that this diagnostic projection does not yet
        // classify explicitly.
        add_reason(inspection.reasons, ModuleScanReason::dependency_changed);
    }

    if (!inspection.reasons.empty()) {
        return inspection;
    }

    auto text = read_text_file(request.module_dependencies_file);
    if (!text) {
        add_reason(
            inspection.reasons,
            ModuleScanReason::dependency_metadata_invalid);
        return inspection;
    }
    auto dependencies = modules::P1689Parser::parse(*text);
    if (!dependencies) {
        add_reason(
            inspection.reasons,
            ModuleScanReason::dependency_metadata_invalid);
        return inspection;
    }
    inspection.dependencies = std::move(*dependencies);
    return inspection;
}

std::expected<IncrementalModuleScanResult, msvc::ModuleScanError>
MsvcIncrementalModuleScanCoordinator::run(
    const IncrementalModuleScanRequest& request) const {
    auto inspection = inspect(request);
    if (!inspection) return std::unexpected(inspection.error());

    if (inspection->reusable()) {
        return IncrementalModuleScanResult{
            .inspection = std::move(*inspection),
            .result = msvc::ModuleScanResult{
                .process = process::ProcessResult{},
                .dependencies = *inspection->dependencies,
                .reused = true,
            },
            .scanned = false,
        };
    }

    auto scanned = scanner_.execute_recipe(inspection->recipe);
    if (!scanned) return std::unexpected(scanned.error());
    return IncrementalModuleScanResult{
        .inspection = std::move(*inspection),
        .result = std::move(*scanned),
        .scanned = true,
    };
}

} // namespace mqb::orchestration
