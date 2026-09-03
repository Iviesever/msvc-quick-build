#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
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

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

class RecordingRunner final : public mqb::process::ProcessRunner {
public:
    std::optional<mqb::process::ProcessSpec> last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        last_spec = spec;
        return mqb::process::ProcessResult{.exit_code = 0};
    }
};

[[nodiscard]] bool same_environment(
    const std::vector<mqb::process::EnvironmentVariable>& left,
    const std::vector<mqb::process::EnvironmentVariable>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].name != right[index].name
            || left[index].value != right[index].value
            || left[index].remove != right[index].remove) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_process_spec(
    const mqb::process::ProcessSpec& left,
    const mqb::process::ProcessSpec& right) {
    return left.executable == right.executable
        && left.arguments == right.arguments
        && left.working_directory == right.working_directory
        && same_environment(left.environment, right.environment)
        && left.inherit_environment == right.inherit_environment
        && left.capture_stdout == right.capture_stdout
        && left.capture_stderr == right.capture_stderr;
}

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

    // A pure MsvcLinkRecipe must describe the exact ProcessSpec that the real
    // MsvcLinker::link() path hands to its ProcessRunner. This locks `mqb plan`
    // to the production linker authority without executing link.exe.
    {
        mqb::msvc::LinkInvocation parity;
        parity.objects = {tree.root / "parity" / "input.obj"};
        parity.output = tree.root / "parity" / "plan.exe";
        parity.working_directory = tree.root;
        parity.options.configuration = mqb::BuildConfiguration::debug;
        parity.options.architecture = mqb::Architecture::x64;
        parity.options.subsystem = mqb::LinkSubsystem::console;
        parity.options.additional_arguments = {"/MAP"};
        parity.force_full_link = true;
        parity.observe_library_search = true;

        auto recipe = mqb::msvc::MsvcLinker::build_recipe(*toolchain, parity);
        expect(recipe.has_value(), "pure link recipe construction should succeed");

        RecordingRunner recording_runner;
        mqb::msvc::MsvcLinker recording_linker{*toolchain, recording_runner};
        auto modeled_execution = recording_linker.link(parity);
        expect(modeled_execution.has_value(),
               "recorded production link path should accept parity invocation");
        expect(recording_runner.last_spec.has_value(),
               "production link path should hand one ProcessSpec to the runner");
        if (recipe && recording_runner.last_spec) {
            expect(same_process_spec(recipe->process, *recording_runner.last_spec),
                   "pure link recipe must exactly match production link ProcessSpec");
        }
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
