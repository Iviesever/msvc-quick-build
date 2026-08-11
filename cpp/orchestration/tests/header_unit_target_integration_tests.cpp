#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-header-unit-target-" + std::to_string(tick));
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

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool has_reason(const mqb::orchestration::ModuleCompileResult& result, mqb::BuildReason reason) {
    return std::find(
        result.result.validation.reasons.begin(),
        result.result.validation.reasons.end(),
        reason) != result.result.validation.reasons.end();
}

void print_error(const mqb::orchestration::IncrementalModuleTargetError& error) {
    std::cerr << "module target error: " << error.message << '\n';
    if (!error.source.empty()) std::cerr << "  source=" << error.source.generic_string() << '\n';
    if (!error.artifact.empty()) std::cerr << "  artifact=" << error.artifact.generic_string() << '\n';
    if (error.artifact_layout_error) std::cerr << "  layout=" << error.artifact_layout_error->message << '\n';
    if (error.scan_error) {
        std::cerr << "  scan=" << error.scan_error->message << '\n';
        if (error.scan_error->process_result) {
            std::cerr << error.scan_error->process_result->stdout_text
                      << error.scan_error->process_result->stderr_text;
        }
    }
    if (error.graph_error) std::cerr << "  graph=" << error.graph_error->message << '\n';
    if (error.compile_error) {
        std::cerr << "  wave=" << error.compile_error->message << '\n';
        if (error.compile_error->compile_error
            && error.compile_error->compile_error->compile_error
            && error.compile_error->compile_error->compile_error->compiler_error
            && error.compile_error->compile_error->compile_error->compiler_error->process_result) {
            const auto& process = *error.compile_error->compile_error
                ->compile_error->compiler_error->process_result;
            std::cerr << process.stdout_text << process.stderr_text;
        }
    }
    if (error.link_error) std::cerr << "  link=" << error.link_error->message << '\n';
}

bool run_executable(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& cwd) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.working_directory = cwd;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    expect(result.has_value(), "header-unit target executable should launch");
    if (!result) return false;
    expect(result->exit_code == 0, "header-unit target executable should return zero");
    if (result->exit_code != 0) std::cerr << result->stdout_text << result->stderr_text;
    return result->exit_code == 0;
}

} // namespace

