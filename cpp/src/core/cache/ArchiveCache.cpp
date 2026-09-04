#include "mqb/core/ArchiveCache.hpp"

#include <algorithm>
#include <filesystem>
#include <span>

#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb {
namespace {

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    // Cache entries and current archive inputs normally carry the exact same
    // path spelling. Keep Windows alias identity as the fallback without paying
    // for two normalized UTF-8 identity strings on aligned warm-cache inputs.
    if (left == right) {
        return true;
    }
    return platform::windows::path_identity_key(left)
        == platform::windows::path_identity_key(right);
}

[[nodiscard]] bool same_paths(
    const std::span<const std::filesystem::path> left,
    const std::span<const std::filesystem::path> right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_path(left[index], right[index])) return false;
    }
    return true;
}

[[nodiscard]] bool same_librarian(
    const LibrarianIdentity& left,
    const LibrarianIdentity& right) {
    return same_path(left.librarian, right.librarian)
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
        snapshots.begin(), snapshots.end(),
        [&path](const FileSnapshot& snapshot) { return same_path(snapshot.path, path); });
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

} // namespace

ArchiveCacheValidation ArchiveCacheValidator::validate(
    const std::span<const std::filesystem::path> current_objects,
    const std::filesystem::path& current_output,
    const LibrarianIdentity& current_librarian,
    const std::optional<ArchiveCacheEntry>& cached_entry,
    const FileSnapshot& output_snapshot,
    const std::span<const FileSnapshot> object_snapshots,
    const bool force_archive,
    const bool link_time_code_generation,
    const Architecture architecture,
    const std::span<const std::string> additional_arguments) {
    ArchiveCacheValidation result;

    if (force_archive) add_reason(result.reasons, BuildReason::explicit_rebuild);

    if (!cached_entry) {
        add_reason(result.reasons, BuildReason::missing_cache_entry);
        if (!output_snapshot.exists) add_reason(result.reasons, BuildReason::missing_output);
        return result;
    }

    const auto& cached = *cached_entry;
    const bool librarian_matches = same_librarian(cached.librarian, current_librarian);
    if (!librarian_matches) add_reason(result.reasons, BuildReason::toolchain_changed);

    const bool inputs_match = same_paths(cached.objects, current_objects);
    if (!inputs_match) add_reason(result.reasons, BuildReason::archive_inputs_changed);

    const auto signature = BuildSignature::for_archive(
        current_objects,
        current_output,
        current_librarian,
        link_time_code_generation,
        architecture,
        additional_arguments);
    if (cached.signature != signature && librarian_matches && inputs_match) {
        add_reason(result.reasons, BuildReason::archive_recipe_changed);
    }
    if (!same_path(cached.output, current_output)) {
        add_reason(result.reasons, BuildReason::archive_recipe_changed);
    }
    if (!output_snapshot.exists) add_reason(result.reasons, BuildReason::missing_output);

    for (std::size_t index = 0; index < current_objects.size(); ++index) {
        const auto& object = current_objects[index];
        const auto* snapshot = aligned_snapshot_or_find(object_snapshots, object, index);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(result.reasons, BuildReason::archive_inputs_changed);
            continue;
        }
        if (output_snapshot.exists && snapshot->modified > output_snapshot.modified) {
            add_reason(result.reasons, BuildReason::archive_inputs_changed);
        }
    }

    return result;
}

} // namespace mqb
