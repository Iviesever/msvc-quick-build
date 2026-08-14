#include "DiscoveryCache.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace mqb::discovery::detail {
namespace {

namespace fs = std::filesystem;
using FileTimeRep = fs::file_time_type::duration::rep;
static_assert(std::is_integral_v<FileTimeRep> && sizeof(FileTimeRep) <= sizeof(std::int64_t));

constexpr std::array<std::uint8_t, 8> magic{
    'M', 'Q', 'B', 'D', 'I', 'S', 'C', '1'};
constexpr std::uint32_t format_version = 1;
constexpr std::size_t max_cache_file_size = 64u * 1024u * 1024u;
constexpr std::uint32_t max_string_size = 4u * 1024u * 1024u;
constexpr std::uint32_t max_path_count = 250000u;

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

[[nodiscard]] std::int64_t timestamp_to_i64(const fs::file_time_type value) noexcept {
    return static_cast<std::int64_t>(value.time_since_epoch().count());
}

[[nodiscard]] fs::file_time_type timestamp_from_i64(const std::int64_t value) noexcept {
    return fs::file_time_type{
        fs::file_time_type::duration{static_cast<FileTimeRep>(value)}};
}

class BinaryWriter {
public:
    void write_u8(const std::uint8_t value) { bytes_.push_back(value); }

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

