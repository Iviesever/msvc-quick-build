#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcIncludeSearchFreshness.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"
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
        root_ = fs::temp_directory_path()
            / ("mqb-module-scan-inspection-" + std::to_string(tick));
        fs::create_directories(root_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    [[nodiscard]] const fs::path& root() const noexcept { return root_; }

private:
    fs::path root_;
};

void write_text(const fs::path& path, const std::string_view text) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::string read_text(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{stream},
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
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

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

[[nodiscard]] bool has_reason(
    const mqb::orchestration::IncrementalModuleScanInspection& inspection,
    const mqb::orchestration::ModuleScanReason reason) {
    return std::find(
        inspection.reasons.begin(),
        inspection.reasons.end(),
        reason) != inspection.reasons.end();
}

[[nodiscard]] mqb::FileSnapshot snapshot(const fs::path& path) {
    std::error_code error_code;
    const auto modified = fs::last_write_time(path, error_code);
    expect(!error_code, "snapshot timestamp should be readable");
    return mqb::FileSnapshot{
        .path = path,
        .exists = !error_code,
        .modified = modified,
    };
}

class ScanRunner final : public mqb::process::ProcessRunner {
public:
    int calls{};
    std::optional<mqb::process::ProcessSpec> last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;

        fs::path output;
        for (std::size_t index = 0; index + 1 < spec.arguments.size(); ++index) {
            if (spec.arguments[index] == "/scanDependencies") {
                output = fs::path{spec.arguments[index + 1]};
                break;
            }
        }
        if (output.empty()) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "missing /scanDependencies output",
            };
        }

        const std::string primary = json_escape(path_to_utf8(output.parent_path() / "demo.obj"));
        write_text(output,
            "{\n"
            "  \"version\": 1,\n"
            "  \"revision\": 0,\n"
            "  \"rules\": [ {\n"
            "    \"primary-output\": \"" + primary + "\",\n"
            "    \"provides\": [ { \"logical-name\": \"demo\" } ]\n"
            "  } ]\n"
            "}\n");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "module scan complete",
        };
    }
};

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path source = fixture.root() / "input" / "demo.ixx";
    const fs::path output = fixture.root() / "state" / "demo.p1689.json";
    const fs::path cache = fixture.root() / "cache" / "demo.mqbcache";
    write_text(source, "export module demo;\nexport int value() { return 7; }\n");

    mqb::msvc::MsvcToolchain toolchain;
    toolchain.identity.compiler = fixture.root() / "toolchain" / "cl.exe";
    toolchain.identity.version = "19.51.module-inspection";
    toolchain.identity.binary_stamp = "fake-module-scanner-stamp";
    toolchain.environment.push_back(
        mqb::process::EnvironmentVariable{"PATH", "fake-toolchain-path", false});

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"FEATURE=1"};

    const mqb::orchestration::IncrementalModuleScanRequest request{
        .source = source,
        .module_dependencies_file = output,
        .compile_cache_file = cache,
        .options = options,
        .kind = mqb::TranslationUnitKind::module_interface,
        .working_directory = fixture.root(),
    };

    ScanRunner runner;
    mqb::msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    mqb::orchestration::MsvcIncrementalModuleScanCoordinator coordinator{scanner};

    const auto cold = coordinator.inspect(request);
    expect(cold.has_value(), "cold module scan inspection should succeed");
    if (cold) {
        expect(cold->scan_required(), "cold module scan inspection should require a scan");
        expect(has_reason(
                   *cold,
                   mqb::orchestration::ModuleScanReason::missing_cache_entry),
               "cold module scan inspection should report missing cache evidence");
        expect(!cold->dependencies.has_value(),
               "cold module scan inspection must not invent a P1689 graph");
        expect(cold->recipe.process.executable == toolchain.identity.compiler,
               "cold inspection should expose the exact selected cl.exe");
        expect(cold->recipe.process.working_directory == fixture.root(),
               "cold inspection should preserve the scan working directory");
        expect(std::find(
                   cold->recipe.process.arguments.begin(),
                   cold->recipe.process.arguments.end(),
                   "/scanDependencies") != cold->recipe.process.arguments.end(),
               "cold inspection should expose /scanDependencies argv");
    }
    expect(runner.calls == 0, "cold module scan inspection must not launch cl.exe");
    expect(!fs::exists(output) && !fs::exists(cache),
           "cold module scan inspection must not create output or cache state");
    expect(!fs::exists(output.parent_path()),
           "cold module scan inspection must not prepare output directories");

    auto cold_run = coordinator.run(request);
    expect(cold_run.has_value() && cold_run->scanned,
           "cold module scan run should execute the inspected scan recipe");
    expect(runner.calls == 1, "cold module scan run should launch cl.exe exactly once");
    expect(fs::is_regular_file(output),
           "cold module scan run should produce P1689 metadata");
    expect(!fs::exists(cache),
           "module scan alone must not seal compile-cache evidence before compilation");
    if (cold_run) {
        expect(cold_run->result.dependencies.rules.size() == 1,
               "cold module scan run should parse one P1689 rule");
        expect(cold_run->result.dependencies.rules.front().provided_modules.size() == 1
                   && cold_run->result.dependencies.rules.front()
                       .provided_modules.front().logical_name == "demo",
               "cold module scan run should expose the provided module identity");
        expect(runner.last_spec.has_value()
                   && same_process_spec(
                       cold_run->inspection.recipe.process,
                       *runner.last_spec),
               "incremental scan run must execute the exact inspected ProcessSpec");
    }

    mqb::TranslationUnit unit;
    unit.source = source;
    unit.kind = mqb::TranslationUnitKind::module_interface;
    unit.outputs = {
        mqb::Artifact{fixture.root() / "objects" / "demo.obj", mqb::ArtifactKind::object},
        mqb::Artifact{fixture.root() / "ifc" / "demo.ifc", mqb::ArtifactKind::module_interface},
    };
    const auto include_roots = mqb::msvc::include_search_roots(
        options,
        toolchain.environment,
        request.working_directory);
    mqb::CompileCacheEntry entry{
        .source = source,
        .kind = unit.kind,
        .toolchain = toolchain.identity,
        .signature = mqb::BuildSignature::for_compile(
            unit,
            toolchain.identity,
            options),
        .outputs = unit.outputs,
        .include_search_roots = include_roots,
        .module_scan = mqb::ModuleScanEvidence{
            .signature = mqb::BuildSignature::for_module_scan(
                source,
                unit.kind,
                toolchain.identity,
                options),
            .source = snapshot(source),
            .output = snapshot(output),
            .dependencies = {},
        },
    };
    const auto saved = mqb::CompileCacheFile::save(cache, entry);
    expect(saved.has_value(), "test should be able to seal module scan evidence");

    const std::string output_before = read_text(output);
    const std::string cache_before = read_text(cache);
    const auto warm = coordinator.inspect(request);
    expect(warm.has_value() && warm->reusable(),
           "warm module scan inspection should reuse sealed P1689 evidence");
    if (warm) {
        expect(warm->reasons.empty(),
               "warm module scan inspection should expose no rebuild reasons");
        expect(warm->dependencies.has_value()
                   && warm->dependencies->rules.size() == 1,
               "warm module scan inspection should parse reusable P1689 metadata");
    }
    expect(runner.calls == 1, "warm module scan inspection must not launch cl.exe");
    expect(read_text(output) == output_before && read_text(cache) == cache_before,
           "warm module scan inspection must not mutate output or cache state");

    const auto warm_run = coordinator.run(request);
    expect(warm_run.has_value() && !warm_run->scanned && warm_run->result.reused,
           "warm module scan run should return reusable evidence without execution");
    expect(runner.calls == 1, "warm module scan run must not launch cl.exe");

    const auto output_time = fs::last_write_time(output);
    write_text(output, "{ malformed P1689 metadata");
    fs::last_write_time(output, output_time);
    const auto corrupt = coordinator.inspect(request);
    expect(corrupt.has_value() && corrupt->scan_required(),
           "corrupted P1689 content should require a fresh scan");
    if (corrupt) {
        expect(has_reason(
                   *corrupt,
                   mqb::orchestration::ModuleScanReason::dependency_metadata_invalid),
               "timestamp-preserving P1689 corruption should expose a metadata reason");
    }
    expect(runner.calls == 1,
           "corrupted metadata inspection must not repair or execute the scan");
    expect(read_text(cache) == cache_before,
           "corrupted metadata inspection must not rewrite sealed cache state");

    write_text(output, output_before);
    fs::last_write_time(output, output_time);
    const auto source_time = fs::last_write_time(source);
    write_text(source, "export module demo;\nexport int value() { return 8; }\n");
    fs::last_write_time(source, source_time + std::chrono::seconds{1});
    const auto stale_source = coordinator.inspect(request);
    expect(stale_source.has_value() && stale_source->scan_required(),
           "source mutation should invalidate module scan evidence");
    if (stale_source) {
        expect(has_reason(
                   *stale_source,
                   mqb::orchestration::ModuleScanReason::source_changed),
               "source mutation should expose a source_changed scan reason");
    }
    expect(runner.calls == 1,
           "stale source inspection must not launch cl.exe");

    auto rescanned = coordinator.run(request);
    expect(rescanned.has_value() && rescanned->scanned,
           "stale source module scan run should execute a fresh recipe");
    expect(runner.calls == 2,
           "stale source module scan run should launch cl.exe exactly once more");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_module_scan_inspection_contract passed\n";
    return 0;
}
