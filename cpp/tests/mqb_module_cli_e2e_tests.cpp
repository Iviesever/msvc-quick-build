#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/platform/windows/WindowsProcessRunner.hpp"
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

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
}

[[nodiscard]] bool contains_line(std::string_view output, std::string_view expected) {
    std::size_t begin = 0;
    while (begin <= output.size()) {
        const std::size_t newline = output.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? output.size() : newline;
        std::string_view line = output.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == expected) return true;
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
    return false;
}

[[nodiscard]] std::optional<fs::path> first_ifc(const fs::path& root) {
    const fs::path ifc_root = root / ".mqb" / "ifc";
    std::error_code error_code;
    fs::recursive_directory_iterator iterator{
        ifc_root,
        fs::directory_options::skip_permission_denied,
        error_code};
    if (error_code) return std::nullopt;
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error_code)) {
        if (error_code) return std::nullopt;
        if (iterator->is_regular_file(error_code) && !error_code
            && iterator->path().extension() == ".ifc") {
            return iterator->path();
        }
        error_code.clear();
    }
    return std::nullopt;
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& root,
    const std::string& jobs,
    const bool explicit_provider,
    const std::string& output_name) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = {"main.cpp"};
    if (explicit_provider) {
        // Consumer first intentionally verifies that the public CLI delegates ordering
        // to the P1689 provider graph rather than positional source order.
        spec.arguments.push_back("math.ixx");
    }
    spec.arguments.insert(
        spec.arguments.end(),
        {
            "--env",
            "vs",
            "--std",
            "latest",
            "--jobs",
            jobs,
            "--verbose",
            "-o",
            output_name,
            "--run",
        });
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_module_cli_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_module_cli_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    write_text(
        tree.root / "math.ixx",
        "export module math;\n"
        "export int answer() { return 42; }\n");
    write_text(
        tree.root / "unused.ixx",
        "export module unused;\n"
        "export int unused_value() { return 7; }\n");
    write_text(
        tree.root / "main.cpp",
        "import math;\n"
        "int main() { return answer() >= 40 ? 0 : 1; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_mqb(runner, mqb_executable, tree.root, "2", true, "module-cli");
    expect(cold.has_value(), "cold public module CLI invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0,
               "cold explicit module CLI target should build and run successfully");
        expect(cold->stdout_text.find("  pipeline: named-modules") != std::string::npos,
               "verbose CLI output should report the named-module pipeline");
        expect(cold->stdout_text.find("[compile] math.ixx") != std::string::npos,
               "cold module CLI target should compile the provider");
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold module CLI target should compile the consumer");
        expect(cold->stdout_text.find("[link] module-cli.exe") != std::string::npos,
               "cold module CLI target should link the executable");
        expect(cold->stdout_text.find("[run] module-cli.exe") != std::string::npos,
               "--run should launch the module target executable");
    }

    const fs::path executable = tree.root / ".mqb" / "bin" / "module-cli.exe";
    expect(fs::is_regular_file(executable),
           "public module CLI target should produce its executable");

    const auto explicit_ifc = first_ifc(tree.root);
    expect(explicit_ifc.has_value() && fs::is_regular_file(*explicit_ifc),
           "public module CLI target should produce a provider IFC");

    auto warm = run_mqb(runner, mqb_executable, tree.root, "1", true, "module-cli");
    expect(warm.has_value(), "warm public module CLI invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0,
               "warm module CLI target should build and run successfully");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "warm module consumer should reuse its compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] math.ixx"),
               "warm module provider should reuse its compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] module-cli.exe"),
               "warm module target should reuse its link cache");
    }

    if (explicit_ifc) {
        std::error_code remove_error;
        fs::remove(*explicit_ifc, remove_error);
        expect(!remove_error, "test should be able to remove the provider IFC");
        auto repaired = run_mqb(runner, mqb_executable, tree.root, "2", true, "module-cli");
        expect(repaired.has_value(), "IFC-repair public module CLI invocation should launch");
        if (repaired) {
            if (repaired->exit_code != 0) dump_failure(*repaired);
            expect(repaired->exit_code == 0,
                   "missing provider IFC should be repaired through the public CLI");
            expect(repaired->stdout_text.find("[compile] math.ixx") != std::string::npos,
                   "missing IFC should rebuild its provider");
            expect(repaired->stdout_text.find("[compile] main.cpp") != std::string::npos,
                   "provider repair should propagate an explicit consumer rebuild");
            expect(repaired->stdout_text.find("[link] module-cli.exe") != std::string::npos,
                   "provider repair should relink the final executable");
        }
    }

    // Clear the explicit-target state so the next phase proves source discovery
    // from a single ordinary entry rather than reusing explicit source selection.
    std::error_code cleanup_error;
    fs::remove_all(tree.root / ".mqb", cleanup_error);
    expect(!cleanup_error, "test should be able to clear explicit module build state");

    auto discovered_cold = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        "2",
        false,
        "module-discovered");
    expect(discovered_cold.has_value(),
           "cold single-entry module-discovery CLI invocation should launch");
    if (discovered_cold) {
        if (discovered_cold->exit_code != 0) dump_failure(*discovered_cold);
        expect(discovered_cold->exit_code == 0,
               "single-entry module target should discover its provider, build, and run");
        expect(discovered_cold->stdout_text.find("[discover] 2 translation units")
                   != std::string::npos,
               "single-entry discovery should select only the ordinary consumer and imported provider");
        expect(discovered_cold->stdout_text.find("unused.ixx") == std::string::npos,
               "unreferenced project-local module interfaces must not be selected by discovery");
        expect(discovered_cold->stdout_text.find("  pipeline: named-modules")
                   != std::string::npos,
               "discovered module target should route through the named-module pipeline");
        expect(discovered_cold->stdout_text.find("[compile] math.ixx") != std::string::npos,
               "discovered provider should compile on the cold build");
        expect(discovered_cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "single-entry consumer should compile on the cold build");
        expect(discovered_cold->stdout_text.find("[link] module-discovered.exe")
                   != std::string::npos,
               "discovered module target should link the requested output");
        expect(discovered_cold->stdout_text.find("[run] module-discovered.exe")
                   != std::string::npos,
               "discovered module target should preserve structured --run behavior");
    }

    auto discovered_warm = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        "1",
        false,
        "module-discovered");
    expect(discovered_warm.has_value(),
           "warm single-entry module-discovery CLI invocation should launch");
    if (discovered_warm) {
        if (discovered_warm->exit_code != 0) dump_failure(*discovered_warm);
        expect(discovered_warm->exit_code == 0,
               "warm discovered module target should build and run successfully");
        expect(contains_line(discovered_warm->stdout_text, "[up-to-date] main.cpp"),
               "warm discovered consumer should reuse its compile cache");
        expect(contains_line(discovered_warm->stdout_text, "[up-to-date] math.ixx"),
               "warm discovered provider should reuse its compile cache");
        expect(contains_line(discovered_warm->stdout_text, "[up-to-date] module-discovered.exe"),
               "warm discovered target should reuse its link cache even when job count changes");
    }

    write_text(
        tree.root / "math.ixx",
        "export module math;\n"
        "export int answer() { return 43; }\n");
    auto provider_mutation = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        "2",
        false,
        "module-discovered");
    expect(provider_mutation.has_value(),
           "provider-mutation single-entry CLI invocation should launch");
    if (provider_mutation) {
        if (provider_mutation->exit_code != 0) dump_failure(*provider_mutation);
        expect(provider_mutation->exit_code == 0,
               "provider-only source mutation should rebuild and preserve executable behavior");
        expect(provider_mutation->stdout_text.find("[compile] math.ixx") != std::string::npos,
               "provider source mutation should rebuild the provider");
        expect(provider_mutation->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "provider source mutation should explicitly rebuild the consumer");
        expect(provider_mutation->stdout_text.find("[link] module-discovered.exe")
                   != std::string::npos,
               "provider source mutation should relink the discovered target");
    }

    const auto discovered_ifc = first_ifc(tree.root);
    expect(discovered_ifc.has_value() && fs::is_regular_file(*discovered_ifc),
           "single-entry module target should produce a provider IFC");
    if (discovered_ifc) {
        std::error_code remove_error;
        fs::remove(*discovered_ifc, remove_error);
        expect(!remove_error, "test should be able to remove discovered provider IFC");
        auto discovered_repair = run_mqb(
            runner,
            mqb_executable,
            tree.root,
            "2",
            false,
            "module-discovered");
        expect(discovered_repair.has_value(),
               "single-entry IFC-repair module CLI invocation should launch");
        if (discovered_repair) {
            if (discovered_repair->exit_code != 0) dump_failure(*discovered_repair);
            expect(discovered_repair->exit_code == 0,
                   "single-entry discovered target should repair a missing provider IFC");
            expect(discovered_repair->stdout_text.find("[compile] math.ixx")
                       != std::string::npos,
                   "single-entry IFC repair should rebuild the provider");
            expect(discovered_repair->stdout_text.find("[compile] main.cpp")
                       != std::string::npos,
                   "single-entry IFC repair should rebuild the consumer");
            expect(discovered_repair->stdout_text.find("[link] module-discovered.exe")
                       != std::string::npos,
                   "single-entry IFC repair should relink the executable");
        }
    }

    // Replace the entry with an import that has no project-local provider. The
    // build must still enter the P1689 path and fail closed; falling back to the
    // ordinary target here would bypass module provider validation.
    write_text(
        tree.root / "main.cpp",
        "import definitely.missing.module;\n"
        "int main() { return 0; }\n");
    auto missing_provider = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        "1",
        false,
        "module-missing");
    expect(missing_provider.has_value(),
           "missing-provider public CLI invocation should launch");
    if (missing_provider) {
        expect(missing_provider->exit_code != 0,
               "unsupported external named module must fail instead of building as an ordinary target");
        expect(missing_provider->stdout_text.find("  pipeline: named-modules")
                   != std::string::npos,
               "import-only target with zero local providers must explicitly enter named-module routing");
        expect(missing_provider->stdout_text.find("[link] module-missing.exe")
                   == std::string::npos,
               "missing named-module provider must fail before final link");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_module_cli_e2e_tests passed\n";
    return 0;
}
