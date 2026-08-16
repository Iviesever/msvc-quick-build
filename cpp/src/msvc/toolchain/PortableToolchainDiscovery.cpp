#include "PortableToolchainDiscovery.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ToolchainDiscoveryPrimitives.hpp"

namespace mqb::msvc::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string joined_environment(const std::vector<fs::path>& paths) {
    std::string value;
    for (const auto& path : paths) {
        if (!value.empty()) value.push_back(';');
        value += path_to_utf8(path);
    }
    return value;
}

} // namespace

using process::EnvironmentVariable;

std::expected<MsvcToolchain, ToolchainError> discover_portable_toolchain(
    const fs::path& portable_root,
    const DiscoveryOptions& options) {
    const auto vc_tools = latest_directory(portable_root / "VC" / "Tools" / "MSVC");
    const auto sdk_include = latest_directory(portable_root / "Windows Kits" / "10" / "Include");
    const auto sdk_lib = latest_directory(portable_root / "Windows Kits" / "10" / "Lib");
    const auto sdk_bin = latest_directory(portable_root / "Windows Kits" / "10" / "bin");

    if (!vc_tools || !sdk_include || !sdk_lib || !sdk_bin) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::invalid_portable_layout,
            "portable_msvc is missing VC Tools or Windows Kit version directories",
            portable_root));
    }

    const fs::path target = architecture_name(options.target_architecture);
    const fs::path tool_bin = *vc_tools / "bin" / host_directory(options.host_architecture) / target;
    const fs::path compiler = tool_bin / "cl.exe";
    const fs::path linker = tool_bin / "link.exe";
    const fs::path librarian = tool_bin / "lib.exe";

    std::error_code error_code;
    if (!fs::is_regular_file(compiler, error_code)) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::compiler_not_found,
            "portable MSVC compiler was not found for the requested architecture",
            compiler));
    }
    if (!fs::is_regular_file(linker, error_code) || !fs::is_regular_file(librarian, error_code)) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::invalid_portable_layout,
            "portable MSVC linker/librarian is missing",
            tool_bin));
    }

    auto stamp = binary_stamp(compiler);
    if (!stamp) {
        return std::unexpected(stamp.error());
    }

    const std::vector<fs::path> include_prefixes{
        *vc_tools / "include",
        *sdk_include / "ucrt",
        *sdk_include / "shared",
        *sdk_include / "um",
        *sdk_include / "winrt",
        *sdk_include / "cppwinrt",
    };
    const std::vector<fs::path> lib_prefixes{
        *vc_tools / "lib" / target,
        *sdk_lib / "ucrt" / target,
        *sdk_lib / "um" / target,
    };
    const std::vector<fs::path> path_prefixes{
        tool_bin,
        *sdk_bin / target,
    };

    // Portable mode is an MQB-owned toolchain environment. Do not append the
    // launching shell's INCLUDE/LIB/LIBPATH/PATH: those values can silently
    // redirect headers, libraries, metadata, or helper tools away from the
    // portable bundle. Explicit empty LIBPATH also masks an inherited value
    // because process launch otherwise inherits unspecified environment names.
    return MsvcToolchain{
        .identity = ToolchainIdentity{
            .compiler = compiler,
            .version = path_to_utf8(vc_tools->filename()),
            .binary_stamp = std::move(*stamp),
        },
        .linker = linker,
        .librarian = librarian,
        .vc_tools_root = *vc_tools,
        .standard_library_modules = discover_standard_library_modules(*vc_tools),
        .source = ToolchainSource::portable,
        .environment = {
            EnvironmentVariable{"PATH", joined_environment(path_prefixes)},
            EnvironmentVariable{"INCLUDE", joined_environment(include_prefixes)},
            EnvironmentVariable{"LIB", joined_environment(lib_prefixes)},
            EnvironmentVariable{"LIBPATH", {}},
        },
    };
}

} // namespace mqb::msvc::detail
