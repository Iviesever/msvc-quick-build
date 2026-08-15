#pragma once

#include <expected>
#include <filesystem>
#include <span>
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

    // Resolve only default-library declarations that are currently discoverable
    // through MQB's existing library search order. Missing default libraries are
    // deliberately skipped: raw LINK owns whether an unused default library is
    // required. Returned paths are freshness evidence only and must never be
    // re-emitted as explicit library argv.
    [[nodiscard]] static std::expected<ResolvedLibraries, LibraryResolutionError>
    resolve_available(
        const MsvcToolchain& toolchain,
        std::span<const std::string> libraries,
        std::span<const std::filesystem::path> library_directories,
        const std::filesystem::path& working_directory = {});
};

} // namespace mqb::msvc
