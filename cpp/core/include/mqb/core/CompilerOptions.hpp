#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct CompilerOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> additional_arguments;
};

} // namespace mqb
