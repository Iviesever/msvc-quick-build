#include "mqb/core/CompileCacheFile.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb {
namespace {

namespace fs = std::filesystem;

constexpr std::array<std::uint8_t, 8> magic{
    'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E'};
constexpr std::uint32_t format_version = 2;
constexpr std::size_t max_cache_file_size = 64u * 1024u * 1024u;
constexpr std::uint32_t max_string_size = 4u * 1024u * 1024u;
constexpr std::uint32_t max_output_count = 100000u;
constexpr std::uint32_t max_dependency_count = 100000u;

[[nodiscard]] CompileCacheFileError make_error(
    const CompileCacheFileErrorCode code,
    const fs::path& file,
    const std::size_t offset,
    std::string message) {
    return CompileCacheFileError{
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
    BinaryReader(
        const fs::path& file,
        const std::span<const std::uint8_t> bytes)
        : file_(file), bytes_(bytes) {}

    [[nodiscard]] std::size_t offset() const noexcept {
        return offset_;
    }

    [[nodiscard]] bool at_end() const noexcept {
        return offset_ == bytes_.size();
    }

    [[nodiscard]] std::expected<std::uint8_t, CompileCacheFileError> read_u8() {
        if (offset_ >= bytes_.size()) {
            return std::unexpected(corrupt("unexpected end of cache file"));
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::expected<std::uint32_t, CompileCacheFileError> read_u32() {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) {
                return std::unexpected(byte.error());
            }
            value |= static_cast<std::uint32_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::expected<std::uint64_t, CompileCacheFileError> read_u64() {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) {
                return std::unexpected(byte.error());
            }
            value |= static_cast<std::uint64_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::expected<std::string, CompileCacheFileError> read_string() {
        auto length = read_u32();
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > max_string_size) {
            return std::unexpected(corrupt("cache string length exceeds safety limit"));
        }
        if (static_cast<std::size_t>(*length) > bytes_.size() - offset_) {
            return std::unexpected(corrupt("cache string extends past end of file"));
        }

        const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
        std::string value{begin, begin + *length};
        offset_ += *length;
        return value;
    }

    [[nodiscard]] std::expected<fs::path, CompileCacheFileError> read_path() {
        auto value = read_string();
        if (!value) {
            return std::unexpected(value.error());
        }
        return path_from_utf8(*value);
    }

    [[nodiscard]] CompileCacheFileError corrupt(std::string message) const {
        return make_error(
            CompileCacheFileErrorCode::corrupt_data,
            file_,
            offset_,
            std::move(message));
    }

private:
    const fs::path& file_;
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

[[nodiscard]] bool valid_translation_unit_kind(const std::uint32_t value) noexcept {
    return value <= static_cast<std::uint32_t>(TranslationUnitKind::module_interface);
}

[[nodiscard]] bool valid_artifact_kind(const std::uint32_t value) noexcept {
    return value <= static_cast<std::uint32_t>(ArtifactKind::static_library);
}

[[nodiscard]] std::expected<std::vector<std::uint8_t>, CompileCacheFileError>
serialize(const fs::path& file, const CompileCacheEntry& entry) {
    if (entry.outputs.empty()) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_write_failed,
            file,
            0,
            "compile cache entry must contain at least one planned output"));
    }
    if (entry.outputs.size() > max_output_count) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_write_failed,
            file,
            0,
            "output count exceeds cache format safety limit"));
    }
    if (entry.dependencies.size() > max_dependency_count) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_write_failed,
            file,
            0,
            "dependency count exceeds cache format safety limit"));
    }

    BinaryWriter writer;
    writer.write_bytes(magic);
    writer.write_u32(format_version);

    if (!writer.write_path(entry.source)) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_write_failed,
            file,
            0,
            "source path is too long for cache format"));
    }
    writer.write_u32(static_cast<std::uint32_t>(entry.kind));

    if (!writer.write_path(entry.toolchain.compiler)
        || !writer.write_string(entry.toolchain.version)
        || !writer.write_string(entry.toolchain.binary_stamp)) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_write_failed,
            file,
            0,
            "toolchain identity is too large for cache format"));
    }

    writer.write_u64(entry.signature.digest().high);
    writer.write_u64(entry.signature.digest().low);

    writer.write_u32(static_cast<std::uint32_t>(entry.outputs.size()));
    for (const auto& output : entry.outputs) {
        if (output.path.empty()) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::file_write_failed,
                file,
                0,
                "compile cache output path must not be empty"));
        }
        if (!writer.write_path(output.path)) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::file_write_failed,
                file,
                0,
                "output path is too long for cache format"));
        }
        writer.write_u32(static_cast<std::uint32_t>(output.kind));
    }

    writer.write_u32(static_cast<std::uint32_t>(entry.dependencies.size()));
    for (const auto& dependency : entry.dependencies) {
        if (!writer.write_path(dependency)) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::file_write_failed,
                file,
                0,
                "dependency path is too long for cache format"));
        }
    }

    return writer.bytes();
}

