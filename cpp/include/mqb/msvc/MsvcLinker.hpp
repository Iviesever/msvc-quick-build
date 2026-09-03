#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
    // Use a non-incremental linker pass when resolved library inputs changed.
    // This avoids stale archive-member reuse inside MSVC's .ilk state while
    // preserving ordinary Debug incremental linking for object-only changes.
    bool force_full_link{false};
    // Ask LINK itself to report the exact libraries it searches while resolving
    // object/archive directives. This is observation only: it does not alter the
    // user's linker policy or explicit library argv.
    bool observe_library_search{false};
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

// Pure representation of the exact link.exe process contract. Construction has
// no filesystem/process side effects; real execution and `mqb plan` consume the
// same recipe so argv, working directory and environment isolation cannot drift.
struct MsvcLinkRecipe {
    LinkInvocation invocation;
    process::ProcessSpec process;
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

    [[nodiscard]] static std::filesystem::path
    program_database_path(const std::filesystem::path& output);

    [[nodiscard]] static std::filesystem::path
    manifest_file_path(const std::filesystem::path& output);

    [[nodiscard]] static std::optional<std::filesystem::path>
    map_file_path(
        const std::filesystem::path& output,
        const LinkOptions& options,
        const std::filesystem::path& working_directory = {});

    // Evaluate final LINK output semantics after MQB's configuration defaults
    // and raw user linker options are applied in command-line order.
    [[nodiscard]] static bool
    program_database_enabled(const LinkOptions& options);

    [[nodiscard]] static bool
    external_manifest_enabled(const LinkOptions& options);

    // Deterministic LINK side outputs derivable from configuration/raw policy.
    // The coordinator distinguishes explicitly requested outputs (for example
    // /MAP) from conditionally emitted outputs such as PDB/manifest files.
    [[nodiscard]] static std::vector<std::filesystem::path>
    required_side_output_paths(
        const std::filesystem::path& output,
        const LinkOptions& options,
        const std::filesystem::path& working_directory = {});

    // Parse /VERBOSE:LIB output without depending on localized progress labels.
    // LINK documents that the library/object names are emitted as full paths;
    // MQB extracts only absolute .lib path tokens and treats them as non-owning
    // freshness evidence.
    [[nodiscard]] static std::vector<std::filesystem::path>
    observed_library_paths(std::string_view stdout_text);

    // Internal /VERBOSE:LIB is an implementation detail. Unless the user asked
    // for linker progress output themselves, retain only LNK diagnostics in the
    // visible stdout after observation has been parsed.
    static void sanitize_library_observation_output(
        process::ProcessResult& result,
        const LinkOptions& options);

    [[nodiscard]] static std::expected<std::vector<std::string>, LinkerError>
    build_arguments(const LinkInvocation& invocation);

    [[nodiscard]] static std::expected<MsvcLinkRecipe, LinkerError>
    build_recipe(
        const MsvcToolchain& toolchain,
        const LinkInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, LinkerError>
    execute_recipe(const MsvcLinkRecipe& recipe) const;

    [[nodiscard]] std::expected<process::ProcessResult, LinkerError>
    link(const LinkInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
