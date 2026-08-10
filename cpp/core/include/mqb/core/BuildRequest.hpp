#pragma once

#include <filesystem>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct BuildRequest {
    std::vector<std::filesystem::path> sources;
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    bool run_after_build{false};
};

} // namespace mqb
