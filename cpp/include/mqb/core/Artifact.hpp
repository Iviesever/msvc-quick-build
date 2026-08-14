#pragma once

#include <filesystem>

namespace mqb {

enum class ArtifactKind {
    object,
    module_interface,
    precompiled_header,
    executable,
    dynamic_library,
    static_library,
};

struct Artifact {
    std::filesystem::path path;
    ArtifactKind kind{ArtifactKind::object};
};

} // namespace mqb
