#pragma once

#include <filesystem>
#include <string>

namespace mqb::platform::windows {

// MQB targets Windows/MSVC. Internal path identity must therefore be stable
// across ordinary ASCII case differences without routing non-ASCII path bytes
// through the active narrow-character locale. Preserve UTF-8 bytes verbatim
// and fold only ASCII A-Z after lexical normalization.
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

    const std::u8string utf8 = normalized.generic_u8string();
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

} // namespace mqb::platform::windows
