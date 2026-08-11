#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "mqb/core/ArchiveCache.hpp"

namespace mqb {

enum class ArchiveCacheFileErrorCode {
    file_open_failed,
    file_read_failed,
    file_write_failed,
    corrupt_data,
    unsupported_version,
    replace_failed,
};

struct ArchiveCacheFileError {
    ArchiveCacheFileErrorCode code{ArchiveCacheFileErrorCode::corrupt_data};
    std::filesystem::path file;
    std::string message;
};

class ArchiveCacheFile {
public:
    [[nodiscard]] static std::expected<std::optional<ArchiveCacheEntry>, ArchiveCacheFileError>
    load(const std::filesystem::path& file);

    [[nodiscard]] static std::expected<void, ArchiveCacheFileError>
    save(const std::filesystem::path& file, const ArchiveCacheEntry& entry);
};

} // namespace mqb