int main() {
    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions discovery;
    discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    discovery.target_architecture = mqb::Architecture::x64;
    discovery.host_architecture = mqb::Architecture::x64;
    auto toolchain = locator.discover(discovery);
    expect(toolchain.has_value(), "header-unit target E2E requires installed Visual Studio");
    if (!toolchain) return 1;

    TemporaryDirectory fixture;
    const fs::path root = fixture.path();
    const fs::path include_dir = root / "include";
    const fs::path header = include_dir / "util.hpp";
    const fs::path consumer = root / "src" / "main.cpp";
    write_text(header, "inline int header_answer() { return 42; }\n");
    write_text(consumer,
        "import \"util.hpp\";\n"
        "int main() { return header_answer() == 42 ? 0 : 1; }\n");

    auto layout = mqb::ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "header-unit target should create artifact layout");
    if (!layout) return 1;
    auto consumer_artifacts = layout->for_source(consumer);
    auto expected_header_artifacts = layout->for_source(header);
    auto target_artifacts = layout->for_target("header-unit-target");
    expect(consumer_artifacts && expected_header_artifacts && target_artifacts,
           "fixture should resolve consumer/header/target artifacts");
    if (!consumer_artifacts || !expected_header_artifacts || !target_artifacts) return 1;

    mqb::msvc::MsvcModuleDependencyScanner scanner{*toolchain, runner};
    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{*toolchain, executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{*toolchain, linker};
    mqb::orchestration::MsvcModuleTargetCoordinator target{scanner, module_compile, incremental_link};

    mqb::orchestration::IncrementalModuleTargetRequest request;
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer,
            .artifacts = *consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
    };
    request.target = *target_artifacts;
    request.artifact_layout = *layout;
    request.compiler_options.configuration = mqb::BuildConfiguration::debug;
    request.compiler_options.architecture = mqb::Architecture::x64;
    request.compiler_options.standard = mqb::CppStandard::latest;
    request.compiler_options.include_directories = {include_dir};
    request.link_options.configuration = mqb::BuildConfiguration::debug;
    request.link_options.architecture = mqb::Architecture::x64;
    request.link_options.subsystem = mqb::LinkSubsystem::console;
    request.working_directory = root;
    request.max_parallel_scans = 2;
    request.max_parallel_compiles = 2;

    auto cold = target.run(request);
    expect(cold.has_value(), "cold target should discover, allocate, build, and link header unit");
    if (!cold) { print_error(cold.error()); return 1; }
    expect(cold->plan.header_units.size() == 1,
           "real P1689 target scan should expose exactly one project-local header unit");
    if (cold->plan.header_units.size() == 1) {
        expect(cold->plan.header_units[0].source.lexically_normal() == header.lexically_normal(),
               "P1689 source-path should resolve to the physical project header");
        expect(cold->plan.header_units[0].header_name == "util.hpp",
               "P1689 header-unit logical name should preserve import spelling");
        expect(cold->plan.header_units[0].lookup_method == mqb::modules::LookupMethod::include_quote,
               "P1689 header-unit lookup method should preserve quote semantics");
    }
    expect(cold->compiles.header_unit_compiles.size() == 1
               && cold->compiles.header_unit_compiles[0].result.compiled,
           "cold target should compile dynamically allocated header-unit provider");
    expect(cold->compiles.compiles.size() == 1 && cold->compiles.compiles[0].result.compiled,
           "cold target should compile consumer after header-unit provider");
    expect(cold->link.linked, "cold target should link executable");
    expect(fs::is_regular_file(expected_header_artifacts->module_interface),
           "target should allocate and create the layout-derived header-unit IFC");
    expect(fs::is_regular_file(expected_header_artifacts->dependencies),
           "target should allocate header-unit sourceDependencies metadata");
    expect(fs::is_regular_file(expected_header_artifacts->compile_cache),
           "target should allocate header-unit compile-cache metadata");
    expect(!fs::exists(expected_header_artifacts->object),
           "dynamically allocated header-unit provider must remain IFC-only");
    if (!run_executable(runner, target_artifacts->executable, root)) return 1;

    auto warm = target.run(request);
    if (!warm) { print_error(warm.error()); return 1; }
    expect(!warm->compiles.header_unit_compiles[0].result.compiled,
           "warm target should reuse dynamically allocated header-unit IFC");
    expect(!warm->compiles.compiles[0].result.compiled,
           "warm target should reuse consumer object");
    expect(!warm->link.linked, "warm target should reuse linked executable");

    std::error_code time_error;
    const auto old_ifc_time = fs::last_write_time(expected_header_artifacts->module_interface, time_error);
    expect(!time_error, "mutation fixture requires header-unit IFC timestamp");
    write_text(header, "inline int header_answer() { return 43; }\n");
    time_error.clear();
    fs::last_write_time(header, old_ifc_time + std::chrono::seconds{2}, time_error);
    expect(!time_error, "test should make header source deterministically newer than IFC");

    auto mutated = target.run(request);
    if (!mutated) { print_error(mutated.error()); return 1; }
    expect(mutated->compiles.header_unit_compiles[0].result.compiled,
           "header mutation should rebuild dynamically allocated provider");
    expect(mutated->compiles.compiles[0].result.compiled,
           "header provider rebuild should rebuild consumer downstream");
    expect(has_reason(mutated->compiles.compiles[0], mqb::BuildReason::explicit_rebuild),
           "header provider rebuild should propagate explicit_rebuild to consumer");
    expect(mutated->link.linked, "header provider mutation should relink target");

    time_error.clear();
    const auto rebuilt_ifc_time = fs::last_write_time(expected_header_artifacts->module_interface, time_error);
    expect(!time_error, "rebuilt target requires header-unit IFC timestamp");
    time_error.clear();
    fs::last_write_time(header, rebuilt_ifc_time - std::chrono::seconds{1}, time_error);
    expect(!time_error, "test should normalize header source behind rebuilt IFC");
    auto warm_again = target.run(request);
    if (!warm_again) { print_error(warm_again.error()); return 1; }
    expect(!warm_again->compiles.header_unit_compiles[0].result.compiled
               && !warm_again->compiles.compiles[0].result.compiled
               && !warm_again->link.linked,
           "rebuilt target should become fully warm again");

    fs::remove(expected_header_artifacts->module_interface, time_error);
    expect(!time_error, "test should delete only layout-derived header-unit IFC");
    auto repaired = target.run(request);
    if (!repaired) { print_error(repaired.error()); return 1; }
    expect(repaired->compiles.header_unit_compiles[0].result.compiled,
           "missing dynamic header IFC should rebuild provider");
    expect(repaired->compiles.compiles[0].result.compiled,
           "missing header IFC repair should rebuild consumer");
    expect(repaired->link.linked, "missing header IFC repair should relink target");
    expect(fs::is_regular_file(expected_header_artifacts->module_interface),
           "missing dynamic header IFC should be recreated");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_header_unit_target_integration_tests passed\n";
    return 0;
}
