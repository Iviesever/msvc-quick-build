#include "VisualStudioEnvironment.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "ToolchainDiscoveryPrimitives.hpp"

namespace mqb::msvc::detail {
namespace {

namespace fs = std::filesystem;
using process::EnvironmentVariable;
using process::ProcessSpec;

[[nodiscard]] std::expected<std::wstring, ToolchainError> decode_utf16le(const std::string& bytes) {
    if ((bytes.size() % 2) != 0) {
        return std::unexpected(toolchain_failure(
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

[[nodiscard]] std::expected<std::string, ToolchainError> wide_to_utf8(const std::wstring_view value) {
    if (value.empty()) return std::string{};

    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::invalid_environment_output,
            "failed to size UTF-8 environment conversion"));
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required, nullptr, nullptr) != required) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::invalid_environment_output,
            "failed to convert environment output to UTF-8"));
    }
    return result;
}

[[nodiscard]] bool wide_name_equal(
    const std::wstring_view left,
    const std::wstring_view right) noexcept {
    return ::CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()),
               right.data(), static_cast<int>(right.size()), TRUE)
        == CSTR_EQUAL;
}

[[nodiscard]] std::expected<CapturedVisualStudioEnvironment, ToolchainError>
parse_environment_dump(const std::string& raw_bytes) {
    auto decoded = decode_utf16le(raw_bytes);
    if (!decoded) return std::unexpected(decoded.error());

    CapturedVisualStudioEnvironment environment;
    std::wistringstream stream{*decoded};
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line.front() == L'=') continue;

        const auto delimiter = line.find(L'=');
        if (delimiter == std::wstring::npos || delimiter == 0) continue;

        const std::wstring_view name{line.data(), delimiter};
        const std::wstring_view value{line.data() + delimiter + 1, line.size() - delimiter - 1};
        if (wide_name_equal(name, L"MQB_VCVARS") || wide_name_equal(name, L"MQB_VC_TARGET")) {
            continue;
        }

        auto name_utf8 = wide_to_utf8(name);
        if (!name_utf8) return std::unexpected(name_utf8.error());
        auto value_utf8 = wide_to_utf8(value);
        if (!value_utf8) return std::unexpected(value_utf8.error());

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
    explicit TemporaryBatchFile(fs::path path) : path_(std::move(path)) {}
    ~TemporaryBatchFile() { remove(); }

    TemporaryBatchFile(const TemporaryBatchFile&) = delete;
    TemporaryBatchFile& operator=(const TemporaryBatchFile&) = delete;

    TemporaryBatchFile(TemporaryBatchFile&& other) noexcept
        : path_(std::exchange(other.path_, {})) {}

    TemporaryBatchFile& operator=(TemporaryBatchFile&& other) noexcept {
        if (this != &other) {
            remove();
            path_ = std::exchange(other.path_, {});
        }
        return *this;
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    void remove() noexcept {
        std::error_code ignored;
        if (!path_.empty()) fs::remove(path_, ignored);
    }

    fs::path path_;
};

[[nodiscard]] std::expected<TemporaryBatchFile, ToolchainError> create_environment_script() {
    std::error_code error_code;
    const fs::path temp_directory = fs::temp_directory_path(error_code);
    if (error_code) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to locate the temporary directory"));
    }

    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path script_path = temp_directory
        / ("mqb-vcvars-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(tick) + ".cmd");
    std::ofstream script{script_path, std::ios::binary | std::ios::trunc};
    if (!script) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to create vcvars environment script",
            script_path));
    }

    script << "@echo off\r\n"
           << "call \"%MQB_VCVARS%\" %MQB_VC_TARGET% >nul 2>&1\r\n"
           << "if errorlevel 1 exit /b %errorlevel%\r\n"
           << "set\r\n";
    script.close();
    if (!script) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "failed to write vcvars environment script",
            script_path));
    }
    return TemporaryBatchFile{script_path};
}

} // namespace

fs::path default_command_processor() {
    if (const auto comspec = environment_path("ComSpec")) return *comspec;
    if (const auto system_root = environment_path("SystemRoot")) {
        return *system_root / "System32" / "cmd.exe";
    }
    return fs::path{"cmd.exe"};
}

std::expected<CapturedVisualStudioEnvironment, ToolchainError>
capture_visual_studio_environment(
    process::ProcessRunner& runner,
    const fs::path& command_processor,
    const fs::path& vcvarsall,
    const Architecture target_architecture) {
    auto script = create_environment_script();
    if (!script) return std::unexpected(script.error());

    ProcessSpec spec;
    spec.executable = command_processor;
    spec.arguments = {"/d", "/u", "/c", path_to_utf8(script->path())};
    spec.environment = {
        EnvironmentVariable{"MQB_VCVARS", path_to_utf8(vcvarsall)},
        EnvironmentVariable{"MQB_VC_TARGET", architecture_name(target_architecture)},
    };

    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(ToolchainError{
            .code = ToolchainErrorCode::process_failed,
            .path = command_processor,
            .message = "failed to launch cmd.exe for vcvarsall",
            .process_error = result.error(),
        });
    }
    if (result->exit_code != 0) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::visual_studio_environment_failed,
            "vcvarsall.bat failed with exit code " + std::to_string(result->exit_code),
            vcvarsall));
    }
    return parse_environment_dump(result->stdout_text);
}

} // namespace mqb::msvc::detail
