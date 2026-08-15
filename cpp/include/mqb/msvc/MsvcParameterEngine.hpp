#pragma once

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

    [[nodiscard]] static std::expected<LinkerParameterRouting, ParameterError>
    route_linker(std::span<const std::string> arguments);

    [[nodiscard]] static std::expected<LibrarianParameterRouting, ParameterError>
    route_librarian(std::span<const std::string> arguments);
};

[[nodiscard]] std::string_view to_string(ParameterTool tool) noexcept;
[[nodiscard]] std::string_view to_string(ParameterOwnership ownership) noexcept;

} // namespace mqb::msvc
