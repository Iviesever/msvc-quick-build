#pragma once

#include <filesystem>
#include <vector>

#include "mqb/core/Artifact.hpp"

namespace mqb {

enum class TranslationUnitKind {
    source,
    module_interface,
};

struct TranslationUnit {
    std::filesystem::path source;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::vector<std::filesystem::path> dependencies;
    std::vector<Artifact> outputs;
};

} // namespace mqb
