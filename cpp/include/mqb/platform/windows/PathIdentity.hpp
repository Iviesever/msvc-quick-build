#pragma once

#include <filesystem>
#include <string>

namespace mqb::platform::windows {

// MQB targets Windows/MSVC. Internal path identity must therefore be stable
// across ordinary ASCII case differences without routing non-ASCII path bytes
// through the active narrow-character locale. Preserve UTF-8 bytes verbatim
// and fold only ASCII A-Z after lexical normalization.
[[nodiscard]] inline std::string path_identity_key(const std::filesystem::path& path) {
    const std::u8string utf8 = path.lexically_normal().generic_u8string();
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