    void write_i64(const std::int64_t value) {
        write_u64(std::bit_cast<std::uint64_t>(value));
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
    explicit BinaryReader(const std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::optional<std::uint8_t> read_u8() {
        if (offset_ >= bytes_.size()) return std::nullopt;
        return bytes_[offset_++];
    }

    [[nodiscard]] std::optional<std::uint32_t> read_u32() {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) return std::nullopt;
            value |= static_cast<std::uint32_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::uint64_t> read_u64() {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
            auto byte = read_u8();
            if (!byte) return std::nullopt;
            value |= static_cast<std::uint64_t>(*byte) << shift;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::int64_t> read_i64() {
        auto value = read_u64();
        if (!value) return std::nullopt;
        return std::bit_cast<std::int64_t>(*value);
    }

    [[nodiscard]] std::optional<std::string> read_string() {
        auto length = read_u32();
        if (!length || *length > max_string_size) return std::nullopt;
        if (static_cast<std::size_t>(*length) > bytes_.size() - offset_) return std::nullopt;
        const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
        std::string value{begin, begin + *length};
        offset_ += *length;
        return value;
    }

    [[nodiscard]] std::optional<fs::path> read_path() {
        auto value = read_string();
        if (!value) return std::nullopt;
        return path_from_utf8(*value);
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

[[nodiscard]] bool write_paths(BinaryWriter& writer, const std::vector<fs::path>& paths) {
    if (paths.size() > max_path_count) return false;
    writer.write_u32(static_cast<std::uint32_t>(paths.size()));
    for (const auto& path : paths) {
        if (!writer.write_path(path)) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<fs::path>> read_paths(BinaryReader& reader) {
    auto count = reader.read_u32();
    if (!count || *count > max_path_count) return std::nullopt;
    std::vector<fs::path> paths;
    paths.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto path = reader.read_path();
        if (!path) return std::nullopt;
        paths.push_back(std::move(*path));
    }
    return paths;
}

[[nodiscard]] bool write_snapshots(
    BinaryWriter& writer,
    const std::vector<FileSnapshot>& snapshots) {
    if (snapshots.size() > max_path_count) return false;
    writer.write_u32(static_cast<std::uint32_t>(snapshots.size()));
    for (const auto& snapshot : snapshots) {
        if (!snapshot.exists || !writer.write_path(snapshot.path)) return false;
        writer.write_i64(timestamp_to_i64(snapshot.modified));
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<FileSnapshot>> read_snapshots(BinaryReader& reader) {
    auto count = reader.read_u32();
    if (!count || *count > max_path_count) return std::nullopt;
    std::vector<FileSnapshot> snapshots;
    snapshots.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto path = reader.read_path();
        auto modified = reader.read_i64();
        if (!path || !modified) return std::nullopt;
        snapshots.push_back(FileSnapshot{
            .path = std::move(*path),
            .exists = true,
            .modified = timestamp_from_i64(*modified),
        });
    }
    return snapshots;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> serialize(
    const DiscoveryCacheRecord& record) {
    BinaryWriter writer;
    writer.write_bytes(magic);
    writer.write_u32(format_version);

    if (!writer.write_path(record.request.project_root)
        || !writer.write_path(record.request.entry)
        || !write_paths(writer, record.request.include_directories)
        || !write_paths(writer, record.request.excluded_directories)
        || !write_paths(writer, record.request.extra_sources)
        || !write_paths(writer, record.request.excluded_sources)
        || !write_paths(writer, record.result.sources)) {
        return std::nullopt;
    }
    writer.write_u64(static_cast<std::uint64_t>(record.result.indexed_files));
    writer.write_u8(record.result.requires_module_pipeline ? 1u : 0u);
    if (!write_snapshots(writer, record.files)
        || !write_snapshots(writer, record.directories)) {
        return std::nullopt;
    }
    return writer.bytes();
}

[[nodiscard]] std::optional<DiscoveryCacheRecord> deserialize(
    const std::span<const std::uint8_t> bytes) {
    BinaryReader reader{bytes};
    for (const std::uint8_t expected : magic) {
        auto actual = reader.read_u8();
        if (!actual || *actual != expected) return std::nullopt;
    }
    auto version = reader.read_u32();
    if (!version || *version != format_version) return std::nullopt;

    auto project_root = reader.read_path();
    auto entry = reader.read_path();
    auto include_directories = read_paths(reader);
    auto excluded_directories = read_paths(reader);
    auto extra_sources = read_paths(reader);
    auto excluded_sources = read_paths(reader);
    auto sources = read_paths(reader);
    auto indexed_files = reader.read_u64();
    auto requires_module_pipeline = reader.read_u8();
    auto files = read_snapshots(reader);
    auto directories = read_snapshots(reader);
    if (!project_root || !entry || !include_directories || !excluded_directories
        || !extra_sources || !excluded_sources || !sources || !indexed_files
        || !requires_module_pipeline || *requires_module_pipeline > 1u
        || !files || !directories || !reader.at_end()) {
        return std::nullopt;
    }
    if (*indexed_files > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }

    Result result;
    result.sources = std::move(*sources);
    result.indexed_files = static_cast<std::size_t>(*indexed_files);
    result.requires_module_pipeline = *requires_module_pipeline != 0;
    result.reused = true;

    return DiscoveryCacheRecord{
        .request = DiscoveryRequestIdentity{
            .project_root = std::move(*project_root),
            .entry = std::move(*entry),
            .include_directories = std::move(*include_directories),
            .excluded_directories = std::move(*excluded_directories),
            .extra_sources = std::move(*extra_sources),
            .excluded_sources = std::move(*excluded_sources),
        },
        .result = std::move(result),
        .files = std::move(*files),
        .directories = std::move(*directories),
    };
}

[[nodiscard]] bool snapshot_matches(
    const FileSnapshot& snapshot,
    const bool directory) noexcept {
    std::error_code error_code;
    const auto status = fs::status(snapshot.path, error_code);
    if (error_code) return false;
    if (directory ? !fs::is_directory(status) : !fs::is_regular_file(status)) {
        return false;
    }
    const auto modified = fs::last_write_time(snapshot.path, error_code);
    return !error_code && modified == snapshot.modified;
}

[[nodiscard]] bool evidence_matches(const DiscoveryCacheRecord& record) noexcept {
    for (const auto& snapshot : record.files) {
        if (!snapshot_matches(snapshot, false)) return false;
    }
    for (const auto& snapshot : record.directories) {
        if (!snapshot_matches(snapshot, true)) return false;
    }
    return true;
}

[[nodiscard]] fs::path temporary_path_for(const fs::path& file) {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary = file;
    temporary += ".tmp." + std::to_string(tick);
    return temporary;
}

[[nodiscard]] std::optional<DiscoveryCacheRecord> load_record(const fs::path& file) {
    std::error_code error_code;
    if (!fs::is_regular_file(file, error_code) || error_code) return std::nullopt;
    const auto size = fs::file_size(file, error_code);
    if (error_code || size > max_cache_file_size) return std::nullopt;

    std::ifstream stream{file, std::ios::binary};
    if (!stream) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream && !bytes.empty()) return std::nullopt;
    return deserialize(bytes);
}

} // namespace

std::optional<Result> try_reuse_discovery_cache(
    const fs::path& cache_file,
    const DiscoveryRequestIdentity& request) noexcept {
    try {
        auto record = load_record(cache_file);
        if (!record || record->request != request || !evidence_matches(*record)) {
            return std::nullopt;
        }
        return std::move(record->result);
    } catch (...) {
        return std::nullopt;
    }
}

void save_discovery_cache_best_effort(
    const fs::path& cache_file,
    const DiscoveryCacheRecord& record) noexcept {
    try {
        auto bytes = serialize(record);
        if (!bytes) return;

        std::error_code error_code;
        if (!cache_file.parent_path().empty()) {
            fs::create_directories(cache_file.parent_path(), error_code);
            if (error_code) return;
        }

        const fs::path temporary = temporary_path_for(cache_file);
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) return;
            if (!bytes->empty()) {
                stream.write(
                    reinterpret_cast<const char*>(bytes->data()),
                    static_cast<std::streamsize>(bytes->size()));
            }
            stream.flush();
            if (!stream) {
                stream.close();
                fs::remove(temporary, error_code);
                return;
            }
        }

        error_code.clear();
        if (fs::exists(cache_file, error_code) && !error_code) {
            fs::remove(cache_file, error_code);
            if (error_code) {
                fs::remove(temporary, error_code);
                return;
            }
        } else if (error_code) {
            fs::remove(temporary, error_code);
            return;
        }

        fs::rename(temporary, cache_file, error_code);
        if (error_code) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
    } catch (...) {
        return;
    }
}

} // namespace mqb::discovery::detail
