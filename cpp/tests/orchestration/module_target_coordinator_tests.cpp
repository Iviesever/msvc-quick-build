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
        path_ = fs::temp_directory_path() / ("mqb-module-target-test-" + std::to_string(tick));
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

class ToolchainLikeRunner final : public mqb::process::ProcessRunner {
public:
    fs::path linker;
    std::atomic<int> scan_calls{0};
    std::atomic<int> compile_calls{0};
    std::atomic<int> link_calls{0};
    std::atomic<bool> emit_two_scan_rules{false};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        if (spec.executable == linker) {
            return run_link(spec);
        }
        for (const auto& argument : spec.arguments) {
            if (argument == "/scanDependencies") {
                return run_scan(spec);
            }
        }
        return run_compile(spec);
    }

private:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run_scan(const mqb::process::ProcessSpec& spec) {
        ++scan_calls;
        fs::path output;
        fs::path source;
        for (std::size_t index = 0; index < spec.arguments.size(); ++index) {
            if (spec.arguments[index] == "/scanDependencies"
                && index + 1 < spec.arguments.size()) {
                output = fs::path{spec.arguments[index + 1]};
            }
        }
        if (!spec.arguments.empty()) {
            source = fs::path{spec.arguments.back()};
        }

        const bool module_interface = source.extension() == ".ixx";
        std::string rule = module_interface
            ? R"json({"provides":[{"logical-name":"math"}]})json"
            : R"json({"requires":[{"logical-name":"math"}]})json";
        std::string json = "{\"version\":1,\"revision\":0,\"rules\":[" + rule;
        if (emit_two_scan_rules.load()) {
            json += ",{}";
        }
        json += "]}";
        write_text(output, json);
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
        const std::string metadata =
            "{\"Data\":{\"Source\":\""
            + json_escape(path_to_utf8(source))
            + "\",\"Includes\":[]}}";
        write_text(dependencies, metadata);
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
        write_text(output, "fake executable");
        return mqb::process::ProcessResult{.exit_code = 0};
    }
};

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path root = fixture.path();
    const fs::path module_source = root / "modules" / "math.ixx";
    const fs::path consumer_source = root / "src" / "main.cpp";
    write_text(module_source, "export module math;\n");
    write_text(consumer_source, "import math;\nint main() { return 0; }\n");

    auto layout = mqb::ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "module target fixture should create artifact layout");
    if (!layout) return 1;
    auto module_artifacts = layout->for_source(module_source);
    auto consumer_artifacts = layout->for_source(consumer_source);
    auto target_artifacts = layout->for_target("module-app");
    expect(module_artifacts && consumer_artifacts && target_artifacts,
           "module target fixture should map all source and target artifacts");
    if (!module_artifacts || !consumer_artifacts || !target_artifacts) return 1;

    const fs::path fake_compiler = root / "toolchain" / "cl.exe";
    const fs::path fake_linker = root / "toolchain" / "link.exe";
    const fs::path fake_librarian = root / "toolchain" / "lib.exe";
    write_text(fake_compiler, "fake compiler binary");
    write_text(fake_linker, "fake linker binary");
    write_text(fake_librarian, "fake librarian binary");

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = fake_compiler,
            .version = "19.51.test",
            .binary_stamp = "fake-compiler-stamp",
        },
        .linker = fake_linker,
        .librarian = fake_librarian,
        .vc_tools_root = fake_compiler.parent_path(),
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    ToolchainLikeRunner runner;
    runner.linker = fake_linker;
    mqb::msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    mqb::msvc::MsvcLinker linker{toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{toolchain, linker};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

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
    request.compiler_options.standard = mqb::CppStandard::latest;
    request.link_options.architecture = mqb::Architecture::x64;
    request.link_options.subsystem = mqb::LinkSubsystem::console;
    request.working_directory = root;
    request.max_parallel_scans = 2;
    request.max_parallel_compiles = 2;

    const auto cold = target.run(request);
    expect(cold.has_value(), "cold module target should scan, graph, compile and link successfully");
    if (!cold) return 1;
    expect(cold->scans.size() == 2
               && cold->scans[0].source == consumer_source
               && cold->scans[1].source == module_source,
           "module target scan results should preserve request source order");
    expect(cold->plan.compile_levels.size() == 2,
           "scanned provider and consumer should form two compile levels");
    expect(cold->plan.resolved_dependencies.size() == 1
               && cold->plan.resolved_dependencies.front().logical_name == "math"
               && cold->plan.resolved_dependencies.front().provider_source == module_source
               && cold->plan.resolved_dependencies.front().consumer_source == consumer_source,
           "module target should retain graph-selected math provider mapping");
    expect(cold->compiles.any_compiled,
           "cold module target should report compile work");
    expect(cold->link.linked,
           "cold module target should execute the link action");
    expect(runner.scan_calls.load() == 2
               && runner.compile_calls.load() == 2
               && runner.link_calls.load() == 1,
           "cold module target should run two scans, two compiles and one link");

    const auto warm = target.run(request);
    expect(warm.has_value(), "warm module target should validate successfully");
    if (!warm) return 1;
    expect(!warm->compiles.any_compiled,
           "warm module target should reuse both compile caches");
    expect(!warm->link.linked,
           "warm module target should reuse link cache");
    expect(runner.scan_calls.load() == 4,
           "topology scanning is intentionally repeated on the warm target build");
    expect(runner.compile_calls.load() == 2 && runner.link_calls.load() == 1,
           "warm module target should run zero compiles and zero links");

    runner.emit_two_scan_rules = true;
    const int compile_calls_before_invalid_scan = runner.compile_calls.load();
    const int link_calls_before_invalid_scan = runner.link_calls.load();
    const auto invalid = target.run(request);
    expect(!invalid
               && invalid.error().code
                   == mqb::orchestration::IncrementalModuleTargetErrorCode::invalid_scan_result,
           "multi-rule output from a one-source scan should fail closed");
    expect(runner.compile_calls.load() == compile_calls_before_invalid_scan
               && runner.link_calls.load() == link_calls_before_invalid_scan,
           "invalid scan cardinality should stop before compile and link");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_target_coordinator_tests passed\n";
    return 0;
}
