#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#ifdef _WIN32
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;
#ifdef _WIN32
void save_target_cache_evidence(const fs::path&, const RecordedModuleTargetResult&);
void check_target_cache_history(const fs::path&, const RecordedModuleTargetResult&);
std::expected<RecordedModuleTargetResult, IncrementalModuleTargetError> run_target_cache_checked(
    MsvcModuleTargetCoordinator&, const IncrementalModuleTargetRequest&, ArtifactGenerationLabel, const fs::path&);
#endif
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
std::string text(const fs::path& path) {
    const auto s = path.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
void model_contracts() {
    static_assert(!ModuleTargetArtifactRecord::deletion_authorized);
    static_assert(!ModuleTargetArtifactRecord::physical_identity_verified);
    static_assert(!ModuleTargetArtifactRecord::complete_producer_inventory);
    ModuleTargetScanArtifactRecord scan;
    scan.scan.completion = ArtifactCompletion::reused;
    scan.scan.recipe.invocation.source = "std.ixx";
    scan.scan.dependencies.rules.push_back({.provided_modules = {{.logical_name = "std"}}});
    scan.toolchain_owned = true;
    ModuleTargetArtifactRecord first{
        .caller_label = ArtifactGenerationLabel{"target", "first"},
        .scans = {scan}, .compiles = {},
        .link = {.completion = ArtifactCompletion::executed, .cache_state = ArtifactCacheState::saved,
            .association = {.linker = {}, .signature = BuildSignature::from_digest({1, 2}),
                .objects = {}, .output = "target.exe", .libraries = {}, .file_inputs = {}, .side_outputs = {}},
            .options = {}, .cache_file = {}, .working_directory = {}}};
    auto second = first;
    second.scans[0].toolchain_owned = false;
    second.scans[0].scan.dependencies.rules[0].provided_modules[0].logical_name = "Changed";
    second.link.association.output = "other.exe";
    second.caller_label->generation = "second";
    require(first.scans[0].toolchain_owned &&
        first.scans[0].scan.dependencies.rules[0].provided_modules[0].logical_name == "std" &&
        first.link.association.output == "target.exe" && first.caller_label->generation == "first",
        "target records own their stage data and annotations");
}
#ifdef _WIN32
fs::path utf8_path(std::string_view s) {
    return fs::path{std::u8string{reinterpret_cast<const char8_t*>(s.data()), s.size()}};
}
void write(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out.write(value.data(), static_cast<std::streamsize>(value.size())); out.close();
    require(static_cast<bool>(out), "write target fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary}; require(in.is_open(), "open target fixture/evidence");
    std::string result{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    require(!in.bad(), "read target fixture/evidence"); return result;
}
struct FileState {
    fs::file_time_type modified;
    std::string data;
    bool operator==(const FileState&) const = default;
};
auto snapshot(const fs::path& root) {
    std::map<std::string, FileState> result;
    if (fs::exists(root)) for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) result.emplace(text(e.path().lexically_relative(root)),
            FileState{e.last_write_time(), bytes(e.path())});
    return result;
}
void spec_log(const fs::path& prefix, const process::ProcessSpec& spec) {
    std::ostringstream out;
    out << "executable=" << text(spec.executable) << '\n';
    if (spec.working_directory) out << "cwd=" << text(*spec.working_directory) << '\n';
    for (const auto& a : spec.arguments) out << "arg=" << a << '\n';
    out << "inherit=" << spec.inherit_environment << '\n';
    // Do not publish environment values or discovery environment dumps.
    for (const auto& e : spec.environment) out << "environment_name=" << std::quoted(e.name) << '|' << e.remove << '\n';
    write(prefix.string() + ".argv.txt", out.str());
}
struct Counts {
    unsigned scan{}, compile{}, link{}, program{}, discovery{};
    bool operator==(const Counts&) const = default;
};
class TracedRunner final : public process::ProcessRunner {
public:
    process::ProcessRunner& delegate;
    fs::path evidence, linker;
    std::string phase{"discovery"};
    const bool mock;
    std::atomic<unsigned> serial{0}, scans{0}, compiles{0}, links{0}, programs{0}, discoveries{0};
    TracedRunner(process::ProcessRunner& runner, fs::path output, bool is_mock)
        : delegate(runner), evidence(std::move(output)), mock(is_mock) {}
    Counts counts() const { return {scans.load(), compiles.load(), links.load(), programs.load(), discoveries.load()}; }
    std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
        const unsigned n = ++serial;
        const auto prefix = evidence / (std::to_string(n) + "-" + phase);
        spec_log(prefix, spec);
        const bool discovery = phase == "discovery";
        if (discovery) ++discoveries;
        else if (phase.starts_with("program")) require(++programs <= 2, "program budget");
        else if (spec.executable == linker) require(++links <= 10, "link budget");
        else if (std::find(spec.arguments.begin(), spec.arguments.end(), "/scanDependencies") != spec.arguments.end())
            require(++scans <= (mock ? 50u : 28u), "scan budget");
        else require(++compiles <= (mock ? 60u : 36u), "compile budget");
        if (mock) require(n <= 100, "total mock tool budget");
        auto result = delegate.run(spec);
        if (!result) write(prefix.string() + ".launch-error.txt", result.error().message);
        else {
            write(prefix.string() + ".exit.txt", std::to_string(result->exit_code));
            write(prefix.string() + ".stdout.txt", discovery ? "discovery output withheld" : result->stdout_text);
            write(prefix.string() + ".stderr.txt", discovery ? "discovery diagnostics withheld" : result->stderr_text);
        }
        return result;
    }
};
struct Harness {
    msvc::MsvcModuleDependencyScanner scanning;
    msvc::MsvcCompileExecutor executor;
    MsvcIncrementalCompileCoordinator compiling;
    MsvcModuleCompileCoordinator wave;
    msvc::MsvcLinker linker;
    MsvcIncrementalLinkCoordinator linking;
    MsvcModuleTargetCoordinator target;
    Harness(const msvc::MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : scanning(toolchain, runner), executor(toolchain, runner), compiling(toolchain, executor),
          wave(compiling), linker(toolchain, runner), linking(toolchain, linker), target(scanning, wave, linking) {}
};
void fixture(const fs::path& root, const char* name = "Value") {
    write(root / "A.ixx", "export module " + std::string{name} + ";\nexport int value(){return 7;}\n");
    write(root / "extra.hpp", "#pragma once\ninline int header_value(){return 4;}\n");
    write(root / "main.cpp", "import " + std::string{name} + ";\nimport \"extra.hpp\";\nint main(){return value()+header_value()==11?0:1;}\n");
}
IncrementalModuleTargetRequest request_for(const fs::path& root, const char* name = "module-target") {
    const auto layout = ProjectArtifactLayout::create(root); require(layout.has_value(), "target layout");
    IncrementalModuleTargetRequest r;
    r.artifact_layout = *layout;
    for (const auto* filename : {"main.cpp", "A.ixx"}) {
        const auto source = root / filename; const auto artifacts = layout->for_source(source);
        require(artifacts.has_value(), "source layout");
        r.sources.push_back({source, *artifacts, std::string_view{filename} == "main.cpp" ?
            TranslationUnitKind::source : TranslationUnitKind::module_interface});
    }
    const auto target = layout->for_target(name); require(target.has_value(), "target outputs");
    r.target = *target; r.compiler_options.standard = CppStandard::latest;
    r.compiler_options.include_directories = {root};
    r.link_options.subsystem = LinkSubsystem::console;
    r.link_options.additional_arguments = {"/INCREMENTAL:NO"};
    r.working_directory = root; r.max_parallel_scans = 2; r.max_parallel_compiles = 2;
    return r;
}
const char* completion(ArtifactCompletion c) { return c == ArtifactCompletion::executed ? "executed" : "reused"; }
const char* cache_state(ArtifactCacheState s) {
    return s == ArtifactCacheState::saved ? "saved" : s == ArtifactCacheState::reused ? "reused" : "save_failed";
}
void save_failure(const fs::path& prefix, const IncrementalModuleTargetError& e) {
    std::ostringstream out;
    out << "target_error=" << static_cast<int>(e.code) << "\nsource=" << text(e.source) << "\nmessage=" << e.message << '\n';
    if (e.scan_error) out << "scan_error=" << e.scan_error->message << '\n';
    if (e.graph_error) out << "graph_error=" << e.graph_error->message << '\n';
    if (e.compile_error) {
        out << "compile_error=" << e.compile_error->message << '\n';
        if (e.compile_error->compile_error) out << "incremental_error=" << e.compile_error->compile_error->message << '\n';
    }
    if (e.link_error) out << "link_error=" << e.link_error->message << '\n';
    write(prefix.string() + ".error.txt", out.str());
}
void save_success(const fs::path& prefix, const RecordedModuleTargetResult& r, bool copies) {
    std::ostringstream out;
    out << "target_success=true\nphysical_identity_verified=false\ncomplete_producer_inventory=false\n";
    out << "deletion_authorized=false\npublic_project_scans=" << r.result.scans.size() << '\n';
    if (r.record.caller_label) out << "label=" << r.record.caller_label->target << '|' << r.record.caller_label->generation << '\n';
    for (std::size_t i = 0; i < r.record.scans.size(); ++i) {
        const auto& s = r.record.scans[i]; const auto stem = prefix.string() + ".scan" + std::to_string(i);
        out << "scan=" << i << '|' << s.toolchain_owned << '|' << completion(s.scan.completion) << '|'
            << text(s.scan.recipe.invocation.source) << '|' << text(s.scan.recipe.invocation.output_file) << '\n';
        for (const auto& rule : s.scan.dependencies.rules) {
            for (const auto& p : rule.provided_modules) out << "provides=" << i << '|' << p.logical_name << '\n';
            for (const auto& q : rule.required_modules) out << "requires=" << i << '|' << q.logical_name << '\n';
        }
        spec_log(stem, s.scan.recipe.process);
        write(stem + ".input", bytes(s.scan.recipe.invocation.source));
        write(stem + ".p1689.json", bytes(s.scan.recipe.invocation.output_file));
    }
    auto nodes = [&](const auto& records, const auto& results, const char* category) {
        require(records.size() == results.size(), "target compile projection count");
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& n = records[i]; const auto stem = prefix.string() + '.' + category + std::to_string(i);
            out << "node=" << category << ':' << i << '|' << completion(n.completion) << '|'
                << cache_state(n.cache_state) << '|' << to_string(n.compiler_options.configuration) << '|'
                << n.force_rebuild << '|' << text(n.unit.source) << '\n';
            for (const auto& q : n.unit.module_references) out << "module_input=" << q.logical_name << '|' << text(q.interface_file) << '\n';
            for (const auto& q : n.unit.header_unit_references) out << "header_input=" << q.header_name << '|' << text(q.interface_file) << '\n';
            // Native standard-library output sizes are retained, but not their
            // potentially large byte copies. This is explicitly NOT a snapshot.
            const bool toolchain = std::any_of(r.record.scans.begin(), r.record.scans.end(), [&](const auto& s) {
                return s.toolchain_owned && s.scan.recipe.invocation.source == n.unit.source;
            });
            for (std::size_t j = 0; j < n.unit.outputs.size(); ++j) {
                const auto& a = n.unit.outputs[j];
                out << "output=" << static_cast<int>(a.kind) << '|' << text(a.path) << '|'
                    << fs::file_size(a.path) << "|byte_copy=" << (copies && !toolchain) << '\n';
                if (copies && !toolchain) write(stem + ".output" + std::to_string(j), bytes(a.path));
            }
            if (copies) {
                write(stem + ".deps.json", bytes(n.dependencies));
                if (n.cache_state != ArtifactCacheState::save_failed) write(stem + ".cache", bytes(n.compile_cache));
            }
            for (const auto& w : results[i].result.warnings) out << "compile_warning=" << static_cast<int>(w.code) << '|' << w.message << '\n';
        }
    };
    nodes(r.record.compiles.compiles, r.result.compiles.compiles, "source");
    nodes(r.record.compiles.header_unit_compiles, r.result.compiles.header_unit_compiles, "header");
    const auto& link = r.record.link;
    out << "link=" << completion(link.completion) << '|' << cache_state(link.cache_state) << '|'
        << text(link.association.output) << '|' << text(link.cache_file) << '\n';
    for (const auto& p : link.association.objects) out << "link_object=" << text(p) << '\n';
    for (const auto& w : r.result.link.warnings) out << "link_warning=" << static_cast<int>(w.code) << '|' << w.message << '\n';
    if (copies) {
        write(prefix.string() + ".exe", bytes(link.association.output));
        if (link.cache_state != ArtifactCacheState::save_failed) write(prefix.string() + ".linkcache", bytes(link.cache_file));
    }
    write(prefix.string() + ".record.txt", out.str());
}
void check_success(const RecordedModuleTargetResult& r, const IncrementalModuleTargetRequest& request) {
    require(r.result.scans.size() == request.sources.size() &&
        r.record.scans.size() == r.record.compiles.compiles.size(), "public prefix and recorded injected scans");
    for (std::size_t i = 0; i < r.record.scans.size(); ++i) {
        const auto& s = r.record.scans[i]; const auto& c = r.record.compiles.compiles[i];
        require(s.scan.recipe.invocation.source == c.unit.source && s.scan.compile_cache_reference == c.compile_cache,
            "same source links its actual scan and compile records");
        require(s.toolchain_owned == (i >= request.sources.size()), "toolchain provenance follows actual injection");
        if (i < request.sources.size()) require(c.unit.source == request.sources[i].source &&
            s.scan.completion == (r.result.scans[i].result.reused ? ArtifactCompletion::reused : ArtifactCompletion::executed),
            "project prefix and actual scan state preserved");
        require(c.completion == (r.result.compiles.compiles[i].result.compiled ? ArtifactCompletion::executed : ArtifactCompletion::reused),
            "actual compile state preserved");
    }
    require(r.record.link.association.output == request.target.executable &&
        r.record.link.association.objects.size() == r.record.compiles.compiles.size() &&
        r.record.link.completion == (r.result.link.linked ? ArtifactCompletion::executed : ArtifactCompletion::reused),
        "actual terminal link and no fabricated header object");
    for (const auto& h : r.record.compiles.header_unit_compiles)
        require(h.unit.outputs.size() == 1 && h.unit.outputs[0].kind == ArtifactKind::module_interface,
            "dynamic header unit is IFC-only");
}
struct Calls {
    Harness& harness; TracedRunner& runner; fs::path evidence; unsigned count{};
    std::expected<RecordedModuleTargetResult, IncrementalModuleTargetError>
    run(const char* phase, const IncrementalModuleTargetRequest& request, bool copies) {
        require(++count <= 10, "fixed target call budget"); runner.phase = phase;
        const auto prefix = evidence / phase; const auto before = runner.counts();
        std::ostringstream attempt;
        attempt << "mock=" << runner.mock << "\ntarget=" << text(request.target.executable) << '\n';
        for (std::size_t i = 0; i < request.sources.size(); ++i) {
            attempt << "source=" << text(request.sources[i].source) << '\n';
            write(prefix.string() + ".attempt" + std::to_string(i), bytes(request.sources[i].source));
        }
        write(prefix.string() + ".attempt.txt", attempt.str());
        auto r = run_target_cache_checked(harness.target, request, ArtifactGenerationLabel{"fixture", phase}, prefix);
        const auto after = runner.counts();
        std::ostringstream delta;
        delta << "scan=" << after.scan - before.scan << "\ncompile=" << after.compile - before.compile
            << "\nlink=" << after.link - before.link << '\n';
        write(prefix.string() + ".counts.txt", delta.str());
        if (!r) save_failure(prefix, r.error());
        else { save_success(prefix, *r, copies); check_success(*r, request); save_target_cache_evidence(prefix, *r); }
        return r;
    }
};
// Test-only serialization of controlled sourceDependencies paths.
std::string quote(std::string_view s) {
    std::string r{"\""};
    for (char c : s) { if (c == '\\' || c == '"') r += '\\'; r += c; }
    return r + '"';
}
struct MockTools final : process::ProcessRunner {
    enum class Fault { none, scan, graph, compile, link } fault{Fault::none};
    fs::path linker, evidence;
    std::atomic<unsigned> serial{0};
    std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
        const auto prefix = evidence / std::to_string(++serial);
        fs::path scan, deps, obj, ifc, output;
        for (std::size_t i = 0; i < spec.arguments.size(); ++i) {
            const auto& a = spec.arguments[i];
            auto next = [&]() { require(i + 1 < spec.arguments.size(), "mock option operand"); return utf8_path(spec.arguments[++i]); };
            if (a == "/scanDependencies") scan = next();
            else if (a == "/sourceDependencies") deps = next();
            else if (a == "/ifcOutput") ifc = next();
            else if (a.starts_with("/Fo")) obj = utf8_path(a.substr(3));
            else if (a.starts_with("/OUT:")) output = utf8_path(a.substr(5));
        }
        if (spec.executable == linker) {
            if (fault == Fault::link) return process::ProcessResult{.exit_code = 1120, .stderr_text = "MOCK_LINK_FAILURE"};
            require(!output.empty(), "mock link output"); write(output, "mock executable");
            return process::ProcessResult{.exit_code = 0};
        }
        require(!spec.arguments.empty(), "mock source operand");
        fs::path source = utf8_path(spec.arguments.back());
        if (std::find(spec.arguments.begin(), spec.arguments.end(), "/exportHeader") != spec.arguments.end()) {
            auto at = std::find(spec.arguments.begin(), spec.arguments.end(), "/headerName:quote");
            require(at != spec.arguments.end() && std::next(at) != spec.arguments.end() &&
                *std::next(at) == "extra.hpp" && spec.working_directory, "known mock header operand/cwd");
            source = *spec.working_directory / "extra.hpp";
        }
        write(prefix.string() + ".source.txt", text(source));
        if (!scan.empty()) {
            if (fault == Fault::scan) return process::ProcessResult{.exit_code = 2, .stderr_text = "MOCK_SCAN_FAILURE"};
            std::string rule;
            const auto filename = source.filename();
            if (filename == "A.ixx" || (filename == "main.cpp" && fault == Fault::graph))
                rule = R"({"provides":[{"logical-name":"Value"}]})";
            else if (filename == "std.ixx") rule = R"({"provides":[{"logical-name":"std"}]})";
            else if (filename == "std.compat.ixx") rule = R"({"provides":[{"logical-name":"std.compat"}],"requires":[{"logical-name":"std"}]})";
            else {
                require(filename == "main.cpp", "only known mock scan sources");
                rule = R"({"requires":[{"logical-name":"Value"},{"logical-name":"std.compat"},{"logical-name":"External"},{"logical-name":"extra.hpp","unique-on-source-path":true,"lookup-method":"include-quote","source-path":)" + quote(text(source.parent_path() / "extra.hpp")) + "}]}";
            }
            const auto metadata = "{\"version\":1,\"revision\":0,\"rules\":[" + rule + "]}";
            write(scan, metadata); write(prefix.string() + ".p1689.json", metadata);
        } else {
            require(!deps.empty(), "mock compile dependency output");
            if (fault == Fault::compile && source.filename() == "A.ixx")
                return process::ProcessResult{.exit_code = 2, .stderr_text = "MOCK_COMPILE_FAILURE"};
            if (!obj.empty()) write(obj, "mock object " + text(source));
            if (!ifc.empty()) write(ifc, "mock IFC " + text(source));
            const auto metadata = "{\"Version\":\"1.2\",\"Data\":{\"Source\":" + quote(text(source)) + ",\"Includes\":[]}}";
            write(deps, metadata); write(prefix.string() + ".deps.json", metadata);
        }
        return process::ProcessResult{.exit_code = 0};
    }
};
void mock_cases(const fs::path& root, const fs::path& evidence) {
    const auto tools = root / "toolchain";
    write(tools / "cl.exe", "mock compiler"); write(tools / "link.exe", "mock linker");
    write(tools / "std.ixx", "export module std;\n");
    write(tools / "std.compat.ixx", "export module std.compat;\nimport std;\n");
    write(tools / "external.ifc", "unchanged external IFC");
    const auto tool_inputs = snapshot(tools);
    msvc::MsvcToolchain toolchain{
        .identity = {.compiler = tools / "cl.exe", .version = "target-record-mock", .binary_stamp = "fixture"},
        .linker = tools / "link.exe",
        .standard_library_modules = {.std = tools / "std.ixx", .std_compat = tools / "std.compat.ixx"}};
    MockTools mock; mock.linker = toolchain.linker; mock.evidence = evidence / "emitted";
    TracedRunner runner{mock, evidence / "processes", true}; runner.linker = toolchain.linker;
    Harness h{toolchain, runner}; Calls calls{h, runner, evidence};
    auto setup = [&](const char* name) {
        const auto project = root / name; fixture(project); auto r = request_for(project);
        r.compiler_options.external_module_providers = {{"External", tools / "external.ifc"}};
        return r;
    };
    auto r = setup("successful");
    const auto first = calls.run("01-cold", r, false);
    require(first.has_value(), "cold mock recorded target");
    require(first->record.scans.size() == 4 && first->result.scans.size() == 2 &&
        first->record.scans[2].scan.recipe.invocation.source == tools / "std.compat.ixx" &&
        first->record.scans[3].scan.recipe.invocation.source == tools / "std.ixx" &&
        first->record.compiles.header_unit_compiles.size() == 1 &&
        first->record.compiles.dependencies.resolved_external_dependencies.size() == 1,
        "actual fixed-point std chain, header and external association");
    const auto old = snapshot(r.working_directory / ".mqb"); const auto before = runner.counts();
    const auto warm = calls.run("02-reuse", r, false);
    require(warm && runner.counts() == before && old == snapshot(r.working_directory / ".mqb"),
        "recorded warm target adds neither process nor writes");
    r.target = *r.artifact_layout->for_target("shared");
    const auto shared = calls.run("03-shared", r, false);
    require(shared && !shared->result.compiles.any_compiled && shared->result.link.linked &&
        shared->record.link.association.objects == first->record.link.association.objects,
        "another target references the same objects without claiming exclusive ownership");
    require(fs::remove(r.sources[1].artifacts.module_interface), "remove fixture provider IFC");
    const auto repaired = calls.run("04-repair", r, false);
    require(repaired && repaired->record.compiles.compiles[0].force_rebuild, "missing IFC rebuilds dependent consumer");
    r.target.link_cache = r.working_directory / ".mqb/cache/link/blocked";
    write(r.target.link_cache / "sentinel", "preserve obstruction");
    const auto warning = calls.run("05-save-failed", r, false);
    require(warning && warning->record.link.cache_state == ArtifactCacheState::save_failed &&
        !warning->result.link.warnings.empty(), "terminal cache save warning remains non-durable success");
    auto empty = r; empty.sources.clear(); const auto validation_before = runner.counts();
    const auto absent = calls.run("06-empty", empty, false);
    require(!absent && absent.error().code == IncrementalModuleTargetErrorCode::no_sources &&
        validation_before == runner.counts(), "validation failure has no successful target or tools");
    struct Case { const char* name; MockTools::Fault fault; IncrementalModuleTargetErrorCode code; };
    for (const auto& c : {
        Case{"07-scan-failed", MockTools::Fault::scan, IncrementalModuleTargetErrorCode::scan_failed},
        Case{"08-graph-failed", MockTools::Fault::graph, IncrementalModuleTargetErrorCode::graph_failed},
        Case{"09-compile-failed", MockTools::Fault::compile, IncrementalModuleTargetErrorCode::compile_failed},
        Case{"10-link-failed", MockTools::Fault::link, IncrementalModuleTargetErrorCode::link_failed}}) {
        auto failed_request = setup(c.name); mock.fault = c.fault; const auto n = runner.counts();
        const auto failed = calls.run(c.name, failed_request, false);
        require(!failed && failed.error().code == c.code && !fs::exists(evidence / (std::string{c.name} + ".record.txt")),
            "original stage error yields no public successful target");
        if (c.fault != MockTools::Fault::link) require(runner.links == n.link, "failed earlier stage never links");
        if (c.fault == MockTools::Fault::scan || c.fault == MockTools::Fault::graph)
            require(runner.compiles == n.compile, "failed preparation never compiles");
    }
    check_target_cache_history(evidence / "01-cold", *first);
    check_target_cache_history(evidence / "02-reuse", *warm);
    require(first->cache_evidence.compiles.size() == 4 && first->cache_evidence.header_unit_compiles.size() == 1,
        "target keeps actual injected providers but no fabricated external cache slot");
    require(bytes(r.target.link_cache / "sentinel") == "preserve obstruction", "terminal save failure preserves original obstruction");
    require(calls.count == 10 && snapshot(tools) == tool_inputs, "fixed mock calls, read-only toolchain/external inputs");
    write(evidence / "completed.txt", "10 targets; 5 successes; 5 expected stage errors; mock tools\n");
}
void native_cases(const fs::path& root, const fs::path& evidence) {
    platform::windows::WindowsProcessRunner actual_runner;
    TracedRunner runner{actual_runner, evidence / "processes", false};
    msvc::MsvcToolchainLocator locator{runner}; msvc::DiscoveryOptions options;
    options.preference = msvc::ToolchainPreference::visual_studio;
    options.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    const auto toolchain = locator.discover(options);
    if (!toolchain) write(evidence / "discovery.error.txt", toolchain.error().message);
    require(toolchain.has_value(), "real Visual Studio toolchain"); runner.linker = toolchain->linker;
    Harness h{*toolchain, runner}; Calls calls{h, runner, evidence};
    auto setup = [&](const char* name) {
        const auto project = root / name; fixture(project); return request_for(project);
    };
    auto r = setup("successful");
    const auto cold = calls.run("01-cold", r, true);
    require(cold && cold->record.scans.size() == 2 && cold->record.compiles.header_unit_compiles.size() == 1,
        "real named-module and dynamically discovered header-unit target");
    const auto old = snapshot(r.working_directory / ".mqb"); const auto before = runner.counts();
    const auto warm = calls.run("02-reuse", r, true);
    require(warm && before == runner.counts() && old == snapshot(r.working_directory / ".mqb"),
        "real whole-target recorded reuse adds no process or writes");
    r.target = *r.artifact_layout->for_target("shared");
    const auto shared = calls.run("03-shared", r, true);
    require(shared && !shared->result.compiles.any_compiled && shared->result.link.linked &&
        shared->record.link.association.objects == cold->record.link.association.objects &&
        fs::exists(cold->record.link.association.output), "two targets share actual objects and retain prior output");
    const auto stamp = fs::last_write_time(r.sources[1].source);
    std::this_thread::sleep_for(std::chrono::milliseconds{40}); fixture(r.working_directory, "ValueNext");
    require(fs::last_write_time(r.sources[1].source) != stamp, "actual provider identity change timestamp");
    const auto changed = calls.run("04-topology", r, true);
    require(changed && changed->record.scans[1].scan.dependencies.rules[0].provided_modules[0].logical_name == "ValueNext" &&
        cold->record.scans[1].scan.dependencies.rules[0].provided_modules[0].logical_name == "Value",
        "target rescans changed topology; old owned record remains unchanged");
    r.compiler_options.configuration = BuildConfiguration::release;
    r.link_options.configuration = BuildConfiguration::release;
    const auto release = calls.run("05-release", r, true);
    require(release && release->record.compiles.compiles[0].compiler_options.configuration == BuildConfiguration::release &&
        cold->record.compiles.compiles[0].compiler_options.configuration == BuildConfiguration::debug &&
        release->record.compiles.compiles[0].unit.outputs[0].path == cold->record.compiles.compiles[0].unit.outputs[0].path,
        "same-path configuration overwrite does not mutate past record");
    auto run_program = [&](const RecordedModuleTargetResult& completed, const char* phase) {
        runner.phase = phase; process::ProcessSpec spec;
        spec.executable = completed.record.link.association.output;
        spec.working_directory = spec.executable.parent_path(); spec.capture_stdout = spec.capture_stderr = true;
        const auto ran = runner.run(spec); require(ran && ran->exit_code == 0, "successful recorded target program");
    };
    run_program(*release, "program-project");
    // The actual VC Tools sources, not fake std stubs. Missing capability fails
    // this fixed test rather than silently skipping an acceptance requirement.
    require(toolchain->standard_library_modules.std && toolchain->standard_library_modules.std_compat,
        "real std/std.compat source capability");
    const auto std_before = bytes(*toolchain->standard_library_modules.std);
    const auto compat_before = bytes(*toolchain->standard_library_modules.std_compat);
    const auto standard_root = root / "standard";
    write(standard_root / "main.cpp", "import std.compat;\nint main(){return std::char_traits<char>::length(\"abc\")==3?0:1;}\n");
    auto standard = request_for(standard_root); standard.sources.resize(1);
    const auto std_cold = calls.run("06-standard-cold", standard, true);
    require(std_cold && std_cold->result.scans.size() == 1 && std_cold->record.scans.size() == 3 &&
        std_cold->record.scans[1].toolchain_owned && std_cold->record.scans[2].toolchain_owned,
        "actual injected std.compat/std records do not leak into project-only public scan prefix");
    const auto std_snapshot = snapshot(standard_root / ".mqb"); const auto standard_count = runner.counts();
    const auto std_warm = calls.run("07-standard-reuse", standard, true);
    require(std_warm && standard_count == runner.counts() && std_snapshot == snapshot(standard_root / ".mqb"),
        "real toolchain-provider reuse without extra work");
    require(std_before == bytes(*toolchain->standard_library_modules.std) &&
        compat_before == bytes(*toolchain->standard_library_modules.std_compat), "toolchain source inputs unchanged");
    run_program(*std_warm, "program-standard");
    struct ErrorCase { const char* name; const char* source; IncrementalModuleTargetErrorCode code; };
    for (const auto& c : {
        ErrorCase{"08-scan-failed", "#error MQB_TARGET_RECORD_SCAN_FAILURE\n", IncrementalModuleTargetErrorCode::scan_failed},
        ErrorCase{"09-compile-failed", "import Value;\nint main(){return MQB_TARGET_RECORD_COMPILE_FAILURE();}\n", IncrementalModuleTargetErrorCode::compile_failed},
        ErrorCase{"10-link-failed", "import Value;\nint MQB_TARGET_RECORD_UNDEFINED();\nint main(){return MQB_TARGET_RECORD_UNDEFINED();}\n", IncrementalModuleTargetErrorCode::link_failed}}) {
        auto bad = setup(c.name); write(bad.sources[0].source, c.source); const auto n = runner.counts();
        const auto failure = calls.run(c.name, bad, false);
        require(!failure && failure.error().code == c.code && !fs::exists(evidence / (std::string{c.name} + ".record.txt")),
            "real stage failure returns original error, not a successful target");
        if (c.code != IncrementalModuleTargetErrorCode::link_failed) require(runner.links == n.link, "real early failure suppresses link");
        if (c.code == IncrementalModuleTargetErrorCode::scan_failed) require(runner.compiles == n.compile, "real scan failure suppresses compile");
    }
    check_target_cache_history(evidence / "01-cold", *cold);
    check_target_cache_history(evidence / "02-reuse", *warm);
    check_target_cache_history(evidence / "06-standard-cold", *std_cold);
    check_target_cache_history(evidence / "07-standard-reuse", *std_warm);
    require(cold->cache_evidence.compiles[0].request.options.configuration == BuildConfiguration::debug &&
        release->cache_evidence.compiles[0].request.options.configuration == BuildConfiguration::release,
        "retained target cache request survives configuration overwrite and later errors");
    require(calls.count == 10 && runner.programs == 2, "fixed real target/program count");
    write(evidence / "completed.txt", "10 targets; 7 successes; 3 independent expected stage errors; 2 successful programs before faults\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work = fs::current_path();
        require(!fs::exists(work / "storage-fixtures") && !fs::exists(work / "storage-evidence"), "fresh test paths, never overwrite evidence");
        mock_cases(work / "storage-fixtures/target-mock", work / "storage-evidence/module-target-records/mock");
        native_cases(work / "storage-fixtures/target-native", work / "storage-evidence/module-target-records/native");
        std::cout << "mock and native complete-target evidence retained separately\n";
#else
        std::cout << "portable target value model only; pipelines and MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
// BEGIN MQB_MODULE_TARGET_CACHE_EVIDENCE_CASES
#ifdef _WIN32
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
namespace {
void save_target_cache_evidence(const fs::path& prefix, const RecordedModuleTargetResult& wave) {
    static_assert(!ModuleCompileArtifactRecord::exact_cache_entry_captured);
    static_assert(!CompileCacheEvidence::producer_identity_verified);
    static_assert(!CompileCacheEvidence::current_content_verified);
    static_assert(!CompileCacheEvidence::complete_producer_inventory);
    static_assert(!CompileCacheEvidence::deletion_authorized);
    auto nodes = [&](const auto& captured, const auto& records, const auto& results, const char* category) {
        require(captured.size() == records.size() && records.size() == results.size(),
            "every successful target node owns one exact cache value in request order");
        for (std::size_t i=0; i<captured.size(); ++i) {
            const auto& e = captured[i]; const auto& r = records[i];
            require(e.request.unit.source == r.unit.source && e.cache_entry.source == r.unit.source &&
                e.request.unit.kind == r.unit.kind && e.cache_entry.kind == r.unit.kind &&
                e.request.cache_file == r.compile_cache && e.request.source_dependencies_file == r.dependencies &&
                e.request.module_scan_output == r.module_scan_output && e.request.working_directory == r.working_directory &&
                e.request.force_rebuild == r.force_rebuild,
                "captured cache/request belongs to this node and effective provider force");
            require(e.request.options.configuration == r.compiler_options.configuration &&
                e.request.options.standard == r.compiler_options.standard &&
                e.request.options.defines == r.compiler_options.defines &&
                e.request.options.include_directories == r.compiler_options.include_directories &&
                e.request.options.additional_arguments == r.compiler_options.additional_arguments,
                "recorded node retains effective ordered options");
            auto outputs = [&](const auto& values) {
                require(values.size() == r.unit.outputs.size(), "captured output cardinality");
                for (std::size_t j=0; j<values.size(); ++j)
                    require(values[j].path == r.unit.outputs[j].path && values[j].kind == r.unit.outputs[j].kind,
                        "object/IFC ordered output pairing matches original projection");
            };
            outputs(e.request.unit.outputs); outputs(e.cache_entry.outputs);
            require(e.request.unit.module_references.size() == r.unit.module_references.size() &&
                e.request.unit.header_unit_references.size() == r.unit.header_unit_references.size(),
                "captured provider reference cardinality");
            for (std::size_t j=0; j<r.unit.module_references.size(); ++j) {
                const auto& a=e.request.unit.module_references[j]; const auto& b=r.unit.module_references[j];
                require(a.logical_name==b.logical_name && a.interface_file==b.interface_file,
                    "named and external references remain inputs, not extra evidence slots");
            }
            for (std::size_t j=0; j<r.unit.header_unit_references.size(); ++j) {
                const auto& a=e.request.unit.header_unit_references[j]; const auto& b=r.unit.header_unit_references[j];
                require(a.header_name==b.header_name && a.interface_file==b.interface_file && a.lookup_method==b.lookup_method,
                    "header references retain exact identity and order");
            }
            if (r.unit.header_unit) require(e.request.unit.header_unit &&
                e.request.unit.header_unit->header_name == r.unit.header_unit->header_name &&
                e.request.unit.header_unit->lookup_method == r.unit.header_unit->lookup_method &&
                !e.cache_entry.module_scan, "HU value retains header identity and no fabricated scan");
            const auto state = r.cache_state==ArtifactCacheState::reused ? CompileCacheEvidenceState::reused :
                r.cache_state==ArtifactCacheState::saved ? CompileCacheEvidenceState::saved : CompileCacheEvidenceState::save_failed;
            require(e.state==state && (e.state==CompileCacheEvidenceState::reused)==!results[i].result.compiled,
                "typed cache disposition agrees with original completion and warning projection");
            if (e.state==CompileCacheEvidenceState::save_failed) {
                require(e.save_error && e.save_error->code==CompileCacheFileErrorCode::replace_failed &&
                    e.save_error->file==r.compile_cache && e.save_error->offset==0 &&
                    e.save_error->message=="failed to remove previous cache entry",
                    "retain original typed save failure and attempted cache value");
                require(bytes(r.compile_cache/"sentinel")=="retain obstruction", "save failure preserves sentinel");
            } else require(!e.save_error, "saved/reused evidence has no invented save error");
            const auto stem=prefix.string()+"."+category+std::to_string(i);
            require(CompileCacheFile::save(stem+".captured.cache",e.cache_entry).has_value(),
                "test-only serialization of owning cache with unchanged codec");
            if (e.state!=CompileCacheEvidenceState::save_failed)
                require(bytes(stem+".captured.cache")==bytes(r.compile_cache), "same-call cache bytes match disk copy");
            std::ostringstream out;
            out<<"attached_exact_cache_entry_captured=true\nlegacy_projection_exact_cache_entry_captured=false\n"
                <<"state="<<static_cast<int>(e.state)<<"\nsource="<<text(e.request.unit.source)
                <<"\nforce_rebuild="<<e.request.force_rebuild<<"\nconfiguration="<<to_string(e.request.options.configuration)
                <<"\nsave_error="<<e.save_error.has_value()<<"\nproducer_identity_verified=false\ncurrent_content_verified=false\n"
                <<"complete_producer_inventory=false\ndeletion_authorized=false\n";
            if (e.save_error) out<<"save_code="<<static_cast<int>(e.save_error->code)
                <<"\nsave_path="<<text(e.save_error->file)<<"\nsave_offset="<<e.save_error->offset
                <<"\nsave_message="<<e.save_error->message<<'\n';
            write(stem+".cache-evidence.txt",out.str());
            // Value ownership control, not a new compiler/cache operation.
            const auto source=e.request.unit.source;
            const auto environments=e.inspection_toolchain.environment.size();
            auto copy=e;
            copy.request.unit.source="changed-copy.cpp";
            copy.cache_entry.outputs.clear();
            copy.inspection_toolchain.environment.push_back({"SYNTHETIC_ONLY","not serialized"});
            require(e.request.unit.source==source && e.cache_entry.outputs.size()==r.unit.outputs.size() &&
                e.inspection_toolchain.environment.size()==environments, "cache/request/toolchain vectors own their values");
        }
    };
    require(wave.cache_evidence.compiles.size() == wave.record.scans.size(), "target scan/compile evidence correspondence");
    for (std::size_t i=0; i<wave.record.scans.size(); ++i) {
        const auto& scan=wave.record.scans[i]; const auto& cache=wave.cache_evidence.compiles[i];
        require(scan.scan.recipe.invocation.source==cache.request.unit.source &&
            scan.scan.compile_cache_reference==cache.request.cache_file,
            "cache belongs to actual project or injected provider scan, never filename-inferred");
    }
    nodes(wave.cache_evidence.compiles,wave.record.compiles.compiles,wave.result.compiles.compiles,"source");
    nodes(wave.cache_evidence.header_unit_compiles,wave.record.compiles.header_unit_compiles,wave.result.compiles.header_unit_compiles,"header");
}
void check_target_cache_history(const fs::path& prefix, const RecordedModuleTargetResult& wave) {
    auto nodes=[&](const auto& values,const char* category) {
        for (std::size_t i=0;i<values.size();++i) {
            const auto stem=prefix.string()+"."+category+std::to_string(i);
            require(CompileCacheFile::save(stem+".history.cache",values[i].cache_entry).has_value(),"serialize old target value after later failures");
            require(bytes(stem+".history.cache")==bytes(stem+".captured.cache"), "later overwrite/failure cannot mutate earlier target cache evidence");
        }
    };
    nodes(wave.cache_evidence.compiles,"source"); nodes(wave.cache_evidence.header_unit_compiles,"header");
}
std::expected<RecordedModuleTargetResult, IncrementalModuleTargetError> run_target_cache_checked(
    MsvcModuleTargetCoordinator& target, const IncrementalModuleTargetRequest& request,
    ArtifactGenerationLabel label, const fs::path& prefix) {
    performance::Collector collector;
    auto result=[&]() {
        performance::Activation active{collector};
        return target.run_recorded(request,std::move(label));
    }();
    const auto count=collector.snapshot();
    constexpr auto ci=static_cast<std::size_t>(performance::CacheKind::compile);
    if (result) {
        const auto nodes=result->cache_evidence.compiles.size()+result->cache_evidence.header_unit_compiles.size();
        require(count.cache_files_opened[ci]<=nodes && count.cache_files_written[ci]<=nodes,
            "target pass-through adds no compile cache read/save beyond its single wave");
        if (prefix.filename()=="02-reuse" || prefix.filename()=="07-standard-reuse")
            require(count.cache_files_opened[ci]==nodes && count.cache_files_written[ci]==0,
                "warm target has one accepted payload open per actual node and zero writes");
    } else require(!fs::exists(prefix.string()+".source0.captured.cache"), "failed target publishes no successful cache evidence");
    std::ostringstream out;
    out<<"compile_cache_reads="<<count.cache_files_opened[ci]<<"\ncompile_cache_writes="<<count.cache_files_written[ci]
        <<"\ntarget_success="<<result.has_value()<<'\n';
    write(prefix.string()+".cache-counts.txt",out.str());
    return result;
}
} // namespace
#endif
// END MQB_MODULE_TARGET_CACHE_EVIDENCE_CASES
