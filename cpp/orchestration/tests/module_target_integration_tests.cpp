#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/BuildTypes.hpp"
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

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("mqb-module-target-vs-" + std::to_string(tick));
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

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] const mqb::orchestration::ModuleCompileResult* find_compile(
    const mqb::orchestration::IncrementalModuleTargetResult& result,
    const fs::path& source) {
    const auto found = std::find_if(
        result.compiles.compiles.begin(),
        result.compiles.compiles.end(),
        [&source](const mqb::orchestration::ModuleCompileResult& item) {
            return item.source.lexically_normal() == source.lexically_normal();
        });
    return found == result.compiles.compiles.end() ? nullptr : &*found;
}

[[nodiscard]] bool has_reason(
    const mqb::orchestration::ModuleCompileResult& result,
    const mqb::BuildReason reason) {
    return std::find(
               result.result.validation.reasons.begin(),
               result.result.validation.reasons.end(),
               reason)
        != result.result.validation.reasons.end();
}

void print_target_error(const mqb::orchestration::IncrementalModuleTargetError& error) {
    std::cerr << "module target error: " << error.message << '\n';
    if (!error.source.empty()) {
        std::cerr << "source: " << error.source.string() << '\n';
    }
    if (error.scan_error) {
        std::cerr << "scan error: " << error.scan_error->message << '\n';
        if (error.scan_error->process_result) {
            std::cerr << error.scan_error->process_result->stdout_text;
            std::cerr << error.scan_error->process_result->stderr_text;
        }
    }
    if (error.graph_error) {
        std::cerr << "graph error: " << error.graph_error->message << '\n';
    }
    if (error.compile_error) {
        std::cerr << "compile wave error: " << error.compile_error->message << '\n';
        if (error.compile_error->compile_error
            && error.compile_error->compile_error->compile_error
            && error.compile_error->compile_error->compile_error->compiler_error
            && error.compile_error->compile_error->compile_error->compiler_error->process_result) {
            const auto& process = *error.compile_error->compile_error
                ->compile_error->compiler_error->process_result;
            std::cerr << process.stdout_text;
            std::cerr << process.stderr_text;
        }
    }
    if (error.link_error) {
        std::cerr << "link error: " << error.link_error->message << '\n';
        if (error.link_error->linker_error
            && error.link_error->linker_error->process_result) {
            std::cerr << error.link_error->linker_error->process_result->stdout_text;
            std::cerr << error.link_error->linker_error->process_result->stderr_text;
        }
    }
}

[[nodiscard]] bool run_executable(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory) {
    mqb::process::ProcessSpec run;
    run.executable = executable;
    run.working_directory = working_directory;
    run.capture_stdout = true;
    run.capture_stderr = true;
    auto executed = runner.run(run);
    expect(executed.has_value(), "linked module executable should launch");
    if (!executed) {
        std::cerr << "launch error: " << executed.error().message << '\n';
        return false;
    }
    expect(executed->exit_code == 0,
           "linked module executable should observe imported module behavior and return zero");
    if (executed->exit_code != 0) {
        std::cerr << executed->stdout_text << executed->stderr_text;
        return false;
    }
    return true;
}

