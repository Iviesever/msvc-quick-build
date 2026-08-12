#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct LinkInvocation {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::vector<std::filesystem::path> libraries;
    LinkOptions options;
    std::filesystem::path working_directory;
};

enum class LinkerErrorCode {
    invalid_request,
    identity_failed,
    output_prepare_failed,
    process_failed,
    link_failed,
};

struct LinkerError {
    LinkerErrorCode code{LinkerErrorCode::invalid_request};
    std::string message;
    std::filesystem::path path;
    std::optional<process::ProcessError> process_error;
    std::optional<process::ProcessResult> process_result;
};

class MsvcLinker {
public:
    MsvcLinker(const MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] static std::expected<LinkerIdentity, LinkerError>
    identity(const MsvcToolchain& toolchain);

    [[nodiscard]] static std::filesystem::path
    import_library_path(const std::filesystem::path& output);

    [[nodiscard]] static std::filesystem::path
    export_file_path(const std::filesystem::path& output);

    [[nodiscard]] static std::expected<std::vector<std::string>, LinkerError>
    build_arguments(const LinkInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, LinkerError>
    link(const LinkInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
