#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
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

[[nodiscard]] fs::path make_temp_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("mqb_module_scanner_vs_" + std::to_string(stamp));
}

[[nodiscard]] bool has_named_provide(
    const mqb::modules::P1689Document& document,
    const std::string_view logical_name) {
    for (const auto& rule : document.rules) {
        for (const auto& provided : rule.provided_modules) {
            if (provided.logical_name == logical_name) return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_named_requirement(
    const mqb::modules::P1689Document& document,
    const std::string_view logical_name) {
    for (const auto& rule : document.rules) {
        for (const auto& required : rule.required_modules) {
            if (required.logical_name == logical_name
                && required.lookup_method == mqb::modules::LookupMethod::by_name) {
                return true;
            }
        }
    }
    return false;
}

void print_scan_error(const mqb::msvc::ModuleScanError& error) {
    std::cerr << "module scan error: " << error.message << '\n';
    if (error.process_error) {
        std::cerr << "process error: " << error.process_error->message
                  << " native=" << error.process_error->native_code << '\n';
    }
    if (error.process_result) {
        std::cerr << "exit=" << error.process_result->exit_code << '\n';
        std::cerr << error.process_result->stdout_text;
        std::cerr << error.process_result->stderr_text;
    }
    if (error.dependency_error) {
        std::cerr << "P1689 error " << error.dependency_error->line << ':'
                  << error.dependency_error->column << ' '
                  << error.dependency_error->message << '\n';
    }
}

} // namespace

int main() {
    using mqb::Architecture;
    using mqb::CppStandard;
    using mqb::TranslationUnitKind;
    using mqb::msvc::DiscoveryOptions;
    using mqb::msvc::ModuleScanInvocation;
    using mqb::msvc::MsvcModuleDependencyScanner;
    using mqb::msvc::MsvcToolchainLocator;
    using mqb::msvc::ToolchainPreference;

    mqb::platform::windows::WindowsProcessRunner runner;
    MsvcToolchainLocator locator{runner};

    DiscoveryOptions discovery;
    discovery.preference = ToolchainPreference::visual_studio;
    discovery.target_architecture = Architecture::x64;
    discovery.host_architecture = Architecture::x64;

    auto toolchain = locator.discover(discovery);
    expect(toolchain.has_value(), "Visual Studio toolchain should be available for module scan E2E");
    if (!toolchain) {
        std::cerr << "toolchain error: " << toolchain.error().message << '\n';
        return 1;
    }

    const fs::path root = make_temp_root();
    const fs::path scan_dir = root / "scan";
    const fs::path module_source = root / "math.ixx";
    const fs::path consumer_source = root / "consumer.cpp";
    const fs::path module_scan = scan_dir / "math.json";
    const fs::path consumer_scan = scan_dir / "consumer.json";

    fs::create_directories(root);
    {
        std::ofstream source(module_source, std::ios::binary | std::ios::trunc);
        source << "export module math;\n"
                  "export int add(int a, int b) { return a + b; }\n";
    }
    {
        std::ofstream source(consumer_source, std::ios::binary | std::ios::trunc);
        source << "import math;\n"
                  "int use_math() { return add(2, 3); }\n";
    }

    MsvcModuleDependencyScanner scanner{*toolchain, runner};

    ModuleScanInvocation module_invocation;
    module_invocation.source = module_source;
    module_invocation.output_file = module_scan;
    module_invocation.kind = TranslationUnitKind::module_interface;
    module_invocation.working_directory = root;
    module_invocation.options.standard = CppStandard::latest;

    auto module_result = scanner.scan(module_invocation);
    expect(module_result.has_value(), "real MSVC scan should accept a named module interface");
    if (!module_result) {
        print_scan_error(module_result.error());
    } else {
        expect(has_named_provide(module_result->dependencies, "math"),
               "module-interface scan should provide logical module 'math'");
    }

    ModuleScanInvocation consumer_invocation;
    consumer_invocation.source = consumer_source;
    consumer_invocation.output_file = consumer_scan;
    consumer_invocation.kind = TranslationUnitKind::source;
    consumer_invocation.working_directory = root;
    consumer_invocation.options.standard = CppStandard::latest;

    auto consumer_result = scanner.scan(consumer_invocation);
    expect(consumer_result.has_value(), "real MSVC scan should accept a consumer before IFC exists");
    if (!consumer_result) {
        print_scan_error(consumer_result.error());
    } else {
        expect(has_named_requirement(consumer_result->dependencies, "math"),
               "consumer scan should require logical module 'math' by name");
    }

    expect(fs::is_regular_file(module_scan) && fs::is_regular_file(consumer_scan),
           "scan phase should emit the requested P1689 JSON files");

    bool found_compiled_artifact = false;
    std::error_code walk_error;
    for (fs::recursive_directory_iterator it(root, walk_error), end; it != end && !walk_error; it.increment(walk_error)) {
        if (!it->is_regular_file()) continue;
        const auto extension = it->path().extension().string();
        if (extension == ".obj" || extension == ".ifc") {
            found_compiled_artifact = true;
            std::cerr << "unexpected compiled artifact from scan: " << it->path().string() << '\n';
        }
    }
    expect(!found_compiled_artifact,
           "/scanDependencies topology scan should not create .obj or .ifc artifacts");

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_module_scanner_integration_tests passed\n";
    return 0;
}
