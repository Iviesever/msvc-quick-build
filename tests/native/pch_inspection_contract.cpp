#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"
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

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("mqb-pch-inspection-test-" + std::to_string(tick));
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
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::string read_text(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string result;
    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

[[nodiscard]] bool has_reason(
    const mqb::CompileCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

class PchCompilerRunner final : public mqb::process::ProcessRunner {
public:
    fs::path source;
    fs::path header;
    fs::path object;
    fs::path dependencies;
    fs::path pch;
    int calls{};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;

        write_text(object, "PCH creator object");
        write_text(pch, "PCH artifact");
        const std::string json =
            "{\"Data\":{\"Source\":\""
            + json_escape(path_to_utf8(source))
            + "\",\"Includes\":[\""
            + json_escape(path_to_utf8(header))
            + "\"]}}";
        write_text(dependencies, json);

        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "PCH compile success",
        };
    }

    mqb::process::ProcessSpec last_spec;
};

} // namespace

int main() {
    TemporaryDirectory fixture;

    const fs::path fake_compiler = fixture.path() / "toolchain" / "cl.exe";
    write_text(fake_compiler, "compiler identity");

    const mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = fake_compiler,
            .version = "19.51.pch-inspect",
            .binary_stamp = "compiler-stamp",
        },
        .linker = fixture.path() / "toolchain" / "link.exe",
        .librarian = fixture.path() / "toolchain" / "lib.exe",
        .vc_tools_root = fixture.path() / "toolchain",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    const fs::path header = fixture.path() / "input" / "include" / "pch.hpp";
    write_text(header, "#pragma once\ninline constexpr int pch_value = 42;\n");

    const fs::path state_root = fixture.path() / "owned-state" / "pch";
    const mqb::PrecompiledHeaderArtifacts artifacts{
        .source = state_root / "creator.cpp",
        .object = state_root / "creator.obj",
        .dependencies = state_root / "creator.deps.json",
        .precompiled_header = state_root / "project.pch",
        .compile_cache = state_root / "creator.cache",
    };

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;

    PchCompilerRunner runner;
    runner.source = artifacts.source;
    runner.header = header;
    runner.object = artifacts.object;
    runner.dependencies = artifacts.dependencies;
    runner.pch = artifacts.precompiled_header;

    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        toolchain,
        executor};
    mqb::orchestration::MsvcIncrementalPchCoordinator pch_coordinator{
        compile_coordinator};

    const mqb::orchestration::IncrementalPchRequest request{
        .header = header,
        .artifacts = artifacts,
        .compiler_options = options,
        .working_directory = fixture.path(),
    };

    const auto cold_inspection = pch_coordinator.inspect(request);
    expect(cold_inspection.has_value(), "cold PCH inspection should succeed");
    if (cold_inspection) {
        expect(cold_inspection->creator_source_materialization_required,
               "cold PCH inspection should report missing synthetic creator materialization");
        expect(cold_inspection->compile.plan.actions.size() == 1
                   && std::holds_alternative<mqb::CompileAction>(
                       cold_inspection->compile.plan.actions.front()),
               "cold PCH inspection should expose one creator CompileAction");
        expect(has_reason(
                   cold_inspection->compile.validation,
                   mqb::BuildReason::missing_cache_entry),
               "cold PCH inspection should report missing creator cache");
        expect(has_reason(
                   cold_inspection->compile.validation,
                   mqb::BuildReason::missing_output),
               "cold PCH inspection should report missing creator outputs");
        expect(cold_inspection->compile_request.unit.source == artifacts.source,
               "PCH inspection should expose the exact synthetic creator source path");
        expect(cold_inspection->compile_request.source_dependencies_file == artifacts.dependencies,
               "PCH inspection should expose the exact sourceDependencies path");
        expect(cold_inspection->compile_request.options.precompiled_header.has_value(),
               "PCH inspection should project a typed PCH creator binding");
        if (cold_inspection->compile_request.options.precompiled_header) {
            const auto& binding = *cold_inspection->compile_request.options.precompiled_header;
            expect(binding.role == mqb::PrecompiledHeaderRole::create,
                   "PCH inspection binding should use create role");
            expect(binding.header == header.lexically_normal(),
                   "PCH inspection binding should retain the configured header");
            expect(binding.artifact == artifacts.precompiled_header.lexically_normal(),
                   "PCH inspection binding should retain the owned .pch artifact");
        }

        const auto recipe = executor.build_recipe(cold_inspection->compile_request);
        expect(recipe.has_value(),
               "PCH inspection compile request should produce an exact recipe before creator materialization");
        expect(!fs::exists(state_root),
               "PCH recipe modeling after inspection must still leave owned state absent");
    }
    expect(runner.calls == 0,
           "cold PCH inspection must not launch cl.exe");
    expect(!fs::exists(state_root),
           "cold PCH inspection must not create PCH directories or synthetic source state");

    const auto cold_run = pch_coordinator.run(request);
    expect(cold_run.has_value() && cold_run->compile.compiled,
           "cold PCH run should materialize the creator and compile");
    if (cold_inspection && cold_run) {
        expect(cold_run->compile.validation.reasons
                   == cold_inspection->compile.validation.reasons,
               "cold PCH run and inspection should expose identical rebuild reasons");
        expect(cold_run->compile.plan.actions.size()
                   == cold_inspection->compile.plan.actions.size(),
               "cold PCH run and inspection should expose identical plan cardinality");
    }
    expect(runner.calls == 1,
           "cold PCH run should launch cl.exe exactly once");
    expect(fs::is_regular_file(artifacts.source)
               && fs::is_regular_file(artifacts.object)
               && fs::is_regular_file(artifacts.dependencies)
               && fs::is_regular_file(artifacts.precompiled_header)
               && fs::is_regular_file(artifacts.compile_cache),
           "cold PCH run should materialize creator, outputs, dependency metadata, and cache");
    expect(read_text(artifacts.source).find("MQB synthetic precompiled-header creator")
               != std::string::npos,
           "cold PCH run should materialize the canonical MQB creator source");

    const std::string warm_source_before = read_text(artifacts.source);
    const std::string warm_object_before = read_text(artifacts.object);
    const std::string warm_pch_before = read_text(artifacts.precompiled_header);
    const std::string warm_cache_before = read_text(artifacts.compile_cache);
    const auto warm_inspection = pch_coordinator.inspect(request);
    expect(warm_inspection.has_value(), "warm PCH inspection should succeed");
    if (warm_inspection) {
        expect(!warm_inspection->creator_source_materialization_required,
               "warm PCH inspection should recognize the canonical creator source");
        expect(warm_inspection->compile.validation.reusable(),
               "warm PCH inspection should report reusable compile state");
        expect(warm_inspection->compile.plan.empty(),
               "warm PCH inspection should expose an empty compile plan");
    }
    expect(runner.calls == 1,
           "warm PCH inspection must not launch cl.exe");
    expect(read_text(artifacts.source) == warm_source_before
               && read_text(artifacts.object) == warm_object_before
               && read_text(artifacts.precompiled_header) == warm_pch_before
               && read_text(artifacts.compile_cache) == warm_cache_before,
           "warm PCH inspection must not mutate creator, outputs, or cache");

    const auto warm_run = pch_coordinator.run(request);
    expect(warm_run.has_value() && !warm_run->compile.compiled,
           "warm PCH run should remain a no-op");
    expect(runner.calls == 1,
           "warm PCH run should not launch cl.exe");
    expect(read_text(artifacts.source) == warm_source_before,
           "warm PCH run should not rewrite the canonical creator source");

    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write_text(header, "#pragma once\ninline constexpr int pch_value = 43;\n");
    const auto header_inspection = pch_coordinator.inspect(request);
    expect(header_inspection.has_value(), "header-changed PCH inspection should succeed");
    if (header_inspection) {
        expect(!header_inspection->creator_source_materialization_required,
               "header change should not require creator-source repair");
        expect(has_reason(
                   header_inspection->compile.validation,
                   mqb::BuildReason::dependency_changed),
               "PCH header change should surface dependency_changed");
        expect(header_inspection->compile.plan.actions.size() == 1,
               "PCH header change should plan one creator compile");
    }
    expect(runner.calls == 1,
           "header-change inspection must not launch cl.exe");

    const auto header_run = pch_coordinator.run(request);
    expect(header_run.has_value() && header_run->compile.compiled,
           "PCH header change should execute creator compilation");
    if (header_inspection && header_run) {
        expect(header_run->compile.validation.reasons
                   == header_inspection->compile.validation.reasons,
               "PCH header-change run and inspection should expose identical reasons");
    }
    expect(runner.calls == 2,
           "PCH header change should launch cl.exe exactly once more");

    const auto creator_timestamp = fs::last_write_time(artifacts.source);
    const std::string cache_before_corruption = read_text(artifacts.compile_cache);
    const std::string pch_before_corruption = read_text(artifacts.precompiled_header);
    write_text(artifacts.source, "// tampered MQB-owned creator\nint injected = 1;\n");
    std::error_code timestamp_error;
    fs::last_write_time(artifacts.source, creator_timestamp, timestamp_error);
    expect(!timestamp_error,
           "test should restore the creator timestamp to simulate stale content hidden from mtime freshness");

    const auto corrupt_inspection = pch_coordinator.inspect(request);
    expect(corrupt_inspection.has_value(), "corrupt creator PCH inspection should succeed");
    if (corrupt_inspection) {
        expect(corrupt_inspection->creator_source_materialization_required,
               "corrupt creator content should require materialization repair");
        expect(has_reason(
                   corrupt_inspection->compile.validation,
                   mqb::BuildReason::source_changed),
               "corrupt creator content should surface semantic source_changed");
        expect(corrupt_inspection->compile.plan.actions.size() == 1,
               "corrupt creator content should plan one creator compile even with an old timestamp");
    }
    expect(runner.calls == 2,
           "corrupt creator inspection must not launch cl.exe");
    expect(read_text(artifacts.source).find("tampered") != std::string::npos,
           "corrupt creator inspection must not repair the file while observing it");
    expect(read_text(artifacts.compile_cache) == cache_before_corruption
               && read_text(artifacts.precompiled_header) == pch_before_corruption,
           "corrupt creator inspection must not mutate cache or .pch output");

    const auto repaired = pch_coordinator.run(request);
    expect(repaired.has_value() && repaired->compile.compiled,
           "corrupt creator run should repair and recompile the PCH");
    if (repaired) {
        expect(has_reason(
                   repaired->compile.validation,
                   mqb::BuildReason::source_changed),
               "creator repair execution should preserve semantic source_changed diagnostics");
    }
    expect(runner.calls == 3,
           "creator repair should launch cl.exe exactly once");
    expect(read_text(artifacts.source).find("tampered") == std::string::npos
               && read_text(artifacts.source).find("MQB synthetic precompiled-header creator")
                   != std::string::npos,
           "creator repair should restore canonical MQB-owned source content");

    const auto final_warm = pch_coordinator.inspect(request);
    expect(final_warm.has_value(), "post-repair warm PCH inspection should succeed");
    if (final_warm) {
        expect(!final_warm->creator_source_materialization_required,
               "post-repair creator should be canonical");
        expect(final_warm->compile.validation.reusable()
                   && final_warm->compile.plan.empty(),
               "post-repair PCH state should immediately return to reusable no-op");
    }
    expect(runner.calls == 3,
           "post-repair inspection must remain execution-free");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_pch_inspection_contract passed\n";
    return 0;
}
