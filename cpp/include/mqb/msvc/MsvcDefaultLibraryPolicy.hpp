#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

    // LINK /NODEFAULTLIB without a payload suppresses every library introduced
    // through default-library directives, including directives embedded by cl
    // in object files. Keep that state so compiler->link policies can decide
    // whether an implicit runtime library still participates in the link.
    bool suppress_all_default_libraries{false};

    // Canonical lowercase filename keys (with .lib suffix) supplied through
    // /NODEFAULTLIB:<name>. Kept as policy evidence rather than user argv.
    std::vector<std::string> suppressed_library_keys;
};

class MsvcDefaultLibraryPolicy {
public:
    [[nodiscard]] static std::expected<DefaultLibraryRouting, ParameterError>
    route(
        std::span<const std::string> arguments,
        std::optional<std::filesystem::path> path_base = std::nullopt);

    // Returns whether a compiler/object default-library directive for `library`
    // remains effective after the user's final /NODEFAULTLIB policy.
    [[nodiscard]] static bool allows_implicit_library(
        const DefaultLibraryRouting& routing,
        std::string_view library);
};

} // namespace mqb::msvc