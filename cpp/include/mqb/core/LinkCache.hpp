#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"

namespace mqb {

struct LinkCacheEntry {
    LinkerIdentity linker;
    BuildSignature signature;
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::vector<std::filesystem::path> libraries;
    // File-bearing raw linker inputs whose contents affect the linked image but
    // are not ordinary object/library inputs (initially native /DEF files).
    std::vector<std::filesystem::path> file_inputs;
    std::vector<std::filesystem::path> side_outputs;
    // Opaque state for the library search roots used to resolve LINK-observed
    // transitive default libraries. When unchanged, cached absolute library
    // paths can be reused without repeating basename search on every warm build.
    // This state is validation metadata only and is not linker argv/identity.
    std::string observed_library_search_state;
};

struct LinkCacheValidation {
    std::vector<BuildReason> reasons;
    // Runtime execution evidence only; this does not participate in cache
    // identity. MSVC incremental linking can otherwise reuse stale archive
    // members when a .lib is rebuilt inside the linker timestamp granularity.
    bool library_inputs_changed{false};
    // Same execution-only safety signal for other tracked linker file inputs.
    bool file_inputs_changed{false};

    [[nodiscard]] bool reusable() const noexcept {
        return reasons.empty();
    }
};

class LinkCacheValidator {
public:
    [[nodiscard]] static LinkCacheValidation validate(
        std::span<const std::filesystem::path> current_objects,
        const std::filesystem::path& current_output,
        const LinkerIdentity& current_linker,
        const LinkOptions& current_options,
        const std::optional<LinkCacheEntry>& cached_entry,
        const FileSnapshot& output_snapshot,
        std::span<const FileSnapshot> object_snapshots,
        bool force_relink = false);

    [[nodiscard]] static LinkCacheValidation validate(
        std::span<const std::filesystem::path> current_objects,
        std::span<const std::filesystem::path> current_libraries,
        const std::filesystem::path& current_output,
        const LinkerIdentity& current_linker,
        const LinkOptions& current_options,
        const std::optional<LinkCacheEntry>& cached_entry,
        const FileSnapshot& output_snapshot,
        std::span<const FileSnapshot> object_snapshots,
        std::span<const FileSnapshot> library_snapshots,
        bool force_relink = false);

    // Compatibility overload retained for existing callers that track linker
    // side outputs but have no generic file-bearing linker inputs.
    [[nodiscard]] static LinkCacheValidation validate(
        std::span<const std::filesystem::path> current_objects,
        std::span<const std::filesystem::path> current_libraries,
        const std::filesystem::path& current_output,
        const LinkerIdentity& current_linker,
        const LinkOptions& current_options,
        const std::optional<LinkCacheEntry>& cached_entry,
        const FileSnapshot& output_snapshot,
        std::span<const FileSnapshot> object_snapshots,
        std::span<const FileSnapshot> library_snapshots,
        std::span<const FileSnapshot> side_output_snapshots,
        bool force_relink = false);

    [[nodiscard]] static LinkCacheValidation validate(
        std::span<const std::filesystem::path> current_objects,
        std::span<const std::filesystem::path> current_libraries,
        std::span<const std::filesystem::path> current_file_inputs,
        const std::filesystem::path& current_output,
        const LinkerIdentity& current_linker,
        const LinkOptions& current_options,
        const std::optional<LinkCacheEntry>& cached_entry,
        const FileSnapshot& output_snapshot,
        std::span<const FileSnapshot> object_snapshots,
        std::span<const FileSnapshot> library_snapshots,
        std::span<const FileSnapshot> file_input_snapshots,
        std::span<const FileSnapshot> side_output_snapshots,
        bool force_relink = false);
};

} // namespace mqb