[[nodiscard]] std::expected<CompileCacheEntry, CompileCacheFileError>
deserialize(const fs::path& file, const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < magic.size() + sizeof(std::uint32_t)) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::invalid_magic,
            file,
            0,
            "cache file is too short"));
    }

    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != magic[index]) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::invalid_magic,
                file,
                index,
                "cache magic does not match MQB format"));
        }
    }

    BinaryReader reader{file, bytes};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        auto ignored = reader.read_u8();
        if (!ignored) {
            return std::unexpected(ignored.error());
        }
    }

    auto version = reader.read_u32();
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version != format_version) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::unsupported_version,
            file,
            reader.offset(),
            "cache file version is not supported"));
    }

    auto source = reader.read_path();
    auto kind_value = reader.read_u32();
    auto compiler = reader.read_path();
    auto toolchain_version = reader.read_string();
    auto binary_stamp = reader.read_string();
    auto signature_high = reader.read_u64();
    auto signature_low = reader.read_u64();
    auto output_count = reader.read_u32();

    if (!source) return std::unexpected(source.error());
    if (!kind_value) return std::unexpected(kind_value.error());
    if (!compiler) return std::unexpected(compiler.error());
    if (!toolchain_version) return std::unexpected(toolchain_version.error());
    if (!binary_stamp) return std::unexpected(binary_stamp.error());
    if (!signature_high) return std::unexpected(signature_high.error());
    if (!signature_low) return std::unexpected(signature_low.error());
    if (!output_count) return std::unexpected(output_count.error());

    if (!valid_translation_unit_kind(*kind_value)) {
        return std::unexpected(reader.corrupt("invalid translation-unit kind in cache file"));
    }
    if (*output_count == 0 || *output_count > max_output_count) {
        return std::unexpected(reader.corrupt("output count is outside the supported safety range"));
    }

    std::vector<Artifact> outputs;
    outputs.reserve(*output_count);
    for (std::uint32_t index = 0; index < *output_count; ++index) {
        auto output_path = reader.read_path();
        auto output_kind_value = reader.read_u32();
        if (!output_path) return std::unexpected(output_path.error());
        if (!output_kind_value) return std::unexpected(output_kind_value.error());
        if (output_path->empty()) {
            return std::unexpected(reader.corrupt("compile cache output path is empty"));
        }
        if (!valid_artifact_kind(*output_kind_value)) {
            return std::unexpected(reader.corrupt("invalid artifact kind in cache file"));
        }
        outputs.push_back(Artifact{
            .path = std::move(*output_path),
            .kind = static_cast<ArtifactKind>(*output_kind_value),
        });
    }

    auto dependency_count = reader.read_u32();
    if (!dependency_count) return std::unexpected(dependency_count.error());
    if (*dependency_count > max_dependency_count) {
        return std::unexpected(reader.corrupt("dependency count exceeds safety limit"));
    }

    std::vector<fs::path> dependencies;
    dependencies.reserve(*dependency_count);
    for (std::uint32_t index = 0; index < *dependency_count; ++index) {
        auto dependency = reader.read_path();
        if (!dependency) {
            return std::unexpected(dependency.error());
        }
        dependencies.push_back(std::move(*dependency));
    }

    if (!reader.at_end()) {
        return std::unexpected(reader.corrupt("unexpected trailing bytes in cache file"));
    }

    return CompileCacheEntry{
        .source = std::move(*source),
        .kind = static_cast<TranslationUnitKind>(*kind_value),
        .toolchain = ToolchainIdentity{
            .compiler = std::move(*compiler),
            .version = std::move(*toolchain_version),
            .binary_stamp = std::move(*binary_stamp),
        },
        .signature = BuildSignature::from_digest(SignatureDigest{
            .high = *signature_high,
            .low = *signature_low,
        }),
        .outputs = std::move(outputs),
        .dependencies = std::move(dependencies),
    };
}

[[nodiscard]] fs::path temporary_path_for(const fs::path& file) {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary = file;
    temporary += ".tmp." + std::to_string(tick);
    return temporary;
}

} // namespace

std::expected<std::optional<CompileCacheEntry>, CompileCacheFileError>
CompileCacheFile::load(const fs::path& file) {
    std::error_code error_code;
    const bool exists = fs::exists(file, error_code);
    if (error_code) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_open_failed,
            file,
            0,
            "failed to query cache file"));
    }
    if (!exists) {
        return std::optional<CompileCacheEntry>{};
    }

    const auto size = fs::file_size(file, error_code);
    if (error_code) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_read_failed,
            file,
            0,
            "failed to query cache file size"));
    }
    if (size > max_cache_file_size) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::corrupt_data,
            file,
            0,
            "cache file exceeds safety size limit"));
    }

    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_open_failed,
            file,
            0,
            "failed to open cache file"));
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream && !bytes.empty()) {
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::file_read_failed,
            file,
            static_cast<std::size_t>(stream.gcount()),
            "failed to read complete cache file"));
    }

    auto entry = deserialize(file, bytes);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    return std::optional<CompileCacheEntry>{std::move(*entry)};
}

std::expected<void, CompileCacheFileError>
CompileCacheFile::save(const fs::path& file, const CompileCacheEntry& entry) {
    auto bytes = serialize(file, entry);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    std::error_code error_code;
    if (!file.parent_path().empty()) {
        fs::create_directories(file.parent_path(), error_code);
        if (error_code) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::file_write_failed,
                file,
                0,
                "failed to create cache directory"));
        }
    }

    const fs::path temporary = temporary_path_for(file);
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) {
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::file_open_failed,
                temporary,
                0,
                "failed to open temporary cache file for writing"));
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
                CompileCacheFileErrorCode::file_write_failed,
                temporary,
                0,
                "failed to write complete cache file"));
        }
    }

    if (fs::exists(file, error_code) && !error_code) {
        fs::remove(file, error_code);
        if (error_code) {
            fs::remove(temporary, error_code);
            return std::unexpected(make_error(
                CompileCacheFileErrorCode::replace_failed,
                file,
                0,
                "failed to remove previous cache entry"));
        }
    } else if (error_code) {
        fs::remove(temporary, error_code);
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::replace_failed,
            file,
            0,
            "failed to query previous cache entry"));
    }

    fs::rename(temporary, file, error_code);
    if (error_code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected(make_error(
            CompileCacheFileErrorCode::replace_failed,
            file,
            0,
            "failed to install new cache entry"));
    }

    return {};
}

} // namespace mqb