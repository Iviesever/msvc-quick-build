#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
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
        path_ = fs::temp_directory_path() / ("mqb-header-unit-incremental-" + std::to_string(tick));
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

[[nodiscard]] bool has_reason(
    const mqb::CompileCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

void print_incremental_error(const mqb::orchestration::IncrementalCompileError& error) {
    std::cerr << "incremental compile error: " << error.message << '\n';
    if (error.planner_error) {
        std::cerr << "  planner code=" << static_cast<int>(error.planner_error->code)
                  << " source=" << error.planner_error->source.generic_string()
                  << " object_outputs=" << error.planner_error->object_output_count
                  << " ifc_outputs=" << error.planner_error->module_interface_output_count
                  << '\n';
    }
    if (!error.compile_error) {
        return;
    }
    const auto& compile = *error.compile_error;
    std::cerr << "  executor: " << compile.message << '\n';
    if (compile.compiler_error) {
        const auto& compiler = *compile.compiler_error;
        std::cerr << "  compiler: " << compiler.message << '\n';
        if (compiler.process_error) {
            std::cerr << "  process: " << compiler.process_error->message
                      << " native=" << compiler.process_error->native_code << '\n';
        }
        if (compiler.process_result) {
            std::cerr << "  exit=" << compiler.process_result->exit_code << '\n'
                      << compiler.process_result->stdout_text
                      << compiler.process_result->stderr_text;
        }
    }
    if (compile.dependency_error) {
        std::cerr << "  dependency metadata: " << compile.dependency_error->message << '\n';
    }
}

template <typename Result>
[[nodiscard]] bool require_success(const Result& result, const std::string_view message) {
    if (result) {
        return true;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
    print_incremental_error(result.error());
    return false;
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
    expect(toolchain.has_value(), "header-unit incremental E2E requires installed Visual Studio");
    if (!toolchain) {
        std::cerr << toolchain.error().message << '\n';
        return 1;
    }

    TemporaryDirectory fixture;
    const fs::path include_dir = fixture.path() / "include";
    const fs::path header = include_dir / "util.hpp";
    const fs::path ifc = fixture.path() / ".mqb" / "ifc" / "util.ifc";
    const fs::path object = fixture.path() / ".mqb" / "obj" / "util.obj";
    const fs::path dependencies = fixture.path() / ".mqb" / "deps" / "util.json";
    const fs::path cache = fixture.path() / ".mqb" / "cache" / "compile" / "util.mqbcache";

    write_text(header, "inline int header_answer() { return 42; }\n");

    mqb::TranslationUnit unit;
    unit.source = header;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.header_unit = mqb::HeaderUnitIdentity{
        .header_name = "util.hpp",
        .lookup_method = mqb::HeaderUnitLookupMethod::quote,
    };
    unit.outputs = {
        mqb::Artifact{ifc, mqb::ArtifactKind::module_interface},
    };

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::latest;
    options.include_directories = {include_dir};

    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator coordinator{*toolchain, executor};
    mqb::orchestration::IncrementalCompileRequest request{
        .unit = unit,
        .options = options,
        .cache_file = cache,
        .source_dependencies_file = dependencies,
        .working_directory = fixture.path(),
    };

    auto cold = coordinator.run(request);
    if (!require_success(cold, "cold header-unit producer should execute successfully")) return 1;
    expect(cold->compiled, "cold header-unit producer should execute exactly one compile action");
    expect(has_reason(cold->validation, mqb::BuildReason::missing_cache_entry),
           "cold header-unit build should explain missing cache metadata");
    expect(has_reason(cold->validation, mqb::BuildReason::missing_output),
           "cold header-unit build should explain missing IFC output");
    expect(fs::is_regular_file(ifc), "cold header-unit build should create the planned IFC");
    expect(fs::is_regular_file(dependencies),
           "header-unit producer should emit sourceDependencies metadata");
    expect(fs::is_regular_file(cache), "cold header-unit build should persist compile cache metadata");
    expect(!fs::exists(object), "incremental header-unit producer must remain IFC-only");

    auto cached = mqb::CompileCacheFile::load(cache);
    expect(cached.has_value() && cached->has_value(),
           "header-unit cache should round-trip through compile-cache v2");
    if (cached && cached->has_value()) {
        expect((*cached)->outputs.size() == 1
                   && (*cached)->outputs.front().kind == mqb::ArtifactKind::module_interface
                   && (*cached)->outputs.front().path.lexically_normal() == ifc.lexically_normal(),
               "header-unit cache should persist exactly the planned IFC output");
    }

    auto warm = coordinator.run(request);
    if (!require_success(warm, "warm header-unit producer should validate successfully")) return 1;
    expect(!warm->compiled, "unchanged header-unit producer should be a compile-cache hit");
    expect(warm->plan.actions.empty(), "warm header-unit producer should schedule zero actions");

    auto angle_request = request;
    angle_request.unit.header_unit->lookup_method = mqb::HeaderUnitLookupMethod::angle;
    auto angle = coordinator.run(angle_request);
    if (!require_success(angle, "angle header-unit producer should rebuild successfully")) return 1;
    expect(angle->compiled,
           "changing header-unit lookup identity should invalidate the producer recipe");
    expect(has_reason(angle->validation, mqb::BuildReason::compiler_options_changed),
           "header-unit identity change should surface as recipe/signature invalidation");

    auto quote_again = coordinator.run(request);
    if (!require_success(quote_again, "quote header-unit producer should rebuild successfully")) return 1;
    expect(quote_again->compiled,
           "switching header-unit identity back should rebuild the quote producer");

    std::error_code time_error;
    const auto ifc_before_mutation = fs::last_write_time(ifc, time_error);
    expect(!time_error, "mutation fixture requires an IFC timestamp");
    write_text(header, "inline int header_answer() { return 43; }\n");
    time_error.clear();
    fs::last_write_time(header, ifc_before_mutation + std::chrono::seconds{2}, time_error);
    expect(!time_error, "test should be able to make the header deterministically newer than the IFC");

    auto mutated = coordinator.run(request);
    if (!require_success(mutated, "mutated header-unit producer should rebuild successfully")) return 1;
    expect(mutated->compiled, "header source mutation should rebuild the header-unit IFC");
    expect(has_reason(mutated->validation, mqb::BuildReason::source_changed),
           "header source mutation should report source_changed");

    const auto header_time = fs::last_write_time(header, time_error);
    expect(!time_error, "rebuilt mutation fixture requires the header timestamp");
    time_error.clear();
    fs::last_write_time(ifc, header_time + std::chrono::seconds{1}, time_error);
    expect(!time_error, "test should be able to make rebuilt IFC newer than its source");

    auto warm_after_mutation = coordinator.run(request);
    if (!require_success(warm_after_mutation, "rebuilt header unit should validate successfully")) return 1;
    expect(!warm_after_mutation->compiled,
           "rebuilt header unit should return to a warm cache hit");

    fs::remove(ifc, time_error);
    expect(!time_error, "test should be able to delete only the header-unit IFC");
    auto repaired = coordinator.run(request);
    if (!require_success(repaired, "missing header-unit IFC should repair successfully")) return 1;
    expect(repaired->compiled, "missing header-unit IFC should schedule a repair compile");
    expect(has_reason(repaired->validation, mqb::BuildReason::missing_output),
           "missing header-unit IFC repair should report missing_output");
    expect(fs::is_regular_file(ifc), "missing-IFC repair should recreate the planned IFC");
    expect(!fs::exists(object), "missing-IFC repair must not create an object artifact");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_header_unit_incremental_integration_tests passed\n";
    return 0;
}
