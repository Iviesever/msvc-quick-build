#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
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
    EnvironmentGuard ambient_vc_tools{"VCToolsInstallDir"};
    EnvironmentGuard ambient_sdk_dir{"WindowsSdkDir"};
    EnvironmentGuard ambient_sdk_version{"WindowsSDKVersion"};
    EnvironmentGuard ambient_ucrt_dir{"UniversalCRTSdkDir"};
    EnvironmentGuard ambient_ucrt_version{"UCRTVersion"};
    EnvironmentGuard ambient_netfx{"NETFXSDKDir"};
    ambient_path.set("ambient-path-a;ambient-path-b");
    ambient_include.set("ambient-include-a;ambient-include-b");
    ambient_lib.set("ambient-lib-a;ambient-lib-b");
    ambient_libpath.set("ambient-libpath-a;ambient-libpath-b");
    ambient_vc_tools.set("ambient-vc-tools");
    ambient_sdk_dir.set("ambient-sdk-dir");
    ambient_sdk_version.set("ambient-sdk-version");
    ambient_ucrt_dir.set("ambient-ucrt-dir");
    ambient_ucrt_version.set("ambient-ucrt-version");
    ambient_netfx.set("ambient-netfx-sdk");

    TemporaryDirectory fixture;
    const fs::path portable = fixture.path() / fs::path{u8"portable_msvc_工具链_テスト"};

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
    expect(result.has_value(), "automatic discovery should select an existing Unicode portable toolchain first");
    expect(runner.calls == 0, "portable discovery should not invoke vswhere or cmd.exe");
    if (result) {
        expect(result->source == mqb::msvc::ToolchainSource::portable,
               "portable discovery should retain toolchain provenance");
        expect(result->identity.compiler == tool_bin / "cl.exe",
               "portable discovery should resolve the requested host/target compiler under a Unicode root");
        expect(result->linker == tool_bin / "link.exe",
               "portable discovery should resolve link.exe beside cl.exe");
        expect(result->librarian == tool_bin / "lib.exe",
               "portable discovery should resolve lib.exe beside cl.exe");
        expect(result->identity.version == "14.50.20000",
               "portable discovery should select the lexicographically latest VC tools version");
        expect(!result->identity.binary_stamp.empty(),
               "compiler identity should include a binary and compiler-environment stamp");
        expect(result->environment.size() == 10,
               "portable discovery should own all effective search and toolchain metadata variables");

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

        for (const std::string_view metadata_name : {
                 "VCToolsInstallDir",
                 "WindowsSdkDir",
                 "WindowsSDKVersion",
                 "UniversalCRTSdkDir",
                 "UCRTVersion",
                 "NETFXSDKDir",
             }) {
            const auto* metadata = find_environment(*result, metadata_name);
            expect(metadata != nullptr && metadata->value.empty(),
                   "portable discovery must explicitly mask ambient vcvars metadata");
        }

        const std::string compiler_stamp =
            mqb::msvc::compiler_environment_stamp(result->environment);
        const std::string linker_stamp =
            mqb::msvc::linker_environment_stamp(result->environment);
        const std::string librarian_stamp =
            mqb::msvc::librarian_environment_stamp(result->environment);
        expect(result->identity.binary_stamp.ends_with(compiler_stamp),
               "portable compiler cache identity should seal compiler search environment");

        const auto linker_identity = mqb::msvc::MsvcLinker::identity(*result);
        expect(linker_identity.has_value(),
               "portable link.exe should expose a linker identity without launching it");
        if (linker_identity) {
            expect(linker_identity->binary_stamp.ends_with(linker_stamp),
                   "portable linker cache identity should seal library/helper search environment");
        }
        const auto librarian_identity = mqb::msvc::MsvcLibrarian::identity(*result);
        expect(librarian_identity.has_value(),
               "portable lib.exe should expose a librarian identity without launching it");
        if (librarian_identity) {
            expect(librarian_identity->binary_stamp.ends_with(librarian_stamp),
                   "portable librarian cache identity should seal library/tool search environment");
        }

        auto include_order_changed = result->environment;
        if (auto* effective_include = find_environment(include_order_changed, "INCLUDE")) {
            effective_include->value = "B;A";
        }
        expect(mqb::msvc::compiler_environment_stamp(include_order_changed) != compiler_stamp,
               "effective INCLUDE replacement/order changes must change compiler identity");
        expect(mqb::msvc::linker_environment_stamp(include_order_changed) == linker_stamp,
               "INCLUDE-only changes must not invalidate linker identity");
        expect(mqb::msvc::librarian_environment_stamp(include_order_changed) == librarian_stamp,
               "INCLUDE-only changes must not invalidate librarian identity");

        auto lib_order_changed = *result;
        if (auto* effective_lib = find_environment(lib_order_changed.environment, "LIB")) {
            effective_lib->value = "B;A";
        }
        expect(mqb::msvc::compiler_environment_stamp(lib_order_changed.environment) == compiler_stamp,
               "LIB-only changes must not rebuild translation units");
        expect(mqb::msvc::linker_environment_stamp(lib_order_changed.environment) != linker_stamp,
               "effective LIB replacement/order changes must change linker identity");
        expect(mqb::msvc::librarian_environment_stamp(lib_order_changed.environment) != librarian_stamp,
               "effective LIB replacement/order changes must change librarian identity");
        const auto changed_linker_identity = mqb::msvc::MsvcLinker::identity(lib_order_changed);
        expect(linker_identity && changed_linker_identity
                   && changed_linker_identity->binary_stamp != linker_identity->binary_stamp,
               "LIB order mutation must directly invalidate MsvcLinker identity");
        const auto changed_librarian_identity = mqb::msvc::MsvcLibrarian::identity(lib_order_changed);
        expect(librarian_identity && changed_librarian_identity
                   && changed_librarian_identity->binary_stamp != librarian_identity->binary_stamp,
               "LIB order mutation must directly invalidate MsvcLibrarian identity");

        auto libpath_changed = result->environment;
        if (auto* effective_libpath = find_environment(libpath_changed, "LIBPATH")) {
            effective_libpath->value = "metadata-b;metadata-a";
        }
        expect(mqb::msvc::compiler_environment_stamp(libpath_changed) != compiler_stamp,
               "effective LIBPATH changes must change compiler identity");
        expect(mqb::msvc::linker_environment_stamp(libpath_changed) == linker_stamp,
               "LIBPATH-only changes must not invalidate linker identity");
        expect(mqb::msvc::librarian_environment_stamp(libpath_changed) == librarian_stamp,
               "LIBPATH-only changes must not invalidate librarian identity");

        ambient_path.set("ambient-path-b;ambient-path-a");
        ambient_include.set("ambient-include-b;ambient-include-a");
        ambient_lib.set("ambient-lib-b;ambient-lib-a");
        ambient_libpath.set("ambient-libpath-b;ambient-libpath-a");
        ambient_vc_tools.set("ambient-vc-tools-mutated");
        ambient_sdk_dir.set("ambient-sdk-dir-mutated");
        ambient_sdk_version.set("ambient-sdk-version-mutated");
        ambient_ucrt_dir.set("ambient-ucrt-dir-mutated");
        ambient_ucrt_version.set("ambient-ucrt-version-mutated");
        ambient_netfx.set("ambient-netfx-sdk-mutated");
        const auto after_ambient_mutation = locator.discover(options);
        expect(after_ambient_mutation.has_value()
                   && after_ambient_mutation->identity.binary_stamp == result->identity.binary_stamp,
               "portable ambient search/metadata mutation must not perturb compiler identity");
        if (after_ambient_mutation) {
            const auto after_linker_identity = mqb::msvc::MsvcLinker::identity(*after_ambient_mutation);
            expect(linker_identity && after_linker_identity
                       && after_linker_identity->binary_stamp == linker_identity->binary_stamp,
                   "portable ambient search/metadata mutation must not perturb linker identity");
            const auto after_librarian_identity = mqb::msvc::MsvcLibrarian::identity(*after_ambient_mutation);
            expect(librarian_identity && after_librarian_identity
                       && after_librarian_identity->binary_stamp == librarian_identity->binary_stamp,
                   "portable ambient search/metadata mutation must not perturb librarian identity");
        }

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
