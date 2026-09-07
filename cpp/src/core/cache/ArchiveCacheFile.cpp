#include "mqb/core/ArchiveCacheFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <spanstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/BoundedCacheReader.hpp"
#include "mqb/core/PerformanceEvidence.hpp"

namespace mqb {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view magic = "MQBARCHIVE";
constexpr unsigned int format_version = 1;
constexpr std::size_t max_objects = 100000u;
constexpr std::size_t max_cache_bytes = 64u * 1024u * 1024u;

// Serialize the unchanged quoted-text grammar once, with a bound before any
// growth. No directory or temporary file is touched for an oversized record.
// Unlike ostringstream, vector capacity growth is explicitly capped as well.
class BoundedArchiveBuffer final : public std::streambuf {
public:
    [[nodiscard]] std::span<const char> bytes() const noexcept { return bytes_; }

protected:
    std::streamsize xsputn(const char* data, const std::streamsize count) override {
        if (count < 0 || static_cast<std::uintmax_t>(count) > max_cache_bytes - bytes_.size()) {
            return 0;
        }
        const auto size = static_cast<std::size_t>(count);
        if (size == 0) return 0;
        const auto required = bytes_.size() + size;
        if (required > bytes_.capacity()) {
            const auto grown = std::max(std::size_t{4096}, bytes_.capacity() * 2);
            bytes_.reserve(std::min(max_cache_bytes, std::max(required, grown)));
        }
        bytes_.insert(bytes_.end(), data, data + size);
        return count;
    }

    int_type overflow(const int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) {
            return traits_type::not_eof(value);
        }
        const char ch = traits_type::to_char_type(value);
        return xsputn(&ch, 1) == 1 ? value : traits_type::eof();
    }

private:
    std::vector<char> bytes_;
};

// Match std::quoted's default escaping without an unbounded temporary escaped
// string. All serialized bytes pass through the same bounded destination.
void write_quoted(std::ostream& out, const std::string_view text) {
    if (!out) return;
    out.put('"');
    std::size_t begin = 0;
    while (out) {
        const auto escaped = text.find_first_of("\\\"", begin);
        if (escaped == std::string_view::npos) {
            out.write(text.data() + begin, static_cast<std::streamsize>(text.size() - begin));
            break;
        }
        out.write(text.data() + begin, static_cast<std::streamsize>(escaped - begin));
        out.put('\\');
        out.put(text[escaped]);
        begin = escaped + 1;
    }
    out.put('"');
}

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
    std::ifstream file_stream{file, std::ios::binary};
    if (!file_stream) {
        // Missing remains a normal miss. Query only after a failed open and
        // never reopen; retain the archive owner's historical error category.
        std::error_code ec;
        const bool exists = fs::exists(file, ec);
        if (!ec && !exists) return std::optional<ArchiveCacheEntry>{};
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_open_failed, file,
            ec ? "failed to query archive cache file" : "failed to open archive cache file"));
    }
    auto bytes = read_bounded_cache_stream(
        file_stream, max_cache_bytes,
        [&evidence](const std::uint64_t size) { evidence.opened(size); });
    if (!bytes) {
        const bool corrupt = bytes.error().code == BoundedCacheReadErrorCode::size_limit_exceeded
            || bytes.error().code == BoundedCacheReadErrorCode::trailing_data;
        return std::unexpected(error(
            corrupt ? ArchiveCacheFileErrorCode::corrupt_data
                    : ArchiveCacheFileErrorCode::file_read_failed,
            file, corrupt ? "archive cache exceeds size limit or grew during read"
                          : "failed to read complete archive cache file"));
    }
    // The owned payload outlives its read-only span stream. No second payload
    // copy or repeated external stream access is needed for formatted decoding.
    std::ispanstream stream{std::span<const char>{
        reinterpret_cast<const char*>(bytes->data()), bytes->size()}};

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

    BoundedArchiveBuffer buffer;
    std::ostream encoded{&buffer};
    encoded << magic << ' ' << format_version << '\n';
    const auto field = [&encoded](const std::string_view value) {
        write_quoted(encoded, value);
        encoded.put('\n');
    };
    field(path_to_utf8(entry.librarian.librarian));
    field(entry.librarian.version);
    field(entry.librarian.binary_stamp);
    encoded << entry.signature.digest().high << ' ' << entry.signature.digest().low << '\n';
    field(path_to_utf8(entry.output));
    encoded << entry.objects.size() << '\n';
    for (const auto& object : entry.objects) {
        if (!encoded) break;
        field(path_to_utf8(object));
    }
    if (!encoded) {
        return std::unexpected(error(
            ArchiveCacheFileErrorCode::file_write_failed, file,
            "archive cache exceeds safety size limit"));
    }
    const auto bytes = buffer.bytes();

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
        // Keep the existing write-counter convention. Accepted payload bytes
        // were historically not counted for archives; do not reattribute them.
        evidence.opened(0);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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
