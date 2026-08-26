#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

class VswhereOnlyRunner final : public mqb::process::ProcessRunner {
public:
    explicit VswhereOnlyRunner(fs::path installation)
        : installation_(std::move(installation)) {}

    [[nodiscard]] std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        if (calls != 1) {
            return std::unexpected(mqb::process::ProcessError{
                .code = mqb::process::ProcessErrorCode::launch_failed,
                .message = "ambient adoption attempted more than vswhere validation",
            });
        }
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = installation_.string() + "\n",
        };
    }

    std::size_t calls{};

private:
    fs::path installation_;
};

class ScopedEnvironment final {
public:
    ScopedEnvironment() = default;
    ~ScopedEnvironment() {
        for (auto iterator = originals_.rbegin(); iterator != originals_.rend(); ++iterator) {
            _putenv_s(iterator->first.c_str(), iterator->second ? iterator->second->c_str() : "");
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    void set(const std::string& name, const std::string& value) {
        remember(name);
        _putenv_s(name.c_str(), value.c_str());
    }

    void unset(const std::string& name) {
        remember(name);
        _putenv_s(name.c_str(), "");
    }

private:
    void remember(const std::string& name) {
        const auto existing = std::find_if(originals_.begin(), originals_.end(), [&](const auto& entry) {
            return equals_ignore_case(entry.first, name);
        });
        if (existing != originals_.end()) return;
        const char* value = std::getenv(name.c_str());
        originals_.push_back({
            name,
            value == nullptr ? std::nullopt : std::optional<std::string>{value},
        });
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> originals_;
};

[[nodiscard]] fs::path visual_studio_installation_from_tools_root(fs::path tools_root) {
    tools_root = tools_root.lexically_normal();
    while (tools_root.filename().empty() && tools_root.has_parent_path()) {
        tools_root = tools_root.parent_path();
    }
    for (int level = 0; level < 4 && tools_root.has_parent_path(); ++level) {
        tools_root = tools_root.parent_path();
    }
    return tools_root;
}

[[nodiscard]] fs::path unique_cache_file() {
    return fs::temp_directory_path()
        / ("mqb-vs-toolchain-cache-test-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
           + ".mqbcache");
}

[[nodiscard]] fs::path unique_cache_root() {
    return fs::temp_directory_path()
        / ("mqb-vs-toolchain-cache-root-test-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    // Even a standalone locator call that explicitly supplies no cache path must
    // never fall back to user-global MQB state. The cache authority itself owns
    // this invariant: nullopt resolves below the current working directory's
    // .mqb tree with an architecture-specific filename.
    const fs::path default_cache_root = unique_cache_root();
    const fs::path decoy_local_app_data = default_cache_root / "decoy-local-app-data";
    fs::create_directories(decoy_local_app_data, cleanup_error);
    const fs::path original_working_directory = fs::current_path();
    const char* inherited_local_app_data = std::getenv("LOCALAPPDATA");
    const bool had_local_app_data = inherited_local_app_data != nullptr;
    const std::string original_local_app_data = had_local_app_data
        ? std::string{inherited_local_app_data}
        : std::string{};
    _putenv_s("LOCALAPPDATA", decoy_local_app_data.string().c_str());
    fs::current_path(default_cache_root, cleanup_error);
    expect(!cleanup_error, "standalone cache test should enter its isolated working directory");
    if (!cleanup_error) {
        mqb::msvc::DiscoveryOptions default_cache_options;
        default_cache_options.preference = mqb::msvc::ToolchainPreference::visual_studio;
        default_cache_options.target_architecture = mqb::Architecture::x64;
        default_cache_options.host_architecture = mqb::Architecture::x64;
        default_cache_options.cache_file = std::nullopt;
        const auto default_cache_result = locator.discover(default_cache_options);
        expect(default_cache_result.has_value(),
               "standalone Visual Studio discovery should succeed with the implicit project-local cache");
        expect(
            fs::is_regular_file(
                default_cache_root / ".mqb/cache/toolchain/msvc-vs-x64-x64.mqbcache"),
            "nullopt Visual Studio cache path should resolve under the working directory .mqb tree");
        expect(
            !fs::exists(decoy_local_app_data / "MQB"),
            "nullopt Visual Studio cache path must not write legacy LOCALAPPDATA/MQB state");
    }
    cleanup_error.clear();
    fs::current_path(original_working_directory, cleanup_error);
    expect(!cleanup_error, "standalone cache test should restore its original working directory");
    if (had_local_app_data) {
        _putenv_s("LOCALAPPDATA", original_local_app_data.c_str());
    } else {
        _putenv_s("LOCALAPPDATA", "");
    }
    cleanup_error.clear();
    fs::remove_all(default_cache_root, cleanup_error);

    // Verify ambient Visual Studio toolchain adoption on cold builds:
    // when ambient environment contains valid, trusted MSVC environment,
    // it must be adopted directly without subprocess calls (RejectingRunner).
    if (result) {
        const auto* tools_env = find_environment_variable(*result, "VCToolsInstallDir");
        const auto* include_env = find_environment_variable(*result, "INCLUDE");
        const auto* lib_env = find_environment_variable(*result, "LIB");
        const auto* libpath_env = find_environment_variable(*result, "LIBPATH");
        const auto* path_env = find_environment_variable(*result, "PATH");
        if (tools_env && include_env && lib_env && libpath_env && path_env) {
            ScopedEnvironment ambient_environment;
            for (const auto name : {
                     "INCLUDE", "LIB", "LIBPATH", "PATH", "VCToolsInstallDir",
                     "WindowsSdkDir", "WindowsSDKVersion", "UniversalCRTSdkDir",
                     "UCRTVersion", "NETFXSDKDir",
                 }) {
                if (const auto* variable = find_environment_variable(*result, name)) {
                    ambient_environment.set(variable->name, variable->value);
                } else {
                    ambient_environment.unset(name);
                }
            }
            ambient_environment.set("VSCMD_ARG_TGT_ARCH", "x64");
            ambient_environment.set("VSCMD_ARG_HOST_ARCH", "x64");

            const fs::path ambient_cache_file = unique_cache_file();
            fs::remove(ambient_cache_file, cleanup_error);

            mqb::msvc::DiscoveryOptions ambient_options;
            ambient_options.preference = mqb::msvc::ToolchainPreference::visual_studio;
            ambient_options.target_architecture = mqb::Architecture::x64;
            ambient_options.host_architecture = mqb::Architecture::x64;
            ambient_options.cache_file = ambient_cache_file;

            VswhereOnlyRunner ambient_runner{
                visual_studio_installation_from_tools_root(tools_env->value)};
            mqb::msvc::MsvcToolchainLocator ambient_locator{ambient_runner};
            const auto ambient_result = ambient_locator.discover(ambient_options);
            expect(ambient_result.has_value(),
                   "ambient Visual Studio toolchain should be adopted when valid environment is present");
            expect(ambient_runner.calls == 1,
                   "ambient Visual Studio adoption should run only vswhere validation");
            if (ambient_result) {
                expect(ambient_result->source == mqb::msvc::ToolchainSource::visual_studio,
                       "ambient adoption should have visual_studio source");
                expect(fs::is_regular_file(ambient_cache_file),
                       "ambient adoption should persist validated project toolchain cache");

                // Verify the cache written by ambient adoption is reusable
                RejectingRunner cached_runner;
                mqb::msvc::MsvcToolchainLocator cached_ambient_locator{cached_runner};
                const auto cached_result = cached_ambient_locator.discover(ambient_options);
                expect(cached_result.has_value() && cached_result->reused,
                       "cache written by ambient adoption should be reusable");
                expect(cached_runner.calls == 0,
                       "cache hit after ambient adoption must not execute subprocesses");
            }
            fs::remove(ambient_cache_file, cleanup_error);

            // Mismatched target architecture in ambient environment must fall back to discovery
            ambient_environment.set("VSCMD_ARG_TGT_ARCH", "x86");
            RejectingRunner arch_mismatch_runner;
            mqb::msvc::MsvcToolchainLocator arch_mismatch_locator{arch_mismatch_runner};
            mqb::msvc::DiscoveryOptions no_cache_options = ambient_options;
            no_cache_options.cache_file = fs::path{};
            const auto arch_mismatch = arch_mismatch_locator.discover(no_cache_options);
            expect(!arch_mismatch.has_value(),
                   "mismatched ambient target architecture must reject ambient adoption");
            expect(arch_mismatch_runner.calls != 0,
                   "mismatched ambient target architecture must fall back to subprocess discovery");
            ambient_environment.set("VSCMD_ARG_TGT_ARCH", "x64");

            ambient_environment.unset("VSCMD_ARG_TGT_ARCH");
            RejectingRunner missing_arch_runner;
            mqb::msvc::MsvcToolchainLocator missing_arch_locator{missing_arch_runner};
            const auto missing_arch = missing_arch_locator.discover(no_cache_options);
            expect(!missing_arch.has_value(),
                   "ambient adoption must require an explicit target-architecture marker");
            expect(missing_arch_runner.calls != 0,
                   "missing target-architecture marker must fall back to discovery");
            ambient_environment.set("VSCMD_ARG_TGT_ARCH", "x64");

            ambient_environment.set("VSCMD_ARG_HOST_ARCH", "x86");
            RejectingRunner host_mismatch_runner;
            mqb::msvc::MsvcToolchainLocator host_mismatch_locator{host_mismatch_runner};
            const auto host_mismatch = host_mismatch_locator.discover(no_cache_options);
            expect(!host_mismatch.has_value(),
                   "mismatched ambient host architecture must reject ambient adoption");
            expect(host_mismatch_runner.calls != 0,
                   "mismatched ambient host architecture must fall back to discovery");
            ambient_environment.set("VSCMD_ARG_HOST_ARCH", "x64");

            ambient_environment.unset("VSCMD_ARG_HOST_ARCH");
            RejectingRunner missing_host_runner;
            mqb::msvc::MsvcToolchainLocator missing_host_locator{missing_host_runner};
            const auto missing_host = missing_host_locator.discover(no_cache_options);
            expect(!missing_host.has_value(),
                   "ambient adoption must require an explicit host-architecture marker");
            expect(missing_host_runner.calls != 0,
                   "missing host-architecture marker must fall back to discovery");
            ambient_environment.set("VSCMD_ARG_HOST_ARCH", "x64");

            mqb::msvc::DiscoveryOptions explicit_override_options = no_cache_options;
            explicit_override_options.vswhere_path = fs::path{"C:\\mqb-explicit-missing-vswhere.exe"};
            RejectingRunner explicit_override_runner;
            mqb::msvc::MsvcToolchainLocator explicit_override_locator{explicit_override_runner};
            const auto explicit_override = explicit_override_locator.discover(explicit_override_options);
            expect(!explicit_override.has_value(),
                   "explicit discovery override must remain authoritative over ambient adoption");
            expect(explicit_override_runner.calls == 0,
                   "missing explicit vswhere path should fail before a subprocess launch");

            mqb::msvc::DiscoveryOptions explicit_cmd_options = no_cache_options;
            explicit_cmd_options.cmd_path = fs::path{"C:\\mqb-explicit-missing-cmd.exe"};
            RejectingRunner explicit_cmd_runner;
            mqb::msvc::MsvcToolchainLocator explicit_cmd_locator{explicit_cmd_runner};
            const auto explicit_cmd = explicit_cmd_locator.discover(explicit_cmd_options);
            expect(!explicit_cmd.has_value(),
                   "explicit command-processor override must remain authoritative over ambient adoption");
            expect(explicit_cmd_runner.calls != 0,
                   "explicit command-processor override must force ordinary discovery");

            // Untrusted include directory in ambient environment must fall back to discovery
            const std::string original_include = include_env->value;
            ambient_environment.set("INCLUDE", "C:\\mqb-untrusted-arbitrary-include-root");
            RejectingRunner untrusted_runner;
            mqb::msvc::MsvcToolchainLocator untrusted_locator{untrusted_runner};
            const auto untrusted = untrusted_locator.discover(no_cache_options);
            expect(!untrusted.has_value(),
                   "untrusted ambient include root must reject ambient adoption");
            expect(untrusted_runner.calls != 0,
                   "untrusted ambient environment must fall back to subprocess discovery");
            ambient_environment.set("INCLUDE", original_include);

            const fs::path fake_sdk = unique_cache_root() / "Windows Kits/10";
            const fs::path fake_sdk_include = fake_sdk / "Include/99.0.0.0/ucrt";
            fs::create_directories(fake_sdk_include, cleanup_error);
            ambient_environment.set("WindowsSdkDir", fake_sdk.string());
            ambient_environment.set("WindowsSDKVersion", "99.0.0.0\\");
            ambient_environment.set("INCLUDE", fake_sdk_include.string());
            RejectingRunner fake_sdk_runner;
            mqb::msvc::MsvcToolchainLocator fake_sdk_locator{fake_sdk_runner};
            const auto fake_sdk_result = fake_sdk_locator.discover(no_cache_options);
            expect(!fake_sdk_result.has_value(),
                   "ambient environment must not establish its own Windows SDK trust root");
            expect(fake_sdk_runner.calls != 0,
                   "unregistered fake Windows SDK root must fall back to discovery");
            ambient_environment.set("INCLUDE", original_include);
            if (const auto* variable = find_environment_variable(*result, "WindowsSdkDir")) {
                ambient_environment.set(variable->name, variable->value);
            } else {
                ambient_environment.unset("WindowsSdkDir");
            }
            if (const auto* variable = find_environment_variable(*result, "WindowsSDKVersion")) {
                ambient_environment.set(variable->name, variable->value);
            } else {
                ambient_environment.unset("WindowsSDKVersion");
            }
            fs::remove_all(fake_sdk.parent_path().parent_path(), cleanup_error);

            const fs::path fake_installation = unique_cache_root() / "FakeVisualStudio";
            const fs::path fake_tools = fake_installation / "VC/Tools/MSVC/14.50.00000";
            const fs::path fake_bin = fake_tools / "bin/Hostx64/x64";
            for (const auto& directory : {
                     fake_bin,
                     fake_installation / "include",
                     fake_installation / "lib/x64",
                     fake_installation / "libpath",
                 }) {
                fs::create_directories(directory, cleanup_error);
            }
            for (const auto name : {"cl.exe", "link.exe", "lib.exe"}) {
                std::ofstream{fake_bin / name, std::ios::binary} << "not a Microsoft tool";
            }
            ambient_environment.set("VCToolsInstallDir", fake_tools.string());
            ambient_environment.set("INCLUDE", (fake_installation / "include").string());
            ambient_environment.set("LIB", (fake_installation / "lib/x64").string());
            ambient_environment.set("LIBPATH", (fake_installation / "libpath").string());
            ambient_environment.set("PATH", fake_bin.string());
            ambient_environment.unset("WindowsSdkDir");
            ambient_environment.unset("WindowsSDKVersion");
            ambient_environment.unset("UniversalCRTSdkDir");
            ambient_environment.unset("UCRTVersion");
            ambient_environment.unset("NETFXSDKDir");
            RejectingRunner fake_root_runner;
            mqb::msvc::MsvcToolchainLocator fake_root_locator{fake_root_runner};
            const auto fake_root = fake_root_locator.discover(no_cache_options);
            expect(!fake_root.has_value(),
                   "ambient environment must not establish its own Visual Studio trust root");
            expect(fake_root_runner.calls != 0,
                   "unregistered fake Visual Studio root must fall back to discovery");

            fs::create_directories(
                fake_installation / "VC/Tools/MSVC/14.51.00000",
                cleanup_error);
            VswhereOnlyRunner stale_tools_runner{fake_installation};
            mqb::msvc::MsvcToolchainLocator stale_tools_locator{stale_tools_runner};
            const auto stale_tools = stale_tools_locator.discover(no_cache_options);
            expect(!stale_tools.has_value(),
                   "ambient adoption must reject a non-latest registered MSVC toolset");
            expect(stale_tools_runner.calls > 1,
                   "stale ambient MSVC toolset must fall back to ordinary discovery");
            fs::remove_all(fake_installation.parent_path(), cleanup_error);
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_visual_studio_tests passed\n";
    return 0;
}
