#include <algorithm>
#include <atomic>
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

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
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
            / ("mqb-module-target-inspection-" + std::to_string(tick));
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

class ToolchainLikeRunner final : public mqb::process::ProcessRunner {
public:
    fs::path linker;
    std::atomic<int> scan_calls{0};
    std::atomic<int> compile_calls{0};
    std::atomic<int> link_calls{0};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        if (spec.executable == linker) return run_link(spec);
        if (std::find(
                spec.arguments.begin(),
                spec.arguments.end(),
                "/scanDependencies") != spec.arguments.end()) {
            return run_scan(spec);
        }
        return run_compile(spec);
    }

private:
    [[nodiscard]] static std::string scan_rule_for(const fs::path& source) {
        const std::string filename = source.filename().string();
        if (filename == "math.ixx") {
            return R"json({"provides":[{"logical-name":"math","is-interface":true}]})json";
        }
        if (filename == "std.ixx") {
            return R"json({"provides":[{"logical-name":"std","is-interface":true}]})json";
        }
        if (filename == "std_main.cpp") {
            return R"json({"requires":[{"logical-name":"std"}]})json";
        }
        return R"json({"requires":[{"logical-name":"math"}]})json";
    }

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run_scan(const mqb::process::ProcessSpec& spec) {
        ++scan_calls;
        fs::path output;
        for (std::size_t index = 0; index + 1 < spec.arguments.size(); ++index) {
            if (spec.arguments[index] == "/scanDependencies") {
                output = fs::path{spec.arguments[index + 1]};
                break;
            }
        }
        const fs::path source = spec.arguments.empty()
            ? fs::path{}
            : fs::path{spec.arguments.back()};
        if (output.empty() || source.empty()) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "invalid synthetic scan request",
            };
        }
        write_text(
            output,
            "{\"version\":1,\"revision\":0,\"rules\":["
                + scan_rule_for(source) + "]}");
        return mqb::process::ProcessResult{.exit_code = 0};
    }

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run_compile(const mqb::process::ProcessSpec& spec) {
        ++compile_calls;
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
        if (!spec.arguments.empty()) source = fs::path{spec.arguments.back()};
        if (source.empty() || object.empty() || dependencies.empty()) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "invalid synthetic compile request",
            };
        }

        write_text(object, "fake object");
        if (!interface_file.empty()) write_text(interface_file, "fake ifc");
        write_text(
            dependencies,
            "{\"Data\":{\"Source\":\""
                + json_escape(path_to_utf8(source))
                + "\",\"Includes\":[]}}");
        return mqb::process::ProcessResult{.exit_code = 0};
    }

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run_link(const mqb::process::ProcessSpec& spec) {
        ++link_calls;
        fs::path output;
        for (const auto& argument : spec.arguments) {
            constexpr std::string_view prefix = "/OUT:";
            if (argument.starts_with(prefix)) {
                output = fs::path{argument.substr(prefix.size())};
                break;
            }
        }
        if (output.empty()) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "invalid synthetic link request",
            };
        }
        write_text(output, "fake executable");
        return mqb::process::ProcessResult{.exit_code = 0};
    }
};

