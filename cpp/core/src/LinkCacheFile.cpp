#include "mqb/core/LinkCacheFile.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/LinkerIdentity.hpp"

namespace mqb {
namespace {

namespace fs = std::filesystem;

constexpr std::array<std::uint8_t, 8> magic{
    'M', 'Q', 'B', 'L', 'I', 'N', 'K', 'C'};
constexpr std::uint32_t legacy_format_version = 2;
constexpr std::uint32_t format_version = 3;
constexpr std::size_t max_cache_file_size = 64u * 1024u * 1024u;
constexpr std::uint32_t max_string_size = 4u * 1024u * 1024u;
constexpr std::uint32_t max_link_input_count = 100000u;

[[nodiscard]] LinkCacheFileError make_error(
    const LinkCacheFileErrorCode code,
    const fs::path& file,
    const std::size_t offset,
    std::string message) {
    return LinkCacheFileError{
        .code = code,
        .file = file,
        .offset = offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes}.lexically_normal();
}

class BinaryWriter {
public:
    void write_u8(const std::uint8_t value) {
        bytes_.push_back(value);
    }

    void write_u32(const std::uint32_t value) {
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
            write_u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void write_u64(const std::uint64_t value) {
        for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
            write_u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void write_bytes(const std::span<const std::uint8_t> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] bool write_string(const std::string_view value) {
        if (value.size() > max_string_size
            || value.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        write_u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    [[nodiscard]] bool write_path(const fs::path& path) {
        return write_string(path_to_utf8(path));
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
public:
    BinaryReader(const fs::path& file, const std::span<const std::uint8_t> bytes)
        : file_(file), bytes_(bytes) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::expected<std::uint8_t, LinkCacheFileError> read_u8() {
        if (offset_ >= bytes_.size()) {
            return std::unexpected(corrupt("unexpected end of link cache file"));
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::expected<std::uint32_t, LinkCacheFileError> read_u32() {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) return std::unexpected(byte.error());
            value |= static_cast<std::uint32_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::expected<std::uint64_t, LinkCacheFileError> read_u64() {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) return std::unexpected(byte.error());
            value |= static_cast<std::uint64_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::expected<std::string, LinkCacheFileError> read_string() {
        auto length = read_u32();
        if (!length) return std::unexpected(length.error());
        if (*length > max_string_size) {
            return std::unexpected(corrupt("link cache string exceeds safety limit"));
        }
        if (static_cast<std::size_t>(*length) > bytes_.size() - offset_) {
            return std::unexpected(corrupt("link cache string extends past end of file"));
        }
        const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
        std::string value{begin, begin + *length};
        offset_ += *length;
        return value;
    }

    [[nodiscard]] std::expected<fs::path, LinkCacheFileError> read_path() {
        auto value = read_string();
        if (!value) return std::unexpected(value.error());
        return path_from_utf8(*value);
    }

    [[nodiscard]] LinkCacheFileError corrupt(std::string message) const {
        return make_error(
            LinkCacheFileErrorCode::corrupt_data,
            file_,
            offset_,
            std::move(message));
    }

private:
    const fs::path& file_;
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

[[nodiscard]] bool input_count_valid(const std::size_t size) {
    return size <= max_link_input_count
        && size <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] std::expected<void, LinkCacheFileError>
write_paths(
    BinaryWriter& writer,
    const fs::path& file,
    const std::span<const fs::path> paths,
    const std::string_view description) {
    if (!input_count_valid(paths.size())) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_write_failed,
            file,
            0,
            std::string{description} + " count exceeds cache safety limit"));
    }

    writer.write_u32(static_cast<std::uint32_t>(paths.size()));
    for (const auto& path : paths) {
        if (!writer.write_path(path)) {
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::file_write_failed,
                file,
                0,
                std::string{description} + " path is too long for cache format"));
        }
    }
    return {};
}

[[nodiscard]] std::expected<std::vector<fs::path>, LinkCacheFileError>
read_paths(
    BinaryReader& reader,
    const std::string_view description) {
    auto count = reader.read_u32();
    if (!count) return std::unexpected(count.error());
    if (*count > max_link_input_count) {
        return std::unexpected(reader.corrupt(
            std::string{description} + " count exceeds safety limit"));
    }

    std::vector<fs::path> paths;
    paths.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto path = reader.read_path();
        if (!path) return std::unexpected(path.error());
        paths.push_back(std::move(*path));
    }
    return paths;
}

[[nodiscard]] std::expected<std::vector<std::uint8_t>, LinkCacheFileError>
serialize(const fs::path& file, const LinkCacheEntry& entry) {
    BinaryWriter writer;
    writer.write_bytes(magic);
    writer.write_u32(format_version);
    if (!writer.write_path(entry.linker.linker)
        || !writer.write_string(entry.linker.version)
        || !writer.write_string(entry.linker.binary_stamp)) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_write_failed,
            file,
            0,
            "linker identity is too large for link cache format"));
    }

    writer.write_u64(entry.signature.digest().high);
    writer.write_u64(entry.signature.digest().low);

    if (!writer.write_path(entry.output)) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_write_failed,
            file,
            0,
            "link output path is too long for cache format"));
    }

    auto objects = write_paths(writer, file, entry.objects, "object input");
    if (!objects) return std::unexpected(objects.error());
    auto libraries = write_paths(writer, file, entry.libraries, "library input");
    if (!libraries) return std::unexpected(libraries.error());
    auto side_outputs = write_paths(writer, file, entry.side_outputs, "side output");
    if (!side_outputs) return std::unexpected(side_outputs.error());

    return writer.bytes();
}

