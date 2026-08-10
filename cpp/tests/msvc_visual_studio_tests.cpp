#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {

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

[[nodiscard]] bool has_environment_variable(
    const mqb::msvc::MsvcToolchain& toolchain,
    const std::string_view name) {
    return std::any_of(
        toolchain.environment.begin(),
        toolchain.environment.end(),
        [name](const mqb::process::EnvironmentVariable& variable) {
            return equals_ignore_case(variable.name, std::string{name});
        });
}

} // namespace

int main() {
    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};

    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::visual_studio;
    options.target_architecture = mqb::Architecture::x64;
    options.host_architecture = mqb::Architecture::x64;

    const auto result = locator.discover(options);
    expect(result.has_value(), "Visual Studio toolchain should be discoverable on the Windows CI image");
    if (result) {
        expect(result->source == mqb::msvc::ToolchainSource::visual_studio,
               "forced VS discovery should preserve toolchain provenance");
        expect(std::filesystem::is_regular_file(result->identity.compiler),
               "discovered cl.exe should exist");
        expect(std::filesystem::is_regular_file(result->linker),
               "discovered link.exe should exist");
        expect(std::filesystem::is_regular_file(result->librarian),
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
        expect(has_environment_variable(*result, "VCToolsInstallDir"),
               "vcvars environment should expose VCToolsInstallDir");
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_visual_studio_tests passed\n";
    return 0;
}
