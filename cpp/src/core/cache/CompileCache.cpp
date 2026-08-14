#include "mqb/core/CompileCache.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>

namespace mqb {
namespace {

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return left == right || left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] bool same_toolchain(
    const ToolchainIdentity& left,
    const ToolchainIdentity& right) {
    return same_path(left.compiler, right.compiler)
        && left.version == right.version
        && left.binary_stamp == right.binary_stamp;
}

[[nodiscard]] bool same_artifact(
    const Artifact& left,
    const Artifact& right) {
    return left.kind == right.kind && same_path(left.path, right.path);
}

[[nodiscard]] bool same_snapshot(
    const FileSnapshot& left,
    const FileSnapshot& right) {
    return same_path(left.path, right.path)
        && left.exists == right.exists
        && (!left.exists || left.modified == right.modified);
}

void add_reason(
    std::vector<BuildReason>& reasons,
    const BuildReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

[[nodiscard]] const FileSnapshot* find_snapshot(
    const std::span<const FileSnapshot> snapshots,
    const std::filesystem::path& path) {
    const auto it = std::find_if(
        snapshots.begin(),
        snapshots.end(),
        [&path](const FileSnapshot& snapshot) {
            return same_path(snapshot.path, path);
        });
    return it == snapshots.end() ? nullptr : &*it;
}

[[nodiscard]] const FileSnapshot* aligned_snapshot_or_find(
    const std::span<const FileSnapshot> snapshots,
    const std::filesystem::path& path,
    const std::size_t preferred_index) {
    if (preferred_index < snapshots.size()
        && same_path(snapshots[preferred_index].path, path)) {
        return &snapshots[preferred_index];
    }
    return find_snapshot(snapshots, path);
}

[[nodiscard]] bool same_outputs(
    const std::vector<Artifact>& cached,
    const std::vector<Artifact>& current) {
    if (cached.size() != current.size()) {
        return false;
    }

    std::vector<bool> matched(cached.size(), false);
    for (const auto& current_output : current) {
        bool found = false;
        for (std::size_t index = 0; index < cached.size(); ++index) {
            if (!matched[index] && same_artifact(cached[index], current_output)) {
                matched[index] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::file_time_type>
oldest_output_time(
    const TranslationUnit& unit,
    const std::span<const FileSnapshot> output_snapshots) {
    if (unit.outputs.empty()) {
        return std::nullopt;
    }

    std::optional<std::filesystem::file_time_type> oldest;
    for (std::size_t index = 0; index < unit.outputs.size(); ++index) {
        const auto& output = unit.outputs[index];
        const FileSnapshot* snapshot = aligned_snapshot_or_find(
            output_snapshots,
            output.path,
            index);
        if (snapshot == nullptr || !snapshot->exists) {
            return std::nullopt;
        }
        if (!oldest || snapshot->modified < *oldest) {
            oldest = snapshot->modified;
        }
    }
    return oldest;
}

} // namespace

CompileCacheValidation CompileCacheValidator::validate(
    const TranslationUnit& current_unit,
    const ToolchainIdentity& current_toolchain,
    const CompilerOptions& current_options,
    const std::optional<CompileCacheEntry>& cached_entry,
    const FileSnapshot& source_snapshot,
    const std::span<const FileSnapshot> output_snapshots,
    const std::span<const FileSnapshot> dependency_snapshots) {
    CompileCacheValidation result;

    const auto validate_outputs = [&] {
        if (current_unit.outputs.empty()) {
            add_reason(result.reasons, BuildReason::missing_output);
            return;
        }
        for (std::size_t index = 0; index < current_unit.outputs.size(); ++index) {
            const auto& output = current_unit.outputs[index];
            const auto* snapshot = aligned_snapshot_or_find(
                output_snapshots,
                output.path,
                index);
            if (output.path.empty() || snapshot == nullptr || !snapshot->exists) {
                add_reason(result.reasons, BuildReason::missing_output);
            }
        }
    };

    if (!cached_entry) {
        add_reason(result.reasons, BuildReason::missing_cache_entry);
        validate_outputs();
        return result;
    }

    const auto& cached = *cached_entry;
    const bool same_source_identity = same_path(cached.source, current_unit.source)
        && cached.kind == current_unit.kind;
    if (!same_source_identity) {
        add_reason(result.reasons, BuildReason::source_changed);
    }

    const bool toolchain_matches = same_toolchain(cached.toolchain, current_toolchain);
    if (!toolchain_matches) {
        add_reason(result.reasons, BuildReason::toolchain_changed);
    }

    const auto current_signature = BuildSignature::for_compile(
        current_unit,
        current_toolchain,
        current_options);
    if (cached.signature != current_signature
        && same_source_identity
        && toolchain_matches) {
        add_reason(result.reasons, BuildReason::compiler_options_changed);
    }

    if (!same_outputs(cached.outputs, current_unit.outputs)) {
        add_reason(result.reasons, BuildReason::missing_output);
    }
    validate_outputs();

    const auto freshness_anchor = oldest_output_time(
        current_unit,
        output_snapshots);
    if (!source_snapshot.exists) {
        add_reason(result.reasons, BuildReason::source_changed);
    } else if (freshness_anchor && source_snapshot.modified > *freshness_anchor) {
        add_reason(result.reasons, BuildReason::source_changed);
    }

    for (std::size_t index = 0; index < cached.dependencies.size(); ++index) {
        const auto& dependency = cached.dependencies[index];
        const auto* snapshot = aligned_snapshot_or_find(
            dependency_snapshots,
            dependency,
            index);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(result.reasons, BuildReason::dependency_changed);
            continue;
        }
        if (freshness_anchor && snapshot->modified > *freshness_anchor) {
            add_reason(result.reasons, BuildReason::dependency_changed);
        }
    }

    return result;
}

bool ModuleScanEvidenceValidator::reusable(
    const ModuleScanEvidence& evidence,
    const BuildSignature& current_signature,
    const FileSnapshot& current_source,
    const FileSnapshot& current_output,
    const std::span<const FileSnapshot> current_dependencies) {
    if (evidence.signature != current_signature
        || !same_snapshot(evidence.source, current_source)
        || !same_snapshot(evidence.output, current_output)
        || !evidence.source.exists
        || !evidence.output.exists
        || evidence.dependencies.size() != current_dependencies.size()) {
        return false;
    }

    for (std::size_t index = 0; index < evidence.dependencies.size(); ++index) {
        if (!same_snapshot(evidence.dependencies[index], current_dependencies[index])
            || !current_dependencies[index].exists) {
            return false;
        }
    }
    return true;
}

} // namespace mqb
