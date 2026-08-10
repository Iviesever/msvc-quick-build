#pragma once

#include <filesystem>
#include <string>

namespace mqb {

// Core treats binary_stamp as opaque. The backend decides whether it is an
// mtime, version-resource digest, or stronger content fingerprint.
struct LinkerIdentity {
    std::filesystem::path linker;
    std::string version;
    std::string binary_stamp;
};

} // namespace mqb
