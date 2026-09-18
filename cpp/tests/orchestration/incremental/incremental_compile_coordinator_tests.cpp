#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "prewrite_inventory_cases.hpp"

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcIncludeSearchFreshness.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
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
            / ("mqb-orchestration-test-" + std::to_string(tick));
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

[[nodiscard]] bool same_environment_variable(
    const mqb::process::EnvironmentVariable& left,
    const mqb::process::EnvironmentVariable& right) {
    return left.name == right.name
        && left.value == right.value
        && left.remove == right.remove;
}

[[nodiscard]] bool same_process_spec(
    const mqb::process::ProcessSpec& left,
    const mqb::process::ProcessSpec& right) {
    if (left.executable != right.executable
        || left.arguments != right.arguments
        || left.working_directory != right.working_directory
        || left.inherit_environment != right.inherit_environment
        || left.capture_stdout != right.capture_stdout
        || left.capture_stderr != right.capture_stderr
        || left.environment.size() != right.environment.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.environment.size(); ++index) {
        if (!same_environment_variable(left.environment[index], right.environment[index])) {
            return false;
        }
    }
    return true;
}

class CompilerLikeRunner final : public mqb::process::ProcessRunner {
public:
    fs::path source;
    fs::path header;
    fs::path object;
    fs::path dependencies;
    int calls{};
    bool fail_compile{false};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;

        if (fail_compile) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "simulated cl.exe diagnostic",
            };
        }

        write_text(object, "fake object produced by compiler runner");
        const std::string json =
            "{\"Data\":{\"Source\":\""
            + json_escape(path_to_utf8(source))
            + "\",\"Includes\":[\""
            + json_escape(path_to_utf8(header))
            + "\"]}}";
        write_text(dependencies, json);

        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated cl.exe success",
        };
    }

    mqb::process::ProcessSpec last_spec;
};

[[nodiscard]] bool has_reason(
    const mqb::CompileCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

[[nodiscard]] bool has_warning(
    const mqb::orchestration::IncrementalCompileResult& result,
    const mqb::orchestration::IncrementalCompileWarningCode code) {
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [code](const mqb::orchestration::IncrementalCompileWarning& warning) {
            return warning.code == code;
        });
}

