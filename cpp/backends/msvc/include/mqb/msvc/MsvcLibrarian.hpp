#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/LibrarianIdentity.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct ArchiveInvocation {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::filesystem::path working_directory;
};

enum class LibrarianErrorCode {
    invalid_request,
    identity_failed,
    output_prepare_failed,
    process_failed,
    archive_failed,
};

struct LibrarianError {
    LibrarianErrorCode code{LibrarianErrorCode::invalid_request};
    std::string message;
    std::filesystem::path path;
    std::optional<process::ProcessError> process_error;
    std::optional<process::ProcessResult> process_result;
};

class MsvcLibrarian {
public:
    MsvcLibrarian(const MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] static std::expected<LibrarianIdentity, LibrarianError>
    identity(const MsvcToolchain& toolchain);

    [[nodiscard]] static std::expected<std::vector<std::string>, LibrarianError>
    build_arguments(const ArchiveInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, LibrarianError>
    archive(const ArchiveInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
