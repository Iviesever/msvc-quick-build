#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#define MQB_PATH_IDENTITY_DEFINED_NOMINMAX
#endif
#include <windows.h>
#ifdef MQB_PATH_IDENTITY_DEFINED_NOMINMAX
#undef NOMINMAX
#undef MQB_PATH_IDENTITY_DEFINED_NOMINMAX
#endif

#include <filesystem>
#include <limits>
#include <optional>
#include <string>

namespace mqb::platform::windows {
namespace path_identity_detail {

[[nodiscard]] inline bool is_ascii(const std::u8string& value) noexcept {
    for (const char8_t ch : value) {
        if (static_cast<unsigned char>(ch) >= 0x80u) return false;
    }
    return true;
}

// CompareStringOrdinal(..., TRUE) uses the operating system uppercase table for
// case-insensitive ordinal comparison. LCMapStringEx without
// LCMAP_LINGUISTIC_CASING uses the matching file-system casing rules, giving us
// a reusable canonical spelling for cache/hash keys rather than only a pairwise
// comparison primitive. Do not request Unicode normalization: Windows ordinal
// filename identity deliberately keeps canonically equivalent code-point
// sequences distinct.
[[nodiscard]] inline std::optional<std::wstring> filesystem_uppercase(
    const std::wstring& value) {
    if (value.empty()) return std::wstring{};
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }

    const int source_size = static_cast<int>(value.size());
    const int required = ::LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_UPPERCASE,
        value.data(),
        source_size,
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    if (required <= 0) return std::nullopt;

    std::wstring mapped(static_cast<std::size_t>(required), L'\0');
    const int written = ::LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_UPPERCASE,
        value.data(),
        source_size,
        mapped.data(),
        required,
        nullptr,
        nullptr,
        0);
    if (written != required) return std::nullopt;
    return mapped;
}

[[nodiscard]] inline std::u8string identity_utf8(
    const std::filesystem::path& normalized) {
    std::u8string original = normalized.generic_u8string();
    if (is_ascii(original)) {
        // Keep the overwhelmingly common path through MQB identical to the old
        // authority: no Win32 call and no wide-string conversion for ASCII-only
        // projects/toolchains. path_identity_key() performs the ASCII fold.
        return original;
    }

    if (const auto mapped = filesystem_uppercase(normalized.wstring())) {
        return std::filesystem::path{*mapped}.generic_u8string();
    }

    // Fail conservatively if Windows casing cannot be produced. Preserving the
    // original Unicode spelling can only cause an avoidable cache miss for a
    // case alias; it cannot collapse two distinct paths into one identity.
    return original;
}

} // namespace path_identity_detail

// MQB targets Windows/MSVC. Internal path identity therefore follows Windows
// case-insensitive ordinal filename semantics across Unicode, rather than only
// ASCII case or the process' active linguistic locale. After Windows file-system
// casing, fold ASCII A-Z back to lowercase so existing ASCII-only MQB cache keys
// remain stable while non-ASCII case pairs share one identity.
//
// std::filesystem::path::lexically_normal() preserves a trailing separator on
// non-root paths. Windows path identity does not: `C:/sdk` and `C:/sdk/` name
// the same path. Strip only redundant non-root trailing components here so all
// callers share that rule instead of recreating local `stable_path()` helpers.
[[nodiscard]] inline std::string path_identity_key(const std::filesystem::path& path) {
    std::filesystem::path normalized = path.lexically_normal();
    while (normalized.filename().empty() && normalized.has_parent_path()) {
        const std::filesystem::path parent = normalized.parent_path();
        if (parent.empty() || parent == normalized) {
            break;
        }
        normalized = parent;
    }

    const std::u8string utf8 = path_identity_detail::identity_utf8(normalized);
    std::string key;
    key.reserve(utf8.size());
    for (const char8_t ch : utf8) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= static_cast<unsigned char>('A')
            && byte <= static_cast<unsigned char>('Z')) {
            key.push_back(static_cast<char>(byte + ('a' - 'A')));
        } else {
            key.push_back(static_cast<char>(byte));
        }
    }
    return key;
}

// Root-containment decisions are identity decisions on Windows, not lexical
// spelling decisions. Compare the same canonical keys used by caches,
// discovery, library resolution, and toolchain identity, and require a path
// separator at the boundary so `C:/project2` is not inside `C:/project`.
[[nodiscard]] inline bool path_identity_contains(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    std::string root_key = path_identity_key(root);
    const std::string candidate_key = path_identity_key(candidate);
    if (root_key.empty() || candidate_key.empty()) {
        return false;
    }
    if (candidate_key == root_key) {
        return true;
    }
    if (root_key.back() != '/') {
        root_key.push_back('/');
    }
    return candidate_key.starts_with(root_key);
}

} // namespace mqb::platform::windows
