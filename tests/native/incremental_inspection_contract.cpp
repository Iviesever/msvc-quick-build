#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
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
            / ("mqb-incremental-inspection-test-" + std::to_string(tick));
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

template <typename Validation>
[[nodiscard]] bool has_reason(
    const Validation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

class CompilerRunner final : public mqb::process::ProcessRunner {
public:
    fs::path source;
    fs::path header;
    fs::path object;
    fs::path dependencies;
    int calls{};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        write_text(object, "compiled object");
        const std::string json =
            "{\"Data\":{\"Source\":\""
            + json_escape(path_to_utf8(source))
            + "\",\"Includes\":[\""
            + json_escape(path_to_utf8(header))
            + "\"]}}";
        write_text(dependencies, json);
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "compile success",
        };
    }
};

class LinkerRunner final : public mqb::process::ProcessRunner {
public:
    fs::path output;
    int calls{};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        write_text(output, "linked executable");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "link success",
        };
    }
};

class LibrarianRunner final : public mqb::process::ProcessRunner {
public:
    int calls{};

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        const auto output_argument = std::find_if(
            spec.arguments.begin(),
            spec.arguments.end(),
            [](const std::string& argument) {
                return argument.starts_with("/OUT:");
            });
        if (output_argument == spec.arguments.end()) {
            return mqb::process::ProcessResult{
                .exit_code = 1,
                .stderr_text = "missing /OUT",
            };
        }
        write_text(fs::path{output_argument->substr(5)}, "archived library");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "archive success",
        };
    }
};

} // namespace

