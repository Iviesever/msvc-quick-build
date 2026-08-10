#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct CompileAction {
    std::filesystem::path source;
    std::filesystem::path object;
    std::vector<BuildReason> reasons;
};

struct LinkAction {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::vector<std::filesystem::path> libraries;
    std::vector<BuildReason> reasons;
};

struct RunAction {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
};

using BuildAction = std::variant<CompileAction, LinkAction, RunAction>;

} // namespace mqb
