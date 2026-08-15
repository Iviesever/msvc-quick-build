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
    TargetKind target_kind{TargetKind::executable};
    LinkSubsystem subsystem{LinkSubsystem::console};
    // Coupled with CompilerOptions::link_time_code_generation. False preserves
    // the historical link signature/recipe; true makes /LTCG structured policy.
    bool link_time_code_generation{false};
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    // Non-owning file inputs referenced by validated raw linker arguments. Raw
    // argv remains in additional_arguments; this list exists solely for build
    // signature/cache freshness and full-link safety decisions.
    std::vector<std::filesystem::path> additional_input_files;
    std::vector<std::string> additional_arguments;
};

} // namespace mqb
