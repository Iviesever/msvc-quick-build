#include "mqb/msvc/MsvcDefaultLibraryPolicy.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool starts_with_ascii_ci(
    const std::string_view value,
    const std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto left = static_cast<unsigned char>(value[index]);
        const auto right = static_cast<unsigned char>(prefix[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equals_ascii_ci(
    const std::string_view value,
    const std::string_view expected) {
    return value.size() == expected.size()
        && starts_with_ascii_ci(value, expected);
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return value;
}

[[nodiscard]] std::string library_key(const std::string_view request) {
    fs::path library = path_from_utf8(request).filename();
    if (library.extension().empty()) {
        library += ".lib";
    }
    return lower_ascii(path_text(library.lexically_normal()));
}

[[nodiscard]] bool path_bearing_library(const fs::path& library) {
    return library.has_root_path() || library.has_parent_path();
}

} // namespace

std::expected<DefaultLibraryRouting, ParameterError>
MsvcDefaultLibraryPolicy::route(
    const std::span<const std::string> arguments,
    const std::optional<fs::path> path_base) {
    auto validated = MsvcParameterEngine::route_linker(arguments);
    if (!validated) {
        return std::unexpected(validated.error());
    }

    DefaultLibraryRouting result;
    result.passthrough.reserve(validated->passthrough.size());

    std::vector<std::string> declared_libraries;

    for (const auto& argument : validated->passthrough) {
        const std::string_view body = argument.size() >= 2
                && (argument.front() == '/' || argument.front() == '-')
            ? std::string_view{argument}.substr(1)
            : std::string_view{};

        if (starts_with_ascii_ci(body, "DEFAULTLIB:")) {
            constexpr std::string_view prefix = "DEFAULTLIB:";
            if (body.size() == prefix.size()) {
                return std::unexpected(ParameterError{
                    .code = ParameterErrorCode::invalid_value,
                    .tool = ParameterTool::linker,
                    .argument = argument,
                    .message = "MSVC linker /DEFAULTLIB requires a library name",
                });
            }

            const std::string_view payload = body.substr(prefix.size());
            fs::path library = path_from_utf8(payload);
            if (path_base && path_bearing_library(library) && library.is_relative()) {
                library = (*path_base / library).lexically_normal();
            } else if (path_bearing_library(library)) {
                library = library.lexically_normal();
            }

            const std::string request = path_bearing_library(library)
                ? path_text(library)
                : std::string{payload};
            declared_libraries.push_back(request);
            result.passthrough.push_back(
                path_bearing_library(library)
                    ? argument.substr(0, 1 + prefix.size()) + request
                    : argument);
            continue;
        }

        if (equals_ascii_ci(body, "NODEFAULTLIB")) {
            result.suppress_all_default_libraries = true;
            result.passthrough.push_back(argument);
            continue;
        }

        if (starts_with_ascii_ci(body, "NODEFAULTLIB:")) {
            constexpr std::string_view prefix = "NODEFAULTLIB:";
            if (body.size() == prefix.size()) {
                return std::unexpected(ParameterError{
                    .code = ParameterErrorCode::invalid_value,
                    .tool = ParameterTool::linker,
                    .argument = argument,
                    .message = "MSVC linker /NODEFAULTLIB requires a library name after ':'",
                });
            }
            result.suppressed_library_keys.push_back(
                library_key(body.substr(prefix.size())));
            result.passthrough.push_back(argument);
            continue;
        }

        result.passthrough.push_back(argument);
    }

    if (result.suppress_all_default_libraries) {
        return result;
    }

    result.effective_libraries.reserve(declared_libraries.size());
    for (auto& library : declared_libraries) {
        if (!allows_implicit_library(result, library)) {
            continue;
        }
        result.effective_libraries.push_back(std::move(library));
    }
    return result;
}

bool MsvcDefaultLibraryPolicy::allows_implicit_library(
    const DefaultLibraryRouting& routing,
    const std::string_view library) {
    if (routing.suppress_all_default_libraries) {
        return false;
    }
    const std::string key = library_key(library);
    return std::find(
               routing.suppressed_library_keys.begin(),
               routing.suppressed_library_keys.end(),
               key)
        == routing.suppressed_library_keys.end();
}

} // namespace mqb::msvc