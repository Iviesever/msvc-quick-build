#include "mqb/core/TranslationUnitClassifier.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

namespace mqb {
namespace {

[[nodiscard]] std::string extension_lower(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

[[nodiscard]] bool cpp_ordinary_extension(const std::string& extension) {
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}

[[nodiscard]] bool module_extension(const std::string& extension) {
    return extension == ".ixx" || extension == ".cppm" || extension == ".mpp";
}

} // namespace

std::optional<TranslationUnitKind>
classify_translation_unit_path(const std::filesystem::path& path) {
    const std::string extension = extension_lower(path);
    if (extension == ".c" || cpp_ordinary_extension(extension)) {
        return TranslationUnitKind::source;
    }
    if (module_extension(extension)) {
        return TranslationUnitKind::module_interface;
    }
    return std::nullopt;
}

bool is_translation_unit_path(const std::filesystem::path& path) {
    return classify_translation_unit_path(path).has_value();
}

bool is_c_translation_unit_path(const std::filesystem::path& path) {
    return extension_lower(path) == ".c";
}

bool is_cpp_translation_unit_path(const std::filesystem::path& path) {
    const std::string extension = extension_lower(path);
    return cpp_ordinary_extension(extension) || module_extension(extension);
}

} // namespace mqb
