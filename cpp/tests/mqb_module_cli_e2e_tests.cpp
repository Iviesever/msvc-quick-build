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
    const std::string& jobs) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    // Consumer first intentionally verifies that the public CLI delegates ordering
    // to the P1689 provider graph rather than positional source order.
    spec.arguments = {
        "main.cpp",
        "math.ixx",
        "--env",
        "vs",
        "--std",
        "latest",
        "--jobs",
        jobs,
        "--verbose",
        "-o",
        "module-cli",
        "--run",
    };
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
        tree.root / "main.cpp",
        "import math;\n"
        "int main() { return answer() == 42 ? 0 : 1; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_mqb(runner, mqb_executable, tree.root, "2");
    expect(cold.has_value(), "cold public module CLI invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0,
               "cold explicit .ixx + consumer CLI target should build and run successfully");
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

    const auto ifc = first_ifc(tree.root);
    expect(ifc.has_value() && fs::is_regular_file(*ifc),
           "public module CLI target should produce a provider IFC");

    auto warm = run_mqb(runner, mqb_executable, tree.root, "1");
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

    if (ifc) {
        std::error_code remove_error;
        fs::remove(*ifc, remove_error);
        expect(!remove_error, "test should be able to remove the provider IFC");
        auto repaired = run_mqb(runner, mqb_executable, tree.root, "2");
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_module_cli_e2e_tests passed\n";
    return 0;
}
