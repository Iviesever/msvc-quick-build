#include "mqb/core/ArchiveCacheFile.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/PerformanceEvidence.hpp"

namespace mqb {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view magic = "MQBARCHIVE";
constexpr unsigned int format_version = 1;
constexpr std::size_t max_objects = 100000u;

[[nodiscard]] ArchiveCacheFileError error(
    const ArchiveCacheFileErrorCode code,
    const fs::path& file,
    std::string message) {
    return ArchiveCacheFileError{.code = code, .file = file, .message = std::move(message)};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes}.lexically_normal();
}

[[nodiscard]] fs::path temporary_path_for(const fs::path& file) {
    fs::path temporary = file;
    temporary += ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return temporary;
}

} // namespace

std::expected<std::optional<ArchiveCacheEntry>, ArchiveCacheFileError>
ArchiveCacheFile::load(const fs::path& file) {
    mqb::performance::ScopedCacheRead evidence{
        mqb::performance::CacheKind::archive};
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        if (ec) return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_open_failed, file, "failed to query archive cache file"));
        return std::optional<ArchiveCacheEntry>{};
    }

    std::ifstream stream{file, std::ios::binary | std::ios::ate};
    if (!stream) return std::unexpected(error(
        ArchiveCacheFileErrorCode::file_open_failed, file, "failed to open archive cache file"));
    const std::streampos end = stream.tellg();
    const std::uint64_t size = end == std::streampos{-1}
        ? 0u
        : static_cast<std::uint64_t>(static_cast<std::streamoff>(end));
    stream.clear();
    stream.seekg(0, std::ios::beg);
    if (!stream) return std::unexpected(error(
        ArchiveCacheFileErrorCode::file_open_failed, file, "failed to seek archive cache file"));
    evidence.opened(size);

    std::string loaded_magic;
    unsigned int version = 0;
    if (!(stream >> loaded_magic >> version) || loaded_magic != magic) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::corrupt_data, file, "archive cache header is invalid"));
    }
    if (version != format_version) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::unsupported_version, file, "archive cache version is not supported"));
    }

    std::string librarian_path;
    std::string librarian_version;
    std::string librarian_stamp;
    std::uint64_t signature_high = 0;
    std::uint64_t signature_low = 0;
    std::string output;
    std::size_t object_count = 0;
    if (!(stream >> std::quoted(librarian_path)
                 >> std::quoted(librarian_version)
                 >> std::quoted(librarian_stamp)
                 >> signature_high
                 >> signature_low
                 >> std::quoted(output)
                 >> object_count)) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::corrupt_data, file, "archive cache body is truncated"));
    }
    if (object_count > max_objects) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::corrupt_data, file, "archive cache object count exceeds safety limit"));
    }

    std::vector<fs::path> objects;
    objects.reserve(object_count);
    for (std::size_t index = 0; index < object_count; ++index) {
        std::string object;
        if (!(stream >> std::quoted(object))) {
            return std::unexpected(error(
                ArchiveCacheFileErrorCode::corrupt_data, file, "archive cache object list is truncated"));
        }
        objects.push_back(path_from_utf8(object));
    }

    std::string trailing;
    if (stream >> trailing) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::corrupt_data, file, "archive cache has unexpected trailing data"));
    }

    return std::optional<ArchiveCacheEntry>{ArchiveCacheEntry{
        .librarian = LibrarianIdentity{
            .librarian = path_from_utf8(librarian_path),
            .version = std::move(librarian_version),
            .binary_stamp = std::move(librarian_stamp),
        },
        .signature = BuildSignature::from_digest(SignatureDigest{
            .high = signature_high,
            .low = signature_low,
        }),
        .objects = std::move(objects),
        .output = path_from_utf8(output),
    }};
}

std::expected<void, ArchiveCacheFileError>
ArchiveCacheFile::save(const fs::path& file, const ArchiveCacheEntry& entry) {
    mqb::performance::ScopedCacheWrite evidence{
        mqb::performance::CacheKind::archive};
    if (entry.objects.size() > max_objects) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_write_failed, file, "archive cache object count exceeds safety limit"));
    }

    std::error_code ec;
    if (!file.parent_path().empty()) {
        fs::create_directories(file.parent_path(), ec);
        if (ec) return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_write_failed, file, "failed to create archive cache directory"));
    }

    const fs::path temporary = temporary_path_for(file);
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_open_failed, temporary, "failed to open temporary archive cache"));
        evidence.opened(0);
        stream << magic << ' ' << format_version << '\n'
               << std::quoted(path_to_utf8(entry.librarian.librarian)) << '\n'
               << std::quoted(entry.librarian.version) << '\n'
               << std::quoted(entry.librarian.binary_stamp) << '\n'
               << entry.signature.digest().high << ' ' << entry.signature.digest().low << '\n'
               << std::quoted(path_to_utf8(entry.output)) << '\n'
               << entry.objects.size() << '\n';
        for (const auto& object : entry.objects) {
            stream << std::quoted(path_to_utf8(object)) << '\n';
        }
        stream.flush();
        if (!stream) {
            stream.close();
            fs::remove(temporary, ec);
            return std::unexpected(error(
                ArchiveCacheFileErrorCode::file_write_failed, temporary, "failed to write archive cache"));
        }
    }

    ec.clear();
    if (fs::exists(file, ec) && !ec) {
        fs::remove(file, ec);
        if (ec) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected(error(
                ArchiveCacheFileErrorCode::replace_failed, file, "failed to remove previous archive cache"));
        }
    } else if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::replace_failed, file, "failed to query previous archive cache"));
    }

    fs::rename(temporary, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::replace_failed, file, "failed to install archive cache"));
    }
    return {};
}

} // namespace mqb
