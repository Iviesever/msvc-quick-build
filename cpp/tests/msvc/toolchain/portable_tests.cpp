#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcToolchainEnvironmentIdentity.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class UnexpectedRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "portable discovery must not launch a process",
        });
    }

    int calls{};
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-portable-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(std::string name) : name_(std::move(name)) {
        if (const char* current = std::getenv(name_.c_str()); current != nullptr) {
            original_ = std::string{current};
        }
    }

    ~EnvironmentGuard() {
        _putenv_s(name_.c_str(), original_ ? original_->c_str() : "");
    }

    void set(const std::string_view value) const {
        _putenv_s(name_.c_str(), std::string{value}.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> original_;
};

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    file << "fixture";
}

void make_sdk_version(const fs::path& root, const std::string_view version) {
    const std::string version_text{version};
    fs::create_directories(root / "Windows Kits" / "10" / "Include" / version_text);
    fs::create_directories(root / "Windows Kits" / "10" / "Lib" / version_text);
    fs::create_directories(root / "Windows Kits" / "10" / "bin" / version_text);
}

[[nodiscard]] const mqb::process::EnvironmentVariable* find_environment(
    const mqb::msvc::MsvcToolchain& toolchain,
    const std::string_view name) {
    for (const auto& variable : toolchain.environment) {
        if (variable.name == name) return &variable;
    }
    return nullptr;
}

[[nodiscard]] mqb::process::EnvironmentVariable* find_environment(
    std::vector<mqb::process::EnvironmentVariable>& environment,
    const std::string_view name) {
    for (auto& variable : environment) {
        if (variable.name == name) return &variable;
    }
    return nullptr;
}

} // namespace

