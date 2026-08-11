#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

enum class RuntimeLibrary {
    md,
    mdd,
    mt,
    mtd,
};

struct CompilerOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    // No override preserves the historical preset recipe: Debug -> /MDd,
    // Release -> /MD. An explicit value is emitted later in argv and is
    // additional compile identity without perturbing old default signatures.
    std::optional<RuntimeLibrary> runtime_library;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> additional_arguments;
};

} // namespace mqb
