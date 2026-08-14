#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

#include "mqb/core/TranslationUnitClassifier.hpp"

namespace mqb::discovery::detail {

namespace fs = std::filesystem;

[[nodiscard]] inline std::string ascii_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) {
            if (ch >= static_cast<unsigned char>('A')
                && ch <= static_cast<unsigned char>('Z')) {
                return static_cast<char>(ch + ('a' - 'A'));
            }
            return static_cast<char>(ch);
        });
    return value;
}

[[nodiscard]] inline std::string utf8_path_text(const fs::path& path) {
    const std::u8string value = path.generic_u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t ch : value) {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

[[nodiscard]] inline std::string path_key(const fs::path& path) {
    return ascii_lower(utf8_path_text(path.lexically_normal()));
}

[[nodiscard]] inline bool inside_root(const fs::path& root, const fs::path& path) {
    const fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool same_or_inside(const fs::path& root, const fs::path& path) {
    return path.lexically_normal() == root.lexically_normal() || inside_root(root, path);
}

[[nodiscard]] inline std::string extension_lower(const fs::path& path) {
    return ascii_lower(utf8_path_text(path.extension()));
}

[[nodiscard]] inline bool header_extension(const fs::path& path) {
    const std::string extension = extension_lower(path);
    return extension == ".h"
        || extension == ".hh"
        || extension == ".hpp"
        || extension == ".hxx"
        || extension == ".inl"
        || extension == ".ipp";
}

[[nodiscard]] inline bool indexed_path(const fs::path& path) {
    return is_translation_unit_path(path) || header_extension(path);
}

[[nodiscard]] inline bool default_excluded_directory(const fs::path& path) {
    const std::string name = ascii_lower(utf8_path_text(path.filename()));
    return name == ".mqb"
        || name == ".git"
        || name == ".vs"
        || name == "build"
        || name == "out"
        || name.starts_with("cmake-build-");
}

} // namespace mqb::discovery::detail
