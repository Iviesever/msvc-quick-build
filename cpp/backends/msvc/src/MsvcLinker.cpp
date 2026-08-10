#include "mqb/msvc/MsvcLinker.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] LinkerError failure(
    const LinkerErrorCode code,
    std::string message,
    fs::path path = {}) {
    return LinkerError{
        .code = code,
        .message = std::move(message),
        .path = std::move(path),
    };
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string architecture_argument(const Architecture architecture) {
    return architecture == Architecture::x86 ? "/MACHINE:X86" : "/MACHINE:X64";
}

[[nodiscard]] std::string subsystem_argument(const LinkSubsystem subsystem) {
    return subsystem == LinkSubsystem::windows ? "/SUBSYSTEM:WINDOWS" : "/SUBSYSTEM:CONSOLE";
}

[[nodiscard]] std::expected<void, LinkerError> prepare_output_parent(const fs::path& output) {
    const fs::path parent = output.parent_path();
    if (parent.empty()) {
        return {};
    }

    std::error_code error_code;
    fs::create_directories(parent, error_code);
    if (error_code) {
        return std::unexpected(failure(
            LinkerErrorCode::output_prepare_failed,
            "failed to prepare linker output directory",
            parent));
    }
    return {};
}

} // namespace

std::expected<LinkerIdentity, LinkerError>
MsvcLinker::identity(const MsvcToolchain& toolchain) {
    if (toolchain.linker.empty()) {
        return std::unexpected(failure(
            LinkerErrorCode::identity_failed,
            "MSVC linker path is empty"));
    }

    std::error_code error_code;
    if (!fs::is_regular_file(toolchain.linker, error_code) || error_code) {
        return std::unexpected(failure(
            LinkerErrorCode::identity_failed,
            "MSVC linker executable does not exist",
            toolchain.linker));
    }

    const auto size = fs::file_size(toolchain.linker, error_code);
    if (error_code) {
        return std::unexpected(failure(
            LinkerErrorCode::identity_failed,
            "cannot read MSVC linker size",
            toolchain.linker));
    }
    const auto modified = fs::last_write_time(toolchain.linker, error_code);
    if (error_code) {
        return std::unexpected(failure(
            LinkerErrorCode::identity_failed,
            "cannot read MSVC linker timestamp",
            toolchain.linker));
    }

    return LinkerIdentity{
        .linker = toolchain.linker,
        .version = toolchain.identity.version,
        .binary_stamp = std::to_string(size) + ":"
            + std::to_string(modified.time_since_epoch().count()),
    };
}

std::expected<std::vector<std::string>, LinkerError>
MsvcLinker::build_arguments(const LinkInvocation& invocation) {
    if (invocation.objects.empty()) {
        return std::unexpected(failure(
            LinkerErrorCode::invalid_request,
            "link invocation has no object inputs"));
    }
    for (const auto& object : invocation.objects) {
        if (object.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "link invocation contains an empty object path"));
        }
    }
    if (invocation.output.empty()) {
        return std::unexpected(failure(
            LinkerErrorCode::invalid_request,
            "link output path is empty"));
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        12
        + invocation.objects.size()
        + invocation.options.library_directories.size()
        + invocation.options.libraries.size()
        + invocation.options.additional_arguments.size());

    arguments.emplace_back("/NOLOGO");
    if (invocation.options.configuration == BuildConfiguration::debug) {
        arguments.emplace_back("/DEBUG");
        arguments.emplace_back("/INCREMENTAL");
    } else {
        arguments.emplace_back("/INCREMENTAL:NO");
        arguments.emplace_back("/OPT:REF");
        arguments.emplace_back("/OPT:ICF");
    }

    for (const auto& directory : invocation.options.library_directories) {
        if (directory.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "library directory must not be empty"));
        }
        arguments.push_back("/LIBPATH:" + path_to_utf8(directory));
    }
    for (const auto& library : invocation.options.libraries) {
        if (library.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "library name must not be empty"));
        }
        arguments.push_back(library);
    }
    for (const auto& argument : invocation.options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "additional linker argument must not be empty"));
        }
        arguments.push_back(argument);
    }

    // Structured routing is emitted after raw flags so the BuildPlan owns the
    // final target identity even if a user supplied a conflicting raw switch.
    arguments.push_back(architecture_argument(invocation.options.architecture));
    arguments.push_back(subsystem_argument(invocation.options.subsystem));
    arguments.push_back("/OUT:" + path_to_utf8(invocation.output));
    for (const auto& object : invocation.objects) {
        arguments.push_back(path_to_utf8(object));
    }
    return arguments;
}

std::expected<process::ProcessResult, LinkerError>
MsvcLinker::link(const LinkInvocation& invocation) const {
    auto arguments = build_arguments(invocation);
    if (!arguments) {
        return std::unexpected(arguments.error());
    }

    auto prepared = prepare_output_parent(invocation.output);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }

    process::ProcessSpec spec;
    spec.executable = toolchain_.linker;
    spec.arguments = std::move(*arguments);
    spec.working_directory = invocation.working_directory;
    spec.environment = toolchain_.environment;
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner_.run(spec);
    if (!result) {
        return std::unexpected(LinkerError{
            .code = LinkerErrorCode::process_failed,
            .message = "failed to launch MSVC linker",
            .path = toolchain_.linker,
            .process_error = result.error(),
        });
    }
    if (result->exit_code != 0) {
        return std::unexpected(LinkerError{
            .code = LinkerErrorCode::link_failed,
            .message = "MSVC linker returned a non-zero exit code",
            .path = toolchain_.linker,
            .process_result = std::move(*result),
        });
    }
    return std::move(*result);
}

} // namespace mqb::msvc
