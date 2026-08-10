#pragma once

#include <filesystem>
#include <optional>
#include <span>
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
};

struct LinkCacheValidation {
    std::vector<BuildReason> reasons;

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
};

} // namespace mqb
