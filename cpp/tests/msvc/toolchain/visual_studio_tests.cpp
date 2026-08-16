#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainEnvironmentIdentity.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool equals_ignore_case(std::string left, std::string right) {
    const auto lower = [](std::string& value) {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    };
    lower(left);
    lower(right);
    return left == right;
}

[[nodiscard]] const mqb::process::EnvironmentVariable* find_environment_variable(
    const mqb::msvc::MsvcToolchain& toolchain,
    const std::string_view name) {
    const auto found = std::find_if(
        toolchain.environment.begin(),
        toolchain.environment.end(),
        [name](const mqb::process::EnvironmentVariable& variable) {
            return equals_ignore_case(variable.name, std::string{name});
        });
    return found == toolchain.environment.end() ? nullptr : &*found;
}

[[nodiscard]] mqb::process::EnvironmentVariable* find_environment_variable(
    mqb::msvc::MsvcToolchain& toolchain,
    const std::string_view name) {
    const auto found = std::find_if(
        toolchain.environment.begin(),
        toolchain.environment.end(),
        [name](const mqb::process::EnvironmentVariable& variable) {
            return equals_ignore_case(variable.name, std::string{name});
        });
    return found == toolchain.environment.end() ? nullptr : &*found;
}

[[nodiscard]] bool has_environment_variable(
    const mqb::msvc::MsvcToolchain& toolchain,
    const std::string_view name) {
    return find_environment_variable(toolchain, name) != nullptr;
}

class RejectingRunner final : public mqb::process::ProcessRunner {
public:
    [[nodiscard]] std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "toolchain cache miss attempted a subprocess",
        });
    }

    std::size_t calls{};
};

[[nodiscard]] fs::path unique_cache_file() {
    return fs::temp_directory_path()
        / ("mqb-vs-toolchain-cache-test-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
           + ".mqbcache");
}

[[nodiscard]] bool poison_cached_windows_sdk_version(
    const fs::path& cache_file,
    std::string cache_text) {
    constexpr std::string_view marker = "env_name \"WindowsSDKVersion\"";
    const auto name = cache_text.find(marker);
    if (name == std::string::npos) return false;
    const auto value = cache_text.find("env_value ", name + marker.size());
    if (value == std::string::npos) return false;
    const auto line_end = cache_text.find('\n', value);
    if (line_end == std::string::npos) return false;
    cache_text.replace(value, line_end - value, "env_value \"0.0.0.0\\\\\"");

    std::ofstream stream{cache_file, std::ios::binary | std::ios::trunc};
    stream << cache_text;
    stream.flush();
    return static_cast<bool>(stream);
}

void restore_cache_text(const fs::path& cache_file, const std::string& cache_text) {
    std::ofstream stream{cache_file, std::ios::binary | std::ios::trunc};
    stream << cache_text;
}

} // namespace