int main() {
    EnvironmentGuard ambient_path{"PATH"};
    EnvironmentGuard ambient_include{"INCLUDE"};
    EnvironmentGuard ambient_lib{"LIB"};
    EnvironmentGuard ambient_libpath{"LIBPATH"};
    ambient_path.set("ambient-path-a;ambient-path-b");
    ambient_include.set("ambient-include-a;ambient-include-b");
    ambient_lib.set("ambient-lib-a;ambient-lib-b");
    ambient_libpath.set("ambient-libpath-a;ambient-libpath-b");

    TemporaryDirectory fixture;
    const fs::path portable = fixture.path() / "portable_msvc";

    fs::create_directories(portable / "VC" / "Tools" / "MSVC" / "14.40.10000");
    const fs::path latest_vc = portable / "VC" / "Tools" / "MSVC" / "14.50.20000";
    const fs::path tool_bin = latest_vc / "bin" / "Hostx64" / "x64";
    touch(tool_bin / "cl.exe");
    touch(tool_bin / "link.exe");
    touch(tool_bin / "lib.exe");
    const fs::path std_source = latest_vc / "modules" / "std.ixx";
    const fs::path std_compat_source = latest_vc / "modules" / "std.compat.ixx";
    touch(std_source);
    touch(std_compat_source);

    make_sdk_version(portable, "10.0.20000.0");
    make_sdk_version(portable, "10.0.30000.0");

    UnexpectedRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};

    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::automatic;
    options.target_architecture = mqb::Architecture::x64;
    options.host_architecture = mqb::Architecture::x64;
    options.portable_roots = {portable};

    const auto result = locator.discover(options);
    expect(result.has_value(), "automatic discovery should select an existing portable toolchain first");
    expect(runner.calls == 0, "portable discovery should not invoke vswhere or cmd.exe");
    if (result) {
        expect(result->source == mqb::msvc::ToolchainSource::portable,
               "portable discovery should retain toolchain provenance");
        expect(result->identity.compiler == tool_bin / "cl.exe",
               "portable discovery should resolve the requested host/target compiler");
        expect(result->linker == tool_bin / "link.exe",
               "portable discovery should resolve link.exe beside cl.exe");
        expect(result->librarian == tool_bin / "lib.exe",
               "portable discovery should resolve lib.exe beside cl.exe");
        expect(result->identity.version == "14.50.20000",
               "portable discovery should select the lexicographically latest VC tools version");
        expect(!result->identity.binary_stamp.empty(),
               "compiler identity should include a binary and effective-environment stamp");
        expect(result->environment.size() == 4,
               "portable discovery should provide deterministic PATH/INCLUDE/LIB/LIBPATH overrides");

        const auto* path = find_environment(*result, "PATH");
        const auto* include = find_environment(*result, "INCLUDE");
        const auto* lib = find_environment(*result, "LIB");
        const auto* libpath = find_environment(*result, "LIBPATH");
        expect(path != nullptr && path->value.find("10.0.30000.0") != std::string::npos,
               "portable PATH should use the latest Windows Kit version");
        expect(path != nullptr && path->value.find("ambient-path") == std::string::npos,
               "portable PATH must not inherit ambient tool lookup roots");
        expect(include != nullptr && include->value.find("ambient-include") == std::string::npos,
               "portable INCLUDE must not inherit ambient header roots");
        expect(lib != nullptr && lib->value.find("ambient-lib") == std::string::npos,
               "portable LIB must not inherit ambient library roots");
        expect(libpath != nullptr && libpath->value.empty(),
               "portable LIBPATH must explicitly mask ambient metadata roots");

        const std::string environment_stamp =
            mqb::msvc::effective_toolchain_environment_stamp(result->environment);
        expect(result->identity.binary_stamp.ends_with(environment_stamp),
               "portable compiler cache identity should seal the effective search environment");

        auto include_order_changed = result->environment;
        if (auto* effective_include = find_environment(include_order_changed, "INCLUDE")) {
            effective_include->value = "B;A";
        }
        expect(mqb::msvc::effective_toolchain_environment_stamp(include_order_changed)
                   != environment_stamp,
               "effective INCLUDE replacement/order changes must change toolchain identity");

        auto lib_order_changed = result->environment;
        if (auto* effective_lib = find_environment(lib_order_changed, "LIB")) {
            effective_lib->value = "B;A";
        }
        expect(mqb::msvc::effective_toolchain_environment_stamp(lib_order_changed)
                   != environment_stamp,
               "effective LIB replacement/order changes must change toolchain identity");

        auto libpath_changed = result->environment;
        if (auto* effective_libpath = find_environment(libpath_changed, "LIBPATH")) {
            effective_libpath->value = "metadata-b;metadata-a";
        }
        expect(mqb::msvc::effective_toolchain_environment_stamp(libpath_changed)
                   != environment_stamp,
               "effective LIBPATH changes must change toolchain identity");

        ambient_path.set("ambient-path-b;ambient-path-a");
        ambient_include.set("ambient-include-b;ambient-include-a");
        ambient_lib.set("ambient-lib-b;ambient-lib-a");
        ambient_libpath.set("ambient-libpath-b;ambient-libpath-a");
        const auto after_ambient_mutation = locator.discover(options);
        expect(after_ambient_mutation.has_value()
                   && after_ambient_mutation->identity.binary_stamp == result->identity.binary_stamp,
               "portable ambient search-environment mutation must not perturb effective identity");

        expect(result->standard_library_modules.std
                   && *result->standard_library_modules.std == std_source.lexically_normal(),
               "portable discovery should expose std.ixx from the selected VC Tools version");
        expect(result->standard_library_modules.std_compat
                   && *result->standard_library_modules.std_compat == std_compat_source.lexically_normal(),
               "portable discovery should expose std.compat.ixx from the selected VC Tools version");
    }

    TemporaryDirectory no_modules_fixture;
    const fs::path no_modules = no_modules_fixture.path() / "portable_msvc";
    const fs::path no_modules_vc = no_modules / "VC" / "Tools" / "MSVC" / "14.50.20000";
    const fs::path no_modules_bin = no_modules_vc / "bin" / "Hostx64" / "x64";
    touch(no_modules_bin / "cl.exe");
    touch(no_modules_bin / "link.exe");
    touch(no_modules_bin / "lib.exe");
    make_sdk_version(no_modules, "10.0.30000.0");

    mqb::msvc::DiscoveryOptions no_modules_options;
    no_modules_options.preference = mqb::msvc::ToolchainPreference::portable;
    no_modules_options.portable_roots = {no_modules};
    const auto no_modules_result = locator.discover(no_modules_options);
    expect(no_modules_result.has_value(),
           "missing standard-library module sources must not invalidate an otherwise usable toolchain");
    if (no_modules_result) {
        expect(!no_modules_result->standard_library_modules.std
                   && !no_modules_result->standard_library_modules.std_compat,
               "standard-library module capability should remain explicitly unavailable when sources are absent");
    }

    TemporaryDirectory broken_fixture;
    const fs::path broken = broken_fixture.path() / "portable_msvc";
    fs::create_directories(broken);

    mqb::msvc::DiscoveryOptions broken_options;
    broken_options.preference = mqb::msvc::ToolchainPreference::portable;
    broken_options.portable_roots = {broken};
    const auto broken_result = locator.discover(broken_options);
    expect(!broken_result.has_value(), "invalid portable layout should fail instead of falling through to VS");
    if (!broken_result) {
        expect(broken_result.error().code == mqb::msvc::ToolchainErrorCode::invalid_portable_layout,
               "invalid portable layout should report a precise error code");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_portable_tests passed\n";
    return 0;
}
