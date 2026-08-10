#pragma once

#include <filesystem>

namespace mqb {

struct FileSnapshot {
    std::filesystem::path path;
    bool exists{false};
    std::filesystem::file_time_type modified{};
};

} // namespace mqb
