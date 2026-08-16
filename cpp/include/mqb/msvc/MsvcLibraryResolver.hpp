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

    // Refresh full library paths previously observed from LINK /VERBOSE:LIB.
    // Libraries that were selected from the normal -L / working-directory / LIB
    // search path are re-resolved by basename so a newly higher-priority same-name
    // library is visible before LINK runs again. Paths outside those search roots
    // are treated as absolute directive inputs and keep their exact identity.
    // Non-library paths are ignored, allowing callers to pass mixed cached
    // file-input evidence without duplicating library-search policy.
    [[nodiscard]] static std::expected<ResolvedLibraries, LibraryResolutionError>
    refresh_observed(
        const MsvcToolchain& toolchain,
        std::span<const std::filesystem::path> observed_inputs,
        std::span<const std::filesystem::path> library_directories,
        const std::filesystem::path& working_directory = {});
};

} // namespace mqb::msvc
