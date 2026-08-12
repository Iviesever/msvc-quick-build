#include "mqb/msvc/MsvcToolchainLocator.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;
using process::EnvironmentVariable;
using process::ProcessSpec;

[[nodiscard]] ToolchainError failure(
    const ToolchainErrorCode code,
    std::string message,
    fs::path path = {}) {
    return ToolchainError{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] std::string trim_ascii(std::string value) {
    const auto is_space = [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), [&](const char ch) {
            return !is_space(static_cast<unsigned char>(ch));
        }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&](const char ch) {
            return !is_space(static_cast<unsigned char>(ch));
        }).base(),
        value.end());

    constexpr std::string_view utf8_bom{"\xef\xbb\xbf"};
    if (value.starts_with(utf8_bom)) {
        value.erase(0, utf8_bom.size());
    }
    return value;
}

[[nodiscard]] std::optional<std::string> environment_value(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string{value};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<fs::path> environment_path(const char* name) {
    const auto value = environment_value(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return path_from_utf8(*value);
}

[[nodiscard]] std::string architecture_name(const Architecture architecture) {
    return architecture == Architecture::x86 ? "x86" : "x64";
}

[[nodiscard]] fs::path host_directory(const Architecture architecture) {
    return architecture == Architecture::x86 ? fs::path{"Hostx86"} : fs::path{"Hostx64"};
}

[[nodiscard]] std::optional<fs::path> latest_directory(const fs::path& root) {
    std::error_code error_code;
    if (!fs::is_directory(root, error_code)) {
        return std::nullopt;
    }

    std::vector<fs::path> directories;
    for (fs::directory_iterator iterator{root, error_code}; !error_code && iterator != fs::directory_iterator{}; iterator.increment(error_code)) {
        if (iterator->is_directory(error_code) && !error_code) {
            directories.push_back(iterator->path());
        }
    }
    if (error_code || directories.empty()) {
        return std::nullopt;
    }

    std::sort(
        directories.begin(),
        directories.end(),
        [](const fs::path& left, const fs::path& right) {
            return left.filename().native() > right.filename().native();
        });
    return directories.front();
}

[[nodiscard]] std::expected<std::string, ToolchainError> binary_stamp(const fs::path& file) {
    std::error_code error_code;
    const auto size = fs::file_size(file, error_code);
    if (error_code) {
        return std::unexpected(failure(
            ToolchainErrorCode::compiler_not_found,
            "cannot read compiler size",
            file));
    }

    const auto modified = fs::last_write_time(file, error_code);
    if (error_code) {
        return std::unexpected(failure(
            ToolchainErrorCode::compiler_not_found,
            "cannot read compiler timestamp",
            file));
    }

    return std::to_string(size) + ":" + std::to_string(modified.time_since_epoch().count());
}

[[nodiscard]] std::string prepend_environment(
    const std::vector<fs::path>& prefixes,
    const char* inherited_name) {
    std::string value;
    for (const auto& prefix : prefixes) {
        if (!value.empty()) {
            value.push_back(';');
        }
        value += path_to_utf8(prefix);
    }

    if (const auto inherited = environment_value(inherited_name); inherited && !inherited->empty()) {
        if (!value.empty()) {
            value.push_back(';');
        }
        value += *inherited;
    }
    return value;
}

[[nodiscard]] std::expected<MsvcToolchain, ToolchainError> discover_portable(
    const fs::path& portable_root,
    const DiscoveryOptions& options) {
    const auto vc_tools = latest_directory(portable_root / "VC" / "Tools" / "MSVC");
    const auto sdk_include = latest_directory(portable_root / "Windows Kits" / "10" / "Include");
    const auto sdk_lib = latest_directory(portable_root / "Windows Kits" / "10" / "Lib");
    const auto sdk_bin = latest_directory(portable_root / "Windows Kits" / "10" / "bin");

    if (!vc_tools || !sdk_include || !sdk_lib || !sdk_bin) {
        return std::unexpected(failure(
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
        return std::unexpected(failure(
            ToolchainErrorCode::compiler_not_found,
            "portable MSVC compiler was not found for the requested architecture",
            compiler));
    }
    if (!fs::is_regular_file(linker, error_code) || !fs::is_regular_file(librarian, error_code)) {
        return std::unexpected(failure(
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

    return MsvcToolchain{
        .identity = ToolchainIdentity{
            .compiler = compiler,
            .version = path_to_utf8(vc_tools->filename()),
            .binary_stamp = std::move(*stamp),
        },
        .linker = linker,
        .librarian = librarian,
        .vc_tools_root = *vc_tools,
        .source = ToolchainSource::portable,
        .environment = {
            EnvironmentVariable{"PATH", prepend_environment(path_prefixes, "PATH")},
            EnvironmentVariable{"INCLUDE", prepend_environment(include_prefixes, "INCLUDE")},
            EnvironmentVariable{"LIB", prepend_environment(lib_prefixes, "LIB")},
        },
    };
}

[[nodiscard]] std::optional<fs::path> default_vswhere_path() {
    if (const auto program_files_x86 = environment_path("ProgramFiles(x86)")) {
        return *program_files_x86 / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<fs::path> visual_studio_fallbacks() {
    std::vector<fs::path> candidates;
    const auto program_files = environment_path("ProgramFiles");
    if (!program_files) {
        return candidates;
    }

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

[[nodiscard]] std::optional<fs::path> default_cmd_path() {
    if (const auto comspec = environment_path("ComSpec")) {
        return comspec;
    }
    if (const auto system_root = environment_path("SystemRoot")) {
        return *system_root / "System32" / "cmd.exe";
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<std::wstring, ToolchainError> decode_utf16le(
    const std::string& bytes) {
    if ((bytes.size() % 2) != 0) {
        return std::unexpected(failure(
            ToolchainErrorCode::invalid_environment_output,
            "cmd.exe /u returned an odd number of bytes"));
    }

    std::wstring text;
    text.reserve(bytes.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); index += 2) {
        const auto low = static_cast<unsigned char>(bytes[index]);
        const auto high = static_cast<unsigned char>(bytes[index + 1]);
        text.push_back(static_cast<wchar_t>(low | (static_cast<unsigned int>(high) << 8u)));
    }
    if (!text.empty() && text.front() == static_cast<wchar_t>(0xfeff)) {
        text.erase(text.begin());
    }
    return text;
}

[[nodiscard]] std::expected<std::string, ToolchainError> wide_to_utf8(
    const std::wstring_view value) {
    if (value.empty()) {
        return std::string{};
    }

    const int required = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required == 0) {
        return std::unexpected(failure(
            ToolchainErrorCode::invalid_environment_output,
            "failed to size UTF-8 environment conversion"));
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required,
            nullptr,
            nullptr)
        != required) {
        return std::unexpected(failure(
            ToolchainErrorCode::invalid_environment_output,
            "failed to convert environment output to UTF-8"));
    }
    return result;
}

struct CapturedEnvironment {
    std::vector<EnvironmentVariable> variables;
    std::optional<fs::path> vc_tools_root;
};

[[nodiscard]] bool wide_name_equal(
    const std::wstring_view left,
    const std::wstring_view right) noexcept {
    return ::CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()),
               right.data(), static_cast<int>(right.size()),
               TRUE)
        == CSTR_EQUAL;
}

[[nodiscard]] std::expected<CapturedEnvironment, ToolchainError> parse_environment_dump(
    const std::string& raw_bytes) {
    auto decoded = decode_utf16le(raw_bytes);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    CapturedEnvironment environment;
    std::wistringstream stream{*decoded};
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == L'=') {
            continue;
        }

        const auto delimiter = line.find(L'=');
        if (delimiter == std::wstring::npos || delimiter == 0) {
            continue;
        }

        const std::wstring_view name{line.data(), delimiter};
        const std::wstring_view value{line.data() + delimiter + 1, line.size() - delimiter - 1};
        if (wide_name_equal(name, L"MQB_VCVARS") || wide_name_equal(name, L"MQB_VC_TARGET")) {
            continue;
        }

        auto name_utf8 = wide_to_utf8(name);
        if (!name_utf8) {
            return std::unexpected(name_utf8.error());
        }
        auto value_utf8 = wide_to_utf8(value);
        if (!value_utf8) {
            return std::unexpected(value_utf8.error());
        }

        if (wide_name_equal(name, L"VCToolsInstallDir")) {
            environment.vc_tools_root = fs::path{std::wstring{value}};
        }
        environment.variables.push_back(EnvironmentVariable{
            .name = std::move(*name_utf8),
            .value = std::move(*value_utf8),
        });
    }

    return environment;
}

class TemporaryBatchFile {
public:
    TemporaryBatchFile() = default;
    explicit TemporaryBatchFile(fs::path path) : path_(std::move(path)) {}
    ~TemporaryBatchFile() {
        std::error_code ignored;
        if (!path_.empty()) {
            fs::remove(path_, ignored);
        }
    }

    TemporaryBatchFile(const TemporaryBatchFile&) = delete;
    TemporaryBatchFile& operator=(const TemporaryBatchFile&) = delete;

    TemporaryBatchFile(TemporaryBatchFile&& other) noexcept
        : path_(std::exchange(other.path_, {})) {}

    TemporaryBatchFile& operator=(TemporaryBatchFile&& other) noexcept {
        if (this != &other) {
            std::error_code ignored;
            if (!path_.empty()) {
                fs::remove(path_, ignored);
            }
            path_ = std::exchange(other.path_, {});
        }
        return *this;
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

[[nodiscard]] std::expected<TemporaryBatchFile, ToolchainError> create_environment_script() {
    std::error_code error_code;
    const fs::path temp_directory = fs::temp_directory_path(error_code);
    if (error_code) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to locate the temporary directory"));
    }

    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path script_path = temp_directory
        / ("mqb-vcvars-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(tick) + ".cmd");
    std::ofstream script{script_path, std::ios::binary | std::ios::trunc};
    if (!script) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to create vcvars environment script",
            script_path));
    }

    script
        << "@echo off\r\n"
        << "call \"%MQB_VCVARS%\" %MQB_VC_TARGET% >nul 2>&1\r\n"
        << "if errorlevel 1 exit /b %errorlevel%\r\n"
        << "set\r\n";
    script.close();
    if (!script) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to write vcvars environment script",
            script_path));
    }
    return TemporaryBatchFile{script_path};
}

[[nodiscard]] std::expected<CapturedEnvironment, ToolchainError> capture_visual_studio_environment(
    process::ProcessRunner& runner,
    const fs::path& cmd,
    const fs::path& vcvarsall,
    const Architecture target_architecture) {
    auto script = create_environment_script();
    if (!script) {
        return std::unexpected(script.error());
    }

    ProcessSpec spec;
    spec.executable = cmd;
    spec.arguments = {
        "/d",
        "/u",
        "/c",
        path_to_utf8(script->path()),
    };
    spec.environment = {
        EnvironmentVariable{"MQB_VCVARS", path_to_utf8(vcvarsall)},
        EnvironmentVariable{"MQB_VC_TARGET", architecture_name(target_architecture)},
    };

    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(ToolchainError{
            .code = ToolchainErrorCode::process_failed,
            .path = cmd,
            .message = "failed to launch cmd.exe for vcvarsall",
            .process_error = result.error(),
        });
    }
    if (result->exit_code != 0) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "vcvarsall.bat failed with exit code " + std::to_string(result->exit_code),
            vcvarsall));
    }
    return parse_environment_dump(result->stdout_text);
}

