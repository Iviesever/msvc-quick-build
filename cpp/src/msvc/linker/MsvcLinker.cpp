#include "mqb/msvc/MsvcLinker.hpp"

#include <cctype>
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

void suppress_ambient_linker_options(
    std::vector<process::EnvironmentVariable>& environment) {
    // link.exe prepends LINK and appends _LINK_ to its explicit argv. Hidden
    // arguments could otherwise bypass MQB-owned /OUT, target policy, tracked
    // linker inputs, and link identity. Preserve LIB/PATH/vcvars state while
    // making the explicit MQB argv authoritative.
    environment.push_back(process::EnvironmentVariable{"LINK", {}});
    environment.push_back(process::EnvironmentVariable{"_LINK_", {}});
}

[[nodiscard]] std::string linker_option_body_upper(const std::string_view argument) {
    if (argument.size() < 2 || (argument.front() != '/' && argument.front() != '-')) {
        return {};
    }
    std::string body{argument.substr(1)};
    for (char& character : body) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return body;
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

fs::path MsvcLinker::import_library_path(const fs::path& output) {
    fs::path import_library = output;
    import_library.replace_extension(".lib");
    return import_library;
}

fs::path MsvcLinker::export_file_path(const fs::path& output) {
    fs::path export_file = output;
    export_file.replace_extension(".exp");
    return export_file;
}

fs::path MsvcLinker::program_database_path(const fs::path& output) {
    fs::path program_database = output;
    program_database.replace_extension(".pdb");
    return program_database;
}

fs::path MsvcLinker::manifest_file_path(const fs::path& output) {
    fs::path manifest = output;
    manifest += ".manifest";
    return manifest;
}

bool MsvcLinker::program_database_enabled(const LinkOptions& options) noexcept {
    bool enabled = options.configuration == BuildConfiguration::debug;
    for (const auto& argument : options.additional_arguments) {
        const std::string body = linker_option_body_upper(argument);
        if (body == "DEBUG" || body == "DEBUG:FULL" || body == "DEBUG:FASTLINK") {
            enabled = true;
        } else if (body == "DEBUG:NONE") {
            enabled = false;
        }
    }
    return enabled;
}

bool MsvcLinker::external_manifest_enabled(const LinkOptions& options) noexcept {
    // LINK's command-line default is an external manifest. Only the explicit
    // disabled or embedded forms remove that standalone output.
    bool enabled = true;
    for (const auto& argument : options.additional_arguments) {
        const std::string body = linker_option_body_upper(argument);
        if (body == "MANIFEST") {
            enabled = true;
        } else if (body == "MANIFEST:NO" || body.starts_with("MANIFEST:EMBED")) {
            enabled = false;
        }
    }
    return enabled;
}

std::vector<fs::path> MsvcLinker::required_side_output_paths(
    const fs::path& output,
    const LinkOptions& options) {
    std::vector<fs::path> paths;
    paths.reserve(2);
    if (program_database_enabled(options)) {
        paths.push_back(program_database_path(output));
    }
    if (external_manifest_enabled(options)) {
        paths.push_back(manifest_file_path(output));
    }
    return paths;
}

std::expected<std::vector<std::string>, LinkerError>
MsvcLinker::build_arguments(const LinkInvocation& invocation) {
    if (invocation.options.target_kind == TargetKind::static_library) {
        return std::unexpected(failure(
            LinkerErrorCode::invalid_request,
            "static-library targets require the MSVC librarian pipeline"));
    }
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
    if (invocation.libraries.size() != invocation.options.libraries.size()) {
        return std::unexpected(failure(
            LinkerErrorCode::invalid_request,
            "resolved library inputs must match requested library count"));
    }
    for (const auto& library : invocation.libraries) {
        if (library.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "resolved library path must not be empty"));
        }
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        17
        + invocation.objects.size()
        + invocation.options.library_directories.size()
        + invocation.libraries.size()
        + invocation.options.additional_arguments.size());

    arguments.emplace_back("/NOLOGO");
    if (invocation.options.configuration == BuildConfiguration::debug) {
        arguments.emplace_back("/DEBUG");
        if (!invocation.force_full_link) {
            arguments.emplace_back(
                invocation.options.link_time_code_generation
                    ? "/INCREMENTAL:NO"
                    : "/INCREMENTAL");
        }
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
    for (const auto& library : invocation.libraries) {
        arguments.push_back(path_to_utf8(library));
    }
    for (const auto& argument : invocation.options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(failure(
                LinkerErrorCode::invalid_request,
                "additional linker argument must not be empty"));
        }
        arguments.push_back(argument);
    }

    // A library-change full link is MQB execution policy rather than a user
    // linker preference. Emit it after raw flags so stale .ilk state cannot be
    // re-enabled by a passthrough /INCREMENTAL argument.
    if (invocation.force_full_link) {
        arguments.emplace_back("/INCREMENTAL:NO");
    }

    // Typed LTCG is downstream structured policy and therefore follows raw
    // linker arguments. This keeps the coupled /GL + /LTCG contract authoritative.
    if (invocation.options.link_time_code_generation) {
        arguments.emplace_back("/LTCG");
    }

    // When debug information is effective, make the linker PDB an MQB-owned
    // deterministic side artifact. The explicit path follows raw flags so a
    // user cannot redirect it outside the cache/artifact graph.
    if (program_database_enabled(invocation.options)) {
        arguments.push_back("/PDB:" + path_to_utf8(program_database_path(invocation.output)));
    }

    // Structured routing is emitted after raw flags so the BuildPlan owns the
    // final target identity even if a user supplied a conflicting raw switch.
    if (invocation.options.target_kind == TargetKind::dynamic_library) {
        arguments.emplace_back("/DLL");
        arguments.push_back("/IMPLIB:" + path_to_utf8(import_library_path(invocation.output)));
    }
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
    suppress_ambient_linker_options(spec.environment);
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
