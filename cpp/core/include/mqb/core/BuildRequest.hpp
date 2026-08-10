#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct BuildRequest {
    std::vector<std::filesystem::path> sources;
    std::optional<std::string> output_name;
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    bool run_after_build{false};
    std::vector<std::string> run_arguments;
};

} // namespace mqb
