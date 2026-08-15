#include "mqb/core/LinkCache.hpp"

#include <algorithm>
#include <filesystem>
#include <span>

namespace mqb {
namespace {

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return left == right || left.lexically_normal() == right.lexically_normal();
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

[[nodiscard]] bool validate_freshness(
    const std::span<const std::filesystem::path> inputs,
    const std::span<const FileSnapshot> snapshots,
    const FileSnapshot& output_snapshot,
    std::vector<BuildReason>& reasons) {
    bool changed = false;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        const auto* snapshot = aligned_snapshot_or_find(snapshots, input, index);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(reasons, BuildReason::link_inputs_changed);
            changed = true;
            continue;
        }
        if (output_snapshot.exists && snapshot->modified > output_snapshot.modified) {
            add_reason(reasons, BuildReason::link_inputs_changed);
            changed = true;
        }
    }
    return changed;
}

void validate_side_outputs(
    const std::span<const std::filesystem::path> cached_side_outputs,
    const std::span<const FileSnapshot> snapshots,
    std::vector<BuildReason>& reasons) {
    for (std::size_t index = 0; index < cached_side_outputs.size(); ++index) {
        const auto& output = cached_side_outputs[index];
        const auto* snapshot = aligned_snapshot_or_find(snapshots, output, index);
        if (snapshot == nullptr || !snapshot->exists) {
            add_reason(reasons, BuildReason::missing_output);
        }
    }
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
    return validate(
        current_objects,
        std::span<const std::filesystem::path>{},
        std::span<const std::filesystem::path>{},
        current_output,
        current_linker,
        current_options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        std::span<const FileSnapshot>{},
        std::span<const FileSnapshot>{},
        std::span<const FileSnapshot>{},
        force_relink);
}

LinkCacheValidation LinkCacheValidator::validate(
    const std::span<const std::filesystem::path> current_objects,
    const std::span<const std::filesystem::path> current_libraries,
    const std::filesystem::path& current_output,
    const LinkerIdentity& current_linker,
    const LinkOptions& current_options,
    const std::optional<LinkCacheEntry>& cached_entry,
    const FileSnapshot& output_snapshot,
    const std::span<const FileSnapshot> object_snapshots,
    const std::span<const FileSnapshot> library_snapshots,
    const bool force_relink) {
    return validate(
        current_objects,
        current_libraries,
        std::span<const std::filesystem::path>{},
        current_output,
        current_linker,
        current_options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        std::span<const FileSnapshot>{},
        std::span<const FileSnapshot>{},
        force_relink);
}

LinkCacheValidation LinkCacheValidator::validate(
    const std::span<const std::filesystem::path> current_objects,
    const std::span<const std::filesystem::path> current_libraries,
    const std::span<const std::filesystem::path> current_file_inputs,
    const std::filesystem::path& current_output,
    const LinkerIdentity& current_linker,
    const LinkOptions& current_options,
    const std::optional<LinkCacheEntry>& cached_entry,
    const FileSnapshot& output_snapshot,
    const std::span<const FileSnapshot> object_snapshots,
    const std::span<const FileSnapshot> library_snapshots,
    const std::span<const FileSnapshot> file_input_snapshots,
    const std::span<const FileSnapshot> side_output_snapshots,
    const bool force_relink) {
    LinkCacheValidation result;

    if (force_relink) {
        add_reason(result.reasons, BuildReason::explicit_rebuild);
    }

    if (!cached_entry) {
        add_reason(result.reasons, BuildReason::missing_cache_entry);
        if (!output_snapshot.exists) {
            add_reason(result.reasons, BuildReason::missing_output);
        } else {
            if (!current_libraries.empty()) {
                result.library_inputs_changed = true;
            }
            if (!current_file_inputs.empty()) {
                result.file_inputs_changed = true;
            }
        }
        return result;
    }

    const auto& cached = *cached_entry;
    const bool linker_matches = same_linker(cached.linker, current_linker);
    if (!linker_matches) {
        add_reason(result.reasons, BuildReason::toolchain_changed);
    }

    const bool object_inputs_match = same_paths(cached.objects, current_objects);
    const bool library_inputs_match = same_paths(cached.libraries, current_libraries);
    const bool file_inputs_match = same_paths(cached.file_inputs, current_file_inputs);
    const bool inputs_match = object_inputs_match && library_inputs_match && file_inputs_match;
    if (!inputs_match) {
        add_reason(result.reasons, BuildReason::link_inputs_changed);
    }
    if (!library_inputs_match) {
        result.library_inputs_changed = true;
    }
    if (!file_inputs_match) {
        result.file_inputs_changed = true;
    }

    const auto current_signature = BuildSignature::for_link(
        current_objects,
        current_libraries,
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
    validate_side_outputs(cached.side_outputs, side_output_snapshots, result.reasons);

    (void)validate_freshness(
        current_objects,
        object_snapshots,
        output_snapshot,
        result.reasons);
    if (validate_freshness(
            current_libraries,
            library_snapshots,
            output_snapshot,
            result.reasons)) {
        result.library_inputs_changed = true;
    }
    if (validate_freshness(
            current_file_inputs,
            file_input_snapshots,
            output_snapshot,
            result.reasons)) {
        result.file_inputs_changed = true;
    }

    return result;
}

} // namespace mqb
