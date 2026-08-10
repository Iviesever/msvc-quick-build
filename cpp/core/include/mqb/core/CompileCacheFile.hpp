#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "mqb/core/CompileCache.hpp"

namespace mqb {

enum class CompileCacheFileErrorCode {
    file_open_failed,
    file_read_failed,
    file_write_failed,
    invalid_magic,
    unsupported_version,
    corrupt_data,
    replace_failed,
};

struct CompileCacheFileError {
    CompileCacheFileErrorCode code{CompileCacheFileErrorCode::corrupt_data};
    std::filesystem::path file;
    std::size_t offset{};
    std::string message;
};

class CompileCacheFile {
public:
    [[nodiscard]] static std::expected<std::optional<CompileCacheEntry>, CompileCacheFileError>
    load(const std::filesystem::path& file);

    [[nodiscard]] static std::expected<void, CompileCacheFileError>
    save(const std::filesystem::path& file, const CompileCacheEntry& entry);
};

} // namespace mqb
