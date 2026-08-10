#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
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
        path_ = fs::temp_directory_path() / ("mqb-module-wave-test-" + std::to_string(tick));
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
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

struct RecordedCompile {
    fs::path source;
    std::vector<std::string> arguments;
};

class CompilerLikeRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        fs::path source;
        fs::path object;
        fs::path dependencies;
        fs::path interface_file;

        for (std::size_t index = 0; index < spec.arguments.size(); ++index) {
            const auto& argument = spec.arguments[index];
            if (argument.starts_with("/Fo")) {
                object = fs::path{argument.substr(3)};
            } else if (argument == "/sourceDependencies" && index + 1 < spec.arguments.size()) {
                dependencies = fs::path{spec.arguments[++index]};
            } else if (argument == "/ifcOutput" && index + 1 < spec.arguments.size()) {
                interface_file = fs::path{spec.arguments[++index]};
            }
        }
        if (!spec.arguments.empty()) {
            source = fs::path{spec.arguments.back()};
        }

        write_text(object, "fake object");
        if (!interface_file.empty()) {
            write_text(interface_file, "fake ifc");
        }
        const std::string json =
            "{\"Data\":{\"Source\":\""
            + json_escape(path_to_utf8(source))
            + "\",\"Includes\":[]}}";
        write_text(dependencies, json);

        {
            std::scoped_lock lock{mutex_};
            records_.push_back(RecordedCompile{
                .source = source.lexically_normal(),
                .arguments = spec.arguments,
            });
        }
        ++calls_;

        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "fake cl.exe success",
        };
    }

    [[nodiscard]] int calls() const noexcept { return calls_.load(); }

    [[nodiscard]] std::vector<RecordedCompile> records() const {
        std::scoped_lock lock{mutex_};
        return records_;
    }

private:
    std::atomic<int> calls_{0};
    mutable std::mutex mutex_;
    std::vector<RecordedCompile> records_;
};

