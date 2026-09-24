#pragma once
// Test-only prototype. No production include/callsite. Requires generated oracle.
#include <charconv>
#include <limits>
#include <locale>
#include <span>
#include <spanstream>
#include <string_view>
#include "v9_oracle.hpp"

namespace mqb_v9_prototype {
namespace old = mqb_v9_oracle;
using Record = old::CacheRecord;
enum class Route { canonical, byte_fallback, stream_fallback, transport_refused };
struct Result { std::optional<Record> record; Route route; };

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
            if (n == std::string_view::npos || n > old::max_cache_string - value.size()) return false;
            value.append(bytes_.data(), n); bytes_.remove_prefix(n);
            if (take("\"")) return take("\n");
            bytes_.remove_prefix(1); // escape
            if (bytes_.empty() || (bytes_.front() != '\\' && bytes_.front() != '"') ||
                value.size() == old::max_cache_string) return false;
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

inline std::optional<Record> canonical_record(std::string_view bytes) {
    Cursor in(bytes); Record record; std::string root;
    if (!in.take(old::cache_magic) || !in.take("\n") ||
        !in.quoted("target", record.target_architecture) ||
        !in.quoted("host", record.host_architecture) ||
        !in.number("preference", record.preference) ||
        !in.quoted("vc_tools_root", root) || !in.quoted("binary_stamp", record.binary_stamp)) return std::nullopt;
    record.vc_tools_root = old::stable_path(old::detail::path_from_utf8(root));
    std::size_t count{};
    if (!in.number("environment", count) || count > old::max_cache_entries) return std::nullopt;
    for (std::size_t index = 0; index < count; ++index) {
        old::EnvironmentVariable item;
        if (!in.quoted("env_name", item.name) || !in.quoted("env_value", item.value) ||
            !old::cacheable_environment_name(item.name)) return std::nullopt;
        record.environment.push_back(std::move(item));
    }
    if (!in.quoted("ambient_path", record.ambient_path) ||
        !in.quoted("effective_path", record.effective_path) ||
        record.effective_path.empty() || !in.empty()) return std::nullopt;
    return record;
}

inline Result parse_bytes(std::string_view bytes, const std::locale& locale) {
    if (bytes.size() > old::max_cache_size) return {std::nullopt, Route::transport_refused};
    if (locale == std::locale::classic()) {
        if (auto record = canonical_record(bytes)) return {std::move(record), Route::canonical};
    }
    // C++23 bounded read-only view; no istringstream copy. std::quoted/num_get/
    // ctype and their quirks are preserved by the unedited production body.
    std::ispanstream input{std::span<const char>{bytes.data(), bytes.size()}};
    input.imbue(locale);
    return {old::read_record(input), Route::byte_fallback};
}

// Precondition: caller has performed ALL original file admission/freshness
// checks and just opened the same binary stream. This prototype does not perform
// or replace those checks. Tests supply synthetic streams/files only.
inline Result read_prechecked(std::istream& input, std::uintmax_t prechecked_size) {
    if (prechecked_size > old::max_cache_size || !input) return {std::nullopt, Route::transport_refused};
    if (input.getloc() != std::locale::classic()) {
        // Keep custom filebuf codecvt/ctype/num_get behavior. No reopening,
        // narrow-byte conversion bypass or speculative fast path in this lane.
        return {old::read_record(input), Route::stream_fallback};
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
} // namespace mqb_v9_prototype
