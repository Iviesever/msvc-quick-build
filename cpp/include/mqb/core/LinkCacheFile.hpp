#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "mqb/core/LinkCache.hpp"

namespace mqb {

enum class LinkCacheFileErrorCode {
    file_open_failed,
    file_read_failed,
    file_write_failed,
    invalid_magic,
    unsupported_version,
    corrupt_data,
    replace_failed,
};

struct LinkCacheFileError {
    LinkCacheFileErrorCode code{LinkCacheFileErrorCode::corrupt_data};
    std::filesystem::path file;
    std::size_t offset{};
    std::string message;
};

class LinkCacheFile {
public:
    [[nodiscard]] static std::expected<std::optional<LinkCacheEntry>, LinkCacheFileError>
    load(const std::filesystem::path& file);

    [[nodiscard]] static std::expected<void, LinkCacheFileError>
    save(const std::filesystem::path& file, const LinkCacheEntry& entry);
};

} // namespace mqb
