#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"

namespace mqb::msvc {

enum class ParameterTool {
    compiler,
    linker,
    librarian,
};

enum class ParameterOwnership {
    mqb_owned,
    semantic,
    passthrough,
    unsupported,
};

enum class ParameterOperandShape {
    none,
    single,
};

struct ParameterTokenShape {
    ParameterTool tool{ParameterTool::compiler};
    ParameterOperandShape operand{ParameterOperandShape::none};
};

enum class ParameterErrorCode {
    empty_argument,
    unknown_option,
    owned_option,
    unsupported_option,
    invalid_value,
    conflicting_semantic_option,
};

struct ParameterClassification {
    ParameterTool tool{ParameterTool::compiler};
    ParameterOwnership ownership{ParameterOwnership::unsupported};
    std::string canonical_name;
    std::string rationale;
};

struct ParameterError {
    ParameterErrorCode code{ParameterErrorCode::unknown_option};
    ParameterTool tool{ParameterTool::compiler};
    std::string argument;
    std::string message;
};

struct CompilerParameterRouting {
    // Native preprocessor inputs remain in passthrough so their ordering with
    // other raw compiler arguments is preserved. The structured fields expose
    // their semantics to MQB subsystems such as source discovery.
    std::vector<std::string> passthrough;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::optional<CppStandard> standard;
    std::optional<RuntimeLibrary> runtime_library;
    std::optional<bool> link_time_code_generation;
};

struct LinkerParameterRouting {
    std::vector<std::string> passthrough;
    std::optional<Architecture> architecture;
    std::optional<LinkSubsystem> subsystem;
    std::optional<bool> link_time_code_generation;
};

enum class LinkerFileInputKind {
    module_definition,
    function_order,
    msdos_stub,
};

struct LinkerFileInput {
    LinkerFileInputKind kind{LinkerFileInputKind::module_definition};
    std::filesystem::path path;
};

struct LinkerFileInputRouting {
    std::vector<std::string> passthrough;
    std::vector<LinkerFileInput> inputs;
    // Some file-bearing linker modes have execution semantics beyond freshness.
    // /ORDER disables MSVC incremental linking whenever an actual link runs.
    bool requires_full_link{false};
};

struct LibrarianParameterRouting {
    std::vector<std::string> passthrough;
    std::optional<Architecture> architecture;
    std::optional<bool> link_time_code_generation;
};

class MsvcParameterEngine {
public:
    [[nodiscard]] static ParameterClassification classify(
        ParameterTool tool,
        std::string_view argument);

    // Describe how many argv elements belong to a native option before the CLI
    // decides whether a following bare token is a source file. Attached-value
    // spellings report `none`; only an exact option that requires a separate
    // argv element reports `single`.
    [[nodiscard]] static ParameterTokenShape token_shape(
        ParameterTool tool,
        std::string_view argument) noexcept;

    [[nodiscard]] static std::expected<CompilerParameterRouting, ParameterError>
    route_compiler(
        std::span<const std::string> arguments,
        std::optional<std::filesystem::path> path_base = std::nullopt);

    // Observation-only view of validated raw /FI options. /FI remains in the
    // passthrough argv and compile identity; this extracts its quoted-include
    // operand for discovery without rewriting it relative to a config/CLI base.
    [[nodiscard]] static std::expected<std::vector<std::filesystem::path>, ParameterError>
    forced_includes(const std::span<const std::string> arguments) {
        auto validated = route_compiler(arguments);
        if (!validated) {
            return std::unexpected(validated.error());
        }

        const auto path_from_utf8 = [](const std::string_view value) {
            std::u8string bytes;
            bytes.assign(
                reinterpret_cast<const char8_t*>(value.data()),
                reinterpret_cast<const char8_t*>(value.data() + value.size()));
            return std::filesystem::path{bytes}.lexically_normal();
        };

        std::vector<std::filesystem::path> result;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const std::string& argument = arguments[index];
            std::string_view body;
            if (argument.size() >= 2
                && (argument.front() == '/' || argument.front() == '-')) {
                body = std::string_view{argument}.substr(1);
            }

            const ParameterOperandShape shape =
                token_shape(ParameterTool::compiler, argument).operand;
            if (body == "FI") {
                // route_compiler() above has already proven the operand exists.
                ++index;
                result.push_back(path_from_utf8(arguments[index]));
                continue;
            }
            if (body.size() > 2 && body.starts_with("FI")) {
                result.push_back(path_from_utf8(body.substr(2)));
                continue;
            }
            if (shape == ParameterOperandShape::single) {
                ++index;
            }
        }
        return result;
    }

    [[nodiscard]] static std::expected<LinkerParameterRouting, ParameterError>
    route_linker(std::span<const std::string> arguments);

