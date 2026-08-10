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

} // namespace

std::optional<TranslationUnitKind>
classify_translation_unit_path(const std::filesystem::path& path) {
    const std::string extension = extension_lower(path);
    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx") {
        return TranslationUnitKind::source;
    }
    if (extension == ".ixx" || extension == ".cppm" || extension == ".mpp") {
        return TranslationUnitKind::module_interface;
    }
    return std::nullopt;
}

bool is_translation_unit_path(const std::filesystem::path& path) {
    return classify_translation_unit_path(path).has_value();
}

} // namespace mqb
