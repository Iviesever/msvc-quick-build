#include "mqb/msvc/MsvcLibrarian.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

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
        .binary_stamp = std::to_string(size) + ":" + std::to_string(modified.time_since_epoch().count()),
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
            LibrarianErrorCode::invalid_request, "archive output path is empty"));
    }
    std::vector<std::string> arguments;
    arguments.reserve(invocation.objects.size() + 3);
    arguments.emplace_back("/NOLOGO");
    if (invocation.link_time_code_generation) {
        arguments.emplace_back("/LTCG");
    }
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
            LibrarianErrorCode::invalid_request, "archive output path is empty"));
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
