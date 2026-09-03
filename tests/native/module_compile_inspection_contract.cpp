#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
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
        path_ = fs::temp_directory_path()
            / ("mqb-module-compile-inspection-" + std::to_string(tick));
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
    std::string escaped;
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

[[nodiscard]] mqb::SourceArtifacts artifacts_for(
    const fs::path& root,
    const std::string_view key) {
    const fs::path state = root / "state";
    const std::string stem{key};
    return mqb::SourceArtifacts{
        .object = state / "obj" / (stem + ".obj"),
        .dependencies = state / "deps" / (stem + ".json"),
        .module_dependencies = state / "scan" / (stem + ".p1689.json"),
        .module_interface = state / "ifc" / (stem + ".ifc"),
        .compile_cache = state / "cache" / (stem + ".mqbcache"),
    };
}

struct RecordedCompile {
    fs::path source;
    mqb::process::ProcessSpec process;
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
            } else if (argument == "/sourceDependencies"
                       && index + 1 < spec.arguments.size()) {
                dependencies = fs::path{spec.arguments[++index]};
            } else if (argument == "/ifcOutput"
                       && index + 1 < spec.arguments.size()) {
                interface_file = fs::path{spec.arguments[++index]};
            }
        }
        if (!spec.arguments.empty()) {
            source = fs::path{spec.arguments.back()};
        }

        if (!object.empty()) write_text(object, "fake object");
        if (!interface_file.empty()) write_text(interface_file, "fake ifc");
        if (dependencies.empty()) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "missing sourceDependencies output",
            };
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
                .process = spec,
            });
        }
        ++calls_;
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "fake cl.exe success",
        };
    }

    [[nodiscard]] int calls() const noexcept { return calls_.load(); }

private:
    std::atomic<int> calls_{0};
    std::mutex mutex_;
    std::vector<RecordedCompile> records_;
};

[[nodiscard]] const mqb::orchestration::ModuleCompileInspection* find_inspection(
    const mqb::orchestration::ModuleCompileWaveInspection& result,
    const fs::path& source) {
    const auto found = std::find_if(
        result.compiles.begin(),
        result.compiles.end(),
        [&source](const mqb::orchestration::ModuleCompileInspection& item) {
            return item.source.lexically_normal() == source.lexically_normal();
        });
    return found == result.compiles.end() ? nullptr : &*found;
}

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
    const mqb::orchestration::IncrementalCompileInspection& result,
    const mqb::BuildReason reason) {
    return std::find(
        result.validation.reasons.begin(),
        result.validation.reasons.end(),
        reason) != result.validation.reasons.end();
}

[[nodiscard]] bool has_argument(
    const mqb::process::ProcessSpec& process,
    const std::string_view argument) {
    return std::find(
        process.arguments.begin(),
        process.arguments.end(),
        argument) != process.arguments.end();
}