    // Preserve raw linker argv ownership while resolving file-bearing options
    // inside the layer that supplied them. The rewritten passthrough keeps the
    // option at the same argv position; `inputs` is non-owning freshness evidence.
    [[nodiscard]] static std::expected<LinkerFileInputRouting, ParameterError>
    linker_file_inputs(
        const std::span<const std::string> arguments,
        const std::optional<std::filesystem::path> path_base = std::nullopt) {
        auto validated = route_linker(arguments);
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
        const auto resolve_path = [&](const std::string_view value) {
            std::filesystem::path path = path_from_utf8(value);
            if (path_base && path.is_relative()) {
                path = *path_base / path;
            }
            return path.lexically_normal();
        };
        const auto replace_last_wins_input = [](
            std::vector<LinkerFileInput>& inputs,
            const LinkerFileInputKind kind,
            std::filesystem::path path) {
            const auto existing = std::find_if(
                inputs.begin(),
                inputs.end(),
                [kind](const LinkerFileInput& input) {
                    return input.kind == kind;
                });
            if (existing == inputs.end()) {
                inputs.push_back(LinkerFileInput{
                    .kind = kind,
                    .path = std::move(path),
                });
            } else {
                existing->path = std::move(path);
            }
        };

        LinkerFileInputRouting result;
        result.passthrough.reserve(validated->passthrough.size());
        for (const auto& argument : validated->passthrough) {
            const std::string_view body = argument.size() >= 2
                    && (argument.front() == '/' || argument.front() == '-')
                ? std::string_view{argument}.substr(1)
                : std::string_view{};

            if (starts_with_ascii_ci(body, "DEF:")) {
                if (body.size() == 4) {
                    return std::unexpected(ParameterError{
                        .code = ParameterErrorCode::invalid_value,
                        .tool = ParameterTool::linker,
                        .argument = argument,
                        .message = "MSVC linker /DEF requires a module-definition file path",
                    });
                }
                const bool duplicate = std::any_of(
                    result.inputs.begin(),
                    result.inputs.end(),
                    [](const LinkerFileInput& input) {
                        return input.kind == LinkerFileInputKind::module_definition;
                    });
                if (duplicate) {
                    return std::unexpected(ParameterError{
                        .code = ParameterErrorCode::conflicting_semantic_option,
                        .tool = ParameterTool::linker,
                        .argument = argument,
                        .message = "MSVC LINK accepts only one /DEF module-definition file",
                    });
                }

                const std::filesystem::path path = resolve_path(body.substr(4));
                result.inputs.push_back(LinkerFileInput{
                    .kind = LinkerFileInputKind::module_definition,
                    .path = path,
                });
                result.passthrough.push_back(
                    path_base ? argument.substr(0, 5) + path_text(path) : argument);
                continue;
            }

            if (starts_with_ascii_ci(body, "ORDER:")) {
                constexpr std::string_view order_prefix = "ORDER:@";
                if (!starts_with_ascii_ci(body, order_prefix)
                    || body.size() == order_prefix.size()) {
                    return std::unexpected(ParameterError{
                        .code = ParameterErrorCode::invalid_value,
                        .tool = ParameterTool::linker,
                        .argument = argument,
                        .message = "MSVC linker /ORDER requires /ORDER:@<filename>",
                    });
                }

                const std::filesystem::path path = resolve_path(
                    body.substr(order_prefix.size()));
                replace_last_wins_input(
                    result.inputs,
                    LinkerFileInputKind::function_order,
                    path);
                result.requires_full_link = true;
                result.passthrough.push_back(
                    path_base
                        ? argument.substr(0, 1 + order_prefix.size()) + path_text(path)
                        : argument);
                continue;
            }

            if (starts_with_ascii_ci(body, "STUB:")) {
                constexpr std::string_view stub_prefix = "STUB:";
                if (body.size() == stub_prefix.size()) {
                    return std::unexpected(ParameterError{
                        .code = ParameterErrorCode::invalid_value,
                        .tool = ParameterTool::linker,
                        .argument = argument,
                        .message = "MSVC linker /STUB requires an MS-DOS .exe file path",
                    });
                }

                const std::filesystem::path path = resolve_path(
                    body.substr(stub_prefix.size()));
                replace_last_wins_input(
                    result.inputs,
                    LinkerFileInputKind::msdos_stub,
                    path);
                result.passthrough.push_back(
                    path_base
                        ? argument.substr(0, 1 + stub_prefix.size()) + path_text(path)
                        : argument);
                continue;
            }

            result.passthrough.push_back(argument);
        }
        return result;
    }

    [[nodiscard]] static std::expected<LibrarianParameterRouting, ParameterError>
    route_librarian(std::span<const std::string> arguments);
};

[[nodiscard]] std::string_view to_string(ParameterTool tool) noexcept;
[[nodiscard]] std::string_view to_string(ParameterOwnership ownership) noexcept;

} // namespace mqb::msvc
