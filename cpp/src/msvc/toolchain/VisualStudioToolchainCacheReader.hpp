#pragma once
// Private V9 wire reader. Parsing is not toolchain/trust/freshness admission.
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <locale>
#include <optional>
#include <span>
#include <spanstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ToolchainDiscoveryPrimitives.hpp"

namespace mqb::msvc::detail::v9_cache {
namespace fs = std::filesystem;
using process::EnvironmentVariable;

// Moved from VisualStudioToolchainCache.cpp; legacy bodies and limits retained.
constexpr std::string_view cache_magic = "MQB_TOOLCHAIN_CACHE_V9";
constexpr std::uintmax_t max_cache_size = 1024u * 1024u;
constexpr std::size_t max_cache_entries = 64u;
constexpr std::size_t max_cache_string = 256u * 1024u;

struct CacheRecord {
    std::string target_architecture;
    std::string host_architecture;
    int preference{};
    fs::path vc_tools_root;
    std::string binary_stamp;
    std::vector<EnvironmentVariable> environment;
    std::string ambient_path;
    std::string effective_path;
};

[[nodiscard]] inline fs::path stable_path(fs::path path) {
    path = path.lexically_normal();
    while (path.filename().empty() && path.has_parent_path()) {
        const fs::path parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return path;
}

[[nodiscard]] inline bool environment_name_equal(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

[[nodiscard]] inline bool cacheable_environment_name(const std::string_view name) {
    constexpr std::string_view names[]{
        "INCLUDE", "LIB", "LIBPATH", "VCToolsInstallDir", "WindowsSdkDir",
        "WindowsSDKVersion", "UniversalCRTSdkDir", "UCRTVersion", "NETFXSDKDir",
    };
    for (const auto candidate : names) {
        if (environment_name_equal(name, candidate)) return true;
    }
    return false;
}

[[nodiscard]] inline bool read_quoted(std::istream& stream, const std::string_view expected_label, std::string& value) {
    std::string label;
    if (!(stream >> label >> std::quoted(value))) return false;
    return label == expected_label && value.size() <= max_cache_string;
}

[[nodiscard]] inline std::optional<std::size_t> read_count(std::istream& stream, const std::string_view expected_label) {
    std::string label;
    std::size_t count{};
    if (!(stream >> label >> count) || label != expected_label || count > max_cache_entries) return std::nullopt;
    return count;
}

[[nodiscard]] inline std::optional<CacheRecord> read_record(std::istream& stream) {
    std::string magic;
    if (!std::getline(stream, magic) || magic != cache_magic) return std::nullopt;
    CacheRecord record;
    if (!read_quoted(stream, "target", record.target_architecture) || !read_quoted(stream, "host", record.host_architecture)) return std::nullopt;
    std::string preference_label;
    if (!(stream >> preference_label >> record.preference) || preference_label != "preference") return std::nullopt;
    std::string root;
    if (!read_quoted(stream, "vc_tools_root", root) || !read_quoted(stream, "binary_stamp", record.binary_stamp)) return std::nullopt;
    record.vc_tools_root = stable_path(detail::path_from_utf8(root));
    const auto environment_count = read_count(stream, "environment");
    if (!environment_count) return std::nullopt;
    for (std::size_t index = 0; index < *environment_count; ++index) {
        EnvironmentVariable variable;
        if (!read_quoted(stream, "env_name", variable.name)
            || !read_quoted(stream, "env_value", variable.value)
            || !cacheable_environment_name(variable.name)) return std::nullopt;
        record.environment.push_back(std::move(variable));
    }
    if (!read_quoted(stream, "ambient_path", record.ambient_path)
        || !read_quoted(stream, "effective_path", record.effective_path)
        || record.effective_path.empty()) return std::nullopt;
    stream >> std::ws;
    if (!stream.eof()) return std::nullopt;
    return record;
}


enum class Route { canonical, byte_fallback, stream_fallback, transport_refused };
struct Result { std::optional<CacheRecord> record; Route route; };

// Accept a SUBSET of the old grammar. Any miss is retried by the actual old
// parser on the SAME complete bytes; a miss is never a new rejection policy.
class Cursor {
    std::string_view bytes_;
public:
    explicit Cursor(std::string_view bytes) : bytes_(bytes) {}
    bool take(std::string_view prefix) {
        if (!bytes_.starts_with(prefix)) return false;
        bytes_.remove_prefix(prefix.size()); return true;
    }
    bool quoted(std::string_view label, std::string& value) {
        if (!take(label) || !take(" \"")) return false;
        value.clear();
        while (!bytes_.empty()) {
            const auto n = bytes_.find_first_of("\\\"");
            if (n == std::string_view::npos || n > max_cache_string - value.size()) return false;
            value.append(bytes_.data(), n); bytes_.remove_prefix(n);
            if (take("\"")) return take("\n");
            bytes_.remove_prefix(1); // escape
            if (bytes_.empty() || (bytes_.front() != '\\' && bytes_.front() != '"') ||
                value.size() == max_cache_string) return false;
            value.push_back(bytes_.front()); bytes_.remove_prefix(1);
        }
        return false;
    }
    template<class T> bool number(std::string_view label, T& value) {
        if (!take(label) || !take(" ")) return false;
        const auto n = bytes_.find('\n');
        if (n == std::string_view::npos || n == 0 || n > 20) return false;
        const auto token = bytes_.substr(0, n);
        // Canonical writer's nonnegative decimal spelling only. +/-/grouping/
        // leading zeros/overflow etc retain actual formatted-extraction behavior.
        if (token.front() < '0' || token.front() > '9' ||
            (token.size() > 1 && token.front() == '0')) return false;
        const auto [end, ec] = std::from_chars(token.data(), token.data()+token.size(), value);
        if (ec != std::errc{} || end != token.data()+token.size()) return false;
        bytes_.remove_prefix(n+1); return true;
    }
    bool empty() const noexcept { return bytes_.empty(); }
};

inline std::optional<CacheRecord> canonical_record(std::string_view bytes) {
    Cursor in(bytes); CacheRecord record; std::string root;
    if (!in.take(cache_magic) || !in.take("\n") ||
        !in.quoted("target", record.target_architecture) ||
        !in.quoted("host", record.host_architecture) ||
        !in.number("preference", record.preference) ||
        !in.quoted("vc_tools_root", root) || !in.quoted("binary_stamp", record.binary_stamp)) return std::nullopt;
    record.vc_tools_root = stable_path(detail::path_from_utf8(root));
    std::size_t count{};
    if (!in.number("environment", count) || count > max_cache_entries) return std::nullopt;
    for (std::size_t index = 0; index < count; ++index) {
        EnvironmentVariable item;
        if (!in.quoted("env_name", item.name) || !in.quoted("env_value", item.value) ||
            !cacheable_environment_name(item.name)) return std::nullopt;
        record.environment.push_back(std::move(item));
    }
    if (!in.quoted("ambient_path", record.ambient_path) ||
        !in.quoted("effective_path", record.effective_path) ||
        record.effective_path.empty() || !in.empty()) return std::nullopt;
    return record;
}

inline Result parse_bytes(std::string_view bytes, const std::locale& locale) {
    if (bytes.size() > max_cache_size) return {std::nullopt, Route::transport_refused};
    if (locale == std::locale::classic()) {
        if (auto record = canonical_record(bytes)) return {std::move(record), Route::canonical};
    }
    // C++23 bounded read-only view; no istringstream copy. std::quoted/num_get/
    // ctype and their quirks are preserved by the unedited production body.
    std::ispanstream input{std::span<const char>{bytes.data(), bytes.size()}};
    input.imbue(locale);
    return {read_record(input), Route::byte_fallback};
}

// Precondition: caller has performed ALL original file type/size/age admission
// checks and just opened the same binary stream. This reader does not perform
// or replace those checks. The caller retains all toolchain/trust validation after parsing.
inline Result read_prechecked(std::istream& input, std::uintmax_t prechecked_size) {
    if (prechecked_size > max_cache_size || !input) return {std::nullopt, Route::transport_refused};
    if (input.getloc() != std::locale::classic()) {
        // Keep custom filebuf codecvt/ctype/num_get behavior. No reopening,
        // narrow-byte conversion bypass or speculative fast path in this lane.
        return {read_record(input), Route::stream_fallback};
    }
    std::string bytes(static_cast<std::size_t>(prechecked_size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || input.bad())
        return {std::nullopt, Route::transport_refused};
    // At most size+1 logical characters consumed/peeked. Not a syscall-count,
    // atomic snapshot or concurrent-writer guarantee; filebuf may read ahead.
    if (input.peek() != std::char_traits<char>::eof() || input.bad())
        return {std::nullopt, Route::transport_refused};
    return parse_bytes(bytes, input.getloc());
}
} // namespace mqb::msvc::detail::v9_cache