[[nodiscard]] bool has_pair(
    const mqb::process::ProcessSpec& process,
    const std::string_view flag,
    const std::string_view value) {
    for (std::size_t index = 0; index + 1 < process.arguments.size(); ++index) {
        if (process.arguments[index] == flag
            && process.arguments[index + 1] == value) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] mqb::msvc::CompileExecutionRequest execution_request_for(
    const mqb::orchestration::IncrementalCompileRequest& request) {
    return mqb::msvc::CompileExecutionRequest{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
}

[[nodiscard]] bool artifact_absent(const mqb::SourceArtifacts& artifacts) {
    return !fs::exists(artifacts.object)
        && !fs::exists(artifacts.dependencies)
        && !fs::exists(artifacts.module_interface)
        && !fs::exists(artifacts.compile_cache);
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

    const mqb::SourceArtifacts a_artifacts = artifacts_for(root, "A");
    const mqb::SourceArtifacts b_artifacts = artifacts_for(root, "B");
    const mqb::SourceArtifacts consumer_artifacts = artifacts_for(root, "consumer");

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = root / "toolchain" / "cl.exe",
            .version = "19.51.module-plan",
            .binary_stamp = "module-plan-stamp",
        },
        .linker = root / "toolchain" / "link.exe",
        .librarian = root / "toolchain" / "lib.exe",
        .vc_tools_root = root / "toolchain",
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
    // Deliberately differ from graph order: public inspection order must remain
    // the stable request order.
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer_source,
            .artifacts = consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = a_source,
            .artifacts = a_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = b_source,
            .artifacts = b_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
    };
    request.plan = plan;
    request.compiler_options = options;
    request.working_directory = root;
    request.max_parallel_compiles = 2;

    const auto cold = modules.inspect(request);
    expect(cold.has_value(), "cold module compile-wave inspection should succeed");
    if (!cold) return 1;
    expect(cold->any_planned, "cold module wave should expose planned compile work");
    expect(cold->compiles.size() == 3,
           "cold module wave should inspect every requested source");
    expect(cold->compiles[0].source == consumer_source
               && cold->compiles[1].source == a_source
               && cold->compiles[2].source == b_source,
           "module-wave inspection order should follow request order");
    expect(runner.calls() == 0,
           "module compile-wave inspection must not launch cl.exe");
    expect(artifact_absent(a_artifacts)
               && artifact_absent(b_artifacts)
               && artifact_absent(consumer_artifacts),
           "cold module compile-wave inspection must not create outputs or cache state");

    const auto* cold_a = find_inspection(*cold, a_source);
    const auto* cold_b = find_inspection(*cold, b_source);
    const auto* cold_consumer = find_inspection(*cold, consumer_source);
    expect(cold_a != nullptr && !cold_a->result.plan.empty(),
           "cold A provider should have a compile action");
    expect(cold_b != nullptr && !cold_b->result.plan.empty(),
           "cold B provider should have a compile action");
    expect(cold_consumer != nullptr && !cold_consumer->result.plan.empty(),
           "cold consumer should have a compile action");
    if (cold_consumer) {
        expect(cold_consumer->request.force_rebuild,
               "planned providers should project a forced consumer rebuild");
        expect(has_reason(
                   cold_consumer->result,
                   mqb::BuildReason::explicit_rebuild),
               "provider propagation should remain an explicit typed reason");
        expect(cold_consumer->request.unit.module_references.size() == 2,
               "consumer inspection should expose both resolved module references");

        auto recipe = executor.build_recipe(
            execution_request_for(cold_consumer->request));
        expect(recipe.has_value(),
               "consumer inspection request should build the exact MSVC recipe");
        if (recipe) {
            expect(has_pair(
                       recipe->process,
                       "/reference",
                       "A=" + path_to_utf8(a_artifacts.module_interface)),
                   "consumer recipe should expose /reference A=<IFC>");
            expect(has_pair(
                       recipe->process,
                       "/reference",
                       "B=" + path_to_utf8(b_artifacts.module_interface)),
                   "consumer recipe should expose /reference B=<IFC>");
        }
    }
    expect(runner.calls() == 0,
           "building recipes from compile-wave inspection must remain side-effect-free");

    const auto cold_run = modules.run(request);
    expect(cold_run.has_value() && cold_run->any_compiled,
           "cold module wave should execute successfully after inspection");
    if (!cold_run) return 1;
    expect(runner.calls() == 3,
           "cold module wave should launch exactly three compiles");
    for (const auto& inspected : cold->compiles) {
        const auto* executed = find_result(*cold_run, inspected.source);
        expect(executed != nullptr,
               "real module run should return every inspected source");
        if (executed) {
            expect(executed->result.validation.reasons
                       == inspected.result.validation.reasons,
                   "cold inspection and real execution should expose identical reasons");
            expect(executed->result.plan.actions.size()
                       == inspected.result.plan.actions.size(),
                   "cold inspection and real execution should expose identical plan cardinality");
        }
    }

    const std::string a_cache_before = read_text(a_artifacts.compile_cache);
    const std::string b_cache_before = read_text(b_artifacts.compile_cache);
    const std::string consumer_cache_before = read_text(consumer_artifacts.compile_cache);
    const auto warm = modules.inspect(request);
    expect(warm.has_value() && !warm->any_planned,
           "unchanged warm module wave should be fully reusable");
    if (warm) {
        for (const auto& inspected : warm->compiles) {
            expect(inspected.result.plan.empty(),
                   "every warm module node should expose an empty plan");
            expect(!inspected.request.force_rebuild,
                   "warm providers must not force downstream rebuilds");
        }
    }
    expect(runner.calls() == 3,
           "warm module compile-wave inspection must not launch cl.exe");
    expect(read_text(a_artifacts.compile_cache) == a_cache_before
               && read_text(b_artifacts.compile_cache) == b_cache_before
               && read_text(consumer_artifacts.compile_cache) == consumer_cache_before,
           "warm module compile-wave inspection must not mutate cache state");

    std::error_code remove_error;
    fs::remove(a_artifacts.module_interface, remove_error);
    expect(!remove_error, "test should remove one provider IFC");
    const auto repair = modules.inspect(request);
    expect(repair.has_value() && repair->any_planned,
           "missing provider IFC should produce a repair plan");
    if (repair) {
        const auto* repair_a = find_inspection(*repair, a_source);
        const auto* repair_b = find_inspection(*repair, b_source);
        const auto* repair_consumer = find_inspection(*repair, consumer_source);
        expect(repair_a != nullptr && !repair_a->result.plan.empty(),
               "provider with a missing IFC should be planned");
        expect(repair_b != nullptr && repair_b->result.plan.empty(),
               "unaffected same-level provider should remain reusable");
        expect(repair_consumer != nullptr && !repair_consumer->result.plan.empty(),
               "consumer should be planned after one provider repair");
        if (repair_consumer) {
            expect(repair_consumer->request.force_rebuild
                       && has_reason(
                           repair_consumer->result,
                           mqb::BuildReason::explicit_rebuild),
                   "provider repair should propagate explicit downstream invalidation");
        }
    }
    expect(runner.calls() == 3,
           "repair inspection must not launch cl.exe");

    // Header Unit nodes use the same graph-aware inspection authority while
    // retaining their IFC-only producer recipe.
    const fs::path header = root / "include" / "util.hpp";
    const fs::path header_consumer = root / "src" / "header_consumer.cpp";
    write_text(header, "inline int answer() { return 42; }\n");
    write_text(header_consumer, "import \"util.hpp\";\n");
    const mqb::SourceArtifacts header_artifacts = artifacts_for(root, "util_header");
    const mqb::SourceArtifacts header_consumer_artifacts = artifacts_for(
        root,
        "header_consumer");

    mqb::modules::ModuleDependencyPlan header_plan;
    header_plan.compile_levels = {
        {header},
        {header_consumer},
    };
    header_plan.header_units = {
        mqb::modules::PlannedHeaderUnit{
            .source = header,
            .header_name = "util.hpp",
            .lookup_method = mqb::modules::LookupMethod::include_quote,
        },
    };
    header_plan.resolved_header_unit_dependencies = {
        mqb::modules::ResolvedHeaderUnitDependency{
            .consumer_source = header_consumer,
            .provider_source = header,
            .header_name = "util.hpp",
            .lookup_method = mqb::modules::LookupMethod::include_quote,
        },
    };

    mqb::orchestration::ModuleCompileWaveRequest header_request;
    header_request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = header_consumer,
            .artifacts = header_consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
    };
    header_request.header_units = {
        mqb::orchestration::ModuleCompileHeaderUnitRequest{
            .source = header,
            .header_name = "util.hpp",
            .lookup_method = mqb::HeaderUnitLookupMethod::quote,
            .artifacts = header_artifacts,
        },
    };
    header_request.plan = header_plan;
    header_request.compiler_options = options;
    header_request.compiler_options.include_directories = {header.parent_path()};
    header_request.working_directory = root;
    header_request.max_parallel_compiles = 2;

    const auto header_cold = modules.inspect(header_request);
    expect(header_cold.has_value() && header_cold->any_planned,
           "cold Header Unit wave inspection should succeed");
    if (header_cold) {
        expect(header_cold->header_unit_compiles.size() == 1
                   && !header_cold->header_unit_compiles.front().result.plan.empty(),
               "Header Unit provider should expose one planned IFC-only compile");
        expect(header_cold->compiles.size() == 1
                   && !header_cold->compiles.front().result.plan.empty(),
               "Header Unit consumer should be planned after its provider");
        if (!header_cold->header_unit_compiles.empty()) {
            const auto& inspected_header = header_cold->header_unit_compiles.front();
            expect(inspected_header.request.unit.header_unit.has_value(),
                   "Header Unit inspection should preserve typed producer identity");
            expect(inspected_header.request.unit.outputs.size() == 1
                       && inspected_header.request.unit.outputs.front().path
                           == header_artifacts.module_interface,
                   "Header Unit producer should remain IFC-only");
            auto recipe = executor.build_recipe(
                execution_request_for(inspected_header.request));
            expect(recipe.has_value(),
                   "Header Unit inspection request should build an exact recipe");
            if (recipe) {
                expect(has_argument(recipe->process, "/exportHeader")
                           && has_argument(recipe->process, "/headerName:quote")
                           && has_argument(recipe->process, "util.hpp"),
                       "Header Unit recipe should preserve export and quote lookup semantics");
                expect(has_pair(
                           recipe->process,
                           "/ifcOutput",
                           path_to_utf8(header_artifacts.module_interface)),
                       "Header Unit recipe should expose the planned IFC output");
            }
        }
        if (!header_cold->compiles.empty()) {
            const auto& inspected_consumer = header_cold->compiles.front();
            expect(inspected_consumer.request.force_rebuild
                       && has_reason(
                           inspected_consumer.result,
                           mqb::BuildReason::explicit_rebuild),
                   "planned Header Unit provider should force its consumer");
            auto recipe = executor.build_recipe(
                execution_request_for(inspected_consumer.request));
            expect(recipe.has_value(),
                   "Header Unit consumer inspection should build an exact recipe");
            if (recipe) {
                expect(has_pair(
                           recipe->process,
                           "/headerUnit:quote",
                           "util.hpp=" + path_to_utf8(header_artifacts.module_interface)),
                       "consumer recipe should expose /headerUnit:quote util.hpp=<IFC>");
            }
        }
    }
    expect(runner.calls() == 3,
           "Header Unit compile-wave inspection must not launch cl.exe");
    expect(artifact_absent(header_artifacts)
               && artifact_absent(header_consumer_artifacts),
           "Header Unit inspection must not create producer/consumer state");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_compile_inspection_contract passed\n";
    return 0;
}
