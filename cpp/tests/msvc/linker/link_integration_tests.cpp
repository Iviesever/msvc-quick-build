#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
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

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_msvc_link_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path source = tree.root / "main.cpp";
    const fs::path object = tree.root / "obj" / "main.cpp.obj";
    const fs::path dependencies = tree.root / "deps" / "main.cpp.json";
    const fs::path executable = tree.root / "bin" / "hello.exe";

    {
        std::ofstream stream{source, std::ios::binary | std::ios::trunc};
        stream << "#include <cstdio>\n"
                  "int main() { std::puts(\"mqb-link-ok\"); return 0; }\n";
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions discovery;
    discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    discovery.target_architecture = mqb::Architecture::x64;
    discovery.host_architecture = mqb::Architecture::x64;

    auto toolchain = locator.discover(discovery);
    expect(toolchain.has_value(), "installed Visual Studio toolchain should be discoverable");
    if (!toolchain) {
        return 1;
    }

    auto linker_identity = mqb::msvc::MsvcLinker::identity(*toolchain);
    expect(linker_identity.has_value(), "link.exe should have an independent linker identity");
    if (linker_identity) {
        expect(linker_identity->linker == toolchain->linker,
               "linker identity should name the discovered link.exe");
        expect(!linker_identity->binary_stamp.empty(),
               "linker identity should include a binary stamp");
    }

    mqb::msvc::MsvcCompiler compiler{*toolchain, runner};
    mqb::msvc::CompileInvocation compile;
    compile.source = source;
    compile.object = object;
    compile.source_dependencies = dependencies;
    compile.working_directory = tree.root;
    compile.options.configuration = mqb::BuildConfiguration::debug;
    compile.options.architecture = mqb::Architecture::x64;
    compile.options.standard = mqb::CppStandard::cpp23;

    auto compiled = compiler.compile(compile);
    expect(compiled.has_value(), "real MSVC compile should succeed before linking");
    expect(fs::is_regular_file(object), "compiler should create object input for linker");
    if (!compiled) {
        if (compiled.error().process_result) {
            std::cerr << compiled.error().process_result->stdout_text
                      << compiled.error().process_result->stderr_text;
        }
        return 1;
    }

    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::msvc::LinkInvocation link;
    link.objects = {object};
    link.output = executable;
    link.working_directory = tree.root;
    link.options.configuration = mqb::BuildConfiguration::debug;
    link.options.architecture = mqb::Architecture::x64;
    link.options.subsystem = mqb::LinkSubsystem::console;

    auto linked = linker.link(link);
    expect(linked.has_value(), "real link.exe invocation should succeed");
    expect(fs::is_regular_file(executable), "linker should create executable output");
    if (!linked) {
        if (linked.error().process_result) {
            std::cerr << linked.error().process_result->stdout_text
                      << linked.error().process_result->stderr_text;
        }
        return 1;
    }

    mqb::process::ProcessSpec run;
    run.executable = executable;
    run.working_directory = tree.root;
    run.capture_stdout = true;
    run.capture_stderr = true;
    auto executed = runner.run(run);
    expect(executed.has_value(), "linked executable should launch");
    if (executed) {
        expect(executed->exit_code == 0, "linked executable should return zero");
        expect(executed->stdout_text.find("mqb-link-ok") != std::string::npos,
               "linked executable should contain expected program behavior");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_link_integration_tests passed\n";
    return 0;
}