[[nodiscard]] bool has_argument(
    const mqb::process::ProcessSpec& process,
    const std::string_view expected) {
    return std::find(
        process.arguments.begin(),
        process.arguments.end(),
        expected) != process.arguments.end();
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

[[nodiscard]] bool has_reason(
    const mqb::orchestration::IncrementalCompileInspection& inspection,
    const mqb::BuildReason reason) {
    return std::find(
        inspection.validation.reasons.begin(),
        inspection.validation.reasons.end(),
        reason) != inspection.validation.reasons.end();
}

[[nodiscard]] const mqb::orchestration::ModuleTargetScanInspection* find_scan(
    const mqb::orchestration::IncrementalModuleTargetInspection& inspection,
    const fs::path& source) {
    const auto found = std::find_if(
        inspection.scans.begin(),
        inspection.scans.end(),
        [&](const mqb::orchestration::ModuleTargetScanInspection& item) {
            return item.source.lexically_normal() == source.lexically_normal();
        });
    return found == inspection.scans.end() ? nullptr : &*found;
}

[[nodiscard]] const mqb::orchestration::ModuleCompileInspection* find_compile(
    const mqb::orchestration::ModuleCompileWaveInspection& inspection,
    const fs::path& source) {
    const auto found = std::find_if(
        inspection.compiles.begin(),
        inspection.compiles.end(),
        [&](const mqb::orchestration::ModuleCompileInspection& item) {
            return item.source.lexically_normal() == source.lexically_normal();
        });
    return found == inspection.compiles.end() ? nullptr : &*found;
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

void print_target_error(
    const mqb::orchestration::IncrementalModuleTargetError& error) {
    std::cerr << "target error: " << error.message << '\n';
    if (!error.source.empty()) {
        std::cerr << "  source=" << path_to_utf8(error.source) << '\n';
    }
    if (error.scan_error) {
        std::cerr << "  scan=" << error.scan_error->message << '\n';
    }
    if (error.graph_error) {
        std::cerr << "  graph=" << error.graph_error->message << '\n';
    }
    if (error.compile_error) {
        std::cerr << "  compile=" << error.compile_error->message << '\n';
    }
    if (error.link_error) {
        std::cerr << "  link=" << error.link_error->message << '\n';
    }
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path root = fixture.path();
    const fs::path toolchain_root = root / "toolchain";
    const fs::path fake_compiler = toolchain_root / "cl.exe";
    const fs::path fake_linker = toolchain_root / "link.exe";
    const fs::path fake_librarian = toolchain_root / "lib.exe";
    const fs::path std_source = toolchain_root / "modules" / "std.ixx";
    write_text(fake_compiler, "fake compiler");
    write_text(fake_linker, "fake linker");
    write_text(fake_librarian, "fake librarian");
    write_text(std_source, "export module std;\n");

    mqb::msvc::MsvcToolchain toolchain{
        .identity = {
            .compiler = fake_compiler,
            .version = "19.51.module-target-inspection",
            .binary_stamp = "fake-compiler-stamp",
        },
        .linker = fake_linker,
        .librarian = fake_librarian,
        .vc_tools_root = toolchain_root,
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };
    toolchain.standard_library_modules.std = std_source;

    ToolchainLikeRunner runner;
    runner.linker = fake_linker;
    mqb::msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{
        incremental_compile};
    mqb::msvc::MsvcLinker linker{toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{
        toolchain,
        linker};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

    const fs::path project_root = root / "project";
    const fs::path module_source = project_root / "modules" / "math.ixx";
    const fs::path consumer_source = project_root / "src" / "main.cpp";
    write_text(module_source, "export module math;\n");
    write_text(consumer_source, "import math;\nint main(){return 0;}\n");

    auto layout = mqb::ProjectArtifactLayout::create(project_root);
    expect(layout.has_value(), "project artifact layout should be created");
    if (!layout) return 1;
    auto module_artifacts = layout->for_source(module_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    auto target_artifacts = layout->for_target("module-inspection");
    expect(module_artifacts && consumer_artifacts && target_artifacts,
           "module target artifacts should be assigned");
    if (!module_artifacts || !consumer_artifacts || !target_artifacts) return 1;

    mqb::orchestration::IncrementalModuleTargetRequest request;
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = consumer_source,
            .artifacts = *consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = module_source,
            .artifacts = *module_artifacts,
            .kind = mqb::TranslationUnitKind::module_interface,
        },
    };
    request.target = *target_artifacts;
    request.artifact_layout = *layout;
    request.compiler_options.standard = mqb::CppStandard::latest;
    request.link_options.architecture = mqb::Architecture::x64;
    request.link_options.subsystem = mqb::LinkSubsystem::console;
    request.working_directory = project_root;
    request.max_parallel_scans = 2;
    request.max_parallel_compiles = 2;

    const auto cold = target.inspect(request);
    expect(cold.has_value(), "cold module target inspection should succeed");
    if (!cold) {
        print_target_error(cold.error());
        return 1;
    }
    expect(cold->scan_required() && !cold->graph_ready(),
           "cold module target should stop before graph construction");
    expect(cold->scans.size() == 2
               && cold->scans[0].source == consumer_source
               && cold->scans[1].source == module_source,
           "cold scan inspection should preserve requested source order");
    expect(!cold->plan && !cold->compiles && !cold->link,
           "cold scan-only inspection must not invent graph/compile/link stages");
    expect(runner.scan_calls.load() == 0
               && runner.compile_calls.load() == 0
               && runner.link_calls.load() == 0,
           "cold module target inspection must not launch any process");
    expect(!fs::exists(layout->artifact_root()),
           "cold module target inspection must not create project .mqb state");
    for (const auto& scan : cold->scans) {
        expect(scan.result.scan_required(),
               "every cold requested source should expose a pending scan");
        expect(has_argument(scan.result.recipe.process, "/scanDependencies"),
               "cold scan inspection should expose exact /scanDependencies argv");
        expect(scan.result.recipe.process.executable == fake_compiler,
               "cold scan recipe should bind the selected compiler");
    }

    const auto cold_run = target.run(request);
    expect(cold_run.has_value(), "cold module target run should succeed");
    if (!cold_run) {
        print_target_error(cold_run.error());
        return 1;
    }
    expect(runner.scan_calls.load() == 2
               && runner.compile_calls.load() == 2
               && runner.link_calls.load() == 1,
           "cold run should execute two scans, two compiles, and one link");

    const std::string consumer_scan_before = read_text(
        consumer_artifacts->module_dependencies);
    const std::string module_scan_before = read_text(
        module_artifacts->module_dependencies);
    const std::string consumer_cache_before = read_text(
        consumer_artifacts->compile_cache);
    const std::string module_cache_before = read_text(
        module_artifacts->compile_cache);
    const std::string link_cache_before = read_text(target_artifacts->link_cache);

    const auto warm = target.inspect(request);
    expect(warm.has_value(), "warm module target inspection should succeed");
    if (!warm) {
        print_target_error(warm.error());
        return 1;
    }
    expect(warm->graph_ready() && !warm->scan_required(),
           "warm reusable P1689 should unlock the full graph");
    expect(warm->plan && warm->plan->compile_levels.size() == 2,
           "warm module target should expose the provider/consumer levels");
    expect(warm->compile_request && warm->compiles
               && !warm->compiles->any_planned,
           "unchanged warm compile wave should be fully reusable");
    expect(warm->link_request && warm->link && warm->link->plan.empty(),
           "unchanged warm target should expose an up-to-date link decision");
    expect(runner.scan_calls.load() == 2
               && runner.compile_calls.load() == 2
               && runner.link_calls.load() == 1,
           "warm full-target inspection must launch no process");
    expect(read_text(consumer_artifacts->module_dependencies) == consumer_scan_before
               && read_text(module_artifacts->module_dependencies) == module_scan_before
               && read_text(consumer_artifacts->compile_cache) == consumer_cache_before
               && read_text(module_artifacts->compile_cache) == module_cache_before
               && read_text(target_artifacts->link_cache) == link_cache_before,
           "warm full-target inspection must not mutate scan/compile/link state");

    const auto* warm_consumer = find_compile(*warm->compiles, consumer_source);
    expect(warm_consumer != nullptr,
           "warm compile inspection should retain the consumer request");
    if (warm_consumer) {
        auto recipe = executor.build_recipe(execution_request_for(warm_consumer->request));
        expect(recipe.has_value(),
               "warm consumer inspection should model an exact compile recipe");
        if (recipe) {
            expect(has_pair(
                       recipe->process,
                       "/reference",
                       "math=" + path_to_utf8(module_artifacts->module_interface)),
                   "consumer recipe should expose /reference math=<IFC>");
        }
    }

    std::error_code remove_error;
    fs::remove(module_artifacts->module_interface, remove_error);
    expect(!remove_error, "fixture should remove the provider IFC");
    const auto repair = target.inspect(request);
    expect(repair.has_value(), "missing provider IFC inspection should succeed");
    if (!repair) {
        print_target_error(repair.error());
        return 1;
    }
    expect(repair->graph_ready() && repair->compiles && repair->link,
           "reusable P1689 should still permit compile/link repair planning");
    expect(repair->compiles->any_planned && !repair->link->plan.empty(),
           "provider repair should plan compile work and a relink");
    expect(repair->link_request && repair->link_request->force_relink,
           "planned module work should force the exact final-link request");
    const auto* repair_provider = find_compile(*repair->compiles, module_source);
    const auto* repair_consumer = find_compile(*repair->compiles, consumer_source);
    expect(repair_provider != nullptr && !repair_provider->result.plan.empty(),
           "missing provider IFC should plan that provider");
    expect(repair_consumer != nullptr && !repair_consumer->result.plan.empty(),
           "missing provider IFC should plan its downstream consumer");
    if (repair_consumer) {
        expect(repair_consumer->request.force_rebuild
                   && has_reason(
                       repair_consumer->result,
                       mqb::BuildReason::explicit_rebuild),
               "provider repair should propagate as explicit_rebuild");
    }
    expect(runner.scan_calls.load() == 2
               && runner.compile_calls.load() == 2
               && runner.link_calls.load() == 1,
           "provider repair inspection must launch no process");
    expect(!fs::exists(module_artifacts->module_interface),
           "inspection must not repair the missing IFC itself");

    const auto consumer_scan_time = fs::last_write_time(
        consumer_artifacts->module_dependencies);
    write_text(
        consumer_source,
        "import math;\nint main(){return 1;} // topology may have changed\n");
    fs::last_write_time(
        consumer_source,
        consumer_scan_time + std::chrono::seconds{2});
    const auto stale_topology = target.inspect(request);
    expect(stale_topology.has_value(),
           "stale source topology inspection should succeed");
    if (!stale_topology) {
        print_target_error(stale_topology.error());
        return 1;
    }
    const auto* stale_consumer_scan = find_scan(*stale_topology, consumer_source);
    expect(stale_topology->scan_required() && !stale_topology->plan,
           "stale requested scan must stop before graph construction");
    expect(stale_consumer_scan != nullptr
               && stale_consumer_scan->result.scan_required(),
           "changed consumer should expose a pending scan");
    expect(!stale_topology->compiles && !stale_topology->link,
           "stale topology must not expose downstream compile/link guesses");
    expect(runner.scan_calls.load() == 2
               && runner.compile_calls.load() == 2
               && runner.link_calls.load() == 1,
           "stale topology inspection must not execute the pending scan");

    // A second project exercises the toolchain-owned standard-module fixed
    // point. Before its first scan, inspection cannot yet know that std is
    // needed. After a successful build, both project and std scans are reusable
    // and the full target becomes inspectable without execution.
    const fs::path std_project_root = root / "std-project";
    const fs::path std_consumer = std_project_root / "src" / "std_main.cpp";
    write_text(std_consumer, "import std;\nint main(){return 0;}\n");
    auto std_layout = mqb::ProjectArtifactLayout::create(std_project_root);
    expect(std_layout.has_value(), "std project artifact layout should be created");
    if (!std_layout) return 1;
    auto std_consumer_artifacts = std_layout->for_source(std_consumer);
    auto std_provider_artifacts = std_layout->for_source(std_source);
    auto std_target_artifacts = std_layout->for_target("std-inspection");
    expect(std_consumer_artifacts && std_provider_artifacts && std_target_artifacts,
           "std project artifacts should be assigned");
    if (!std_consumer_artifacts || !std_provider_artifacts || !std_target_artifacts) {
        return 1;
    }

    mqb::orchestration::IncrementalModuleTargetRequest std_request;
    std_request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = std_consumer,
            .artifacts = *std_consumer_artifacts,
            .kind = mqb::TranslationUnitKind::source,
        },
    };
    std_request.target = *std_target_artifacts;
    std_request.artifact_layout = *std_layout;
    std_request.compiler_options.standard = mqb::CppStandard::latest;
    std_request.link_options.architecture = mqb::Architecture::x64;
    std_request.link_options.subsystem = mqb::LinkSubsystem::console;
    std_request.working_directory = std_project_root;
    std_request.max_parallel_scans = 2;
    std_request.max_parallel_compiles = 2;

    const int scans_before_std = runner.scan_calls.load();
    const int compiles_before_std = runner.compile_calls.load();
    const int links_before_std = runner.link_calls.load();
    const auto std_cold = target.inspect(std_request);
    expect(std_cold.has_value() && std_cold->scan_required(),
           "cold std consumer should expose only its currently knowable scan");
    if (std_cold) {
        expect(std_cold->scans.size() == 1
                   && !std_cold->scans.front().toolchain_owned,
               "std provider must not be guessed before consumer P1689 exists");
    }
    expect(runner.scan_calls.load() == scans_before_std
               && runner.compile_calls.load() == compiles_before_std
               && runner.link_calls.load() == links_before_std,
           "cold std target inspection must not execute discovery");

    const auto std_run = target.run(std_request);
    expect(std_run.has_value(), "cold std module target should build successfully");
    if (!std_run) {
        print_target_error(std_run.error());
        return 1;
    }
    expect(runner.scan_calls.load() == scans_before_std + 2
               && runner.compile_calls.load() == compiles_before_std + 2
               && runner.link_calls.load() == links_before_std + 1,
           "std target should scan/compile consumer and toolchain provider exactly once");

    const int scans_after_std_run = runner.scan_calls.load();
    const int compiles_after_std_run = runner.compile_calls.load();
    const int links_after_std_run = runner.link_calls.load();
    const auto std_warm = target.inspect(std_request);
    expect(std_warm.has_value() && std_warm->graph_ready(),
           "warm std target should expose the full graph without execution");
    if (!std_warm) {
        print_target_error(std_warm.error());
        return 1;
    }
    expect(std_warm->scans.size() == 2
               && std_warm->scans[0].source == std_consumer
               && !std_warm->scans[0].toolchain_owned
               && std_warm->scans[1].source == std_source
               && std_warm->scans[1].toolchain_owned,
           "warm std inspection should append the selected toolchain-owned provider");
    expect(std_warm->compile_request
               && std_warm->compile_request->sources.size() == 2
               && std_warm->compiles
               && !std_warm->compiles->any_planned
               && std_warm->link
               && std_warm->link->plan.empty(),
           "unchanged warm std target should be fully reusable");
    expect(runner.scan_calls.load() == scans_after_std_run
               && runner.compile_calls.load() == compiles_after_std_run
               && runner.link_calls.load() == links_after_std_run,
           "warm std target inspection must launch no process");

    fs::remove(std_provider_artifacts->compile_cache, remove_error);
    expect(!remove_error, "fixture should remove the std provider scan seal");
    const auto std_provider_pending = target.inspect(std_request);
    expect(std_provider_pending.has_value()
               && std_provider_pending->scan_required()
               && !std_provider_pending->plan,
           "missing std provider scan seal should stop at graph-pending");
    if (std_provider_pending) {
        expect(std_provider_pending->scans.size() == 2,
               "pending std provider inspection should retain both scan steps");
        const auto* provider_scan = find_scan(*std_provider_pending, std_source);
        expect(provider_scan != nullptr
                   && provider_scan->toolchain_owned
                   && provider_scan->result.scan_required()
                   && has_argument(
                       provider_scan->result.recipe.process,
                       "/scanDependencies"),
               "pending std provider should expose its exact toolchain-owned scan recipe");
        expect(!std_provider_pending->compiles && !std_provider_pending->link,
               "pending std topology must not invent compile/link decisions");
    }
    expect(!fs::exists(std_provider_artifacts->compile_cache),
           "std provider inspection must not recreate the missing cache seal");
    expect(runner.scan_calls.load() == scans_after_std_run
               && runner.compile_calls.load() == compiles_after_std_run
               && runner.link_calls.load() == links_after_std_run,
           "pending std provider inspection must not execute any stage");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_target_inspection_contract passed\n";
    return 0;
}