// Exercise the real persisted-cache -> inspect() seam, not a second path
// comparator in the test. Environment roots are outside BuildSignature's argv
// domain: root identity/order must still invalidate an otherwise warm entry.
void check_include_root_identity(
    const mqb::orchestration::IncrementalCompileRequest& original_request,
    const mqb::msvc::MsvcToolchain& original_toolchain,
    CompilerLikeRunner& runner) {
    const auto loaded = mqb::CompileCacheFile::load(original_request.cache_file);
    expect(loaded && loaded->has_value(), "root tests need the actual warm compile cache");
    if (!loaded || !*loaded) return;

    auto request = original_request;
    request.cache_file = original_request.cache_file.parent_path() / "root-identity.mqbcache";
    auto toolchain = original_toolchain;
    const fs::path parent = original_request.working_directory.value();
    const fs::path first = parent / "Root-A";
    const fs::path second = parent / "Root-B";
    const fs::path unicode = parent / fs::path{u8"\u00c4-root"};
    toolchain.environment.push_back({"INCLUDE",
        path_to_utf8(first) + ";" + path_to_utf8(second) + ";" + path_to_utf8(unicode)});
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator coordinator{toolchain, executor};
    const auto roots = mqb::msvc::include_search_roots(
        request.options, toolchain.environment, request.working_directory);
    expect(roots.size() == 4, "one typed and three ordered environment roots are retained");
    if (roots.size() != 4) return;

    const auto probe = [&](const std::vector<fs::path>& stored, bool reusable,
                           const std::string_view label) {
        auto cache = **loaded;
        cache.include_search_roots = stored;
        const auto saved = mqb::CompileCacheFile::save(request.cache_file, cache);
        expect(saved.has_value(), "persist root variant through the real cache serializer");
        if (!saved) return;
        const auto time = fs::last_write_time(request.cache_file);
        const auto size = fs::file_size(request.cache_file);
        const int calls = runner.calls;
        const auto inspected = coordinator.inspect(request);
        expect(inspected.has_value(), label);
        if (inspected) {
            expect(inspected->validation.reusable() == reusable, label);
            expect(inspected->plan.actions.empty() == reusable, label);
            expect(has_reason(inspected->validation, mqb::BuildReason::dependency_changed) == !reusable,
                   label);
            expect(inspected->warnings.empty(), "root identity checks preserve clean diagnostics");
        }
        expect(runner.calls == calls, "root inspection must not execute the compiler");
        expect(fs::last_write_time(request.cache_file) == time && fs::file_size(request.cache_file) == size,
               "root inspection must not rewrite cache metadata");
    };
    probe(roots, true, "exact normalized roots stay reusable");
    const auto exact = mqb::CompileCacheFile::load(request.cache_file);
    expect(exact && exact->has_value(), "read back exact persisted roots");
    if (exact && *exact) {
        const auto& restored = (**exact).include_search_roots;
        expect(restored.size() == roots.size(), "persisted root count unchanged");
        for (std::size_t i = 0; i < std::min(restored.size(), roots.size()); ++i)
            expect(restored[i].native() == roots[i].native(), "exact-spelling fast path is reachable");
    }
    auto variant = roots;
    variant[1] = parent / "root-a";
    probe(variant, true, "ASCII case alias retains Windows fallback");
    variant = roots;
    variant[3] = parent / fs::path{u8"\u00e4-root"};
    probe(variant, true, "Unicode case alias retains Windows fallback");
    variant = roots;
    variant[1] /= "";
    probe(variant, true, "non-root trailing separator retains Windows fallback");
    variant = roots;
    std::swap(variant[1], variant[2]);
    probe(variant, false, "root reordering must invalidate even after an equal first root");
    variant = roots;
    variant[3] = parent / "different-last-root";
    probe(variant, false, "last-root replacement must not be hidden by exact earlier roots");
    variant = roots;
    variant.pop_back();
    probe(variant, false, "root removal remains freshness");
    variant = roots;
    variant.push_back(roots.back());
    probe(variant, false, "duplicate root insertion is not set equivalence");
    variant = roots;
    variant[2] = roots[1];
    probe(variant, false, "same-length duplicate substitution must invalidate");
    probe(roots, true, "restoring the exact roots restores only the original warm decision");
    std::cout << "include_root_identity_cases 10 variants checked; no compiler launch\n";
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path source = fixture.path() / "src" / "main.cpp";
    const fs::path header = fixture.path() / "include" / "value.hpp";
    const fs::path object = fixture.path() / "cache" / "main.obj";
    const fs::path dependencies = fixture.path() / "cache" / "main.deps.json";
    const fs::path cache_file = fixture.path() / "cache" / "main.mqbcache";

    write_text(source, "#include \"value.hpp\"\nint value() { return VALUE; }\n");
    write_text(header, "#pragma once\n#define VALUE 7\n");

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
    debug_options.include_directories = {header.parent_path()};

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = "C:/fake/cl.exe",
            .version = "19.50.test",
            .binary_stamp = "fake-stamp",
        },
        .linker = "C:/fake/link.exe",
        .librarian = "C:/fake/lib.exe",
        .vc_tools_root = "C:/fake",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    CompilerLikeRunner runner;
    runner.source = source;
    runner.header = header;
    runner.object = object;
    runner.dependencies = dependencies;

    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator coordinator{
        toolchain,
        executor};

    const mqb::orchestration::IncrementalCompileRequest request{
        .unit = unit,
        .options = debug_options,
        .cache_file = cache_file,
        .source_dependencies_file = dependencies,
        .working_directory = fixture.path(),
    };
    const mqb::msvc::CompileExecutionRequest execution_request{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };

    const auto modeled_before_cold = executor.build_recipe(execution_request);
    expect(modeled_before_cold.has_value(),
           "compile recipe should be constructible before cache validation or execution");
    expect(!fs::exists(object) && !fs::exists(dependencies) && !fs::exists(cache_file),
           "pure recipe construction must not create outputs, dependency metadata, or cache state");

    mqb::process::ProcessSpec modeled_process;
    bool captured_modeled_process = false;
    if (modeled_before_cold) {
        modeled_process = modeled_before_cold->process;
        captured_modeled_process = true;
        expect(modeled_before_cold->toolchain.version == toolchain.identity.version
                   && modeled_before_cold->toolchain.binary_stamp == toolchain.identity.binary_stamp,
               "compile recipe should retain the selected toolchain identity");
    }

    const auto cold = coordinator.run(request);
    expect(cold.has_value(), "cold incremental compile should succeed");
    if (cold) {
        expect(cold->compiled, "cold cache should execute one compile action");
        expect(cold->plan.actions.size() == 1,
               "cold cache should retain one planned compile action");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_cache_entry),
               "cold cache should explain missing cache metadata");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_output),
               "cold cache should explain missing object output");
        expect(cold->warnings.empty(),
               "ordinary missing cache should not be reported as a warning");
    }
    expect(runner.calls == 1, "cold build should launch the compiler exactly once");
    if (captured_modeled_process) {
        expect(same_process_spec(modeled_process, runner.last_spec),
               "actual compiler execution must consume the exact modeled ProcessSpec contract");
    }
    expect(fs::is_regular_file(cache_file),
           "successful cold build should persist cache metadata");

    const auto warm = coordinator.run(request);
    expect(warm.has_value(), "warm incremental check should succeed");
    if (warm) {
        expect(!warm->compiled, "unchanged warm cache should skip compiler execution");
        expect(warm->plan.actions.empty(),
               "unchanged warm cache should produce an empty build plan");
        expect(warm->validation.reusable(),
               "unchanged warm cache should be reusable");
    }
    expect(runner.calls == 1,
           "warm cache hit should not invoke the compiler again");

    check_include_root_identity(request, toolchain, runner);

    const auto modeled_on_warm_state = executor.build_recipe(execution_request);
    expect(modeled_on_warm_state.has_value(),
           "the same compile recipe must remain constructible when cache state is warm");
    if (captured_modeled_process && modeled_on_warm_state) {
        expect(same_process_spec(modeled_on_warm_state->process, modeled_process),
               "cold-state and warm-state recipe construction must be identical");
    }
    expect(runner.calls == 1,
           "explicit recipe inspection on warm state must not launch the compiler");

    auto forced_request = request;
    forced_request.force_rebuild = true;
    write_text(cache_file, "not an MQB cache file");
    const auto forced = coordinator.run(forced_request);
    expect(forced.has_value(), "explicit rebuild request should compile successfully");
    if (forced) {
        expect(forced->compiled,
               "explicit rebuild should execute even when the cache was otherwise reusable");
        expect(forced->plan.actions.size() == 1,
               "explicit rebuild should produce exactly one compile action");
        expect(has_reason(forced->validation, mqb::BuildReason::explicit_rebuild),
               "explicit rebuild should be visible as explicit_rebuild");
        expect(!has_reason(forced->validation, mqb::BuildReason::compiler_options_changed),
               "explicit rebuild must not masquerade as a compile-signature change");
        expect(!has_reason(forced->validation, mqb::BuildReason::missing_cache_entry),
               "authoritative explicit rebuild should not inspect stale cache metadata");
        expect(forced->warnings.empty(),
               "authoritative explicit rebuild should bypass stale cache load warnings");
    }
    expect(runner.calls == 2,
           "explicit rebuild should invoke the compiler exactly once");

    const auto warm_after_force = coordinator.run(request);
    expect(warm_after_force.has_value(),
           "ordinary warm check after an explicit rebuild should succeed");
    if (warm_after_force) {
        expect(!warm_after_force->compiled,
               "explicit rebuild must not poison the following ordinary cache hit");
        expect(warm_after_force->validation.reusable(),
               "cache should be reusable immediately after a successful explicit rebuild");
        expect(!has_reason(warm_after_force->validation, mqb::BuildReason::explicit_rebuild),
               "explicit rebuild reason must be request-local rather than persisted");
    }
    expect(runner.calls == 2,
           "post-force ordinary warm hit should not invoke the compiler");

    write_text(cache_file, "not an MQB cache file");
    const auto corrupt = coordinator.run(request);
    expect(corrupt.has_value(),
           "corrupt cache metadata should degrade to a rebuild instead of failing the build");
    if (corrupt) {
        expect(corrupt->compiled,
               "corrupt cache metadata should conservatively trigger compilation");
        expect(has_reason(corrupt->validation, mqb::BuildReason::missing_cache_entry),
               "corrupt metadata should be treated as an unavailable cache entry");
        expect(has_warning(
                   *corrupt,
                   mqb::orchestration::IncrementalCompileWarningCode::cache_load_failed),
               "corrupt metadata should remain visible as a cache_load_failed warning");
    }
    expect(runner.calls == 3,
           "corrupt metadata should invoke the compiler exactly once more");

    auto release_request = request;
    release_request.options.configuration = mqb::BuildConfiguration::release;
    const auto release = coordinator.run(release_request);
    expect(release.has_value(), "Debug to Release transition should compile successfully");
    if (release) {
        expect(release->compiled,
               "configuration transition should execute a compile action");
        expect(has_reason(
                   release->validation,
                   mqb::BuildReason::compiler_options_changed),
               "configuration transition should report compiler_options_changed");
    }
    expect(runner.calls == 4,
           "configuration transition should invoke the compiler once");

    const mqb::msvc::CompileExecutionRequest release_execution_request{
        .unit = release_request.unit,
        .options = release_request.options,
        .source_dependencies_file = release_request.source_dependencies_file,
        .working_directory = release_request.working_directory,
    };
    const auto modeled_release = executor.build_recipe(release_execution_request);
    expect(modeled_release.has_value(),
           "release policy should also produce an inspectable compile recipe");
    if (captured_modeled_process && modeled_release) {
        expect(!same_process_spec(modeled_release->process, modeled_process),
               "a real compiler policy change should produce a different recipe");
    }
    expect(runner.calls == 4,
           "modeling a release recipe must not launch the compiler");

    std::error_code ignored;
    fs::remove(cache_file, ignored);
    fs::remove(object, ignored);
    runner.fail_compile = true;
    const auto failed = coordinator.run(request);
    expect(!failed.has_value(), "compiler failure should fail the incremental operation");
    if (!failed) {
        expect(failed.error().code
                   == mqb::orchestration::IncrementalCompileErrorCode::compile_failed,
               "compiler failure should stay distinct from planning failures");
        expect(failed.error().compile_error.has_value(),
               "coordinator should preserve the backend compile error");
    }
    expect(runner.calls == 5,
           "failed cold rebuild should still correspond to one compiler launch");

    failures += mqb::tests::prewrite_inventory_cases();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_orchestration_incremental_tests passed\n";
    return 0;
}
