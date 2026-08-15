#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::msvc {

struct WholeArchiveRouting {
    std::vector<std::string> passthrough;
    // Every path-bearing /WHOLEARCHIVE:<library> is an actual required LINK
    // input. Bare /WHOLEARCHIVE introduces no additional library by itself.
    std::vector<std::string> libraries;
};

class MsvcWholeArchivePolicy {
public:
    [[nodiscard]] static std::expected<WholeArchiveRouting, ParameterError>
    route(
        const std::span<const std::string> arguments,
        const std::optional<std::filesystem::path> path_base = std::nullopt) {
        auto validated = MsvcParameterEngine::route_linker(arguments);
        if (!validated) {
            return std::unexpected(validated.error());
        }

        const auto starts_with_ascii_ci = [](
            const std::string_view value,
            const std::string_view prefix) {
            if (value.size() < prefix.size()) return false;
            for (std::size_t index = 0; index < prefix.size(); ++index) {
                char left = value[index];
                char right = prefix[index];
                if (left >= 'a' && left <= 'z') left = static_cast<char>(left - 'a' + 'A');
                if (right >= 'a' && right <= 'z') right = static_cast<char>(right - 'a' + 'A');
                if (left != right) return false;
            }
            return true;
        };
        const auto path_from_utf8 = [](const std::string_view value) {
            std::u8string bytes;
            bytes.assign(
                reinterpret_cast<const char8_t*>(value.data()),
                reinterpret_cast<const char8_t*>(value.data() + value.size()));
            return std::filesystem::path{bytes};
        };
        const auto path_text = [](const std::filesystem::path& path) {
            const auto bytes = path.generic_u8string();
            return std::string{
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size()};
        };
        const auto path_bearing_library = [](const std::filesystem::path& library) {
            return library.has_root_path() || library.has_parent_path();
        };

        WholeArchiveRouting result;
        result.passthrough.reserve(validated->passthrough.size());

        for (const auto& argument : validated->passthrough) {
            const std::string_view body = argument.size() >= 2
                    && (argument.front() == '/' || argument.front() == '-')
                ? std::string_view{argument}.substr(1)
                : std::string_view{};

            constexpr std::string_view prefix = "WHOLEARCHIVE:";
            if (!starts_with_ascii_ci(body, prefix)) {
                result.passthrough.push_back(argument);
                continue;
            }
            if (body.size() == prefix.size()) {
                return std::unexpected(ParameterError{
                    .code = ParameterErrorCode::invalid_value,
                    .tool = ParameterTool::linker,
                    .argument = argument,
                    .message = "MSVC linker /WHOLEARCHIVE:<library> requires a library name or path",
                });
            }

            const std::string_view payload = body.substr(prefix.size());
            std::filesystem::path library = path_from_utf8(payload);
            if (path_base && path_bearing_library(library) && library.is_relative()) {
                library = (*path_base / library).lexically_normal();
            } else if (path_bearing_library(library)) {
                library = library.lexically_normal();
            }

            const std::string request = path_bearing_library(library)
                ? path_text(library)
                : std::string{payload};
            result.libraries.push_back(request);
            result.passthrough.push_back(
                path_bearing_library(library)
                    ? argument.substr(0, 1 + prefix.size()) + request
                    : argument);
        }

        return result;
    }
};

} // namespace mqb::msvc