[[nodiscard]] const mqb::orchestration::ModuleCompileResult* find_result(
    const mqb::orchestration::ModuleCompileWaveResult& result,
    const fs::path& source) {
    const auto found = std::find_if(
        result.compiles.begin(),
        result.compiles.end(),
        [&source](const mqb::orchestration::ModuleCompileResult& item) {
            return item.source.lexically_normal() == source.lexically_normal();
        });
    return found == result.compiles.end() ? nullptr : &*found;
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

[[nodiscard]] bool has_reference_argument(
    const std::vector<RecordedCompile>& records,
    const fs::path& source,
    const std::string_view logical_name,
    const fs::path& interface_file) {
    for (const auto& record : records) {
        if (record.source != source.lexically_normal()) continue;
        const std::string expected = std::string{logical_name} + "=" + path_to_utf8(interface_file);
        for (std::size_t index = 0; index + 1 < record.arguments.size(); ++index) {
            if (record.arguments[index] == "/reference"
                && record.arguments[index + 1] == expected) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path root = fixture.path();
    const fs::path a_source = root / "modules" / "A.ixx";
    const fs::path b_source = root / "modules" / "B.ixx";
    const fs::path consumer_source = root / "src" / "consumer.cpp";
    write_text(a_source, "export module A;\n");
    write_text(b_source, "export module B;\n");
    write_text(consumer_source, "import A; import B;\n");

    auto layout = mqb::ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "temporary module project should create an artifact layout");
    if (!layout) return 1;

    auto a_artifacts = layout->for_source(a_source);
    auto b_artifacts = layout->for_source(b_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    expect(a_artifacts && b_artifacts && consumer_artifacts,
           "all module-wave sources should receive unique artifacts");
    if (!a_artifacts || !b_artifacts || !consumer_artifacts) return 1;

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = "C:/fake/cl.exe",
            .version = "19.51.test",
            .binary_stamp = "fake-stamp",
        },
        .linker = "C:/fake/link.exe",
        .librarian = "C:/fake/lib.exe",
        .vc_tools_root = "C:/fake",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    CompilerLikeRunner runner;
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental{
        toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator modules{incremental};

    mqb::modules::ModuleDependencyPlan plan;
    plan.compile_levels = {
        {a_source, b_source},
        {consumer_source},
    };
    plan.resolved_dependencies = {
        mqb::modules::ResolvedModuleDependency{
            .consumer_source = consumer_source,
            .provider_source = a_source,
            .logical_name = "A",
        },
        mqb::modules::ResolvedModuleDependency{
            .consumer_source = consumer_source,
            .provider_source = b_source,
            .logical_name = "B",
        },
    };

    mqb::CompilerOptions options;
    options.standard = mqb::CppStandard::latest;

    mqb::orchestration::ModuleCompileWaveRequest request;
    // Deliberately differ from plan order: result order must remain request order.
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer_source,
            .artifacts = *consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = a_source,
            .artifacts = *a_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = b_source,
            .artifacts = *b_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
    };
    request.plan = plan;
    request.compiler_options = options;
    request.working_directory = root;
    request.max_parallel_compiles = 2;

    const auto cold = modules.run(request);
    expect(cold.has_value(), "cold module wave should compile successfully");
    if (!cold) return 1;
    expect(cold->compiles.size() == 3,
           "cold module wave should return one result per requested source");
    expect(cold->compiles[0].source == consumer_source
               && cold->compiles[1].source == a_source
               && cold->compiles[2].source == b_source,
           "module wave result order should follow request source order");
    expect(cold->any_compiled, "cold module wave should report compiled work");
    expect(runner.calls() == 3, "cold module wave should launch exactly three compiles");
    expect(fs::is_regular_file(a_artifacts->module_interface)
               && fs::is_regular_file(b_artifacts->module_interface),
           "cold module wave should produce both provider IFC artifacts");

    const auto cold_records = runner.records();
    expect(has_reference_argument(
               cold_records, consumer_source, "A", a_artifacts->module_interface),
           "consumer compile should receive resolved /reference A=<planned IFC>");
    expect(has_reference_argument(
               cold_records, consumer_source, "B", b_artifacts->module_interface),
           "consumer compile should receive resolved /reference B=<planned IFC>");

    const auto warm = modules.run(request);
    expect(warm.has_value(), "warm module wave should validate successfully");
    if (!warm) return 1;
    expect(!warm->any_compiled, "unchanged warm module wave should be a complete cache hit");
    expect(runner.calls() == 3, "warm module wave should launch no compiler processes");

    std::error_code remove_error;
    fs::remove(a_artifacts->module_interface, remove_error);
    expect(!remove_error, "test should be able to remove one provider IFC");

    const auto repaired = modules.run(request);
    expect(repaired.has_value(), "missing provider IFC should be repaired successfully");
    if (!repaired) return 1;
    expect(repaired->any_compiled, "missing provider IFC should trigger compile work");
    expect(runner.calls() == 5,
           "missing A.ifc should rebuild only A and its consumer, leaving B cached");

    const auto* repaired_a = find_result(*repaired, a_source);
    const auto* repaired_b = find_result(*repaired, b_source);
    const auto* repaired_consumer = find_result(*repaired, consumer_source);
    expect(repaired_a != nullptr && repaired_a->result.compiled,
           "provider with missing IFC should recompile");
    expect(repaired_b != nullptr && !repaired_b->result.compiled,
           "unaffected provider in the same dependency level should remain cached");
    expect(repaired_consumer != nullptr && repaired_consumer->result.compiled,
           "consumer should rebuild after one provider recompiles");
    if (repaired_consumer) {
        expect(has_reason(*repaired_consumer, mqb::BuildReason::explicit_rebuild),
               "downstream provider rebuild should propagate as explicit_rebuild");
    }

    auto unsupported = request;
    unsupported.plan.unresolved_requirements.push_back(
        mqb::modules::UnresolvedModuleRequirement{
            .consumer_source = consumer_source,
            .requirement = mqb::modules::RequiredModule{.logical_name = "external"},
            .kind = mqb::modules::UnresolvedRequirementKind::named_module,
        });
    const int calls_before_unsupported = runner.calls();
    const auto unresolved = modules.run(unsupported);
    expect(!unresolved
               && unresolved.error().code
                   == mqb::orchestration::ModuleCompileErrorCode::unresolved_requirement,
           "unsupported unresolved requirements should fail closed before compilation");
    expect(runner.calls() == calls_before_unsupported,
           "unresolved requirement rejection should not launch the compiler");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_compile_coordinator_tests passed\n";
    return 0;
}
