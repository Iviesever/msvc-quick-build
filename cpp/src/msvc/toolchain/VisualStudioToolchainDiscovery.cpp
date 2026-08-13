#include "VisualStudioToolchainDiscovery.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ToolchainDiscoveryPrimitives.hpp"
#include "VisualStudioEnvironment.hpp"

namespace mqb::msvc::detail {
namespace {

namespace fs = std::filesystem;
using process::ProcessSpec;

[[nodiscard]] std::optional<fs::path> default_vswhere_path() {
    if (const auto program_files_x86 = environment_path("ProgramFiles(x86)")) {
        return *program_files_x86 / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<fs::path> visual_studio_fallbacks() {
    std::vector<fs::path> candidates;
    const auto program_files = environment_path("ProgramFiles");
    if (!program_files) return candidates;

    for (const std::string_view version : {"18", "2022"}) {
        for (const std::string_view edition : {"Community", "Professional", "Enterprise", "BuildTools"}) {
            candidates.push_back(
                *program_files
                / "Microsoft Visual Studio"
                / std::string{version}
                / std::string{edition});
        }
    }
    return candidates;
}

[[nodiscard]] std::expected<fs::path, ToolchainError> locate_visual_studio(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options) {
    std::optional<fs::path> vswhere = options.vswhere_path;
    if (!vswhere) vswhere = default_vswhere_path();

    std::error_code error_code;
    if (vswhere && fs::is_regular_file(*vswhere, error_code)) {
        ProcessSpec spec;
        spec.executable = *vswhere;
        spec.arguments = {"-latest", "-utf8", "-property", "installationPath"};
        auto result = runner.run(spec);
        if (result && result->exit_code == 0) {
            const auto output = trim_ascii(result->stdout_text);
            if (!output.empty()) {
                const fs::path installation = path_from_utf8(output);
                if (fs::is_directory(installation, error_code)) {
                    return installation;
                }
            }
        }
    } else if (options.vswhere_path) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::toolchain_not_found,
            "explicit vswhere.exe path does not exist",
            *options.vswhere_path));
    }

    for (const auto& candidate : visual_studio_fallbacks()) {
        if (fs::is_directory(candidate, error_code)) {
            return candidate;
        }
    }

    return std::unexpected(toolchain_failure(
        ToolchainErrorCode::toolchain_not_found,
        "no Visual Studio installation was found"));
}

} // namespace

std::expected<MsvcToolchain, ToolchainError> discover_visual_studio_toolchain(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options) {
    auto installation = locate_visual_studio(runner, options);
    if (!installation) return std::unexpected(installation.error());

    const fs::path vcvarsall = *installation / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat";
    std::error_code error_code;
    if (!fs::is_regular_file(vcvarsall, error_code)) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "vcvarsall.bat was not found",
            vcvarsall));
    }

    const fs::path command_processor = options.cmd_path.value_or(default_command_processor());
    auto captured = capture_visual_studio_environment(
        runner,
        command_processor,
        vcvarsall,
        options.target_architecture);
    if (!captured) return std::unexpected(captured.error());

    fs::path vc_tools_root;
    if (captured->vc_tools_root) {
        vc_tools_root = captured->vc_tools_root->lexically_normal();
        if (vc_tools_root.filename().empty()) {
            vc_tools_root = vc_tools_root.parent_path();
        }
    } else {
        const auto fallback = latest_directory(*installation / "VC" / "Tools" / "MSVC");
        if (!fallback) {
            return std::unexpected(toolchain_failure(
                ToolchainErrorCode::visual_studio_environment_failed,
                "VCToolsInstallDir was missing and no MSVC tools directory was found",
                *installation));
        }
        vc_tools_root = *fallback;
    }

    const std::string vc_tools_version = path_to_utf8(vc_tools_root.filename());
    if (vc_tools_version.empty()) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "resolved VCToolsInstallDir has no version directory name",
            vc_tools_root));
    }

    const fs::path target = architecture_name(options.target_architecture);
    const fs::path tool_bin = vc_tools_root / "bin" / host_directory(options.host_architecture) / target;
    const fs::path compiler = tool_bin / "cl.exe";
    const fs::path linker = tool_bin / "link.exe";
    const fs::path librarian = tool_bin / "lib.exe";

    if (!fs::is_regular_file(compiler, error_code)) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::compiler_not_found,
            "Visual Studio MSVC compiler was not found for the requested host/target architecture",
            compiler));
    }
    if (!fs::is_regular_file(linker, error_code) || !fs::is_regular_file(librarian, error_code)) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "Visual Studio linker/librarian was not found",
            tool_bin));
    }

    auto stamp = binary_stamp(compiler);
    if (!stamp) return std::unexpected(stamp.error());

    return MsvcToolchain{
        .identity = ToolchainIdentity{
            .compiler = compiler,
            .version = vc_tools_version,
            .binary_stamp = std::move(*stamp),
        },
        .linker = linker,
        .librarian = librarian,
        .vc_tools_root = vc_tools_root,
        .standard_library_modules = discover_standard_library_modules(vc_tools_root),
        .source = ToolchainSource::visual_studio,
        .environment = std::move(captured->variables),
    };
}

} // namespace mqb::msvc::detail
