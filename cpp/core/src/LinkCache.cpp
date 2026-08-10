#include "mqb/core/LinkCache.hpp"

#include <algorithm>
#include <filesystem>
#include <span>

namespace mqb {
namespace {

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] bool same_paths(
    const std::span<const std::filesystem::path> left,
    const std::span<const std::filesystem::path> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_path(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_linker(
    const LinkerIdentity& left,
    const LinkerIdentity& right) {
    return same_path(left.linker, right.linker)
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

} // namespace

LinkCacheValidation LinkCacheValidator::validate(
    const std::span<const std::filesystem::path> current_objects,
    const std::filesystem::path& current_output,
    const LinkerIdentity& current_linker,
    const LinkOptions& current_options,
    const std::optional<LinkCacheEntry>& cached_entry,
    const FileSnapshot& output_snapshot,
    const std::span<const FileSnapshot> object_snapshots,
    const bool force_relink) {
    LinkCacheValidation result;

    if (force_relink) {
        add_reason(result.reasons, BuildReason::explicit_rebuild);
    }

    if (!cached_entry) {
        add_reason(result.reasons, BuildReason::missing_cache_entry);
        if (!output_snapshot.exists) {
            add_reason(result.reasons, BuildReason::missing_output);
        }
        return result;
    }

    const auto& cached = *cached_entry;
    const bool linker_matches = same_linker(cached.linker, current_linker);
    if (!linker_matches) {
        add_reason(result.reasons, BuildReason::toolchain_changed);
    }

    const bool inputs_match = same_paths(cached.objects, current_objects);
    if (!inputs_match) {
        add_reason(result.reasons, BuildReason::link_inputs_changed);
    }

    const auto current_signature = BuildSignature::for_link(
        current_objects,
        current_output,
        current_linker,
        current_options);
    if (cached.signature != current_signature && linker_matches && inputs_match) {
        add_reason(result.reasons, BuildReason::linker_options_changed);
    }

    if (!same_path(cached.output, current_output)) {
        add_reason(result.reasons, BuildReason::linker_options_changed);
    }

    if (!output_snapshot.exists) {
        add_reason(result.reasons, BuildReason::missing_output);
    }

    for (const auto& object : current_objects) {
        const auto* snapshot = find_snapshot(object_snapshots, object);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(result.reasons, BuildReason::link_inputs_changed);
            continue;
        }
        if (output_snapshot.exists && snapshot->modified > output_snapshot.modified) {
            add_reason(result.reasons, BuildReason::link_inputs_changed);
        }
    }

    return result;
}

} // namespace mqb
