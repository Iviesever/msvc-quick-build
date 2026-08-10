#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc {

enum class LibraryResolutionErrorCode {
    invalid_request,
    working_directory_failed,
    library_not_found,
};

struct LibraryResolutionError {
    LibraryResolutionErrorCode code{LibraryResolutionErrorCode::invalid_request};
    std::string library;
    std::filesystem::path path;
    std::string message;
};

struct ResolvedLibraries {
    std::vector<std::filesystem::path> files;
};

class MsvcLibraryResolver {
public:
    [[nodiscard]] static std::expected<ResolvedLibraries, LibraryResolutionError>
    resolve(
        const MsvcToolchain& toolchain,
        const LinkOptions& options,
        const std::filesystem::path& working_directory = {});
};

} // namespace mqb::msvc
