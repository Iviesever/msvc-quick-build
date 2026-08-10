#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

enum class LinkSubsystem {
    console,
    windows,
};

struct LinkOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    LinkSubsystem subsystem{LinkSubsystem::console};
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> additional_arguments;
};

} // namespace mqb
