#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

#include "../../../src/orchestration/incremental/IncrementalFileSnapshot.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path utf8_path(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes}.lexically_normal();
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

void update_max(std::atomic<int>& maximum, const int value) {
    int observed = maximum.load(std::memory_order_relaxed);
    while (observed < value
           && !maximum.compare_exchange_weak(
               observed,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("mqb-target-parallel-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

class TargetToolRunner final : public mqb::process::ProcessRunner {
public:
    TargetToolRunner(
        fs::path compiler,
        fs::path linker,
        const std::size_t synchronized_wave,
        fs::path shared_dependency = {})
        : compiler_(std::move(compiler)),
          linker_(std::move(linker)),
          shared_dependency_(std::move(shared_dependency)),
          synchronized_wave_(synchronized_wave),
          wave_gate_(static_cast<std::ptrdiff_t>(synchronized_wave)) {}

    void fail_sources(std::set<std::string> names) {
        std::scoped_lock lock{mutex_};
        fail_source_names_ = std::move(names);
    }

    [[nodiscard]] int compile_calls() const noexcept {
        return compile_calls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int link_calls() const noexcept {
        return link_calls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int maximum_active_compiles() const noexcept {
        return maximum_active_.load(std::memory_order_relaxed);
    }

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        if (spec.executable.lexically_normal() == compiler_.lexically_normal()) {
            return compile(spec);
        }
        if (spec.executable.lexically_normal() == linker_.lexically_normal()) {
            return link(spec);
        }
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "unexpected fake tool executable",
        });
    }

private:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    compile(const mqb::process::ProcessSpec& spec) {
        fs::path object;
        fs::path dependencies;
        fs::path source;
        for (std::size_t index = 0; index < spec.arguments.size(); ++index) {
            const std::string& argument = spec.arguments[index];
            if (argument.starts_with("/Fo")) {
                object = utf8_path(std::string_view{argument}.substr(3));
            } else if (argument == "/sourceDependencies"
                       && index + 1 < spec.arguments.size()) {
                dependencies = utf8_path(spec.arguments[index + 1]);
            }
        }
        if (!spec.arguments.empty()) {
            source = utf8_path(spec.arguments.back());
        }

        const int call_index =
            phase_compile_calls_.fetch_add(1, std::memory_order_relaxed);
        compile_calls_.fetch_add(1, std::memory_order_relaxed);
        const int active =
            active_compiles_.fetch_add(1, std::memory_order_relaxed) + 1;
        update_max(maximum_active_, active);
        if (static_cast<std::size_t>(call_index) < synchronized_wave_) {
            wave_gate_.arrive_and_wait();
        }

        bool should_fail = false;
        {
            std::scoped_lock lock{mutex_};
            should_fail =
                fail_source_names_.contains(source.filename().string());
        }

        if (!should_fail) {
            write_text(object, "parallel fake object");
            std::string include_list;
            if (!shared_dependency_.empty()) {
                include_list = "\""
                    + json_escape(path_to_utf8(shared_dependency_))
                    + "\"";
            }
            const std::string json =
                "{\"Data\":{\"Source\":\""
                + json_escape(path_to_utf8(source))
                + "\",\"Includes\":["
                + include_list
                + "]}}";
            write_text(dependencies, json);
        }
        active_compiles_.fetch_sub(1, std::memory_order_relaxed);

        if (should_fail) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text =
                    "simulated parallel compile failure: "
                    + source.filename().string(),
            };
        }
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated parallel compile success",
        };
    }

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    link(const mqb::process::ProcessSpec& spec) {
        link_calls_.fetch_add(1, std::memory_order_relaxed);
        fs::path output;
        for (const auto& argument : spec.arguments) {
            if (argument.starts_with("/OUT:")) {
                output = utf8_path(std::string_view{argument}.substr(5));
            }
        }
        write_text(output, "parallel fake executable");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated link success",
        };
    }

    fs::path compiler_;
    fs::path linker_;
    fs::path shared_dependency_;
    std::size_t synchronized_wave_{};
    std::barrier<> wave_gate_;
    std::atomic<int> phase_compile_calls_{0};
    std::atomic<int> compile_calls_{0};
    std::atomic<int> link_calls_{0};
    std::atomic<int> active_compiles_{0};
    std::atomic<int> maximum_active_{0};
    mutable std::mutex mutex_;
    std::set<std::string> fail_source_names_;
};

