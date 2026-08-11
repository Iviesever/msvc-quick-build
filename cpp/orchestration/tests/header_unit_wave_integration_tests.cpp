#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
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
        path_ = fs::temp_directory_path() / ("mqb-header-unit-wave-" + std::to_string(tick));
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

bool has_reason(const mqb::CompileCacheValidation& validation, mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

void print_error(const mqb::orchestration::ModuleCompileError& error) {
    std::cerr << "module wave error: " << error.message << '\n';
    if (!error.source.empty()) std::cerr << "  source=" << error.source.generic_string() << '\n';
    if (!error.provider_source.empty()) std::cerr << "  provider=" << error.provider_source.generic_string() << '\n';
    if (!error.logical_name.empty()) std::cerr << "  logical=" << error.logical_name << '\n';
    if (error.compile_error) {
        std::cerr << "  incremental=" << error.compile_error->message << '\n';
        if (error.compile_error->compile_error) {
            const auto& executor = *error.compile_error->compile_error;
            std::cerr << "  executor=" << executor.message << '\n';
            if (executor.compiler_error) {
                const auto& compiler = *executor.compiler_error;
                std::cerr << "  compiler=" << compiler.message << '\n';
                if (compiler.process_result) {
                    std::cerr << compiler.process_result->stdout_text
                              << compiler.process_result->stderr_text;
                }
            }
        }
    }
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
    expect(toolchain.has_value(), "header-unit wave E2E requires installed Visual Studio");
    if (!toolchain) return 1;

    TemporaryDirectory fixture;
    const fs::path include_dir = fixture.path() / "include";
    const fs::path header = include_dir / "util.hpp";
    const fs::path consumer = fixture.path() / "src" / "consumer.cpp";
    write_text(header, "inline int header_answer() { return 42; }\n");
    write_text(consumer,
        "import \"util.hpp\";\n"
        "int use_header_unit() { return header_answer(); }\n");

    mqb::modules::RequiredModule required;
    required.logical_name = "util.hpp";
    required.source_path = header;
    required.unique_on_source_path = true;
    required.lookup_method = mqb::modules::LookupMethod::include_quote;
    mqb::modules::P1689Rule rule;
    rule.required_modules = {required};
    const std::vector scanned{
        mqb::modules::ScannedModuleUnit{.source = consumer, .rule = rule},
    };
    auto plan = mqb::modules::ModuleDependencyGraphBuilder::build(scanned);
    expect(plan.has_value(), "P1689 header-unit requirement should build a resolved plan");
    if (!plan) return 1;

    auto layout = mqb::ProjectArtifactLayout::create(fixture.path());
    expect(layout.has_value(), "fixture should create project artifact layout");
    if (!layout) return 1;
    auto consumer_artifacts = layout->for_source(consumer);
    auto header_artifacts = layout->for_source(header);
    expect(consumer_artifacts.has_value() && header_artifacts.has_value(),
           "consumer and header unit should receive collision-free artifacts");
    if (!consumer_artifacts || !header_artifacts) return 1;

    mqb::CompilerOptions options;
    options.standard = mqb::CppStandard::latest;
    options.include_directories = {include_dir};

    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental{*toolchain, executor};
    mqb::orchestration::MsvcModuleCompileCoordinator wave{incremental};

    mqb::orchestration::ModuleCompileWaveRequest request;
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer,
            .artifacts = *consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
    };
    request.header_units = {
        mqb::orchestration::ModuleCompileHeaderUnitRequest{
            .source = header,
            .header_name = "util.hpp",
            .lookup_method = mqb::HeaderUnitLookupMethod::quote,
            .artifacts = *header_artifacts,
        },
    };
    request.plan = *plan;
    request.compiler_options = options;
    request.working_directory = fixture.path();
    request.max_parallel_compiles = 2;

    auto cold = wave.run(request);
    if (!cold) {
        ++failures;
        std::cerr << "FAIL: cold header-unit wave should succeed\n";
        print_error(cold.error());
        return 1;
    }
    expect(cold->header_unit_compiles.size() == 1 && cold->header_unit_compiles[0].result.compiled,
           "cold wave should compile the header-unit producer");
    expect(cold->compiles.size() == 1 && cold->compiles[0].result.compiled,
           "cold wave should compile the consumer after the header unit");
    expect(fs::is_regular_file(header_artifacts->module_interface),
           "cold wave should produce the header-unit IFC");
    expect(!fs::exists(header_artifacts->object),
           "header-unit wave producer should remain IFC-only");
    expect(fs::is_regular_file(consumer_artifacts->object),
           "cold wave should produce consumer object");

    auto warm = wave.run(request);
    if (!warm) { print_error(warm.error()); return 1; }
    expect(!warm->header_unit_compiles[0].result.compiled,
           "unchanged header-unit producer should be warm");
    expect(!warm->compiles[0].result.compiled,
           "unchanged consumer should be warm");

    std::error_code time_error;
    const auto ifc_time = fs::last_write_time(header_artifacts->module_interface, time_error);
    expect(!time_error, "mutation fixture requires IFC timestamp");
    write_text(header, "inline int header_answer() { return 43; }\n");
    time_error.clear();
    fs::last_write_time(header, ifc_time + std::chrono::seconds{2}, time_error);
    expect(!time_error, "test should make header newer than IFC deterministically");

    auto mutated = wave.run(request);
    if (!mutated) { print_error(mutated.error()); return 1; }
    expect(mutated->header_unit_compiles[0].result.compiled,
           "header mutation should rebuild header-unit producer");
    expect(mutated->compiles[0].result.compiled,
           "provider rebuild should explicitly rebuild consumer in the next level");
    expect(has_reason(mutated->compiles[0].result.validation, mqb::BuildReason::explicit_rebuild),
           "consumer rebuild propagation should remain an explicit typed reason");

    // The mutation was deliberately future-dated only to make the stale check
    // deterministic. After the rebuild, restore the source to a timestamp just
    // before the newly created IFC. Do not push the IFC itself into the future:
    // consumers track that IFC as a dependency, so doing so would correctly
    // make their existing object stale again.
    time_error.clear();
    const auto rebuilt_ifc_time = fs::last_write_time(
        header_artifacts->module_interface,
        time_error);
    expect(!time_error, "rebuilt fixture requires the new header-unit IFC timestamp");
    time_error.clear();
    fs::last_write_time(header, rebuilt_ifc_time - std::chrono::seconds{1}, time_error);
    expect(!time_error, "test should normalize the header timestamp behind the rebuilt IFC");

    auto warm_again = wave.run(request);
    if (!warm_again) { print_error(warm_again.error()); return 1; }
    expect(!warm_again->header_unit_compiles[0].result.compiled
               && !warm_again->compiles[0].result.compiled,
           "rebuilt header-unit target should return to fully warm state");

    fs::remove(header_artifacts->module_interface, time_error);
    expect(!time_error, "test should delete only the header-unit IFC");
    auto repaired = wave.run(request);
    if (!repaired) { print_error(repaired.error()); return 1; }
    expect(repaired->header_unit_compiles[0].result.compiled,
           "missing IFC should rebuild header-unit producer");
    expect(repaired->compiles[0].result.compiled,
           "missing-IFC provider repair should rebuild consumer downstream");
    expect(fs::is_regular_file(header_artifacts->module_interface),
           "missing-IFC repair should recreate provider IFC");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_header_unit_wave_integration_tests passed\n";
    return 0;
}
