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
    return fs::temp_directory_path() / ("mqb_module_compile_vs_" + std::to_string(stamp));
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
    using mqb::ProjectArtifactLayout;
    using mqb::TranslationUnitKind;
    using mqb::msvc::CompileInvocation;
    using mqb::msvc::DiscoveryOptions;
    using mqb::msvc::ModuleReference;
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
    expect(toolchain.has_value(), "Visual Studio toolchain should be available for module compile E2E");
    if (!toolchain) {
        std::cerr << "toolchain error: " << toolchain.error().message << '\n';
        return 1;
    }

    const fs::path root = make_temp_root();
    const fs::path module_source = root / "modules" / "math.ixx";
    const fs::path consumer_source = root / "src" / "consumer.cpp";
    fs::create_directories(module_source.parent_path());
    fs::create_directories(consumer_source.parent_path());

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

    auto layout = ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "temporary project should create artifact layout");
    if (!layout) return 1;

    auto module_artifacts = layout->for_source(module_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    expect(module_artifacts.has_value() && consumer_artifacts.has_value(),
           "module and consumer should receive planned artifacts");
    if (!module_artifacts || !consumer_artifacts) return 1;

    MsvcCompiler compiler{*toolchain, runner};

    CompileInvocation module;
    module.source = module_source;
    module.object = module_artifacts->object;
    module.source_dependencies = module_artifacts->dependencies;
    module.kind = TranslationUnitKind::module_interface;
    module.module_interface_output = module_artifacts->module_interface;
    module.working_directory = root;
    module.options.standard = CppStandard::latest;

    auto module_result = compiler.compile(module);
    expect(module_result.has_value(), "real MSVC should compile a named module interface to planned IFC");
    if (!module_result) {
        print_compile_error(module_result.error());
    }

    expect(fs::is_regular_file(module_artifacts->object),
           "module interface compilation should produce the planned object");
    expect(fs::is_regular_file(module_artifacts->module_interface),
           "module interface compilation should produce the planned IFC");
    expect(fs::is_regular_file(module_artifacts->dependencies),
           "module interface compilation should still emit sourceDependencies metadata");

    CompileInvocation consumer;
    consumer.source = consumer_source;
    consumer.object = consumer_artifacts->object;
    consumer.source_dependencies = consumer_artifacts->dependencies;
    consumer.kind = TranslationUnitKind::source;
    consumer.module_references = {
        ModuleReference{
            .logical_name = "math",
            .interface_file = module_artifacts->module_interface,
        },
    };
    consumer.working_directory = root;
    consumer.options.standard = CppStandard::latest;

    auto consumer_result = compiler.compile(consumer);
    expect(consumer_result.has_value(),
           "real MSVC consumer should compile using an explicit logical-name to IFC reference");
    if (!consumer_result) {
        print_compile_error(consumer_result.error());
    }

    expect(fs::is_regular_file(consumer_artifacts->object),
           "module consumer should produce the planned object");
    expect(fs::is_regular_file(consumer_artifacts->dependencies),
           "module consumer should still emit sourceDependencies metadata");

    std::size_t ifc_count = 0;
    fs::path discovered_ifc;
    std::error_code walk_error;
    for (fs::recursive_directory_iterator it(root, walk_error), end;
         it != end && !walk_error;
         it.increment(walk_error)) {
        if (!it->is_regular_file()) continue;
        if (it->path().extension() == ".ifc") {
            ++ifc_count;
            discovered_ifc = it->path();
        }
    }
    expect(!walk_error, "temporary module project should be traversable after compilation");
    std::error_code equivalent_error;
    const bool planned_ifc = ifc_count == 1
        && fs::equivalent(discovered_ifc, module_artifacts->module_interface, equivalent_error)
        && !equivalent_error;
    expect(planned_ifc,
           "MSVC should emit exactly the explicitly planned physical IFC rather than a default-path side artifact");

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_module_compile_integration_tests passed\n";
    return 0;
}
