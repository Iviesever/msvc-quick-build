#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/LibrarianIdentity.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct ArchiveInvocation {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::filesystem::path working_directory;
    Architecture architecture{Architecture::x64};
    bool link_time_code_generation{false};
    std::vector<std::string> additional_arguments;

    [[nodiscard]] bool operator==(const ArchiveInvocation&) const = default;
};

enum class LibrarianErrorCode {
    invalid_request,
    identity_failed,
    output_prepare_failed,
    process_failed,
    archive_failed,
    output_install_failed,
};

struct LibrarianError {
    LibrarianErrorCode code{LibrarianErrorCode::invalid_request};
    std::string message;
    std::filesystem::path path;
    std::optional<process::ProcessError> process_error;
    std::optional<process::ProcessResult> process_result;
};

// Pure representation of MQB's transactional lib.exe invocation. The public
// invocation retains the final owned .lib path while process argv targets the
// deterministic sibling transaction path that archive() installs atomically on
// success. Construction does not touch the filesystem or launch lib.exe.
struct MsvcArchiveRecipe {
    ArchiveInvocation invocation;
    std::filesystem::path transaction_output;
    process::ProcessSpec process;
};

class MsvcLibrarian {
public:
    MsvcLibrarian(const MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] static std::expected<LibrarianIdentity, LibrarianError>
    identity(const MsvcToolchain& toolchain);

    [[nodiscard]] static std::expected<std::vector<std::string>, LibrarianError>
    build_arguments(const ArchiveInvocation& invocation);

    [[nodiscard]] static std::expected<MsvcArchiveRecipe, LibrarianError>
    build_recipe(
        const MsvcToolchain& toolchain,
        const ArchiveInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, LibrarianError>
    archive(const ArchiveInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
