#include "mqb/msvc/MsvcParameterEngine.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "MsvcParameterRegistry.hpp"

namespace mqb::msvc {
namespace {

[[nodiscard]] ParameterError error(
    const ParameterErrorCode code,
    const ParameterTool tool,
    std::string argument,
    std::string message) {
    return ParameterError{
        .code = code,
        .tool = tool,
        .argument = std::move(argument),
        .message = std::move(message),
    };
}

template <typename T>
[[nodiscard]] std::expected<void, ParameterError> assign_semantic(
    std::optional<T>& destination,
    const T value,
    const ParameterTool tool,
    const std::string& argument,
    const std::string_view name) {
    if (destination && *destination != value) {
        return std::unexpected(error(
            ParameterErrorCode::conflicting_semantic_option,
            tool,
            argument,
            "conflicting raw MSVC options select different values for " + std::string{name}));
    }
    destination = value;
    return {};
}

[[nodiscard]] std::expected<void, ParameterError> reject_classification(
    const ParameterClassification& classified,
    const std::string& argument) {
    if (classified.ownership == ParameterOwnership::mqb_owned) {
        return std::unexpected(error(
            ParameterErrorCode::owned_option,
            classified.tool,
            argument,
            "MSVC option '" + argument + "' is MQB-owned: " + classified.rationale));
    }
    if (classified.ownership == ParameterOwnership::unsupported) {
        return std::unexpected(error(
            detail::is_unregistered(classified)
                ? ParameterErrorCode::unknown_option
                : ParameterErrorCode::unsupported_option,
            classified.tool,
            argument,
            "MSVC option '" + argument + "' is not accepted: " + classified.rationale));
    }
    return {};
}

[[nodiscard]] std::expected<CppStandard, ParameterError> parse_cpp_standard(
    const std::string& argument) {
    const std::string_view body = detail::option_body(argument);
    const std::string_view value = body.substr(4);
    if (value == "c++14") return CppStandard::cpp14;
    if (value == "c++17") return CppStandard::cpp17;
    if (value == "c++20") return CppStandard::cpp20;
    if (value == "c++23preview") return CppStandard::cpp23;
    if (value == "c++latest") return CppStandard::latest;
    return std::unexpected(error(
        ParameterErrorCode::invalid_value,
        ParameterTool::compiler,
        argument,
        "unsupported /std value; supported C++ modes are c++14, c++17, c++20, c++23preview, and c++latest"));
}

[[nodiscard]] std::expected<Architecture, ParameterError> parse_machine(
    const ParameterTool tool,
    const std::string& argument) {
    const std::string body = detail::upper_ascii(detail::option_body(argument));
    const std::string value = body.substr(std::string{"MACHINE:"}.size());
    if (value == "X86") return Architecture::x86;
    if (value == "X64") return Architecture::x64;
    return std::unexpected(error(
        ParameterErrorCode::invalid_value,
        tool,
        argument,
        "MQB currently supports typed /MACHINE:X86 and /MACHINE:X64 only"));
}

[[nodiscard]] std::expected<std::string_view, ParameterError> require_compiler_operand(
    const std::span<const std::string> arguments,
    std::size_t& index,
    const std::string& argument,
    const std::string_view option_name) {
    if (index + 1 >= arguments.size() || arguments[index + 1].empty()) {
        return std::unexpected(error(
            ParameterErrorCode::invalid_value,
            ParameterTool::compiler,
            argument,
            "MSVC compiler option '" + std::string{option_name} + "' requires a following value"));
    }
    ++index;
    return arguments[index];
}

} // namespace

ParameterClassification MsvcParameterEngine::classify(
    const ParameterTool tool,
    const std::string_view argument) {
    switch (tool) {
    case ParameterTool::compiler:
        return detail::classify_compiler_parameter(argument);
    case ParameterTool::linker:
        return detail::classify_linker_parameter(argument);
    case ParameterTool::librarian:
        return detail::classify_librarian_parameter(argument);
    }
    return ParameterClassification{
        .tool = tool,
        .ownership = ParameterOwnership::unsupported,
        .canonical_name = {},
        .rationale = "unknown MSVC tool",
    };
}

std::expected<CompilerParameterRouting, ParameterError>
MsvcParameterEngine::route_compiler(const std::span<const std::string> arguments) {
    CompilerParameterRouting routed;
    routed.passthrough.reserve(arguments.size());

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument.empty()) {
            return std::unexpected(error(
                ParameterErrorCode::empty_argument,
                ParameterTool::compiler,
                argument,
                "empty raw compiler argument"));
        }

        const std::string_view body = detail::option_body(argument);
        if (body == "I" || body == "D") {
            auto operand = require_compiler_operand(arguments, index, argument, body == "I" ? "/I" : "/D");
            if (!operand) {
                return std::unexpected(operand.error());
            }
            if (body == "I") {
                routed.include_directories.push_back(std::filesystem::u8path(std::string{*operand}));
            } else {
                routed.defines.emplace_back(*operand);
            }
            continue;
        }
        if (body.size() > 1 && body.front() == 'I') {
            routed.include_directories.push_back(std::filesystem::u8path(std::string{body.substr(1)}));
            continue;
        }
        if (body.size() > 1 && body.front() == 'D') {
            routed.defines.emplace_back(body.substr(1));
            continue;
        }

