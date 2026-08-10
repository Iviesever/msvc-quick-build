#pragma once

#include <filesystem>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct BuildRequest {
    std::filesystem::path entry;
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    bool run_after_build{false};
};

} // namespace mqb
