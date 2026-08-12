#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::vector<std::string>& arguments,
    const std::string_view value) {
    return std::find(arguments.begin(), arguments.end(), value) != arguments.end();
}

[[nodiscard]] fs::path make_temp_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("mqb_module_scanner_unit_" + std::to_string(stamp));
}

class FakeRunner final : public mqb::process::ProcessRunner {
public:
    enum class Mode {
        valid,
        malformed,
        no_output,
        nonzero_exit,
    };

    Mode mode{Mode::valid};
    mqb::process::ProcessSpec last_spec;
    int calls{};

    [[nodiscard]] std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
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

        if (mode == Mode::nonzero_exit) {
            return mqb::process::ProcessResult{
                .exit_code = 2,
                .stderr_text = "synthetic scan failure",
            };
        }

        if (mode != Mode::no_output && !output.empty()) {
            fs::create_directories(output.parent_path());
            std::ofstream stream(output, std::ios::binary | std::ios::trunc);
            if (mode == Mode::malformed) {
                stream << "{\"version\":1,]";
            } else {
                stream << R"json({
  "version": 1,
  "revision": 0,
  "rules": [ {
    "primary-output": "module.obj",
    "provides": [ { "logical-name": "demo" } ]
  } ]
})json";
            }
        }

        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "scan-ok",
        };
    }
};

} // namespace

int main() {
    using mqb::BuildConfiguration;
    using mqb::CppStandard;
    using mqb::TranslationUnitKind;
    using mqb::msvc::ModuleScanErrorCode;
    using mqb::msvc::ModuleScanInvocation;
    using mqb::msvc::MsvcModuleDependencyScanner;
    using mqb::msvc::MsvcToolchain;

    {
        ModuleScanInvocation invocation;
        invocation.source = "src/module.cppm";
        invocation.output_file = "scan/module.json";
        invocation.kind = TranslationUnitKind::module_interface;
        invocation.options.configuration = BuildConfiguration::release;
        invocation.options.standard = CppStandard::cpp20;
        invocation.options.defines = {"FEATURE=1"};
        invocation.options.include_directories = {"include dir"};
        invocation.options.additional_arguments = {"/FIforced.hpp"};

        auto arguments = MsvcModuleDependencyScanner::build_arguments(invocation);
        expect(arguments.has_value(), "valid scan invocation should build argv");
        if (arguments) {
            expect(contains(*arguments, "/std:c++20"), "scan should carry the language standard");
            expect(contains(*arguments, "/DNDEBUG"), "release scan should carry NDEBUG");
            expect(contains(*arguments, "/DFEATURE=1"), "scan should carry user defines");
            expect(contains(*arguments, "/Iinclude dir"), "scan should carry include directories");
            expect(contains(*arguments, "/FIforced.hpp"), "scan should preserve additional arguments");
            expect(contains(*arguments, "/interface") && contains(*arguments, "/TP"),
                   "explicit module-interface scans should use /interface with /TP");
            expect(!contains(*arguments, "/c"), "scan must not request object compilation");
            expect(!contains(*arguments, "/Z7"), "scan must not carry compiler debug artifact state");
            expect(!contains(*arguments, "/sourceDependencies"),
                   "scanDependencies and sourceDependencies are separate phases");
            expect(arguments->size() >= 3
                       && (*arguments)[arguments->size() - 3] == "/scanDependencies"
                       && (*arguments)[arguments->size() - 2] == "scan/module.json"
                       && (*arguments).back() == "src/module.cppm",
                   "structured scan routing should be appended at the end");
        }
    }

    {
        ModuleScanInvocation pre20;
        pre20.source = "src/legacy.cpp";
        pre20.output_file = "scan/legacy.json";
        pre20.options.standard = CppStandard::cpp17;
        auto rejected = MsvcModuleDependencyScanner::build_arguments(pre20);
        expect(!rejected && rejected.error().code == ModuleScanErrorCode::invalid_request,
               "P1689 module scanning should reject C++17 before cl.exe is launched");
        if (!rejected) {
            expect(rejected.error().message.find("C++20") != std::string::npos,
                   "pre-C++20 scan rejection should explain the minimum standard");
        }
    }

    const fs::path root = make_temp_root();
    const fs::path output = root / "scan" / "module.json";
    const fs::path source = root / "module.ixx";
    fs::create_directories(root);
    {
        std::ofstream stream(source);
        stream << "export module demo;\n";
    }

    MsvcToolchain toolchain;
    toolchain.identity.compiler = "fake-cl.exe";
    FakeRunner runner;
    MsvcModuleDependencyScanner scanner{toolchain, runner};

    ModuleScanInvocation invocation;
    invocation.source = source;
    invocation.output_file = output;
    invocation.kind = TranslationUnitKind::module_interface;
    invocation.working_directory = root;

    {
        fs::create_directories(output.parent_path());
        std::ofstream stale(output, std::ios::binary | std::ios::trunc);
        stale << "stale metadata that must not survive";
        stale.close();

        runner.mode = FakeRunner::Mode::valid;
        auto scanned = scanner.scan(invocation);
        expect(scanned.has_value(), "valid fake scan should parse P1689 output");
        if (scanned) {
            expect(scanned->dependencies.rules.size() == 1,
                   "scan should return the parsed P1689 rule");
            expect(scanned->dependencies.rules[0].provided_modules.size() == 1
                       && scanned->dependencies.rules[0].provided_modules[0].logical_name == "demo",
                   "scan should expose provided module identity");
        }
        expect(runner.calls == 1, "scanner should launch exactly one process per invocation");
        expect(runner.last_spec.executable == toolchain.identity.compiler,
               "scanner should launch the discovered compiler");
        expect(runner.last_spec.working_directory == root,
               "scanner should preserve the requested working directory");
    }

    {
        runner.mode = FakeRunner::Mode::malformed;
        auto malformed = scanner.scan(invocation);
        expect(!malformed
                   && malformed.error().code == ModuleScanErrorCode::dependency_metadata_failed
                   && malformed.error().dependency_error.has_value(),
               "malformed P1689 should be a typed metadata error");
    }

    {
        runner.mode = FakeRunner::Mode::no_output;
        auto missing = scanner.scan(invocation);
        expect(!missing && missing.error().code == ModuleScanErrorCode::output_missing,
               "successful process without metadata should fail closed");
    }

    {
        std::ofstream stale(output, std::ios::binary | std::ios::trunc);
        stale << R"json({"version":1,"rules":[{}]})json";
        stale.close();
        runner.mode = FakeRunner::Mode::nonzero_exit;
        auto failed = scanner.scan(invocation);
        expect(!failed && failed.error().code == ModuleScanErrorCode::scan_failed,
               "non-zero cl.exe exit should be a scan failure");
        expect(!fs::exists(output),
               "stale metadata must be removed before a failing scan launches");
    }

    {
        ModuleScanInvocation invalid = invocation;
        invalid.options.defines = {""};
        auto arguments = MsvcModuleDependencyScanner::build_arguments(invalid);
        expect(!arguments && arguments.error().code == ModuleScanErrorCode::invalid_request,
               "empty defines should be rejected before launching cl.exe");
    }

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_module_scanner_tests passed\n";
    return 0;
}