[[nodiscard]] std::expected<fs::path, ToolchainError> locate_visual_studio(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options) {
    std::optional<fs::path> vswhere = options.vswhere_path;
    if (!vswhere) {
        vswhere = default_vswhere_path();
    }

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
        return std::unexpected(failure(
            ToolchainErrorCode::toolchain_not_found,
            "explicit vswhere.exe path does not exist",
            *options.vswhere_path));
    }

    for (const auto& candidate : visual_studio_fallbacks()) {
        if (fs::is_directory(candidate, error_code)) {
            return candidate;
        }
    }

    return std::unexpected(failure(
        ToolchainErrorCode::toolchain_not_found,
        "no Visual Studio installation was found"));
}

[[nodiscard]] std::expected<MsvcToolchain, ToolchainError> discover_visual_studio(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options) {
    auto installation = locate_visual_studio(runner, options);
    if (!installation) {
        return std::unexpected(installation.error());
    }

    const fs::path vcvarsall = *installation / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat";
    std::error_code error_code;
    if (!fs::is_regular_file(vcvarsall, error_code)) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "vcvarsall.bat was not found",
            vcvarsall));
    }

    const fs::path cmd = options.cmd_path.value_or(default_cmd_path().value_or(fs::path{"cmd.exe"}));
    auto captured = capture_visual_studio_environment(
        runner,
        cmd,
        vcvarsall,
        options.target_architecture);
    if (!captured) {
        return std::unexpected(captured.error());
    }

    fs::path vc_tools_root;
    if (captured->vc_tools_root) {
        vc_tools_root = captured->vc_tools_root->lexically_normal();
        if (vc_tools_root.filename().empty()) {
            vc_tools_root = vc_tools_root.parent_path();
        }
    } else {
        const auto fallback = latest_directory(*installation / "VC" / "Tools" / "MSVC");
        if (!fallback) {
            return std::unexpected(failure(
                ToolchainErrorCode::visual_studio_environment_failed,
                "VCToolsInstallDir was missing and no MSVC tools directory was found",
                *installation));
        }
        vc_tools_root = *fallback;
    }

    const std::string vc_tools_version = path_to_utf8(vc_tools_root.filename());
    if (vc_tools_version.empty()) {
        return std::unexpected(failure(
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
        return std::unexpected(failure(
            ToolchainErrorCode::compiler_not_found,
            "Visual Studio MSVC compiler was not found for the requested host/target architecture",
            compiler));
    }
    if (!fs::is_regular_file(linker, error_code) || !fs::is_regular_file(librarian, error_code)) {
        return std::unexpected(failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "Visual Studio linker/librarian was not found",
            tool_bin));
    }

    auto stamp = binary_stamp(compiler);
    if (!stamp) {
        return std::unexpected(stamp.error());
    }

    return MsvcToolchain{
        .identity = ToolchainIdentity{
            .compiler = compiler,
            .version = vc_tools_version,
            .binary_stamp = std::move(*stamp),
        },
        .linker = linker,
        .librarian = librarian,
        .vc_tools_root = vc_tools_root,
        .source = ToolchainSource::visual_studio,
        .environment = std::move(captured->variables),
    };
}

} // namespace

std::expected<MsvcToolchain, ToolchainError>
MsvcToolchainLocator::discover(const DiscoveryOptions& options) const {
    if (options.preference != ToolchainPreference::visual_studio) {
        for (const auto& portable_root : options.portable_roots) {
            std::error_code error_code;
            if (fs::is_directory(portable_root, error_code)) {
                return discover_portable(portable_root, options);
            }
        }

        if (options.preference == ToolchainPreference::portable) {
            return std::unexpected(failure(
                ToolchainErrorCode::toolchain_not_found,
                "portable toolchain was requested but no portable_msvc root exists"));
        }
    }

    return discover_visual_studio(runner_, options);
}

} // namespace mqb::msvc
