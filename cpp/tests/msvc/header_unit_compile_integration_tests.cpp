#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
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
    return fs::temp_directory_path() / ("mqb_header_unit_compile_vs_" + std::to_string(stamp));
}

void print_compile_error(const mqb::msvc::CompilerError& error) {
    std::cerr << "compile error: " << error.message << '\n';
    if (error.process_error) {
        std::cerr << "process error: " << error.process_error->message
                  << " native=" << error.process_error->native_code << '\n';
    }
    if (error.process_result) {
        std::cerr << "exit=" << error.process_result->exit_code << '\n';
        std::cerr << error.process_result->stdout_text;
        std::cerr << error.process_result->stderr_text;
    }
}

} // namespace

int main() {
    using mqb::Architecture;
    using mqb::CppStandard;
    using mqb::HeaderUnitLookupMethod;
    using mqb::ProjectArtifactLayout;
    using mqb::msvc::CompileInvocation;
    using mqb::msvc::DiscoveryOptions;
    using mqb::msvc::HeaderUnitCompileInvocation;
    using mqb::msvc::HeaderUnitReference;
    using mqb::msvc::MsvcCompiler;
    using mqb::msvc::MsvcToolchainLocator;
    using mqb::msvc::ToolchainPreference;

    mqb::platform::windows::WindowsProcessRunner runner;
    MsvcToolchainLocator locator{runner};

    DiscoveryOptions discovery;
    discovery.preference = ToolchainPreference::visual_studio;
    discovery.target_architecture = Architecture::x64;
    discovery.host_architecture = Architecture::x64;

    auto toolchain = locator.discover(discovery);
    expect(toolchain.has_value(), "Visual Studio toolchain should be available for header-unit compile E2E");
    if (!toolchain) {
        std::cerr << "toolchain error: " << toolchain.error().message << '\n';
        return 1;
    }

    const fs::path root = make_temp_root();
    const fs::path include_directory = root / "include";
    const fs::path header_source = include_directory / "util.hpp";
    const fs::path consumer_source = root / "src" / "consumer.cpp";
    fs::create_directories(include_directory);
    fs::create_directories(consumer_source.parent_path());

    {
        std::ofstream header(header_source, std::ios::binary | std::ios::trunc);
        header << "inline int header_answer() { return 42; }\n";
    }
    {
        std::ofstream source(consumer_source, std::ios::binary | std::ios::trunc);
        source << "import \"util.hpp\";\n"
                  "int use_header_unit() { return header_answer(); }\n";
    }

    auto layout = ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "temporary project should create artifact layout");
    if (!layout) return 1;

    auto header_artifacts = layout->for_source(header_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    expect(header_artifacts.has_value() && consumer_artifacts.has_value(),
           "header unit and consumer should receive planned artifact paths");
    if (!header_artifacts || !consumer_artifacts) return 1;

    MsvcCompiler compiler{*toolchain, runner};

    HeaderUnitCompileInvocation header_unit;
    header_unit.header_name = "util.hpp";
    header_unit.lookup_method = HeaderUnitLookupMethod::quote;
    header_unit.interface_output = header_artifacts->module_interface;
    header_unit.working_directory = root;
    header_unit.options.standard = CppStandard::latest;
    header_unit.options.include_directories = {include_directory};

    auto header_result = compiler.compile_header_unit(header_unit);
    expect(header_result.has_value(),
           "real MSVC should export a quote header unit to the explicitly planned IFC");
    if (!header_result) {
        print_compile_error(header_result.error());
    }

    expect(fs::is_regular_file(header_artifacts->module_interface),
           "header-unit compilation should produce the explicitly planned IFC");
    expect(!fs::exists(header_artifacts->object),
           "IFC-only header-unit compilation should not invent an object artifact");

    CompileInvocation consumer;
    consumer.source = consumer_source;
    consumer.object = consumer_artifacts->object;
    consumer.source_dependencies = consumer_artifacts->dependencies;
    consumer.header_unit_references = {
        HeaderUnitReference{
            .header_name = "util.hpp",
            .lookup_method = HeaderUnitLookupMethod::quote,
            .interface_file = header_artifacts->module_interface,
        },
    };
    consumer.working_directory = root;
    consumer.options.standard = CppStandard::latest;
    consumer.options.include_directories = {include_directory};

    auto consumer_result = compiler.compile(consumer);
    expect(consumer_result.has_value(),
           "real MSVC consumer should compile through a typed quote header-unit IFC mapping");
    if (!consumer_result) {
        print_compile_error(consumer_result.error());
    }

    expect(fs::is_regular_file(consumer_artifacts->object),
           "header-unit consumer should produce the planned object");
    expect(fs::is_regular_file(consumer_artifacts->dependencies),
           "header-unit consumer should still emit sourceDependencies metadata");

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_header_unit_compile_integration_tests passed\n";
    return 0;
}