[[nodiscard]] bool expect_compile_state(
    const mqb::orchestration::IncrementalModuleTargetResult& result,
    const fs::path& module_source,
    const fs::path& consumer_source,
    const bool module_compiled,
    const bool consumer_compiled,
    const bool linked,
    const std::string_view description) {
    const auto* module = find_compile(result, module_source);
    const auto* consumer = find_compile(result, consumer_source);
    expect(module != nullptr && consumer != nullptr,
           "module target result should contain provider and consumer compile states");
    if (module == nullptr || consumer == nullptr) return false;

    expect(module->result.compiled == module_compiled,
           std::string{description} + ": provider compile state should match expectation");
    expect(consumer->result.compiled == consumer_compiled,
           std::string{description} + ": consumer compile state should match expectation");
    expect(result.link.linked == linked,
           std::string{description} + ": link state should match expectation");
    return module->result.compiled == module_compiled
        && consumer->result.compiled == consumer_compiled
        && result.link.linked == linked;
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
    expect(toolchain.has_value(),
           "installed Visual Studio toolchain should be available for module target E2E");
    if (!toolchain) {
        std::cerr << "toolchain error: " << toolchain.error().message << '\n';
        return 1;
    }

    TemporaryDirectory fixture;
    const fs::path root = fixture.path();
    const fs::path module_source = root / "modules" / "math.ixx";
    const fs::path consumer_source = root / "src" / "main.cpp";
    write_text(
        module_source,
        "export module math;\n"
        "export int add(int a, int b) { return a + b; }\n");
    write_text(
        consumer_source,
        "import math;\n"
        "int main() { return add(2, 3) == 5 ? 0 : 1; }\n");

    auto layout = mqb::ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "module target E2E should create project artifact layout");
    if (!layout) return 1;
    auto module_artifacts = layout->for_source(module_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    auto target_artifacts = layout->for_target("module-e2e");
    expect(module_artifacts && consumer_artifacts && target_artifacts,
           "module target E2E should map all artifacts");
    if (!module_artifacts || !consumer_artifacts || !target_artifacts) return 1;

    mqb::msvc::MsvcModuleDependencyScanner scanner{*toolchain, runner};
    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        *toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{*toolchain, linker};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

    mqb::orchestration::IncrementalModuleTargetRequest request;
    // Consumer first intentionally proves scan/result order is independent of
    // provider-before-consumer compile order discovered from P1689.
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer_source,
            .artifacts = *consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = module_source,
            .artifacts = *module_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
    };
    request.target = *target_artifacts;
    request.compiler_options.configuration = mqb::BuildConfiguration::debug;
    request.compiler_options.architecture = mqb::Architecture::x64;
    request.compiler_options.standard = mqb::CppStandard::latest;
    request.link_options.configuration = mqb::BuildConfiguration::debug;
    request.link_options.architecture = mqb::Architecture::x64;
    request.link_options.subsystem = mqb::LinkSubsystem::console;
    request.working_directory = root;
    request.max_parallel_scans = 2;
    request.max_parallel_compiles = 2;

    // 1. Cold: scan -> graph -> provider -> consumer -> link.
    auto cold = target.run(request);
    expect(cold.has_value(), "cold real module target should build successfully");
    if (!cold) {
        print_target_error(cold.error());
        return 1;
    }
    expect(cold->plan.resolved_dependencies.size() == 1
               && cold->plan.resolved_dependencies.front().logical_name == "math"
               && cold->plan.resolved_dependencies.front().provider_source == module_source
               && cold->plan.resolved_dependencies.front().consumer_source == consumer_source,
           "real P1689 scan should resolve consumer import math to math.ixx");
    if (!expect_compile_state(
            *cold, module_source, consumer_source, true, true, true, "cold build")) {
        return 1;
    }
    expect(fs::is_regular_file(module_artifacts->module_interface),
           "cold module target should produce planned IFC");
    if (!run_executable(runner, target_artifacts->executable, root)) return 1;

    // 2. Warm: scans may repeat, but compile and link must both be complete hits.
    auto warm = target.run(request);
    expect(warm.has_value(), "warm real module target should validate successfully");
    if (!warm) {
        print_target_error(warm.error());
        return 1;
    }
    if (!expect_compile_state(
            *warm, module_source, consumer_source, false, false, false, "warm build")) {
        return 1;
    }
    if (!run_executable(runner, target_artifacts->executable, root)) return 1;

    // 3. Provider source mutation: make source.mtime deterministically newer
    // than its current object so invalidation is independent of clock granularity.
    std::error_code time_error;
    const auto old_object_time = fs::last_write_time(module_artifacts->object, time_error);
    expect(!time_error, "provider object timestamp should be readable before source mutation");
    write_text(
        module_source,
        "export module math;\n"
        "export int add(int a, int b) { return (a + b); }\n");
    time_error.clear();
    fs::last_write_time(
        module_source,
        old_object_time + std::chrono::seconds{2},
        time_error);
    expect(!time_error,
           "provider source timestamp should be made deterministically newer than cached object");

    auto provider_changed = target.run(request);
    expect(provider_changed.has_value(),
           "provider source mutation should rebuild real module target successfully");
    if (!provider_changed) {
        print_target_error(provider_changed.error());
        return 1;
    }
    if (!expect_compile_state(
            *provider_changed,
            module_source,
            consumer_source,
            true,
            true,
            true,
            "provider source mutation")) {
        return 1;
    }
    const auto* changed_consumer = find_compile(*provider_changed, consumer_source);
    if (changed_consumer) {
        expect(has_reason(*changed_consumer, mqb::BuildReason::explicit_rebuild),
               "consumer rebuild after provider mutation should use explicit downstream propagation");
    }
    if (!run_executable(runner, target_artifacts->executable, root)) return 1;

    // The mutation deliberately pushed source time into the future. Normalize
    // it behind the rebuilt object and prove the target becomes warm again
    // before testing IFC-only loss, so the next invalidation has one cause.
    time_error.clear();
    const auto rebuilt_object_time = fs::last_write_time(module_artifacts->object, time_error);
    expect(!time_error, "rebuilt provider object timestamp should be readable");
    time_error.clear();
    fs::last_write_time(
        module_source,
        rebuilt_object_time - std::chrono::seconds{1},
        time_error);
    expect(!time_error, "provider source timestamp should normalize behind rebuilt object");

    auto stable_again = target.run(request);
    expect(stable_again.has_value(), "target should become warm again after timestamp normalization");
    if (!stable_again) {
        print_target_error(stable_again.error());
        return 1;
    }
    if (!expect_compile_state(
            *stable_again,
            module_source,
            consumer_source,
            false,
            false,
            false,
            "post-mutation warm build")) {
        return 1;
    }

    // 4. IFC-only loss: object and source remain untouched. Provider must
    // rebuild because a planned output is missing; consumer must rebuild via
    // the same explicit downstream propagation, then link must refresh.
    std::error_code remove_error;
    const bool removed = fs::remove(module_artifacts->module_interface, remove_error);
    expect(!remove_error && removed,
           "test should remove exactly the planned provider IFC artifact");

    auto missing_ifc = target.run(request);
    expect(missing_ifc.has_value(),
           "missing provider IFC should repair the real module target successfully");
    if (!missing_ifc) {
        print_target_error(missing_ifc.error());
        return 1;
    }
    if (!expect_compile_state(
            *missing_ifc,
            module_source,
            consumer_source,
            true,
            true,
            true,
            "missing IFC repair")) {
        return 1;
    }
    const auto* repaired_provider = find_compile(*missing_ifc, module_source);
    const auto* repaired_consumer = find_compile(*missing_ifc, consumer_source);
    if (repaired_provider) {
        expect(has_reason(*repaired_provider, mqb::BuildReason::missing_output),
               "provider with deleted IFC should report missing_output");
    }
    if (repaired_consumer) {
        expect(has_reason(*repaired_consumer, mqb::BuildReason::explicit_rebuild),
               "consumer after IFC repair should report explicit_rebuild");
    }
    expect(fs::is_regular_file(module_artifacts->module_interface),
           "IFC repair should recreate the planned provider IFC");
    if (!run_executable(runner, target_artifacts->executable, root)) return 1;

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_target_integration_tests passed\n";
    return 0;
}