[[nodiscard]] mqb::orchestration::TargetSourceRequest make_source(
    const fs::path& root,
    const std::string& name) {
    const fs::path source = root / "src" / name;
    write_text(source, "int value_" + name + " = 1;\n");
    return mqb::orchestration::TargetSourceRequest{
        .source = source,
        .artifacts = mqb::SourceArtifacts{
            .object = root / ".mqb" / "obj" / (name + ".obj"),
            .dependencies = root / ".mqb" / "deps" / (name + ".json"),
            .compile_cache =
                root / ".mqb" / "cache" / "compile" / (name + ".cache"),
        },
    };
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path compiler = fixture.path() / "tools" / "cl.exe";
    const fs::path linker_path = fixture.path() / "tools" / "link.exe";
    const fs::path common_header = fixture.path() / "src" / "common.hpp";
    write_text(compiler, "fake compiler identity");
    write_text(linker_path, "fake linker identity");
    write_text(common_header, "#pragma once\ninline int common_value = 7;\n");

    // Directly lock the single-flight and revalidation contracts before using
    // them through the target coordinator.
    {
        mqb::performance::Collector collector;
        mqb::performance::Activation collector_activation{collector};
        mqb::orchestration::detail::FilesystemEvidenceTable table;

        const auto original_time = fs::last_write_time(common_header);
        {
            mqb::performance::ScopedFilesystemDomain filesystem_domain{
                mqb::performance::FilesystemKind::compile};
            mqb::orchestration::detail::ScopedFilesystemEvidenceActivation
                table_activation{table};
            const auto first =
                mqb::orchestration::detail::snapshot_file_or_directory(
                    common_header);
            const auto reused =
                mqb::orchestration::detail::snapshot_file_or_directory(
                    common_header);
            expect(first.snapshot.exists && reused.snapshot.exists,
                   "shared evidence table should preserve existing snapshots");
            expect(first.snapshot.modified == reused.snapshot.modified,
                   "shared evidence table should return the first exact timestamp");
        }

        auto evidence = collector.snapshot();
        const auto compile_index = static_cast<std::size_t>(
            mqb::performance::FilesystemKind::compile);
        expect(evidence.filesystem_snapshot_requests[compile_index] == 1,
               "two shared observations should perform one initial metadata probe");
        expect(evidence.snapshot_evidence_reuses[compile_index] == 1,
               "the second shared observation should be counted as evidence reuse");

        std::error_code timestamp_error;
        fs::last_write_time(
            common_header,
            original_time + std::chrono::seconds{2},
            timestamp_error);
        expect(!timestamp_error,
               "test should be able to advance the common-header timestamp");
        expect(!table.revalidate_shared(),
               "revalidation must reject a shared path that changed");

        evidence = collector.snapshot();
        expect(evidence.filesystem_snapshot_requests[compile_index] == 2,
               "revalidation should add exactly one physical probe for a shared path");

        timestamp_error.clear();
        fs::last_write_time(common_header, original_time, timestamp_error);
        expect(!timestamp_error,
               "test should restore the common-header timestamp");
    }

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = compiler,
            .version = "19.50.parallel-test",
            .binary_stamp = "parallel-test-stamp",
        },
        .linker = linker_path,
        .librarian = fixture.path() / "tools" / "lib.exe",
        .vc_tools_root = fixture.path() / "tools",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    std::vector<mqb::orchestration::TargetSourceRequest> sources;
    sources.push_back(make_source(fixture.path(), "a.cpp"));
    sources.push_back(make_source(fixture.path(), "b.cpp"));
    sources.push_back(make_source(fixture.path(), "c.cpp"));
    sources.push_back(make_source(fixture.path(), "d.cpp"));

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = mqb::BuildConfiguration::debug;
    compiler_options.architecture = mqb::Architecture::x64;
    compiler_options.standard = mqb::CppStandard::cpp23;

    mqb::LinkOptions link_options;
    link_options.configuration = mqb::BuildConfiguration::debug;
    link_options.architecture = mqb::Architecture::x64;
    link_options.subsystem = mqb::LinkSubsystem::console;

    TargetToolRunner runner{compiler, linker_path, 3, common_header};
    mqb::msvc::MsvcCompileExecutor compile_executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        toolchain, compile_executor};
    mqb::msvc::MsvcLinker linker{toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{
        toolchain, linker};
    mqb::orchestration::MsvcIncrementalTargetCoordinator target_coordinator{
        compile_coordinator, link_coordinator};

    const mqb::orchestration::IncrementalTargetRequest request{
        .sources = sources,
        .target = mqb::TargetArtifacts{
            .executable =
                fixture.path() / ".mqb" / "bin" / "parallel.exe",
            .link_cache =
                fixture.path() / ".mqb" / "cache" / "link" / "parallel.cache",
        },
        .compiler_options = compiler_options,
        .link_options = link_options,
        .working_directory = fixture.path(),
        .max_parallel_compiles = 3,
    };

    const auto cold = target_coordinator.run(request);
    expect(cold.has_value(), "cold parallel target build should succeed");
    if (cold) {
        expect(cold->compiles.size() == sources.size(),
               "parallel target result should retain one compile result per source");
        for (std::size_t index = 0;
             index < sources.size() && index < cold->compiles.size();
             ++index) {
            expect(cold->compiles[index].source == sources[index].source,
                   "parallel compile results should remain in original source order");
        }
        expect(cold->any_compiled,
               "cold target should report that at least one TU compiled");
        expect(cold->link.linked,
               "cold parallel target should link after all compiles succeed");
    }
    expect(runner.maximum_active_compiles() == 3,
           "target coordinator should execute three compile requests concurrently");
    expect(runner.compile_calls() == 4,
           "cold target should compile every source exactly once");
    expect(runner.link_calls() == 1,
           "cold target should link exactly once");

    mqb::performance::EvidenceSnapshot warm_evidence;
    const auto warm = [&] {
        mqb::performance::Collector warm_collector;
        mqb::performance::Activation warm_activation{warm_collector};
        auto result = target_coordinator.run(request);
        warm_evidence = warm_collector.snapshot();
        return result;
    }();
    expect(warm.has_value(), "warm parallel target validation should succeed");
    if (warm) {
        expect(!warm->any_compiled,
               "warm target should report no compile execution");
        expect(!warm->link.linked,
               "warm target should reuse link state");
        expect(warm->compiles.size() == sources.size(),
               "warm target should still report every TU in source order");
    }
    const auto compile_index = static_cast<std::size_t>(
        mqb::performance::FilesystemKind::compile);
    expect(
        warm_evidence.snapshot_evidence_reuses[compile_index]
            >= sources.size() - 1,
        "warm target should reuse the shared common-header observation");
    expect(
        warm_evidence.filesystem_snapshot_requests[compile_index]
            <= warm_evidence.unique_filesystem_paths_probed[compile_index] * 2,
        "each compile path should be physically probed at most once plus one revalidation");
    expect(runner.compile_calls() == 4,
           "warm target should not invoke the compiler runner");
    expect(runner.link_calls() == 1,
           "warm target should not invoke the linker runner");

    auto forced_request = request;
    forced_request.force_downstream_rebuild = true;
    const auto forced = target_coordinator.run(forced_request);
    expect(forced.has_value(),
           "upstream rebuild should force the complete target successfully");
    if (forced) {
        expect(forced->any_compiled,
               "upstream rebuild should force every target consumer to compile");
        expect(forced->link.linked,
               "upstream rebuild should link exactly once after forced consumers finish");
        expect(std::all_of(
                   forced->compiles.begin(),
                   forced->compiles.end(),
                   [](const mqb::orchestration::TargetCompileResult& compile) {
                       return std::find(
                                  compile.result.validation.reasons.begin(),
                                  compile.result.validation.reasons.end(),
                                  mqb::BuildReason::explicit_rebuild)
                           != compile.result.validation.reasons.end();
                   }),
               "upstream rebuild should retain explicit evidence on every forced consumer");
    }
    expect(runner.compile_calls() == 8,
           "upstream rebuild should compile all four consumers exactly once more");
    expect(runner.link_calls() == 2,
           "upstream rebuild should add exactly one link invocation");

    const auto warm_after_force = target_coordinator.run(request);
    expect(warm_after_force.has_value(),
           "ordinary warm target after upstream forcing should succeed");
    if (warm_after_force) {
        expect(!warm_after_force->any_compiled,
               "upstream forcing must not poison the next warm compile check");
        expect(!warm_after_force->link.linked,
               "upstream forcing must not poison the next warm link check");
    }
    expect(runner.compile_calls() == 8,
           "post-force warm target should not launch more compiler processes");
    expect(runner.link_calls() == 2,
           "post-force warm target should not launch another linker process");

    {
        auto invalid = request;
        invalid.max_parallel_compiles = 0;
        const auto rejected = target_coordinator.run(invalid);
        expect(!rejected
                   && rejected.error().code
                       == mqb::orchestration::IncrementalTargetErrorCode::
                           invalid_parallelism,
               "zero parallelism should fail before scheduling work");
    }

    {
        auto duplicate = request;
        duplicate.sources[1].artifacts.dependencies =
            duplicate.sources[0].artifacts.dependencies;
        const int calls_before = runner.compile_calls();
        const auto rejected = target_coordinator.run(duplicate);
        expect(!rejected
                   && rejected.error().code
                       == mqb::orchestration::IncrementalTargetErrorCode::
                           duplicate_dependencies,
               "duplicate dependency metadata output should be rejected before workers start");
        expect(runner.compile_calls() == calls_before,
               "duplicate dependency metadata should not launch any compiler work");
    }

    {
        auto duplicate = request;
        duplicate.sources[1].artifacts.compile_cache =
            duplicate.sources[0].artifacts.compile_cache;
        const int calls_before = runner.compile_calls();
        const auto rejected = target_coordinator.run(duplicate);
        expect(!rejected
                   && rejected.error().code
                       == mqb::orchestration::IncrementalTargetErrorCode::
                           duplicate_compile_cache,
               "duplicate compile cache output should be rejected before workers start");
        expect(runner.compile_calls() == calls_before,
               "duplicate compile cache should not launch any compiler work");
    }

    // Remove compile/link state so the next run is a true concurrent failure wave.
    std::error_code ignored;
    fs::remove_all(fixture.path() / ".mqb", ignored);

    TargetToolRunner failing_runner{
        compiler,
        linker_path,
        3,
        common_header};
    failing_runner.fail_sources({"b.cpp", "c.cpp"});
    mqb::msvc::MsvcCompileExecutor failing_executor{
        toolchain,
        failing_runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator
        failing_compile_coordinator{toolchain, failing_executor};
    mqb::msvc::MsvcLinker failing_linker{toolchain, failing_runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator
        failing_link_coordinator{toolchain, failing_linker};
    mqb::orchestration::MsvcIncrementalTargetCoordinator
        failing_target_coordinator{
            failing_compile_coordinator,
            failing_link_coordinator};

    const auto failed = failing_target_coordinator.run(request);
    expect(!failed, "parallel compile failure should fail the target");
    if (!failed) {
        expect(failed.error().code
                   == mqb::orchestration::IncrementalTargetErrorCode::
                       compile_failed,
               "parallel compile failure should report compile_failed");
        expect(failed.error().source == sources[1].source,
               "concurrent failures should deterministically report the lowest source index");
        expect(failed.error().compile_error.has_value(),
               "target failure should retain the selected compile error");
    }
    expect(failing_runner.maximum_active_compiles() == 3,
           "failure wave should still prove three concurrent in-flight compiles");
    expect(failing_runner.link_calls() == 0,
           "target must not link when any parallel compile fails");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_incremental_target_coordinator_tests passed\n";
    return 0;
}
