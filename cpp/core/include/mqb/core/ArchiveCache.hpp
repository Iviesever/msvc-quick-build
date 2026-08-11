#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/LibrarianIdentity.hpp"

namespace mqb {

struct ArchiveCacheEntry {
    LibrarianIdentity librarian;
    BuildSignature signature;
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
};

struct ArchiveCacheValidation {
    std::vector<BuildReason> reasons;

    [[nodiscard]] bool reusable() const noexcept { return reasons.empty(); }
};

class ArchiveCacheValidator {
public:
    [[nodiscard]] static ArchiveCacheValidation validate(
        std::span<const std::filesystem::path> current_objects,
        const std::filesystem::path& current_output,
        const LibrarianIdentity& current_librarian,
        const std::optional<ArchiveCacheEntry>& cached_entry,
        const FileSnapshot& output_snapshot,
        std::span<const FileSnapshot> object_snapshots,
        bool force_archive = false);
};

} // namespace mqb
