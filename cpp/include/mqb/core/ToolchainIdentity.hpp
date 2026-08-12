#pragma once

#include <filesystem>
#include <string>

namespace mqb {

// The MSVC backend constructs this value from the discovered compiler.
// Core treats binary_stamp as an opaque identity component so the backend can
// evolve from mtime-based stamps to stronger fingerprints without changing the
// cache model.
struct ToolchainIdentity {
    std::filesystem::path compiler;
    std::string version;
    std::string binary_stamp;
};

} // namespace mqb
