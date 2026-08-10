#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"
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

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-incremental-loop-" + std::to_string(tick));
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

[[nodiscard]] mqb::FileSnapshot snapshot(const fs::path& path) {
    std::error_code error_code;
    const bool exists = fs::is_regular_file(path, error_code);
    if (error_code || !exists) {
        return mqb::FileSnapshot{
            .path = path,
            .exists = false,
        };
    }

    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        return mqb::FileSnapshot{
            .path = path,
            .exists = false,
        };
    }

    return mqb::FileSnapshot{
        .path = path,
        .exists = true,
        .modified = modified,
    };
}

[[nodiscard]] std::vector<mqb::FileSnapshot> snapshot_all(
    const std::vector<fs::path>& paths) {
    std::vector<mqb::FileSnapshot> snapshots;
    snapshots.reserve(paths.size());
    for (const auto& path : paths) {
        snapshots.push_back(snapshot(path));
    }
    return snapshots;
}

[[nodiscard]] bool has_reason(
    const mqb::CompileCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

[[nodiscard]] std::optional<mqb::CompileCacheEntry> load_cache_or_none(
    const fs::path& cache_file) {
    const auto loaded = mqb::CompileCacheFile::load(cache_file);
    expect(loaded.has_value(), "cache metadata file should load without corruption");
    if (!loaded) {
        std::cerr << "cache load error: " << loaded.error().message << '\n';
        return std::nullopt;
    }
    return std::move(*loaded);
}

[[nodiscard]] mqb::CompileCacheValidation validate_current(
    const mqb::TranslationUnit& unit,
    const mqb::msvc::MsvcToolchain& toolchain,
    const mqb::CompilerOptions& options,
    const std::optional<mqb::CompileCacheEntry>& cached) {
    const auto source_snapshot = snapshot(unit.source);
    const auto object_snapshot = snapshot(unit.outputs.front().path);
    const std::vector<mqb::FileSnapshot> dependency_snapshots = cached
        ? snapshot_all(cached->dependencies)
        : std::vector<mqb::FileSnapshot>{};

    return mqb::CompileCacheValidator::validate(
        unit,
        toolchain.identity,
        options,
        cached,
        source_snapshot,
        object_snapshot,
        dependency_snapshots);
}

[[nodiscard]] bool compile_and_refresh_cache(
    const mqb::TranslationUnit& unit,
    const mqb::msvc::MsvcToolchain& toolchain,
    const mqb::CompilerOptions& options,
    const fs::path& dependency_json,
    const fs::path& cache_file,
    const fs::path& working_directory,
    mqb::process::ProcessRunner& runner) {
    mqb::msvc::CompileInvocation invocation;
    invocation.source = unit.source;
    invocation.object = unit.outputs.front().path;
    invocation.source_dependencies = dependency_json;
    invocation.options = options;
    invocation.working_directory = working_directory;

    mqb::msvc::MsvcCompiler compiler{toolchain, runner};
    const auto compiled = compiler.compile(invocation);
    expect(compiled.has_value(), "planned compile action should succeed with real cl.exe");
    if (!compiled) {
        std::cerr << "compile error: " << compiled.error().message << '\n';
        if (compiled.error().process_result) {
            std::cerr << compiled.error().process_result->stdout_text;
            std::cerr << compiled.error().process_result->stderr_text;
        }
        return false;
    }

    const auto dependencies = mqb::msvc::MsvcSourceDependenciesReader::read(dependency_json);
    expect(dependencies.has_value(), "successful compile should produce parseable dependency metadata");
    if (!dependencies) {
        std::cerr << "dependency parse error: " << dependencies.error().message << '\n';
        return false;
    }

    const mqb::CompileCacheEntry refreshed{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain.identity,
        .signature = mqb::BuildSignature::for_compile(unit, toolchain.identity, options),
        .object = unit.outputs.front(),
        .dependencies = dependencies->includes,
    };

    const auto saved = mqb::CompileCacheFile::save(cache_file, refreshed);
    expect(saved.has_value(), "successful compile should persist refreshed cache metadata");
    if (!saved) {
        std::cerr << "cache save error: " << saved.error().message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};

    mqb::msvc::DiscoveryOptions discovery;
    discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    discovery.target_architecture = mqb::Architecture::x64;
    discovery.host_architecture = mqb::Architecture::x64;

    const auto toolchain_result = locator.discover(discovery);
    expect(toolchain_result.has_value(), "incremental integration requires installed MSVC discovery");
    if (!toolchain_result) {
        std::cerr << "toolchain error: " << toolchain_result.error().message << '\n';
        return 1;
    }
    const auto& toolchain = *toolchain_result;

    TemporaryDirectory fixture;
    const fs::path include_dir = fixture.path() / "include dir";
    const fs::path header = include_dir / "value.hpp";
    const fs::path source = fixture.path() / "src" / "main file.cpp";
    const fs::path object = fixture.path() / "cache" / "main file.obj";
    const fs::path dependency_json = fixture.path() / "cache" / "main file.deps.json";
    const fs::path cache_file = fixture.path() / "cache" / "main file.mqbcache";

    write_text(header, "#pragma once\n#define HEADER_VALUE 5\n");
    write_text(
        source,
        "#include \"value.hpp\"\n"
        "#ifndef MQB_VALUE\n#error MQB_VALUE missing\n#endif\n"
        "static_assert(MQB_VALUE == 7);\n"
        "int answer() { return MQB_VALUE + HEADER_VALUE; }\n");

    mqb::TranslationUnit unit;
    unit.source = source;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{object, mqb::ArtifactKind::object},
    };

    mqb::CompilerOptions debug_options;
    debug_options.configuration = mqb::BuildConfiguration::debug;
    debug_options.architecture = mqb::Architecture::x64;
    debug_options.standard = mqb::CppStandard::cpp23;
    debug_options.defines = {"MQB_VALUE=7"};
    debug_options.include_directories = {include_dir};

    // 1. Cold build: no metadata or object => planner must schedule exactly one compile.
    auto cached = load_cache_or_none(cache_file);
    expect(!cached.has_value(), "cold build should start without cache metadata");
    const auto cold_validation = validate_current(unit, toolchain, debug_options, cached);
    expect(has_reason(cold_validation, mqb::BuildReason::missing_cache_entry),
           "cold build should explain missing cache metadata");
    expect(has_reason(cold_validation, mqb::BuildReason::missing_output),
           "cold build should explain missing object output");

    const std::vector<mqb::CompilePlanItem> cold_items{{unit, cold_validation}};
    const auto cold_plan = mqb::BuildPlanner::plan_compile(cold_items);
    expect(cold_plan.has_value() && cold_plan->size() == 1,
           "cold build should produce one compile action");
    if (!cold_plan || cold_plan->size() != 1) {
        return 1;
    }
    if (!compile_and_refresh_cache(
            unit,
            toolchain,
            debug_options,
            dependency_json,
            cache_file,
            fixture.path(),
            runner)) {
        return 1;
    }

    // 2. Warm build: freshly persisted metadata plus older inputs => no action.
    cached = load_cache_or_none(cache_file);
    expect(cached.has_value(), "first successful compile should create cache metadata");
    const auto warm_validation = validate_current(unit, toolchain, debug_options, cached);
    expect(warm_validation.reusable(), "unchanged warm build should reuse the cached object");
    const std::vector<mqb::CompilePlanItem> warm_items{{unit, warm_validation}};
    const auto warm_plan = mqb::BuildPlanner::plan_compile(warm_items);
    expect(warm_plan.has_value() && warm_plan->empty(),
           "unchanged warm build should produce zero compile actions");

    // 3. Header mutation: dependency snapshot becomes newer than object.
    std::this_thread::sleep_for(std::chrono::milliseconds{1100});
    write_text(header, "#pragma once\n#define HEADER_VALUE 6\n");
    cached = load_cache_or_none(cache_file);
    const auto header_validation = validate_current(unit, toolchain, debug_options, cached);
    expect(!header_validation.reusable(), "header modification must invalidate the cached object");
    expect(has_reason(header_validation, mqb::BuildReason::dependency_changed),
           "header modification should report dependency_changed");
    const std::vector<mqb::CompilePlanItem> header_items{{unit, header_validation}};
    const auto header_plan = mqb::BuildPlanner::plan_compile(header_items);
    expect(header_plan.has_value() && header_plan->size() == 1,
           "stale header should schedule exactly one compile");

    // Source remains valid after the header value changes, so rebuild and refresh metadata.
    if (!compile_and_refresh_cache(
            unit,
            toolchain,
            debug_options,
            dependency_json,
            cache_file,
            fixture.path(),
            runner)) {
        return 1;
    }

    cached = load_cache_or_none(cache_file);
    const auto refreshed_validation = validate_current(unit, toolchain, debug_options, cached);
    expect(refreshed_validation.reusable(),
           "cache should become reusable again after dependency-triggered rebuild");

    // 4. Debug -> Release without any source changes must still rebuild by recipe identity.
    auto release_options = debug_options;
    release_options.configuration = mqb::BuildConfiguration::release;
    const auto release_validation = validate_current(unit, toolchain, release_options, cached);
    expect(!release_validation.reusable(),
           "Debug to Release transition must invalidate compile cache without touching sources");
    expect(has_reason(release_validation, mqb::BuildReason::compiler_options_changed),
           "configuration transition should report compiler_options_changed");
    const std::vector<mqb::CompilePlanItem> release_items{{unit, release_validation}};
    const auto release_plan = mqb::BuildPlanner::plan_compile(release_items);
    expect(release_plan.has_value() && release_plan->size() == 1,
           "configuration transition should schedule one compile action");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_incremental_loop_tests passed\n";
    return 0;
}
