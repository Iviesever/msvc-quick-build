#pragma once

#include <filesystem>
#include <optional>

#include "mqb/core/TranslationUnit.hpp"

namespace mqb {

[[nodiscard]] std::optional<TranslationUnitKind>
classify_translation_unit_path(const std::filesystem::path& path);

[[nodiscard]] bool is_translation_unit_path(const std::filesystem::path& path);

} // namespace mqb