int main() {
    const fs::path cache_file = unique_cache_file();
    std::error_code cleanup_error;
    fs::remove(cache_file, cleanup_error);

    constexpr const char* secret_name = "MQB_TOOLCHAIN_CACHE_TEST_SECRET";
    constexpr const char* secret_value = "mqb-cache-must-not-persist-this-value";
    const char* inherited_secret = std::getenv(secret_name);
    const std::string original_secret = inherited_secret == nullptr ? std::string{} : std::string{inherited_secret};
    const bool had_original_secret = inherited_secret != nullptr;
    _putenv_s(secret_name, secret_value);

    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::visual_studio;
    options.target_architecture = mqb::Architecture::x64;
    options.host_architecture = mqb::Architecture::x64;
    options.cache_file = cache_file;

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    const auto result = locator.discover(options);
    expect(result.has_value(), "Visual Studio toolchain should be discoverable on the Windows CI image");
    if (result) {
        expect(result->source == mqb::msvc::ToolchainSource::visual_studio,
               "forced VS discovery should preserve toolchain provenance");
        expect(!result->reused,
               "cold Visual Studio discovery should not report persistent cache reuse");
        expect(fs::is_regular_file(result->identity.compiler),
               "discovered cl.exe should exist");
        expect(fs::is_regular_file(result->linker),
               "discovered link.exe should exist");
        expect(fs::is_regular_file(result->librarian),
               "discovered lib.exe should exist");
        expect(!result->identity.version.empty(),
               "discovered compiler should expose the VC tools version");
        expect(!result->identity.binary_stamp.empty(),
               "discovered compiler should expose a cache identity stamp");
        expect(has_environment_variable(*result, "PATH"),
               "vcvars environment should contain PATH");
        expect(has_environment_variable(*result, "INCLUDE"),
               "vcvars environment should contain INCLUDE");
        expect(has_environment_variable(*result, "LIB"),
               "vcvars environment should contain LIB");
        expect(has_environment_variable(*result, "LIBPATH"),
               "vcvars environment should contain LIBPATH");
        expect(has_environment_variable(*result, "VCToolsInstallDir"),
               "vcvars environment should expose VCToolsInstallDir");
        expect(has_environment_variable(*result, "WindowsSdkDir")
                   && has_environment_variable(*result, "WindowsSDKVersion"),
               "vcvars environment should expose the selected Windows SDK identity");
        expect(has_environment_variable(*result, "UniversalCRTSdkDir")
                   && has_environment_variable(*result, "UCRTVersion"),
               "vcvars environment should expose the selected UCRT identity");

        const std::string cold_compiler_stamp =
            mqb::msvc::compiler_environment_stamp(result->environment);
        const std::string cold_linker_stamp =
            mqb::msvc::linker_environment_stamp(result->environment);
        expect(result->identity.binary_stamp.ends_with(cold_compiler_stamp),
               "cold VS compiler identity should seal compiler search environment");
        const auto cold_linker_identity = mqb::msvc::MsvcLinker::identity(*result);
        expect(cold_linker_identity.has_value()
                   && cold_linker_identity->binary_stamp.ends_with(cold_linker_stamp),
               "cold VS linker identity should seal library/helper search environment");

        expect(fs::is_regular_file(cache_file),
               "cold Visual Studio discovery should persist validated cache evidence");
        std::ifstream cache_stream{cache_file, std::ios::binary};
        const std::string cache_text{
            std::istreambuf_iterator<char>{cache_stream},
            std::istreambuf_iterator<char>{}};
        cache_stream.close();
        expect(cache_text.find(secret_name) == std::string::npos,
               "toolchain cache must not persist unrelated inherited environment names");
        expect(cache_text.find(secret_value) == std::string::npos,
               "toolchain cache must not persist unrelated inherited environment values");

        RejectingRunner rejecting_runner;
        mqb::msvc::MsvcToolchainLocator cached_locator{rejecting_runner};
        const auto cached = cached_locator.discover(options);
        expect(cached.has_value(),
               "validated Visual Studio cache should be reusable without subprocesses");
        expect(rejecting_runner.calls == 0,
               "Visual Studio cache hit must skip vswhere/cmd/vcvarsall subprocesses");
        if (cached) {
            expect(cached->reused,
                   "validated Visual Studio cache hit should report reuse");
            expect(cached->identity.compiler == result->identity.compiler,
                   "cache hit should preserve compiler identity path");
            expect(cached->identity.version == result->identity.version,
                   "cache hit should preserve VC tools version");
            expect(cached->identity.binary_stamp == result->identity.binary_stamp,
                   "cache hit should preserve binary plus compiler-environment identity");
            expect(cached->linker == result->linker,
                   "cache hit should reconstruct the same linker path");
            expect(cached->librarian == result->librarian,
                   "cache hit should reconstruct the same librarian path");
            const auto cached_linker_identity = mqb::msvc::MsvcLinker::identity(*cached);
            expect(cold_linker_identity && cached_linker_identity
                       && cached_linker_identity->binary_stamp == cold_linker_identity->binary_stamp,
                   "cache hit should preserve linker binary plus effective-environment identity");

            const auto* cold_lib_path = find_environment_variable(*result, "LIBPATH");
            const auto* cached_lib_path = find_environment_variable(*cached, "LIBPATH");
            expect(cold_lib_path != nullptr && cached_lib_path != nullptr
                       && cached_lib_path->value == cold_lib_path->value,
                   "cache hit should preserve vcvars LIBPATH exactly");

            const auto* cold_path = find_environment_variable(*result, "PATH");
            const auto* cached_path = find_environment_variable(*cached, "PATH");
            expect(cold_path != nullptr && cached_path != nullptr
                       && cached_path->value == cold_path->value,
                   "cache hit should preserve the exact effective vcvars PATH");

            auto stale_sdk = *cached;
            if (auto* sdk_version = find_environment_variable(stale_sdk, "WindowsSDKVersion")) {
                sdk_version->value = "0.0.0.0\\";
                expect(!mqb::msvc::cached_visual_studio_environment_is_fresh(stale_sdk),
                       "stale cached Windows SDK selection must fail closed");
            } else {
                expect(false, "cached VS environment should retain WindowsSDKVersion");
            }

            auto lib_order_changed = *cached;
            if (auto* lib = find_environment_variable(lib_order_changed, "LIB")) {
                lib->value += ";C:\\mqb-synthetic-higher-priority-lib-root";
                expect(mqb::msvc::compiler_environment_stamp(lib_order_changed.environment)
                           == mqb::msvc::compiler_environment_stamp(cached->environment),
                       "effective VS LIB mutation must not invalidate compiler identity");
                expect(mqb::msvc::linker_environment_stamp(lib_order_changed.environment)
                           != mqb::msvc::linker_environment_stamp(cached->environment),
                       "effective VS LIB mutation/order must invalidate linker identity");
                const auto mutated_linker_identity = mqb::msvc::MsvcLinker::identity(lib_order_changed);
                expect(cached_linker_identity && mutated_linker_identity
                           && mutated_linker_identity->binary_stamp
                               != cached_linker_identity->binary_stamp,
                       "effective VS LIB mutation must directly invalidate MsvcLinker identity");
            }
        }

        const char* current_path_value = std::getenv("PATH");
        const std::string original_path = current_path_value == nullptr
            ? std::string{}
            : std::string{current_path_value};
        _putenv_s("PATH", (original_path + ";C:\\mqb-synthetic-ambient-path-change").c_str());
        RejectingRunner path_change_runner;
        mqb::msvc::MsvcToolchainLocator path_change_locator{path_change_runner};
        const auto path_changed = path_change_locator.discover(options);
        expect(!path_changed.has_value(),
               "cached VS environment must not be reused against a different ambient PATH");
        expect(path_change_runner.calls != 0,
               "ambient PATH mutation should fall back to ordinary vcvars discovery");
        _putenv_s("PATH", original_path.c_str());

        const bool poisoned = poison_cached_windows_sdk_version(cache_file, cache_text);
        expect(poisoned,
               "test should be able to poison only the cached WindowsSDKVersion field");
        if (poisoned) {
            RejectingRunner stale_runner;
            mqb::msvc::MsvcToolchainLocator stale_locator{stale_runner};
            const auto stale = stale_locator.discover(options);
            expect(!stale.has_value(),
                   "stale cached SDK selection should fall back to ordinary discovery");
            expect(stale_runner.calls != 0,
                   "stale cached SDK selection must not be reused without vcvars discovery");
        }
        restore_cache_text(cache_file, cache_text);

        {
            std::ofstream corrupt{cache_file, std::ios::binary | std::ios::trunc};
            corrupt << "not-an-mqb-toolchain-cache\n";
        }
        RejectingRunner fallback_runner;
        mqb::msvc::MsvcToolchainLocator fallback_locator{fallback_runner};
        const auto fallback = fallback_locator.discover(options);
        expect(!fallback.has_value(),
               "corrupt cache evidence should fall back to ordinary discovery");
        expect(fallback_runner.calls != 0,
               "corrupt cache evidence must not suppress ordinary discovery");
    } else {
        std::cerr << "toolchain discovery error: " << result.error().message << '\n';
        if (!result.error().path.empty()) {
            std::cerr << "path: " << result.error().path.string() << '\n';
        }
        if (result.error().process_error) {
            std::cerr << "process error: " << result.error().process_error->message
                      << " native=" << result.error().process_error->native_code << '\n';
        }
    }

    if (had_original_secret) {
        _putenv_s(secret_name, original_secret.c_str());
    } else {
        _putenv_s(secret_name, "");
    }
    fs::remove(cache_file, cleanup_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_visual_studio_tests passed\n";
    return 0;
}
