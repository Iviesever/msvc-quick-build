#include "mqb/core/CompileCache.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace mqb {
namespace {

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] bool same_toolchain(
    const ToolchainIdentity& left,
    const ToolchainIdentity& right) {
    return same_path(left.compiler, right.compiler)
        && left.version == right.version
        && left.binary_stamp == right.binary_stamp;
}

void add_reason(std::vector<BuildReason>& reasons, const BuildReason reason) {
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

[[nodiscard]] const Artifact* find_artifact(
    const TranslationUnit& unit,
    const ArtifactKind kind) {
    const auto it = std::find_if(
        unit.outputs.begin(),
        unit.outputs.end(),
        [kind](const Artifact& artifact) {
            return artifact.kind == kind;
        });
    return it == unit.outputs.end() ? nullptr : &*it;
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

    const Artifact* current_object = find_artifact(current_unit, ArtifactKind::object);
    const FileSnapshot* object_snapshot = current_object == nullptr
        ? nullptr
        : find_snapshot(output_snapshots, current_object->path);

    const auto validate_outputs = [&] {
        for (const auto& output : current_unit.outputs) {
            const auto* snapshot = find_snapshot(output_snapshots, output.path);
            if (snapshot == nullptr || !snapshot->exists) {
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
    if (cached.signature != current_signature && same_source_identity && toolchain_matches) {
        add_reason(result.reasons, BuildReason::compiler_options_changed);
    }

    if (current_object == nullptr
        || !same_path(cached.object.path, current_object->path)
        || cached.object.kind != ArtifactKind::object) {
        add_reason(result.reasons, BuildReason::missing_output);
    }
    validate_outputs();

    if (!source_snapshot.exists) {
        add_reason(result.reasons, BuildReason::source_changed);
    } else if (object_snapshot != nullptr
               && object_snapshot->exists
               && source_snapshot.modified > object_snapshot->modified) {
        add_reason(result.reasons, BuildReason::source_changed);
    }

    for (const auto& dependency : cached.dependencies) {
        const auto* snapshot = find_snapshot(dependency_snapshots, dependency);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(result.reasons, BuildReason::dependency_changed);
            continue;
        }

        if (object_snapshot != nullptr
            && object_snapshot->exists
            && snapshot->modified > object_snapshot->modified) {
            add_reason(result.reasons, BuildReason::dependency_changed);
        }
    }

    return result;
}

} // namespace mqb
