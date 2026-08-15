#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::msvc {

struct DefaultLibraryRouting {
    // Raw LINK ownership is preserved. Only path-bearing /DEFAULTLIB payloads
    // may be normalized to the base of the config/profile/CLI layer that
    // supplied them.
    std::vector<std::string> passthrough;

    // User-declared /DEFAULTLIB requests that remain effective after applying
    // /NODEFAULTLIB and /NODEFAULTLIB:<name> to the final merged LINK argv.
    // These are freshness observations only; callers must never re-emit them
    // as explicit library argv.
    std::vector<std::string> effective_libraries;
};

class MsvcDefaultLibraryPolicy {
public:
    [[nodiscard]] static std::expected<DefaultLibraryRouting, ParameterError>
    route(
        std::span<const std::string> arguments,
        std::optional<std::filesystem::path> path_base = std::nullopt);
};

} // namespace mqb::msvc
