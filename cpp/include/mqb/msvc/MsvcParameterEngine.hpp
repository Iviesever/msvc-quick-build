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

    [[nodiscard]] static std::expected<CompilerParameterRouting, ParameterError>
    route_compiler(std::span<const std::string> arguments);

    [[nodiscard]] static std::expected<LinkerParameterRouting, ParameterError>
    route_linker(std::span<const std::string> arguments);

    [[nodiscard]] static std::expected<LibrarianParameterRouting, ParameterError>
    route_librarian(std::span<const std::string> arguments);
};

[[nodiscard]] std::string_view to_string(ParameterTool tool) noexcept;
[[nodiscard]] std::string_view to_string(ParameterOwnership ownership) noexcept;

} // namespace mqb::msvc
