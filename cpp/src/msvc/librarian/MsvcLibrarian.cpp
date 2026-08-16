#include "mqb/msvc/MsvcLibrarian.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcToolchainEnvironmentIdentity.hpp"

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] LibrarianError failure(
    const LibrarianErrorCode code,
    std::string message,
    fs::path path = {}) {
    return LibrarianError{.code = code, .message = std::move(message), .path = std::move(path)};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::string machine_argument(const Architecture architecture) {
    return architecture == Architecture::x86 ? "/MACHINE:X86" : "/MACHINE:X64";
}

[[nodiscard]] fs::path temporary_archive_path(const fs::path& output) {
    fs::path temporary = output;
    temporary += ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return temporary;
}

void remove_if_present(const fs::path& path) noexcept {
    std::error_code ignored;
    fs::remove(path, ignored);
}

void suppress_ambient_librarian_repro(
    std::vector<process::EnvironmentVariable>& environment) {
    // LINK/LIB honor the link_repro environment variable independently of the
    // explicit argv. Repro packages are diagnostic artifact trees outside the
    // archive cache identity, so ambient state must not enable them implicitly.
    environment.push_back(process::EnvironmentVariable{"link_repro", {}});
}

} // namespace

std::expected<LibrarianIdentity, LibrarianError>
MsvcLibrarian::identity(const MsvcToolchain& toolchain) {
    if (toolchain.librarian.empty()) {
        return std::unexpected(failure(
            LibrarianErrorCode::identity_failed, "MSVC librarian path is empty"));
    }
    std::error_code ec;
    if (!fs::is_regular_file(toolchain.librarian, ec) || ec) {
        return std::unexpected(failure(
            LibrarianErrorCode::identity_failed,
            "MSVC librarian executable does not exist",
            toolchain.librarian));
    }
    const auto size = fs::file_size(toolchain.librarian, ec);
    if (ec) return std::unexpected(failure(
        LibrarianErrorCode::identity_failed, "cannot read MSVC librarian size", toolchain.librarian));
    const auto modified = fs::last_write_time(toolchain.librarian, ec);
    if (ec) return std::unexpected(failure(
        LibrarianErrorCode::identity_failed, "cannot read MSVC librarian timestamp", toolchain.librarian));
    return LibrarianIdentity{
        .librarian = toolchain.librarian,
        .version = toolchain.identity.version,
        .binary_stamp = std::to_string(size) + ":"
            + std::to_string(modified.time_since_epoch().count()) + "|"
            + librarian_environment_stamp(toolchain.environment),
    };
}

std::expected<std::vector<std::string>, LibrarianError>
MsvcLibrarian::build_arguments(const ArchiveInvocation& invocation) {
    if (invocation.objects.empty()) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request, "archive invocation has no object inputs"));
    }
    if (invocation.output.empty()) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request, "link output path is empty"));
    }

    auto routed = MsvcParameterEngine::route_librarian(invocation.additional_arguments);
    if (!routed) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request,
            "invalid native MSVC librarian argument '" + routed.error().argument
                + "': " + routed.error().message));
    }
    if (routed->architecture && *routed->architecture != invocation.architecture) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request,
            "native MSVC librarian /MACHINE conflicts with the typed target architecture"));
    }

    const bool effective_ltcg = invocation.link_time_code_generation
        || routed->link_time_code_generation.value_or(false);

    std::vector<std::string> arguments;
    arguments.reserve(invocation.objects.size() + routed->passthrough.size() + 4);
    arguments.emplace_back("/NOLOGO");
    arguments.push_back(machine_argument(invocation.architecture));
    if (effective_ltcg) {
        arguments.emplace_back("/LTCG");
    }
    arguments.insert(
        arguments.end(),
        routed->passthrough.begin(),
        routed->passthrough.end());
    arguments.push_back("/OUT:" + path_to_utf8(invocation.output));
    for (const auto& object : invocation.objects) {
        if (object.empty()) {
            return std::unexpected(failure(
                LibrarianErrorCode::invalid_request, "archive invocation contains an empty object path"));
        }
        arguments.push_back(path_to_utf8(object));
    }
    return arguments;
}

std::expected<process::ProcessResult, LibrarianError>
MsvcLibrarian::archive(const ArchiveInvocation& invocation) const {
    if (invocation.objects.empty()) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request, "archive invocation has no object inputs"));
    }
    if (invocation.output.empty()) {
        return std::unexpected(failure(
            LibrarianErrorCode::invalid_request, "link output path is empty"));
    }

    std::error_code ec;
    if (!invocation.output.parent_path().empty()) {
        fs::create_directories(invocation.output.parent_path(), ec);
        if (ec) return std::unexpected(failure(
            LibrarianErrorCode::output_prepare_failed,
            "failed to prepare static-library output directory",
            invocation.output.parent_path()));
    }

    const fs::path temporary_output = temporary_archive_path(invocation.output);
    remove_if_present(temporary_output);
    ArchiveInvocation temporary_invocation = invocation;
    temporary_invocation.output = temporary_output;
    auto arguments = build_arguments(temporary_invocation);
    if (!arguments) return std::unexpected(arguments.error());

    process::ProcessSpec spec;
    spec.executable = toolchain_.librarian;
    spec.arguments = std::move(*arguments);
    spec.working_directory = invocation.working_directory;
    spec.environment = toolchain_.environment;
    suppress_ambient_librarian_repro(spec.environment);
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner_.run(spec);
    if (!result) {
        remove_if_present(temporary_output);
        return std::unexpected(LibrarianError{
            .code = LibrarianErrorCode::process_failed,
            .message = "failed to launch MSVC librarian",
            .path = toolchain_.librarian,
            .process_error = result.error(),
        });
    }
    if (result->exit_code != 0) {
        remove_if_present(temporary_output);
        return std::unexpected(LibrarianError{
            .code = LibrarianErrorCode::archive_failed,
            .message = "MSVC librarian returned a non-zero exit code",
            .path = toolchain_.librarian,
            .process_result = std::move(*result),
        });
    }

    ec.clear();
    if (!fs::is_regular_file(temporary_output, ec) || ec) {
        remove_if_present(temporary_output);
        return std::unexpected(failure(
            LibrarianErrorCode::output_install_failed,
            "MSVC librarian succeeded without producing the temporary archive",
            temporary_output));
    }

    ec.clear();
    if (fs::exists(invocation.output, ec) && !ec) {
        fs::remove(invocation.output, ec);
        if (ec) {
            remove_if_present(temporary_output);
            return std::unexpected(failure(
                LibrarianErrorCode::output_install_failed,
                "failed to remove previous static archive before replacement",
                invocation.output));
        }
    } else if (ec) {
        remove_if_present(temporary_output);
        return std::unexpected(failure(
            LibrarianErrorCode::output_install_failed,
            "failed to query previous static archive before replacement",
            invocation.output));
    }

    fs::rename(temporary_output, invocation.output, ec);
    if (ec) {
        remove_if_present(temporary_output);
        return std::unexpected(failure(
            LibrarianErrorCode::output_install_failed,
            "failed to install completed static archive",
            invocation.output));
    }
    return std::move(*result);
}

} // namespace mqb::msvc
