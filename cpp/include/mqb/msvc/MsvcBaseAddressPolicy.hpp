#pragma once

#include <cctype>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc {

enum class BaseAddressErrorCode {
    invalid_response_file,
    working_directory_failed,
    response_file_not_found,
};

struct BaseAddressError {
    BaseAddressErrorCode code{BaseAddressErrorCode::invalid_response_file};
    std::string argument;
    std::filesystem::path path;
    std::string message;
};

struct BaseAddressRouting {
    std::optional<std::filesystem::path> response_file;
};

class MsvcBaseAddressPolicy {
public:
    // Observe only the effective /BASE option. LINK uses last-one-wins option
    // semantics, so an earlier /BASE:@file,key must not remain freshness
    // evidence when a later numeric /BASE replaces it.
    //
    // The raw linker argv is deliberately left untouched. In particular, a
    // bare /BASE:@filename,key is resolved through the VC toolchain's LIB
    // environment exactly for freshness observation; rewriting it to an
    // absolute path would change native LINK search semantics.
    [[nodiscard]] static std::expected<BaseAddressRouting, BaseAddressError>
    route(
        const MsvcToolchain& toolchain,
        const std::span<const std::string> arguments,
        const std::filesystem::path& working_directory = {}) {
        namespace fs = std::filesystem;

        std::optional<std::string> effective_argument;
        std::optional<std::string_view> effective_payload;
        for (const auto& argument : arguments) {
            if (argument.size() < 2
                || (argument.front() != '/' && argument.front() != '-')) {
                continue;
            }
            const std::string_view body{argument.data() + 1, argument.size() - 1};
            constexpr std::string_view prefix = "BASE:";
            if (!starts_with_ascii_ci(body, prefix)) {
                continue;
            }
            effective_argument = argument;
            effective_payload = std::string_view{*effective_argument}.substr(1 + prefix.size());
        }

        if (!effective_payload || effective_payload->empty()
            || effective_payload->front() != '@') {
            return BaseAddressRouting{};
        }

        const std::string_view response_spec = effective_payload->substr(1);
        const std::size_t comma = response_spec.find(',');
        if (comma == std::string_view::npos
            || comma == 0
            || comma + 1 == response_spec.size()) {
            return std::unexpected(BaseAddressError{
                .code = BaseAddressErrorCode::invalid_response_file,
                .argument = effective_argument.value_or(std::string{}),
                .path = {},
                .message = "MSVC linker /BASE response-file form requires /BASE:@<filename>,<key>",
            });
        }

        std::string_view filename = response_spec.substr(0, comma);
        const std::string_view key = response_spec.substr(comma + 1);
        if (key.empty()) {
            return std::unexpected(BaseAddressError{
                .code = BaseAddressErrorCode::invalid_response_file,
                .argument = effective_argument.value_or(std::string{}),
                .path = {},
                .message = "MSVC linker /BASE response-file form requires a non-empty key",
            });
        }
        if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"') {
            filename.remove_prefix(1);
            filename.remove_suffix(1);
        }
        if (filename.empty()) {
            return std::unexpected(BaseAddressError{
                .code = BaseAddressErrorCode::invalid_response_file,
                .argument = effective_argument.value_or(std::string{}),
                .path = {},
                .message = "MSVC linker /BASE response-file form requires a non-empty filename",
            });
        }

        auto resolved_working_directory = resolve_working_directory(working_directory);
        if (!resolved_working_directory) {
            return std::unexpected(resolved_working_directory.error());
        }

        const fs::path requested = path_from_utf8(filename);
        if (requested.is_absolute() || requested.has_parent_path()) {
            const fs::path resolved = requested.is_absolute()
                ? requested.lexically_normal()
                : (*resolved_working_directory / requested).lexically_normal();
            return BaseAddressRouting{.response_file = resolved};
        }

        // Microsoft documents a distinct search rule for this /BASE form:
        // when filename has no path, LINK searches directories from LIB. Do
        // not add MQB -L directories or the working directory here.
        for (const auto& directory : split_search_path(environment_value(toolchain, "LIB"))) {
            const fs::path root = directory.is_absolute()
                ? directory.lexically_normal()
                : (*resolved_working_directory / directory).lexically_normal();
            const fs::path candidate = (root / requested).lexically_normal();
            std::error_code error_code;
            if (fs::is_regular_file(candidate, error_code) && !error_code) {
                return BaseAddressRouting{.response_file = candidate};
            }
        }

        return std::unexpected(BaseAddressError{
            .code = BaseAddressErrorCode::response_file_not_found,
            .argument = effective_argument.value_or(std::string{}),
            .path = requested,
            .message = "MSVC linker /BASE response file '" + path_to_utf8(requested)
                + "' was not found in the toolchain LIB search path",
        });
    }

private:
    [[nodiscard]] static bool starts_with_ascii_ci(
        const std::string_view value,
        const std::string_view prefix) noexcept {
        if (value.size() < prefix.size()) {
            return false;
        }
        for (std::size_t index = 0; index < prefix.size(); ++index) {
            unsigned char left = static_cast<unsigned char>(value[index]);
            unsigned char right = static_cast<unsigned char>(prefix[index]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool ascii_iequals(
        const std::string_view left,
        const std::string_view right) noexcept {
        return left.size() == right.size() && starts_with_ascii_ci(left, right);
    }

    [[nodiscard]] static std::filesystem::path path_from_utf8(
        const std::string_view value) {
        std::u8string bytes;
        bytes.assign(
            reinterpret_cast<const char8_t*>(value.data()),
            reinterpret_cast<const char8_t*>(value.data() + value.size()));
        return std::filesystem::path{bytes};
    }

    [[nodiscard]] static std::string path_to_utf8(
        const std::filesystem::path& path) {
        const auto bytes = path.generic_u8string();
        return std::string{
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size()};
    }

    [[nodiscard]] static std::string_view environment_value(
        const MsvcToolchain& toolchain,
        const std::string_view name) noexcept {
        for (const auto& variable : toolchain.environment) {
            if (ascii_iequals(variable.name, name)) {
                return variable.value;
            }
        }
        return {};
    }

    [[nodiscard]] static std::vector<std::filesystem::path> split_search_path(
        const std::string_view value) {
        std::vector<std::filesystem::path> result;
        std::size_t begin = 0;
        while (begin <= value.size()) {
            const std::size_t separator = value.find(';', begin);
            const std::size_t end = separator == std::string_view::npos
                ? value.size()
                : separator;
            std::string_view token = value.substr(begin, end - begin);
            while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
                token.remove_prefix(1);
            }
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
                token.remove_suffix(1);
            }
            if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
                token.remove_prefix(1);
                token.remove_suffix(1);
            }
            if (!token.empty()) {
                result.push_back(path_from_utf8(token));
            }
            if (separator == std::string_view::npos) {
                break;
            }
            begin = separator + 1;
        }
        return result;
    }

    [[nodiscard]] static std::expected<std::filesystem::path, BaseAddressError>
    resolve_working_directory(const std::filesystem::path& requested) {
        namespace fs = std::filesystem;
        std::error_code error_code;
        fs::path result = requested.empty()
            ? fs::current_path(error_code)
            : fs::absolute(requested, error_code);
        if (error_code) {
            return std::unexpected(BaseAddressError{
                .code = BaseAddressErrorCode::working_directory_failed,
                .argument = {},
                .path = requested,
                .message = "failed to resolve /BASE response-file working directory: "
                    + error_code.message(),
            });
        }
        return result.lexically_normal();
    }
};

} // namespace mqb::msvc
