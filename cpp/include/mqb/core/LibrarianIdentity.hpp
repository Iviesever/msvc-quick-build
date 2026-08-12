#pragma once

#include <filesystem>
#include <string>

namespace mqb {

struct LibrarianIdentity {
    std::filesystem::path librarian;
    std::string version;
    std::string binary_stamp;
};

} // namespace mqb
