#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"
#ifdef _WIN32
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
constexpr std::string_view mock_metadata = R"({"version":1,"revision":0,"rules":[{"primary-output":"not-built.obj","outputs":["not-built.ifc"],"provides":[{"logical-name":"ScanFixture"}],"requires":[{"logical-name":"External"},{"logical-name":"header.hpp","source-path":"header.hpp","unique-on-source-path":true,"lookup-method":"include-quote"}]}]})";

std::string text(const fs::path& path) {
    const auto s = path.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
// Test-only stable projection of the existing typed model, not a P1689 parser.
std::string describe(const modules::P1689Document& document) {
    std::ostringstream out;
    out << "version=" << document.version << "\nrevision=" << document.revision << '\n';
    auto path = [&](const char* field, const std::optional<fs::path>& value) {
        out << field << '=' << (value ? "present:" + text(*value) : "absent") << '\n';
    };
    for (const auto& rule : document.rules) {
        out << "rule_begin\n";
        path("work_directory", rule.work_directory); path("primary_output_declaration", rule.primary_output);
        for (const auto& value : rule.outputs) out << "output_declaration=" << text(value) << '\n';
        for (const auto& value : rule.provided_modules) {
            out << "provides=" << value.logical_name << "\nunique=" << value.unique_on_source_path
                << "\ninterface=" << value.is_interface << '\n';
            path("source_path", value.source_path); path("compiled_module_declaration", value.compiled_module_path);
        }
        for (const auto& value : rule.required_modules) {
            out << "requires=" << value.logical_name << "\nunique=" << value.unique_on_source_path
                << "\nlookup=" << static_cast<int>(value.lookup_method) << '\n';
            path("source_path", value.source_path); path("compiled_module_reference", value.compiled_module_path);
        }
        out << "rule_end\n";
    }
    return out.str();
}
bool same_spec(const process::ProcessSpec& a, const process::ProcessSpec& b) {
    if (a.executable != b.executable || a.arguments != b.arguments || a.working_directory != b.working_directory
        || a.inherit_environment != b.inherit_environment || a.capture_stdout != b.capture_stdout
        || a.capture_stderr != b.capture_stderr || a.cancellation != b.cancellation
        || a.environment.size() != b.environment.size()) return false;
    for (std::size_t i = 0; i < a.environment.size(); ++i) {
        const auto& x = a.environment[i]; const auto& y = b.environment[i];
        if (x.name != y.name || x.value != y.value || x.remove != y.remove) return false;
    }
    return true;
}
void model_contracts() {
    static_assert(!ModuleScanArtifactRecord::compile_cache_save_performed);
    static_assert(!ModuleScanArtifactRecord::compile_evidence_sealed_by_scan);
    static_assert(!ModuleScanArtifactRecord::exact_cache_entry_captured);
    static_assert(!ModuleScanArtifactRecord::physical_identity_verified);
    static_assert(!ModuleScanArtifactRecord::complete_producer_inventory);
    static_assert(!ModuleScanArtifactRecord::deletion_authorized);
    const auto parsed = modules::P1689Parser::parse(mock_metadata);
    require(parsed && parsed->rules.size() == 1 && parsed->rules[0].required_modules.size() == 2,
        "mock scan metadata must satisfy the real P1689 schema");
    std::string malformed{mock_metadata};
    const std::string source_field = "\"source-path\":\"header.hpp\",";
    const auto position = malformed.find(source_field); require(position != std::string::npos, "negative schema fixture");
    malformed.erase(position, source_field.size());
    require(!modules::P1689Parser::parse(malformed), "unique-on-source-path without source rejected");
    ModuleScanArtifactRecord first;
    first.completion = ArtifactCompletion::executed;
    first.recipe.invocation.source = "source.ixx";
    first.recipe.invocation.output_file = "scan.json";
    first.recipe.process.arguments = {"/scanDependencies", "scan.json", "source.ixx"};
    first.recipe.process.environment = {{"CL", {}, true}};
    first.compile_cache_reference = "cache";
    first.dependencies.rules.push_back({.primary_output = "not-built.obj",
        .outputs = {"declared-not-built.ifc"}, .provided_modules = {{.logical_name = "Provider"}},
        .required_modules = {{.logical_name = "External"}, {.logical_name = "header.hpp", .source_path = "header.hpp",
            .unique_on_source_path = true, .lookup_method = modules::LookupMethod::include_quote}}});
    auto second = first;
    second.caller_label = ArtifactGenerationLabel{"target", "later"};
    second.completion = ArtifactCompletion::reused;
    second.recipe.invocation.source = "new.ixx";
    second.recipe.process.arguments[1] = "new.json";
    second.recipe.process.environment[0].remove = false;
    second.dependencies.rules[0].provided_modules[0].logical_name = "Changed";
    require(!first.caller_label && first.recipe.invocation.source == "source.ixx"
        && first.recipe.process.arguments[1] == "scan.json" && first.recipe.process.environment[0].remove
        && first.dependencies.rules[0].provided_modules[0].logical_name == "Provider", "owned scan record data");
    require(first.recipe.invocation.output_file == "scan.json"
        && first.dependencies.rules[0].primary_output == "not-built.obj"
        && first.dependencies.rules[0].required_modules.size() == 2, "metadata declarations remain separate from scan output");
    require(describe(first.dependencies) != describe(second.dependencies), "typed document copy remains independent");
}
#ifdef _WIN32
void write(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary}; out.write(value.data(), static_cast<std::streamsize>(value.size())); out.close();
    require(static_cast<bool>(out), "write scan fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary}; require(in.is_open(), "open scan fixture/evidence");
    std::string result{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    require(!in.bad(), "read scan fixture/evidence"); return result;
}
struct FileState {
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::optional<FileState> state(const fs::path& path) {
    if (!fs::exists(path)) return std::nullopt;
    require(fs::is_regular_file(path), "state observation expects fixture regular file");
    return FileState{fs::last_write_time(path), bytes(path)};
}
void log_spec(const fs::path& prefix, const process::ProcessSpec& spec) {
    std::ostringstream out;
    out << "executable=" << text(spec.executable) << '\n';
    if (spec.working_directory) out << "cwd=" << text(*spec.working_directory) << '\n';
    for (const auto& arg : spec.arguments) out << "arg=" << arg << '\n';
    out << "inherit=" << spec.inherit_environment << "\ncapture_stdout=" << spec.capture_stdout
        << "\ncapture_stderr=" << spec.capture_stderr << "\nstoppable=" << spec.cancellation.stop_possible() << '\n';
    // Toolchain environment may include inherited secrets. Keep names/removal
    // policy only; same_spec checks complete values in memory before asserting.
    for (const auto& e : spec.environment) out << "environment_name=" << std::quoted(e.name) << '|' << e.remove << '\n';
    write(prefix.string() + ".argv.txt", out.str());
}
void log_result(const fs::path& prefix, const std::expected<process::ProcessResult, process::ProcessError>& r, bool discovery = false) {
    if (!r) {
        write(prefix.string() + ".launch-error.txt", std::to_string(r.error().native_code) + " " + r.error().message);
        return;
    }
    write(prefix.string() + ".exit.txt", std::to_string(r->exit_code));
    write(prefix.string() + ".stdout.txt", discovery ? "toolchain discovery environment output withheld" : r->stdout_text);
    write(prefix.string() + ".stderr.txt", discovery ? "toolchain discovery diagnostics withheld" : r->stderr_text);
}
void save_record(const fs::path& prefix, const std::expected<RecordedModuleScanResult, msvc::ModuleScanError>& r) {
    if (!r) {
        const auto& e = r.error();
        write(prefix.string() + ".error.txt", "code=" + std::to_string(static_cast<int>(e.code)) + "\nmessage=" + e.message);
        if (e.process_result) log_result(prefix, *e.process_result);
        if (e.process_error) log_result(prefix, std::unexpected(*e.process_error));
        if (e.dependency_error) write(prefix.string() + ".parser-error.txt", e.dependency_error->message);
        return;
    }
    const auto& record = r->record;
    std::ostringstream out;
    out << "scan_success=true\ncompletion=" << (r->result.scanned ? "executed" : "reused")
        << "\ncompile_cache_save_performed=false\ncompile_evidence_sealed_by_scan=false\n"
        << "exact_cache_entry_captured=false\nphysical_identity_verified=false\n"
        << "complete_producer_inventory=false\ndeletion_authorized=false\n"
        << "source=" << text(record.recipe.invocation.source) << "\nscan_output=" << text(record.recipe.invocation.output_file)
        << "\ncompile_cache_reference=" << text(record.compile_cache_reference)
        << "\nconfiguration=" << to_string(record.recipe.invocation.options.configuration)
        << "\ntool_version=" << record.recipe.toolchain.version << '\n';
    if (record.caller_label) out << "target=" << record.caller_label->target << "\ngeneration=" << record.caller_label->generation << '\n';
    for (auto reason : r->result.inspection.reasons) out << "reason=" << to_string(reason) << '\n';
    write(prefix.string() + ".record.txt", out.str());
    log_spec(prefix.string() + ".recipe", record.recipe.process);
    write(prefix.string() + ".document.txt", describe(record.dependencies));
    write(prefix.string() + ".p1689.json", bytes(record.recipe.invocation.output_file));
    write(prefix.string() + ".input", bytes(record.recipe.invocation.source));
    if (fs::is_regular_file(record.compile_cache_reference)) write(prefix.string() + ".prior-compilecache", bytes(record.compile_cache_reference));
    require(record.completion == (r->result.scanned ? ArtifactCompletion::executed : ArtifactCompletion::reused)
        && r->result.result.reused == !r->result.scanned, "actual scan completion flags agree");
    require(same_spec(record.recipe.process, r->result.inspection.recipe.process)
        && describe(record.dependencies) == describe(r->result.result.dependencies), "exact same-call recipe and document");
    require(record.recipe.invocation.output_file != record.compile_cache_reference, "fixture scan and cache paths distinct");
}
struct Fixture {
    fs::path root;
    SourceArtifacts artifacts;
    IncrementalModuleScanRequest request;
};
Fixture setup(const fs::path& root) {
    const auto source = root / fs::path{L"scan source \u65e5.ixx"};
    write(source, "#ifdef NDEBUG\nexport module ScanRelease;\n#else\nexport module ScanDebug;\n#endif\nexport int value(){return 7;}\n");
    const auto layout = ProjectArtifactLayout::create(root); require(layout.has_value(), "scan layout");
    const auto a = layout->for_source(source); require(a.has_value(), "scan source artifacts");
    IncrementalModuleScanRequest request;
    request.source = source; request.module_dependencies_file = a->module_dependencies; request.compile_cache_file = a->compile_cache;
    request.kind = TranslationUnitKind::module_interface; request.options.standard = CppStandard::latest;
    request.working_directory = root; request.options.include_directories = {root};
    return {root, *a, request};
}
void compile(MsvcIncrementalCompileCoordinator& coordinator, const Fixture& fixture, const fs::path& evidence,
             const char* phase, bool expected_success) {
    IncrementalCompileRequest request;
    request.unit = {.source = fixture.request.source, .kind = fixture.request.kind,
        .outputs = {{fixture.artifacts.object, ArtifactKind::object}, {fixture.artifacts.module_interface, ArtifactKind::module_interface}}};
    request.options = fixture.request.options; request.cache_file = fixture.request.compile_cache_file;
    request.source_dependencies_file = fixture.artifacts.dependencies;
    request.module_scan_output = fixture.request.module_dependencies_file; request.working_directory = fixture.root;
    const auto old_cache = state(request.cache_file);
    auto result = coordinator.run(request);
    const auto prefix = evidence / phase;
    if (!result) {
        write(prefix.string() + ".compile-error.txt", result.error().message);
        if (result.error().compile_error) {
            write(prefix.string() + ".executor-error.txt", result.error().compile_error->message);
        }
    } else {
        std::ostringstream out; out << "compiled=" << result->compiled << '\n';
        for (const auto& w : result->warnings) out << "warning=" << static_cast<int>(w.code) << '|' << w.message << '\n';
        write(prefix.string() + ".compile-result.txt", out.str());
    }
    require(result.has_value() == expected_success, "expected independent compile outcome (diagnostics retained)");
    if (!result) {
        require(old_cache == state(request.cache_file), "failed compile does not reseal cache");
        return;
    }
    require(result->compiled, "explicit fixture compile really executed");
    auto saved = CompileCacheFile::load(request.cache_file);
    require(saved && *saved && (**saved).module_scan, "successful compile, not scan, sealed scan evidence");
    write(prefix.string() + ".compilecache", bytes(request.cache_file));
    write(prefix.string() + ".obj", bytes(fixture.artifacts.object));
    write(prefix.string() + ".ifc", bytes(fixture.artifacts.module_interface));
}
using ScanResult = std::expected<RecordedModuleScanResult, msvc::ModuleScanError>;
ScanResult scan_call(MsvcIncrementalModuleScanCoordinator& coordinator, const Fixture& fixture,
                    const IncrementalModuleScanRequest& request, const fs::path& evidence, const char* phase,
                    std::optional<ArtifactGenerationLabel> label) {
    const auto prefix = evidence / phase;
    const auto cache = state(fixture.artifacts.compile_cache), obj = state(fixture.artifacts.object), ifc = state(fixture.artifacts.module_interface);
    write(prefix.string() + ".attempt.txt", "source=" + text(request.source) + "\nscan_output=" + text(request.module_dependencies_file)
        + "\ncompile_cache_reference=" + text(request.compile_cache_file) + "\nconfiguration=" + std::string{to_string(request.options.configuration)});
    write(prefix.string() + ".attempt.input", bytes(request.source));
    auto r = coordinator.run_recorded(request, std::move(label)); save_record(prefix, r);
    const bool unchanged = cache == state(fixture.artifacts.compile_cache) && obj == state(fixture.artifacts.object)
        && ifc == state(fixture.artifacts.module_interface);
    write(prefix.string() + ".compile-state-unchanged.txt", unchanged ? "true" : "false");
    require(unchanged, "scan does not seal cache or build object/IFC in this fixture");
    return r;
}
void mock_cases(const fs::path& root, const fs::path& evidence) {
    struct Mock final : process::ProcessRunner {
        enum class Mode { valid, launch, nonzero, missing, malformed } mode{Mode::valid};
        fs::path evidence;
        std::string phase;
        process::ProcessSpec last;
        unsigned scans{}, compiles{}, calls{};
        explicit Mock(fs::path where) : evidence(std::move(where)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            last = spec; const auto prefix = evidence / (std::to_string(++calls) + "-" + phase); log_spec(prefix, spec);
            const auto scan = std::find(spec.arguments.begin(), spec.arguments.end(), "/scanDependencies");
            std::expected<process::ProcessResult, process::ProcessError> r = process::ProcessResult{.stdout_text = "mock-only"};
            if (scan != spec.arguments.end()) {
                require(++scans <= 8 && std::next(scan) != spec.arguments.end(), "fixed mock scan process budget/operand");
                const auto u = *std::next(scan);
                const fs::path output{std::u8string{reinterpret_cast<const char8_t*>(u.data()), u.size()}};
                if (mode == Mode::launch) r = std::unexpected(process::ProcessError{.native_code = 5, .message = "MOCK_SCAN_LAUNCH_FAILURE"});
                else if (mode == Mode::nonzero) r = process::ProcessResult{.exit_code = 2, .stderr_text = "MOCK_SCAN_NONZERO"};
                else if (mode != Mode::missing) {
                    const std::string_view json = mode == Mode::malformed ? std::string_view{"{broken"} : mock_metadata;
                    write(output, json); write(prefix.string() + ".emitted.json", json);
                }
            } else {
                require(++compiles <= 1, "fixed mock compile budget");
                fs::path object, ifc, deps;
                auto path = [](std::string_view s) { return fs::path{std::u8string{reinterpret_cast<const char8_t*>(s.data()), s.size()}}; };
                for (std::size_t i = 0; i < spec.arguments.size(); ++i) {
                    const auto& a = spec.arguments[i];
                    if (a.starts_with("/Fo")) object = path(std::string_view{a}.substr(3));
                    else if (a == "/ifcOutput" && i + 1 < spec.arguments.size()) ifc = path(spec.arguments[++i]);
                    else if (a == "/sourceDependencies" && i + 1 < spec.arguments.size()) deps = path(spec.arguments[++i]);
                }
                require(!object.empty() && !ifc.empty() && !deps.empty(), "mock compile output recipe");
                std::ostringstream json; json << "{\"Version\":\"1.2\",\"Data\":{\"Source\":" << std::quoted(spec.arguments.back()) << ",\"Includes\":[]}}";
                write(object, "mock object"); write(ifc, "mock IFC"); write(deps, json.str());
                write(prefix.string() + ".emitted.json", json.str());
            }
            log_result(prefix, r); return r;
        }
    } runner{evidence / "processes"};
    auto f = setup(root); write(root / "tools/cl.exe", "mock identity, not executable");
    msvc::MsvcToolchain toolchain{.identity = {.compiler = root / "tools/cl.exe", .version = "scan-mock", .binary_stamp = "fixture"}};
    msvc::MsvcModuleDependencyScanner scanner{toolchain, runner}; MsvcIncrementalModuleScanCoordinator coordinator{scanner};
    msvc::MsvcCompileExecutor executor{toolchain, runner}; MsvcIncrementalCompileCoordinator compiler{toolchain, executor};
    unsigned calls{};
    auto invoke = [&](const char* phase, const IncrementalModuleScanRequest& request) {
        require(++calls <= 12, "fixed mock recorded-call budget"); runner.phase = phase;
        auto result = scan_call(coordinator, f, request, evidence, phase, ArtifactGenerationLabel{"mock", phase});
        if (result && result->result.scanned) require(same_spec(result->record.recipe.process, runner.last), "record matches actual mock scan argv");
        return result;
    };
    const auto cold = invoke("01-unsealed", f.request);
    require(cold && cold->result.scanned && !fs::exists(f.artifacts.compile_cache), "first scan does not create compile cache");
    const auto again = invoke("02-unsealed-again", f.request);
    require(again && again->result.scanned && runner.scans == 2, "scan JSON alone is not a reusable seal");
    runner.phase = "seal-debug"; compile(compiler, f, evidence, "seal-debug", true);
    const auto scan_before = state(f.request.module_dependencies_file);
    const auto warm = invoke("03-reuse", f.request);
    require(warm && !warm->result.scanned && runner.scans == 2 && scan_before == state(f.request.module_dependencies_file), "sealed reuse performs no scan/write");
    const auto stamp = fs::last_write_time(f.request.module_dependencies_file);
    write(f.request.module_dependencies_file, "{broken"); fs::last_write_time(f.request.module_dependencies_file, stamp);
    const auto repaired = invoke("04-corrupt-same-mtime", f.request);
    require(repaired && repaired->result.scanned && std::find(repaired->result.inspection.reasons.begin(), repaired->result.inspection.reasons.end(),
        ModuleScanReason::dependency_metadata_invalid) != repaired->result.inspection.reasons.end(), "invalid warm metadata rescanned, not called success");
    auto changed = f.request; changed.options.configuration = BuildConfiguration::release;
    const auto release = invoke("05-option-change", changed);
    require(release && release->result.scanned && cold->record.recipe.invocation.options.configuration == BuildConfiguration::debug
        && release->record.recipe.invocation.options.configuration == BuildConfiguration::release, "same-path scan configuration preserves prior record");
    auto bad = f.request; bad.compile_cache_file = root / ".mqb/no-seal.cache";
    const auto check = [&](const char* phase, Mock::Mode mode, msvc::ModuleScanErrorCode expected) {
        runner.mode = mode; auto result = invoke(phase, bad);
        require(!result && result.error().code == expected, "original scan error, no successful record");
        require(!fs::exists(evidence / (std::string{phase} + ".record.txt")), "failed call saved no success record");
        return result;
    };
    const auto launch = check("06-launch", Mock::Mode::launch, msvc::ModuleScanErrorCode::process_failed);
    require(launch.error().process_error && launch.error().process_error->native_code == 5, "launch diagnostic preserved");
    const auto nonzero = check("07-nonzero", Mock::Mode::nonzero, msvc::ModuleScanErrorCode::scan_failed);
    require(nonzero.error().process_result && nonzero.error().process_result->exit_code == 2, "original failed tool exit preserved");
    check("08-missing", Mock::Mode::missing, msvc::ModuleScanErrorCode::output_missing);
    check("09-malformed", Mock::Mode::malformed, msvc::ModuleScanErrorCode::dependency_metadata_failed);
    runner.mode = Mock::Mode::valid;
    write(root / ".mqb/blocked-parent", "obstruction"); bad.module_dependencies_file = root / ".mqb/blocked-parent/scan.json";
    check("10-blocked-parent", Mock::Mode::valid, msvc::ModuleScanErrorCode::output_prepare_failed);
    bad.module_dependencies_file = root / ".mqb/blocked-output"; write(bad.module_dependencies_file / "sentinel", "retain");
    check("11-blocked-output", Mock::Mode::valid, msvc::ModuleScanErrorCode::stale_output_remove_failed);
    bad.options.standard = CppStandard::cpp17;
    check("12-invalid", Mock::Mode::valid, msvc::ModuleScanErrorCode::invalid_request);
    require(calls == 12 && runner.scans == 8 && runner.compiles == 1, "fixed mock calls complete");
    require(cold->record.dependencies.rules[0].primary_output == "not-built.obj" && !fs::exists(root / "not-built.obj")
        && !fs::exists(root / "not-built.ifc"), "P1689 output declarations are not manufactured artifacts");
    write(evidence / "completed.txt", "12 recorded scans; 5 successes/7 expected errors; 8 mock scan processes; 1 mock compile; no real cl\n");
}
void native_cases(const fs::path& root, const fs::path& evidence) {
    struct Runner final : process::ProcessRunner {
        platform::windows::WindowsProcessRunner actual;
        fs::path evidence; std::string phase{"discovery"}; process::ProcessSpec last;
        unsigned calls{}, scans{}, compiles{};
        explicit Runner(fs::path where) : evidence(std::move(where)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            last = spec;
            if (phase.starts_with("scan-")) require(++scans <= 6, "fixed real scan process budget");
            if (phase.starts_with("compile-")) require(++compiles <= 3, "fixed real compile budget");
            const auto prefix = evidence / (std::to_string(++calls) + "-" + phase); log_spec(prefix, spec);
            auto result = actual.run(spec); log_result(prefix, result, phase == "discovery"); return result;
        }
    } runner{evidence / "processes"};
    auto f = setup(root);
    msvc::MsvcToolchainLocator locator{runner}; msvc::DiscoveryOptions discovery;
    discovery.preference = msvc::ToolchainPreference::visual_studio; discovery.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    auto toolchain = locator.discover(discovery);
    if (!toolchain) write(evidence / "discovery.error.txt", toolchain.error().message);
    require(toolchain.has_value(), "discover real MSVC");
    msvc::MsvcModuleDependencyScanner scanner{*toolchain, runner}; MsvcIncrementalModuleScanCoordinator coordinator{scanner};
    msvc::MsvcCompileExecutor executor{*toolchain, runner}; MsvcIncrementalCompileCoordinator compiler{*toolchain, executor};
    unsigned calls{};
    auto invoke = [&](const char* phase) {
        require(++calls <= 8, "fixed real recorded scan budget"); runner.phase = std::string{"scan-"} + phase;
        auto result = scan_call(coordinator, f, f.request, evidence, phase, std::nullopt);
        if (result && result->result.scanned) require(same_spec(result->record.recipe.process, runner.last), "record matches actual scan process recipe");
        return result;
    };
    auto has_provider = [](const ScanResult& value, const char* name) {
        if (!value) return false;
        for (const auto& rule : value->record.dependencies.rules) for (const auto& p : rule.provided_modules)
            if (p.logical_name == name) return true;
        return false;
    };
    const auto cold = invoke("01-unsealed");
    require(has_provider(cold, "ScanDebug") && cold->result.scanned && !cold->record.caller_label, "real scan keeps provider model and absent label");
    require(!fs::exists(f.artifacts.object) && !fs::exists(f.artifacts.module_interface) && !fs::exists(f.artifacts.compile_cache), "real scan produces no compile outputs/cache");
    const auto repeat = invoke("02-unsealed-again");
    require(repeat && repeat->result.scanned && runner.scans == 2, "real scan-only repetition still unsealed");
    runner.phase = "compile-debug"; compile(compiler, f, evidence, "compile-debug", true);
    const auto scan_before = state(f.request.module_dependencies_file);
    const auto warm = invoke("03-reuse");
    require(warm && !warm->result.scanned && runner.scans == 2 && scan_before == state(f.request.module_dependencies_file), "real sealed warm scan is read-only");
    f.request.options.configuration = BuildConfiguration::release;
    const auto release = invoke("04-release");
    require(has_provider(release, "ScanRelease") && release->result.scanned && has_provider(cold, "ScanDebug"), "scan recipe macros change actual topology without changing old record");
    runner.phase = "compile-release"; compile(compiler, f, evidence, "compile-release", true);
    const auto release_before = state(f.request.module_dependencies_file);
    const auto release_warm = invoke("05-release-reuse");
    require(release_warm && !release_warm->result.scanned && runner.scans == 3 && release_before == state(f.request.module_dependencies_file), "release compile seal reused without tools");
    auto change_source = [&](std::string_view source) {
        const auto stamp = fs::last_write_time(f.request.source); std::this_thread::sleep_for(std::chrono::milliseconds{40});
        write(f.request.source, source); require(fs::last_write_time(f.request.source) != stamp, "observed deliberate source change");
    };
    change_source("export module ScanBroken;\nexport int value(){return MQB_SCAN_RECORD_UNDECLARED;}\n");
    const auto semantic = invoke("06-scan-before-compile-failure");
    require(has_provider(semantic, "ScanBroken") && semantic->result.scanned, "directive scan succeeds before semantic compile failure");
    runner.phase = "compile-semantic-failure"; compile(compiler, f, evidence, "compile-semantic-failure", false);
    const auto unsealed = invoke("07-after-compile-failure");
    require(unsealed && unsealed->result.scanned && has_provider(semantic, "ScanBroken"), "failed compile did not seal earlier successful scan");
    change_source("export module ScanBroken;\n#error MQB_SCAN_RECORD_EXPECTED_FAILURE\n");
    const auto failed = invoke("08-scan-failed");
    require(!failed && failed.error().code == msvc::ModuleScanErrorCode::scan_failed && failed.error().process_result
        && failed.error().process_result->exit_code != 0, "real scan failure keeps original tool error without success record");
    const auto& process = *failed.error().process_result;
    require((process.stdout_text + process.stderr_text).find("MQB_SCAN_RECORD_EXPECTED_FAILURE") != std::string::npos, "original preprocessor failure retained");
    require(calls == 8 && runner.scans == 6 && runner.compiles == 3, "fixed real calls complete");
    write(evidence / "completed.txt", "8 recorded scans; 7 successes/1 expected scan failure; 6 scan cl; 3 compile cl (2 sealed/1 expected semantic failure); discovery separate\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto root = fs::current_path();
        require(!fs::exists(root / "storage-fixtures") && !fs::exists(root / "storage-evidence"), "fresh scan evidence required");
        mock_cases(root / "storage-fixtures/scan-mock", root / "storage-evidence/scan-records/mock");
        native_cases(root / "storage-fixtures/scan-native", root / "storage-evidence/scan-records/native");
        std::cout << "scan producer mock/native records retained separately\n";
#else
        std::cout << "portable scan record value contracts only; coordinator/Windows NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