[[nodiscard]] std::expected<LinkCacheEntry, LinkCacheFileError>
deserialize(const fs::path& file, const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < magic.size() + sizeof(std::uint32_t)) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::invalid_magic,
            file,
            0,
            "link cache file is too short"));
    }
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != magic[index]) {
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::invalid_magic,
                file,
                index,
                "link cache magic does not match MQB format"));
        }
    }

    BinaryReader reader{file, bytes};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        auto ignored = reader.read_u8();
        if (!ignored) return std::unexpected(ignored.error());
    }
    auto version = reader.read_u32();
    if (!version) return std::unexpected(version.error());
    if (*version != legacy_format_version && *version != format_version) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::unsupported_version,
            file,
            reader.offset(),
            "link cache file version is not supported"));
    }

    auto linker = reader.read_path();
    auto linker_version = reader.read_string();
    auto linker_stamp = reader.read_string();
    auto signature_high = reader.read_u64();
    auto signature_low = reader.read_u64();
    auto output = reader.read_path();

    if (!linker) return std::unexpected(linker.error());
    if (!linker_version) return std::unexpected(linker_version.error());
    if (!linker_stamp) return std::unexpected(linker_stamp.error());
    if (!signature_high) return std::unexpected(signature_high.error());
    if (!signature_low) return std::unexpected(signature_low.error());
    if (!output) return std::unexpected(output.error());

    auto objects = read_paths(reader, "object input");
    if (!objects) return std::unexpected(objects.error());
    auto libraries = read_paths(reader, "library input");
    if (!libraries) return std::unexpected(libraries.error());

    std::vector<fs::path> side_outputs;
    if (*version == format_version) {
        auto loaded_side_outputs = read_paths(reader, "side output");
        if (!loaded_side_outputs) return std::unexpected(loaded_side_outputs.error());
        side_outputs = std::move(*loaded_side_outputs);
    }

    if (!reader.at_end()) {
        return std::unexpected(reader.corrupt("unexpected trailing bytes in link cache file"));
    }

    return LinkCacheEntry{
        .linker = LinkerIdentity{
            .linker = std::move(*linker),
            .version = std::move(*linker_version),
            .binary_stamp = std::move(*linker_stamp),
        },
        .signature = BuildSignature::from_digest(SignatureDigest{
            .high = *signature_high,
            .low = *signature_low,
        }),
        .objects = std::move(*objects),
        .output = std::move(*output),
        .libraries = std::move(*libraries),
        .side_outputs = std::move(side_outputs),
    };
}

[[nodiscard]] fs::path temporary_path_for(const fs::path& file) {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary = file;
    temporary += ".tmp." + std::to_string(tick);
    return temporary;
}

} // namespace

std::expected<std::optional<LinkCacheEntry>, LinkCacheFileError>
LinkCacheFile::load(const fs::path& file) {
    std::error_code error_code;
    const bool exists = fs::exists(file, error_code);
    if (error_code) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_open_failed, file, 0, "failed to query link cache file"));
    }
    if (!exists) {
        return std::optional<LinkCacheEntry>{};
    }

    const auto size = fs::file_size(file, error_code);
    if (error_code) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_read_failed, file, 0, "failed to query link cache file size"));
    }
    if (size > max_cache_file_size) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::corrupt_data, file, 0, "link cache file exceeds safety size limit"));
    }

    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_open_failed, file, 0, "failed to open link cache file"));
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream && !bytes.empty()) {
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::file_read_failed,
            file,
            static_cast<std::size_t>(stream.gcount()),
            "failed to read complete link cache file"));
    }

    auto entry = deserialize(file, bytes);
    if (!entry) return std::unexpected(entry.error());
    return std::optional<LinkCacheEntry>{std::move(*entry)};
}

std::expected<void, LinkCacheFileError>
LinkCacheFile::save(const fs::path& file, const LinkCacheEntry& entry) {
    auto bytes = serialize(file, entry);
    if (!bytes) return std::unexpected(bytes.error());

    std::error_code error_code;
    if (!file.parent_path().empty()) {
        fs::create_directories(file.parent_path(), error_code);
        if (error_code) {
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::file_write_failed, file, 0, "failed to create link cache directory"));
        }
    }

    const fs::path temporary = temporary_path_for(file);
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) {
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::file_open_failed,
                temporary,
                0,
                "failed to open temporary link cache file"));
        }
        if (!bytes->empty()) {
            stream.write(
                reinterpret_cast<const char*>(bytes->data()),
                static_cast<std::streamsize>(bytes->size()));
        }
        stream.flush();
        if (!stream) {
            stream.close();
            fs::remove(temporary, error_code);
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::file_write_failed,
                temporary,
                0,
                "failed to write complete link cache file"));
        }
    }

    if (fs::exists(file, error_code) && !error_code) {
        fs::remove(file, error_code);
        if (error_code) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected(make_error(
                LinkCacheFileErrorCode::replace_failed,
                file,
                0,
                "failed to remove previous link cache entry"));
        }
    } else if (error_code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::replace_failed,
            file,
            0,
            "failed to query previous link cache entry"));
    }

    fs::rename(temporary, file, error_code);
    if (error_code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected(make_error(
            LinkCacheFileErrorCode::replace_failed,
            file,
            0,
            "failed to install new link cache entry"));
    }
    return {};
}

} // namespace mqb