        const auto classified = detail::classify_compiler_parameter(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) {
            return std::unexpected(accepted.error());
        }
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }

        if (body == "MD") {
            if (auto assigned = assign_semantic(
                    routed.runtime_library,
                    RuntimeLibrary::md,
                    ParameterTool::compiler,
                    argument,
                    "runtime library"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body == "MDd") {
            if (auto assigned = assign_semantic(
                    routed.runtime_library,
                    RuntimeLibrary::mdd,
                    ParameterTool::compiler,
                    argument,
                    "runtime library"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body == "MT") {
            if (auto assigned = assign_semantic(
                    routed.runtime_library,
                    RuntimeLibrary::mt,
                    ParameterTool::compiler,
                    argument,
                    "runtime library"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body == "MTd") {
            if (auto assigned = assign_semantic(
                    routed.runtime_library,
                    RuntimeLibrary::mtd,
                    ParameterTool::compiler,
                    argument,
                    "runtime library"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body == "GL" || body == "GL-") {
            if (auto assigned = assign_semantic(
                    routed.link_time_code_generation,
                    body == "GL",
                    ParameterTool::compiler,
                    argument,
                    "LTCG"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body.starts_with("std:")) {
            const std::string_view value = body.substr(4);
            if (value == "c11" || value == "c17" || value == "clatest") {
                routed.passthrough.push_back(argument);
                continue;
            }
            auto standard = parse_cpp_standard(argument);
            if (!standard) {
                return std::unexpected(standard.error());
            }
            if (auto assigned = assign_semantic(
                    routed.standard,
                    *standard,
                    ParameterTool::compiler,
                    argument,
                    "C++ standard"); !assigned) {
                return std::unexpected(assigned.error());
            }
        }
    }
    return routed;
}

std::expected<LinkerParameterRouting, ParameterError>
MsvcParameterEngine::route_linker(const std::span<const std::string> arguments) {
    LinkerParameterRouting routed;
    routed.passthrough.reserve(arguments.size());

    for (const auto& argument : arguments) {
        if (argument.empty()) {
            return std::unexpected(error(
                ParameterErrorCode::empty_argument,
                ParameterTool::linker,
                argument,
                "empty raw linker argument"));
        }

        const auto classified = detail::classify_linker_parameter(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) {
            return std::unexpected(accepted.error());
        }
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }

        const std::string body = detail::upper_ascii(detail::option_body(argument));
        if (body.starts_with("MACHINE:")) {
            auto architecture = parse_machine(ParameterTool::linker, argument);
            if (!architecture) {
                return std::unexpected(architecture.error());
            }
            if (auto assigned = assign_semantic(
                    routed.architecture,
                    *architecture,
                    ParameterTool::linker,
                    argument,
                    "target architecture"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body.starts_with("SUBSYSTEM:")) {
            const std::string value = body.substr(std::string{"SUBSYSTEM:"}.size());
            if (value == "CONSOLE") {
                if (auto assigned = assign_semantic(
                        routed.subsystem,
                        LinkSubsystem::console,
                        ParameterTool::linker,
                        argument,
                        "subsystem"); !assigned) {
                    return std::unexpected(assigned.error());
                }
            } else if (value == "WINDOWS") {
                if (auto assigned = assign_semantic(
                        routed.subsystem,
                        LinkSubsystem::windows,
                        ParameterTool::linker,
                        argument,
                        "subsystem"); !assigned) {
                    return std::unexpected(assigned.error());
                }
            } else {
                return std::unexpected(error(
                    ParameterErrorCode::invalid_value,
                    ParameterTool::linker,
                    argument,
                    "MQB currently supports typed /SUBSYSTEM:CONSOLE and /SUBSYSTEM:WINDOWS only"));
            }
        } else if (body == "LTCG" || body == "LTCG:OFF") {
            if (auto assigned = assign_semantic(
                    routed.link_time_code_generation,
                    body == "LTCG",
                    ParameterTool::linker,
                    argument,
                    "LTCG"); !assigned) {
                return std::unexpected(assigned.error());
            }
        }
    }
    return routed;
}

std::expected<LibrarianParameterRouting, ParameterError>
MsvcParameterEngine::route_librarian(const std::span<const std::string> arguments) {
    LibrarianParameterRouting routed;
    routed.passthrough.reserve(arguments.size());

    for (const auto& argument : arguments) {
        if (argument.empty()) {
            return std::unexpected(error(
                ParameterErrorCode::empty_argument,
                ParameterTool::librarian,
                argument,
                "empty raw librarian argument"));
        }

        const auto classified = detail::classify_librarian_parameter(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) {
            return std::unexpected(accepted.error());
        }
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }

        const std::string body = detail::upper_ascii(detail::option_body(argument));
        if (body.starts_with("MACHINE:")) {
            auto architecture = parse_machine(ParameterTool::librarian, argument);
            if (!architecture) {
                return std::unexpected(architecture.error());
            }
            if (auto assigned = assign_semantic(
                    routed.architecture,
                    *architecture,
                    ParameterTool::librarian,
                    argument,
                    "target architecture"); !assigned) {
                return std::unexpected(assigned.error());
            }
        } else if (body == "LTCG") {
            if (auto assigned = assign_semantic(
                    routed.link_time_code_generation,
                    true,
                    ParameterTool::librarian,
                    argument,
                    "LTCG"); !assigned) {
                return std::unexpected(assigned.error());
            }
        }
    }
    return routed;
}

std::string_view to_string(const ParameterTool tool) noexcept {
    switch (tool) {
    case ParameterTool::compiler: return "compiler";
    case ParameterTool::linker: return "linker";
    case ParameterTool::librarian: return "librarian";
    }
    return "unknown";
}

std::string_view to_string(const ParameterOwnership ownership) noexcept {
    switch (ownership) {
    case ParameterOwnership::mqb_owned: return "mqb-owned";
    case ParameterOwnership::semantic: return "semantic";
    case ParameterOwnership::passthrough: return "passthrough";
    case ParameterOwnership::unsupported: return "unsupported";
    }
    return "unknown";
}

} // namespace mqb::msvc