int main() {
    TemporaryDirectory fixture;

    const fs::path fake_compiler = fixture.path() / "toolchain" / "cl.exe";
    const fs::path fake_linker = fixture.path() / "toolchain" / "link.exe";
    const fs::path fake_librarian = fixture.path() / "toolchain" / "lib.exe";
    write_text(fake_compiler, "compiler identity");
    write_text(fake_linker, "linker identity");
    write_text(fake_librarian, "librarian identity");

    const mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = fake_compiler,
            .version = "19.51.inspect",
            .binary_stamp = "compiler-stamp",
        },
        .linker = fake_linker,
        .librarian = fake_librarian,
        .vc_tools_root = fixture.path() / "toolchain",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    // Keep compiler-visible input roots physically separate from generated
    // outputs/state so fixture writes cannot mutate include-directory freshness.
    const fs::path source = fixture.path() / "input" / "compile" / "main.cpp";
    const fs::path header = fixture.path() / "input" / "compile" / "value.hpp";
    const fs::path object = fixture.path() / "state" / "compile" / "main.obj";
    const fs::path dependencies = fixture.path() / "state" / "compile" / "main.deps.json";
    const fs::path compile_cache = fixture.path() / "state" / "compile-cache" / "main.mqbcache";
    write_text(source, "#include \"value.hpp\"\nint main() { return VALUE; }\n");
    write_text(header, "#pragma once\n#define VALUE 0\n");

    mqb::TranslationUnit unit;
    unit.source = source;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {mqb::Artifact{object, mqb::ArtifactKind::object}};

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = mqb::BuildConfiguration::debug;
    compiler_options.architecture = mqb::Architecture::x64;
    compiler_options.standard = mqb::CppStandard::cpp23;
    compiler_options.include_directories = {header.parent_path()};

    CompilerRunner compiler_runner;
    compiler_runner.source = source;
    compiler_runner.header = header;
    compiler_runner.object = object;
    compiler_runner.dependencies = dependencies;
    mqb::msvc::MsvcCompileExecutor compile_executor{toolchain, compiler_runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        toolchain,
        compile_executor};
    const mqb::orchestration::IncrementalCompileRequest compile_request{
        .unit = unit,
        .options = compiler_options,
        .cache_file = compile_cache,
        .source_dependencies_file = dependencies,
        .working_directory = fixture.path(),
    };

    const auto compile_cold_inspection = compile_coordinator.inspect(compile_request);
    expect(compile_cold_inspection.has_value(), "cold compile inspection should succeed");
    if (compile_cold_inspection) {
        expect(compile_cold_inspection->plan.actions.size() == 1
                   && std::holds_alternative<mqb::CompileAction>(
                       compile_cold_inspection->plan.actions.front()),
               "cold compile inspection should expose one CompileAction");
        expect(has_reason(
                   compile_cold_inspection->validation,
                   mqb::BuildReason::missing_cache_entry),
               "cold compile inspection should report missing cache metadata");
        expect(has_reason(
                   compile_cold_inspection->validation,
                   mqb::BuildReason::missing_output),
               "cold compile inspection should report missing object output");
    }
    expect(compiler_runner.calls == 0,
           "compile inspection must not launch cl.exe");
    expect(!fs::exists(object) && !fs::exists(dependencies) && !fs::exists(compile_cache),
           "compile inspection must not create compile outputs, dependency metadata, or cache state");

    const auto compile_cold_run = compile_coordinator.run(compile_request);
    expect(compile_cold_run.has_value() && compile_cold_run->compiled,
           "cold compile run should consume the inspected decision and compile");
    if (compile_cold_inspection && compile_cold_run) {
        expect(compile_cold_run->validation.reasons
                   == compile_cold_inspection->validation.reasons,
               "compile run and cold inspection must expose identical rebuild reasons");
        expect(compile_cold_run->plan.actions.size()
                   == compile_cold_inspection->plan.actions.size(),
               "compile run and cold inspection must expose the same plan cardinality");
    }
    expect(compiler_runner.calls == 1,
           "cold compile run should launch cl.exe exactly once");
    expect(fs::is_regular_file(object) && fs::is_regular_file(compile_cache),
           "cold compile run should create object and cache state");

    const std::string compile_cache_before = read_text(compile_cache);
    const std::string object_before = read_text(object);
    const auto compile_warm_inspection = compile_coordinator.inspect(compile_request);
    expect(compile_warm_inspection.has_value(), "warm compile inspection should succeed");
    if (compile_warm_inspection) {
        expect(compile_warm_inspection->validation.reusable(),
               "warm compile inspection should report reusable cache state");
        expect(compile_warm_inspection->plan.empty(),
               "warm compile inspection should expose an empty plan");
    }
    expect(compiler_runner.calls == 1,
           "warm compile inspection must not launch cl.exe");
    expect(read_text(compile_cache) == compile_cache_before && read_text(object) == object_before,
           "warm compile inspection must not mutate cache or object content");

    // Link inspection: resolve the same freshness decision without link.exe or cache writes.
    const fs::path link_object = fixture.path() / "link" / "main.obj";
    const fs::path link_output = fixture.path() / "link" / "main.exe";
    const fs::path link_cache = fixture.path() / "link" / "main.linkcache";
    write_text(link_object, "link object input");

    mqb::LinkOptions link_options;
    link_options.configuration = mqb::BuildConfiguration::debug;
    link_options.architecture = mqb::Architecture::x64;
    link_options.subsystem = mqb::LinkSubsystem::console;

    LinkerRunner linker_runner;
    linker_runner.output = link_output;
    mqb::msvc::MsvcLinker linker{toolchain, linker_runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{toolchain, linker};
    const mqb::orchestration::IncrementalLinkRequest link_request{
        .objects = {link_object},
        .output = link_output,
        .options = link_options,
        .cache_file = link_cache,
        .working_directory = fixture.path(),
    };

    const auto link_cold_inspection = link_coordinator.inspect(link_request);
    expect(link_cold_inspection.has_value(), "cold link inspection should succeed");
    if (link_cold_inspection) {
        expect(link_cold_inspection->plan.actions.size() == 1
                   && std::holds_alternative<mqb::LinkAction>(
                       link_cold_inspection->plan.actions.front()),
               "cold link inspection should expose one LinkAction");
        expect(has_reason(
                   link_cold_inspection->validation,
                   mqb::BuildReason::missing_cache_entry),
               "cold link inspection should report missing cache metadata");
        expect(has_reason(
                   link_cold_inspection->validation,
                   mqb::BuildReason::missing_output),
               "cold link inspection should report missing target output");
    }
    expect(linker_runner.calls == 0,
           "link inspection must not launch link.exe");
    expect(!fs::exists(link_output) && !fs::exists(link_cache),
           "link inspection must not create target output or link cache state");

    const auto link_cold_run = link_coordinator.run(link_request);
    expect(link_cold_run.has_value() && link_cold_run->linked,
           "cold link run should consume the inspected decision and link");
    if (link_cold_inspection && link_cold_run) {
        expect(link_cold_run->validation.reasons == link_cold_inspection->validation.reasons,
               "link run and cold inspection must expose identical rebuild reasons");
        expect(link_cold_run->plan.actions.size() == link_cold_inspection->plan.actions.size(),
               "link run and cold inspection must expose the same plan cardinality");
    }
    expect(linker_runner.calls == 1,
           "cold link run should launch link.exe exactly once");
    expect(fs::is_regular_file(link_output) && fs::is_regular_file(link_cache),
           "cold link run should create target and cache state");

    const std::string link_cache_before = read_text(link_cache);
    const std::string link_output_before = read_text(link_output);
    const auto link_warm_inspection = link_coordinator.inspect(link_request);
    expect(link_warm_inspection.has_value(), "warm link inspection should succeed");
    if (link_warm_inspection) {
        expect(link_warm_inspection->validation.reusable(),
               "warm link inspection should report reusable cache state");
        expect(link_warm_inspection->plan.empty(),
               "warm link inspection should expose an empty plan");
    }
    expect(linker_runner.calls == 1,
           "warm link inspection must not launch link.exe");
    expect(read_text(link_cache) == link_cache_before
               && read_text(link_output) == link_output_before,
           "warm link inspection must not mutate cache or target content");

    // Archive inspection: exact archive freshness is visible without lib.exe/cache writes.
    const fs::path archive_object = fixture.path() / "archive" / "main.obj";
    const fs::path archive_output = fixture.path() / "archive" / "main.lib";
    const fs::path archive_cache = fixture.path() / "archive" / "main.archivecache";
    write_text(archive_object, "archive object input");

    LibrarianRunner librarian_runner;
    mqb::msvc::MsvcLibrarian librarian{toolchain, librarian_runner};
    mqb::orchestration::MsvcIncrementalArchiveCoordinator archive_coordinator{
        toolchain,
        librarian};
    const mqb::orchestration::IncrementalArchiveRequest archive_request{
        .objects = {archive_object},
        .output = archive_output,
        .cache_file = archive_cache,
        .working_directory = fixture.path(),
        .architecture = mqb::Architecture::x64,
        .link_time_code_generation = false,
        .force_archive = false,
    };

    const auto archive_cold_inspection = archive_coordinator.inspect(archive_request);
    expect(archive_cold_inspection.has_value(), "cold archive inspection should succeed");
    if (archive_cold_inspection) {
        expect(archive_cold_inspection->plan.actions.size() == 1
                   && std::holds_alternative<mqb::ArchiveAction>(
                       archive_cold_inspection->plan.actions.front()),
               "cold archive inspection should expose one ArchiveAction");
        expect(has_reason(
                   archive_cold_inspection->validation,
                   mqb::BuildReason::missing_cache_entry),
               "cold archive inspection should report missing cache metadata");
        expect(has_reason(
                   archive_cold_inspection->validation,
                   mqb::BuildReason::missing_output),
               "cold archive inspection should report missing archive output");
    }
    expect(librarian_runner.calls == 0,
           "archive inspection must not launch lib.exe");
    expect(!fs::exists(archive_output) && !fs::exists(archive_cache),
           "archive inspection must not create archive output or cache state");

    const auto archive_cold_run = archive_coordinator.run(archive_request);
    expect(archive_cold_run.has_value() && archive_cold_run->archived,
           "cold archive run should consume the inspected decision and archive");
    if (archive_cold_inspection && archive_cold_run) {
        expect(archive_cold_run->validation.reasons
                   == archive_cold_inspection->validation.reasons,
               "archive run and cold inspection must expose identical rebuild reasons");
        expect(archive_cold_run->plan.actions.size()
                   == archive_cold_inspection->plan.actions.size(),
               "archive run and cold inspection must expose the same plan cardinality");
    }
    expect(librarian_runner.calls == 1,
           "cold archive run should launch lib.exe exactly once");
    expect(fs::is_regular_file(archive_output) && fs::is_regular_file(archive_cache),
           "cold archive run should create archive and cache state");

    const std::string archive_cache_before = read_text(archive_cache);
    const std::string archive_output_before = read_text(archive_output);
    const auto archive_warm_inspection = archive_coordinator.inspect(archive_request);
    expect(archive_warm_inspection.has_value(), "warm archive inspection should succeed");
    if (archive_warm_inspection) {
        expect(archive_warm_inspection->validation.reusable(),
               "warm archive inspection should report reusable cache state");
        expect(archive_warm_inspection->plan.empty(),
               "warm archive inspection should expose an empty plan");
    }
    expect(librarian_runner.calls == 1,
           "warm archive inspection must not launch lib.exe");
    expect(read_text(archive_cache) == archive_cache_before
               && read_text(archive_output) == archive_output_before,
           "warm archive inspection must not mutate cache or archive content");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_incremental_inspection_tests passed\n";
    return 0;
}
