#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
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

namespace compile_cache_evidence_cases { int run(const mqb::msvc::MsvcToolchain&, mqb::process::ProcessRunner&); }
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

[[nodiscard]] std::vector<mqb::FileSnapshot> snapshot_outputs(
    const mqb::TranslationUnit& unit) {
    std::vector<mqb::FileSnapshot> snapshots;
    snapshots.reserve(unit.outputs.size());
    for (const auto& output : unit.outputs) {
        snapshots.push_back(snapshot(output.path));
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
    const auto output_snapshots = snapshot_outputs(unit);
    const std::vector<mqb::FileSnapshot> dependency_snapshots = cached
        ? snapshot_all(cached->dependencies)
        : std::vector<mqb::FileSnapshot>{};

    return mqb::CompileCacheValidator::validate(
        unit,
        toolchain.identity,
        options,
        cached,
        source_snapshot,
        output_snapshots,
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
        .outputs = unit.outputs,
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

    // 3. Header mutation: explicitly construct dependency.mtime > object.mtime.
    const auto object_before_header_change = snapshot(object);
    expect(object_before_header_change.exists,
           "header invalidation fixture requires an existing object timestamp");
    write_text(header, "#pragma once\n#define HEADER_VALUE 6\n");
    std::error_code timestamp_error;
    fs::last_write_time(
        header,
        object_before_header_change.modified + std::chrono::seconds{2},
        timestamp_error);
    expect(!timestamp_error,
           "test should be able to set a deterministic newer header timestamp");

    cached = load_cache_or_none(cache_file);
    expect(cached.has_value(), "header invalidation requires persisted cache metadata");
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

    // The test intentionally moved the header timestamp into the future to make
    // invalidation deterministic. Move the rebuilt object beyond it as well so
    // the post-rebuild freshness assertion does not depend on wall-clock timing.
    const auto header_after_rebuild = snapshot(header);
    expect(header_after_rebuild.exists,
           "rebuilt freshness fixture requires the header timestamp");
    timestamp_error.clear();
    fs::last_write_time(
        object,
        header_after_rebuild.modified + std::chrono::seconds{1},
        timestamp_error);
    expect(!timestamp_error,
           "test should be able to set a deterministic newer object timestamp");

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

    failures += compile_cache_evidence_cases::run(toolchain, runner);
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_incremental_loop_tests passed\n";
    return 0;
}
// BEGIN MQB_COMPILE_CACHE_EVIDENCE_CASES
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

namespace compile_cache_evidence_cases {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    require(in.is_open(), "read retained bytes: " + utf8(path));
    std::string result{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    require(!in.bad(), "read retained bytes failed");
    return result;
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    out.close();
    require(static_cast<bool>(out), "write fixture/evidence: " + utf8(path));
}
std::string quoted(const fs::path& path) {
    std::string result;
    for (char c : utf8(path)) {
        if (c == '\\' || c == '"') result += '\\';
        result += c;
    }
    return result;
}

struct TraceRunner final : process::ProcessRunner {
    process::ProcessRunner& actual;
    fs::path evidence;
    unsigned calls{};
    TraceRunner(process::ProcessRunner& runner, fs::path where)
        : actual(runner), evidence(std::move(where)) {}
    std::expected<process::ProcessResult, process::ProcessError>
    run(const process::ProcessSpec& spec) override {
        const auto stem = evidence / ("process-" + std::to_string(++calls));
        std::ostringstream argv;
        argv << utf8(spec.executable) << '\n';
        for (const auto& arg : spec.arguments) argv << arg << '\n';
        // The controlled argv is retained; environment values are not exported.
        write(stem.string() + ".argv.txt", argv.str());
        auto result = actual.run(spec);
        if (result) {
            write(stem.string() + ".stdout.txt", result->stdout_text);
            write(stem.string() + ".stderr.txt", result->stderr_text);
            write(stem.string() + ".exit.txt", std::to_string(result->exit_code));
        } else {
            write(stem.string() + ".start-error.txt", result.error().message);
        }
        return result;
    }
};

struct MockRunner final : process::ProcessRunner {
    IncrementalCompileRequest request;
    fs::path header;
    unsigned calls{};
    bool fail{}, omit_output{}, bad_metadata{};
    std::expected<process::ProcessResult, process::ProcessError>
    run(const process::ProcessSpec&) override {
        ++calls;
        if (fail) return process::ProcessResult{.exit_code=37, .stderr_text="fixed mock failure"};
        if (!omit_output) for (const auto& out : request.unit.outputs) write(out.path, "mock output");
        write(request.source_dependencies_file, bad_metadata ? "broken dependency JSON" :
            "{\"Data\":{\"Source\":\"" + quoted(request.unit.source) +
            "\",\"Includes\":[\"" + quoted(header) + "\"]}}");
        return process::ProcessResult{.exit_code=0, .stdout_text="fixed mock success"};
    }
};

IncrementalCompileRequest ordinary(const fs::path& root) {
    fs::create_directories(root / "src");
    fs::create_directories(root / "out");
    write(root / "src/value.hpp", "#pragma once\n#define CACHE_EVIDENCE_VALUE 7\n");
    write(root / "src/unit.cpp", "#include \"value.hpp\"\nint answer(){return CACHE_EVIDENCE_VALUE;}\n");
    IncrementalCompileRequest request;
    request.unit.source = root / "src/unit.cpp";
    request.unit.outputs = {{root / "out/unit.obj", ArtifactKind::object}};
    request.cache_file = root / "out/unit.mqbcache";
    request.source_dependencies_file = root / "out/unit.deps.json";
    request.working_directory = root;
    request.options.include_directories = {root / "src"};
#ifdef NDEBUG
    request.options.configuration = BuildConfiguration::release;
#else
    request.options.configuration = BuildConfiguration::debug;
#endif
    return request;
}

bool warning(const IncrementalCompileResult& result, IncrementalCompileWarningCode code) {
    return std::any_of(result.warnings.begin(), result.warnings.end(),
        [code](const auto& value) { return value.code == code; });
}
void keep(const fs::path& evidence, const std::string& label,
          const std::expected<RecordedIncrementalCompileResult, IncrementalCompileError>& result) {
    if (!result) {
        write(evidence / (label + ".error.txt"),
            std::to_string(static_cast<int>(result.error().code)) + "\n" + result.error().message);
        return;
    }
    const auto& record = result->record;
    const auto saved = CompileCacheFile::save(evidence / (label + ".captured.cache"), record.cache_entry);
    require(saved.has_value(), "serialize retained value with the original codec");
    std::ostringstream selected;
    selected << "compiled=" << result->result.compiled << "\nstate=" << static_cast<int>(record.state)
        << "\nsource=" << utf8(record.request.unit.source) << "\nkind=" << static_cast<int>(record.request.unit.kind)
        << "\nconfiguration=" << static_cast<int>(record.request.options.configuration)
        << "\nmodule_scan=" << record.cache_entry.module_scan.has_value()
        << "\noutputs=" << record.cache_entry.outputs.size()
        << "\nsave_error=" << record.save_error.has_value() << '\n';
    if (record.save_error) selected << "save_error_code=" << static_cast<int>(record.save_error->code)
        << "\nsave_error_file=" << utf8(record.save_error->file) << "\nsave_error_offset=" << record.save_error->offset
        << "\nsave_error_message=" << record.save_error->message << '\n';
    for (const auto& w : result->result.warnings)
        selected << "warning=" << static_cast<int>(w.code) << ':' << w.message << '\n';
    write(evidence / (label + ".record.txt"), selected.str());
    if (record.state != CompileCacheEvidenceState::save_failed) {
        // This read belongs only to the independent test oracle, not the API.
        require(bytes(evidence / (label + ".captured.cache")) == bytes(record.request.cache_file),
            "captured complete cache value equals the accepted/saved serialized entry");
    }
}

void deterministic(const fs::path& root, const fs::path& evidence) {
    auto request = ordinary(root);
    msvc::MsvcToolchain toolchain{
        .identity={.compiler=root / "cl.exe", .version="evidence mock", .binary_stamp="fixed"}};
    write(toolchain.identity.compiler, "not an executable; mock only");
    toolchain.environment = {{"MOCK_CONTEXT", "one"}, {"MOCK_CONTEXT", "two"}};
    request.options.defines = {"DUP=1", "DUP=1"};
    MockRunner runner;
    runner.request = request; runner.header = root / "src/value.hpp";
    msvc::MsvcCompileExecutor executor{toolchain, runner};
    MsvcIncrementalCompileCoordinator coordinator{toolchain, executor};
    auto first = coordinator.run_recorded(request); keep(evidence, "01-cold", first);
    require(first && first->result.compiled && runner.calls == 1, "one mock cold execution");
    const auto original_value = bytes(evidence / "01-cold.captured.cache");
    auto seeded = first->record.cache_entry;
    seeded.dependencies = {runner.header, runner.header};
    require(CompileCacheFile::save(request.cache_file, seeded).has_value(), "seed duplicate accepted dependencies");
    fs::last_write_time(request.unit.outputs.front().path,
        fs::file_time_type::clock::now() + std::chrono::seconds{20});
    const auto warm_bytes = bytes(request.cache_file);
    const auto warm_time = fs::last_write_time(request.cache_file);
    auto warm = coordinator.run_recorded(request); keep(evidence, "02-reuse-duplicates", warm);
    require(warm && !warm->result.compiled && !warm->result.process && runner.calls == 1,
        "accepted reuse does not launch a mock compiler");
    require(warm->record.state == CompileCacheEvidenceState::reused && !warm->record.save_error,
        "reuse does not pretend to save");
    require(warm->record.cache_entry.dependencies == seeded.dependencies,
        "accepted duplicate dependency order is not reconstructed from the request");
    require(bytes(request.cache_file) == warm_bytes && fs::last_write_time(request.cache_file) == warm_time,
        "reuse preserves cache bytes and timestamp");
    require(warm->record.request.options.defines == request.options.defines &&
        warm->record.inspection_toolchain.environment.size() == 2 &&
        warm->record.inspection_toolchain.environment[0].value == "one" &&
        warm->record.inspection_toolchain.environment[1].value == "two", "ordered owning context retained in memory");
    request.options.defines.clear(); toolchain.environment[0].value = "changed";
    require(first->record.request.options.defines.size() == 2 &&
        first->record.inspection_toolchain.environment[0].value == "one", "later caller mutation cannot rewrite history");
    auto altered = warm->record; altered.cache_entry.dependencies.clear();
    require(warm->record.cache_entry.dependencies.size() == 2, "returned cache values own their vectors");

    request.force_rebuild = true; runner.request = request; runner.fail = true;
    const auto failed = coordinator.run_recorded(request); keep(evidence, "03-compiler-failure", failed);
    const auto plain_failed = coordinator.run(request);
    require(!failed && !plain_failed && failed.error().code == plain_failed.error().code &&
        failed.error().message == plain_failed.error().message && failed.error().compile_error &&
        failed.error().compile_error->compiler_error &&
        failed.error().compile_error->compiler_error->process_result &&
        failed.error().compile_error->compiler_error->process_result->exit_code == 37,
        "recorded failure retains original nested compiler result");
    require(!fs::exists(evidence / "03-compiler-failure.captured.cache"), "failure manufactures no success record");
    runner.fail = false; runner.omit_output = true;
    request.unit.outputs[0].path = root / "out/missing.obj"; runner.request = request;
    auto missing = coordinator.run_recorded(request); keep(evidence, "04-missing-output", missing);
    require(!missing && missing.error().compile_error &&
        missing.error().compile_error->code == msvc::CompileExecutorErrorCode::output_missing,
        "tool success without an output has no successful record");
    runner.omit_output = false; runner.bad_metadata = true;
    auto bad = coordinator.run_recorded(request); keep(evidence, "05-bad-metadata", bad);
    require(!bad && bad.error().compile_error &&
        bad.error().compile_error->code == msvc::CompileExecutorErrorCode::dependency_metadata_failed,
        "invalid dependency metadata has no successful record");
    require(CompileCacheFile::save(evidence / "01-cold-after-mutations.cache", first->record.cache_entry).has_value(),
        "serialize historical owned value after failures");
    require(bytes(evidence / "01-cold-after-mutations.cache") == original_value && runner.calls == 5,
        "old record survives later errors; fixed mock process count");
    write(evidence / "completed.txt", "6 recorded/default API calls; 5 mock processes; no native compiler here\n");
}

void native(const msvc::MsvcToolchain& toolchain, process::ProcessRunner& actual,
            const fs::path& root, const fs::path& evidence) {
    TraceRunner runner{actual, evidence};
    msvc::MsvcCompileExecutor executor{toolchain, runner};
    MsvcIncrementalCompileCoordinator coordinator{toolchain, executor};
    unsigned api_calls{};
    const auto invoke = [&](const std::string& label, const IncrementalCompileRequest& request) {
        ++api_calls;
        auto result = coordinator.run_recorded(request);
        keep(evidence, label, result);
        return result;
    };
    auto request = ordinary(root / "ordinary");
    auto cold = invoke("01-ordinary-cold", request);
    require(cold && cold->result.compiled && cold->record.state == CompileCacheEvidenceState::saved,
        "real ordinary cold entry captured");
    const auto old_bytes = bytes(evidence / "01-ordinary-cold.captured.cache");
    const auto cache_time = fs::last_write_time(request.cache_file);
    const auto object_time = fs::last_write_time(request.unit.outputs[0].path);
    const auto count = runner.calls;
    auto warm = invoke("02-ordinary-reuse", request);
    require(warm && !warm->result.compiled && !warm->result.process && runner.calls == count,
        "real ordinary reuse launches zero processes");
    require(fs::last_write_time(request.cache_file) == cache_time &&
        fs::last_write_time(request.unit.outputs[0].path) == object_time, "real reuse does not rewrite files");
    request.force_rebuild = true;
    auto forced = invoke("03-ordinary-forced", request);
    require(forced && forced->result.compiled && runner.calls == count + 1, "forced compile exactly once");
    request.force_rebuild = false;
    write(request.cache_file, "retained deliberately corrupt cache\n");
    fs::copy_file(request.cache_file, evidence / "04-corrupt-input.bin");
    auto repaired = invoke("04-corrupt-cache", request);
    require(repaired && repaired->result.compiled &&
        warning(repaired->result, IncrementalCompileWarningCode::cache_load_failed), "corrupt cache warning retained");
    auto refused = request;
    refused.force_rebuild = true; refused.cache_file = root / "ordinary/out/cache-directory";
    fs::create_directory(refused.cache_file);
    auto save_failed = invoke("05-save-failed", refused);
    require(save_failed && save_failed->result.compiled && save_failed->record.save_error &&
        save_failed->record.state == CompileCacheEvidenceState::save_failed &&
        warning(save_failed->result, IncrementalCompileWarningCode::cache_save_failed),
        "successful compile retains exact entry and typed save failure");
    write(request.unit.source, "static_assert(false, \"MQB_CACHE_EVIDENCE_EXPECTED_FAILURE\");\n");
    request.force_rebuild = true;
    auto failed = invoke("06-compiler-failure", request);
    require(!failed && failed.error().code == IncrementalCompileErrorCode::compile_failed &&
        failed.error().compile_error && failed.error().compile_error->compiler_error &&
        failed.error().compile_error->compiler_error->process_result &&
        failed.error().compile_error->compiler_error->process_result->exit_code != 0,
        "original real compiler failure retained; no retry");
    require(CompileCacheFile::save(evidence / "01-historical-after-failure.cache", cold->record.cache_entry).has_value(),
        "retain old record after same-path overwrite and failure");
    require(bytes(evidence / "01-historical-after-failure.cache") == old_bytes, "historical value is independent");

    auto pch = ordinary(root / "pch");
    pch.options.precompiled_header = PrecompiledHeaderBinding{
        .header="value.hpp", .artifact=root / "pch/out/unit.pch", .role=PrecompiledHeaderRole::create};
    pch.unit.outputs.push_back({pch.options.precompiled_header->artifact, ArtifactKind::precompiled_header});
    auto pch_cold = invoke("07-pch-cold", pch);
    require(pch_cold && pch_cold->result.compiled && pch_cold->record.cache_entry.outputs.size() == 2 &&
        !pch_cold->record.cache_entry.module_scan, "real PCH creator captures paired outputs without scan evidence");
    const auto after_pch = runner.calls;
    auto pch_warm = invoke("08-pch-reuse", pch);
    require(pch_warm && !pch_warm->result.compiled && runner.calls == after_pch, "PCH exact accepted reuse");

    auto hu = ordinary(root / "header-unit");
    hu.unit.source = root / "header-unit/src/value.hpp";
    hu.unit.header_unit = HeaderUnitIdentity{"value.hpp", HeaderUnitLookupMethod::quote};
    hu.unit.outputs = {{root / "header-unit/out/value.ifc", ArtifactKind::module_interface}};
    auto hu_cold = invoke("09-header-unit-cold", hu);
    require(hu_cold && hu_cold->result.compiled && hu_cold->record.cache_entry.outputs.size() == 1 &&
        !hu_cold->record.cache_entry.module_scan && hu_cold->record.request.unit.header_unit,
        "real header-unit captures IFC only and explicit header identity");
    const auto after_hu = runner.calls;
    auto hu_warm = invoke("10-header-unit-reuse", hu);
    require(hu_warm && !hu_warm->result.compiled && runner.calls == after_hu, "header-unit exact accepted reuse");

    auto module = ordinary(root / "module");
    module.unit.source = root / "module/src/value.ixx";
    module.unit.kind = TranslationUnitKind::module_interface;
    write(module.unit.source, "export module cache_evidence_value;\nexport int value(){return 7;}\n");
    module.unit.outputs.push_back({root / "module/out/value.ifc", ArtifactKind::module_interface});
    module.module_scan_output = root / "module/out/value.scan.json";
    msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    const auto scanned = scanner.scan(msvc::ModuleScanInvocation{
        .source=module.unit.source, .output_file=*module.module_scan_output,
        .options=module.options, .kind=module.unit.kind, .working_directory=module.working_directory});
    require(scanned.has_value(), "one real named-module scan before compilation");
    fs::copy_file(*module.module_scan_output, evidence / "11-original-scan.json");
    auto module_cold = invoke("11-module-cold", module);
    require(module_cold && module_cold->result.compiled && module_cold->record.cache_entry.module_scan &&
        module_cold->record.cache_entry.module_scan->output.path == *module.module_scan_output,
        "entry captured after successful real module evidence sealing");
    const auto after_module = runner.calls;
    auto module_warm = invoke("12-module-reuse", module);
    require(module_warm && !module_warm->result.compiled && runner.calls == after_module &&
        module_warm->record.cache_entry.module_scan, "accepted warm module retains sealed scan evidence");
    // Deterministic stale-scan boundary, not another timing experiment.
    fs::last_write_time(module.unit.source, fs::last_write_time(*module.module_scan_output) + std::chrono::seconds{2});
    module.force_rebuild = true;
    auto stale_scan = invoke("13-module-stale-scan", module);
    require(stale_scan && stale_scan->result.compiled && !stale_scan->record.cache_entry.module_scan &&
        module_cold->record.cache_entry.module_scan, "stale scan is omitted without rewriting the earlier record");
    require(api_calls == 13 && runner.calls == 10, "fixed 13 recorded calls, 9 compiles and 1 scan; no links or retries");
    write(evidence / "completed.txt", "13 recorded calls; 12 successes; 1 expected failure; 9 compile processes; 1 scan; 0 links\n");
}

int run(const msvc::MsvcToolchain& toolchain, process::ProcessRunner& runner) {
    static_assert(CompileCacheEvidence::exact_cache_entry_captured);
    static_assert(!CompileCacheEvidence::producer_identity_verified);
    static_assert(!CompileCacheEvidence::current_content_verified);
    static_assert(!CompileCacheEvidence::complete_producer_inventory);
    static_assert(!CompileCacheEvidence::deletion_authorized);
    const auto root = fs::current_path() / "storage-fixtures/compile-cache-evidence";
    const auto evidence = fs::current_path() / "storage-evidence/compile-cache-evidence";
    try {
        require(!fs::exists(root) && !fs::exists(evidence), "fresh evidence directories required; never overwrite a previous run");
        fs::create_directories(evidence);
        deterministic(root / "mock", evidence / "mock");
        native(toolchain, runner, root / "native", evidence / "native");
        write(evidence / "completed.txt", "compile-cache evidence native and deterministic contracts passed\n");
        std::cout << "compile-cache evidence: 13 real recorded calls and deterministic controls passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: compile-cache evidence: " << error.what() << '\n';
        // Leave all partial fixtures and process output for the existing always-upload step.
        return 1;
    }
}
} // namespace compile_cache_evidence_cases
// END MQB_COMPILE_CACHE_EVIDENCE_CASES
